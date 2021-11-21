#include "param.h"
#include "types.h"
#include "defs.h"
#include "x86.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "elf.h"

extern char data[];  // defined by kernel.ld
pde_t *kpgdir;  // for use in scheduler()


// Set up CPU's kernel segment descriptors.
// Run once on entry on each CPU.
void
seginit(void)
{
  struct cpu *c;

  // Map "logical" addresses to virtual addresses using identity map.
  // Cannot share a CODE descriptor for both kernel and user
  // because it would have to have DPL_USR, but the CPU forbids
  // an interrupt from CPL=0 to DPL=3.
  c = &cpus[cpuid()];
  c->gdt[SEG_KCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, 0);
  c->gdt[SEG_KDATA] = SEG(STA_W, 0, 0xffffffff, 0);
  c->gdt[SEG_UCODE] = SEG(STA_X|STA_R, 0, 0xffffffff, DPL_USER);
  c->gdt[SEG_UDATA] = SEG(STA_W, 0, 0xffffffff, DPL_USER);
  lgdt(c->gdt, sizeof(c->gdt));
}

// Return the address of the PTE in page table pgdir
// that corresponds to virtual address va.  If alloc!=0,
// create any required page table pages.
static pte_t *
walkpgdir(pde_t *pgdir, const void *va, int alloc)
{
  pde_t *pde;
  pte_t *pgtab;

  pde = &pgdir[PDX(va)];
  if(*pde & PTE_P){
    pgtab = (pte_t*)P2V(PTE_ADDR(*pde));
  } else {
    if(!alloc || (pgtab = (pte_t*)kalloc()) == 0)
      return 0;
    // Make sure all those PTE_P bits are zero.
    memset(pgtab, 0, PGSIZE);
    // The permissions here are overly generous, but they can
    // be further restricted by the permissions in the page table
    // entries, if necessary.
    *pde = V2P(pgtab) | PTE_P | PTE_W | PTE_U;
  }
  return &pgtab[PTX(va)];
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa. va and size might not
// be page-aligned.
static int
mappages(pde_t *pgdir, void *va, uint size, uint pa, int perm)
{
  char *a, *last;
  pte_t *pte;

  a = (char*)PGROUNDDOWN((uint)va);
  last = (char*)PGROUNDDOWN(((uint)va) + size - 1);
  for(;;){
    if((pte = walkpgdir(pgdir, a, 1)) == 0)
      return -1;
    if(*pte & PTE_P)
      panic("remap");
    *pte = pa | perm | PTE_P;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// There is one page table per process, plus one that's used when
// a CPU is not running any process (kpgdir). The kernel uses the
// current process's page table during system calls and interrupts;
// page protection bits prevent user code from using the kernel's
// mappings.
//
// setupkvm() and exec() set up every page table like this:
//
//   0..KERNBASE: user memory (text+data+stack+heap), mapped to
//                phys memory allocated by the kernel
//   KERNBASE..KERNBASE+EXTMEM: mapped to 0..EXTMEM (for I/O space)
//   KERNBASE+EXTMEM..data: mapped to EXTMEM..V2P(data)
//                for the kernel's instructions and r/o data
//   data..KERNBASE+PHYSTOP: mapped to V2P(data)..PHYSTOP,
//                                  rw data + free physical memory
//   0xfe000000..0: mapped direct (devices such as ioapic)
//
// The kernel allocates physical memory for its heap and for user memory
// between V2P(end) and the end of physical memory (PHYSTOP)
// (directly addressable from end..P2V(PHYSTOP)).

// This table defines the kernel's mappings, which are present in
// every process's page table.
static struct kmap {
  void *virt;
  uint phys_start;
  uint phys_end;
  int perm;
} kmap[] = {
 { (void*)KERNBASE, 0,             EXTMEM,    PTE_W}, // I/O space
 { (void*)KERNLINK, V2P(KERNLINK), V2P(data), 0},     // kern text+rodata
 { (void*)data,     V2P(data),     PHYSTOP,   PTE_W}, // kern data+memory
 { (void*)DEVSPACE, DEVSPACE,      0,         PTE_W}, // more devices
};

// Set up kernel part of a page table.
pde_t*
setupkvm(void)
{
  pde_t *pgdir;
  struct kmap *k;

  if((pgdir = (pde_t*)kalloc()) == 0)
    return 0;
  memset(pgdir, 0, PGSIZE);
  if (P2V(PHYSTOP) > (void*)DEVSPACE)
    panic("PHYSTOP too high");
  for(k = kmap; k < &kmap[NELEM(kmap)]; k++)
    if(mappages(pgdir, k->virt, k->phys_end - k->phys_start,
                (uint)k->phys_start, k->perm) < 0) {
      freevm(pgdir);
      return 0;
    }
  return pgdir;
}

// Allocate one page table for the machine for the kernel address
// space for scheduler processes.
void
kvmalloc(void)
{
  kpgdir = setupkvm();
  switchkvm();
}

// Switch h/w page table register to the kernel-only page table,
// for when no process is running.
void
switchkvm(void)
{
  lcr3(V2P(kpgdir));   // switch to the kernel page table
}

// Switch TSS and h/w page table to correspond to process p.
void
switchuvm(struct proc *p)
{
  if(p == 0)
    panic("switchuvm: no process");
  if(p->kstack == 0)
    panic("switchuvm: no kstack");
  if(p->pgdir == 0)
    panic("switchuvm: no pgdir");

  pushcli();
  mycpu()->gdt[SEG_TSS] = SEG16(STS_T32A, &mycpu()->ts,
                                sizeof(mycpu()->ts)-1, 0);
  mycpu()->gdt[SEG_TSS].s = 0;
  mycpu()->ts.ss0 = SEG_KDATA << 3;
  mycpu()->ts.esp0 = (uint)p->kstack + KSTACKSIZE;
  // setting IOPL=0 in eflags *and* iomb beyond the tss segment limit
  // forbids I/O instructions (e.g., inb and outb) from user space
  mycpu()->ts.iomb = (ushort) 0xFFFF;
  ltr(SEG_TSS << 3);
  lcr3(V2P(p->pgdir));  // switch to process's address space
  popcli();
}

// Load the initcode into address 0 of pgdir.
// sz must be less than a page.
void
inituvm(pde_t *pgdir, char *init, uint sz)
{
  char *mem;

  if(sz >= PGSIZE)
    panic("inituvm: more than a page");
  mem = kalloc();
  memset(mem, 0, PGSIZE);
  mappages(pgdir, 0, PGSIZE, V2P(mem), PTE_W|PTE_U);
  memmove(mem, init, sz);
}

// Load a program segment into pgdir.  addr must be page-aligned
// and the pages from addr to addr+sz must already be mapped.
int
loaduvm(pde_t *pgdir, char *addr, struct inode *ip, uint offset, uint sz)
{
  uint i, pa, n;
  pte_t *pte;

  if((uint) addr % PGSIZE != 0)
    panic("loaduvm: addr must be page aligned");
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, addr+i, 0)) == 0)
      panic("loaduvm: address should exist");
    pa = PTE_ADDR(*pte);
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, P2V(pa), offset+i, n) != n)
      return -1;
  }
  return 0;
}

// Allocate page tables and physical memory to grow process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
int
allocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  char *mem;
  uint a;

  if(newsz >= KERNBASE)
    return 0;
  if(newsz < oldsz)
    return oldsz;

  a = PGROUNDUP(oldsz);
  for(; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      cprintf("allocuvm out of memory\n");
      deallocuvm(pgdir, newsz, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U) < 0){
      cprintf("allocuvm out of memory (2)\n");
      deallocuvm(pgdir, newsz, oldsz);
      kfree(mem);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
int
deallocuvm(pde_t *pgdir, uint oldsz, uint newsz)
{
  pte_t *pte;
  uint a, pa;

  if(newsz >= oldsz)
    return oldsz;

  a = PGROUNDUP(newsz);
  for(; a  < oldsz; a += PGSIZE){
    pte = walkpgdir(pgdir, (char*)a, 0);
    if(!pte)
      a = PGADDR(PDX(a) + 1, 0, 0) - PGSIZE;
    else if((*pte & PTE_P) != 0){
      pa = PTE_ADDR(*pte);
      if(pa == 0)
        panic("kfree");
      char *v = P2V(pa);
      kfree(v);
      *pte = 0;
    }
  }
  return newsz;
}

// Free a page table and all the physical memory pages
// in the user part.
void
freevm(pde_t *pgdir)
{
  uint i;

  if(pgdir == 0)
    panic("freevm: no pgdir");
  deallocuvm(pgdir, KERNBASE, 0);
  for(i = 0; i < NPDENTRIES; i++){
    if(pgdir[i] & PTE_P){
      char * v = P2V(PTE_ADDR(pgdir[i]));
      kfree(v);
    }
  }
  kfree((char*)pgdir);
}

// Clear PTE_U on a page. Used to create an inaccessible
// page beneath the user stack.
void
clearpteu(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if(pte == 0)
    panic("clearpteu");
  *pte &= ~PTE_U;
}

// Given a parent process's page table, create a copy
// of it for a child.
pde_t*
copyuvm(pde_t *pgdir, uint sz)
{
  pde_t *d;
  pte_t *pte;
  uint pa, i, flags;
  char *mem;

  if((d = setupkvm()) == 0)
    return 0;
  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walkpgdir(pgdir, (void *) i, 0)) == 0)
      panic("copyuvm: pte should exist");
    if(!(*pte & PTE_P))
      panic("copyuvm: page not present");
    pa = PTE_ADDR(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto bad;
    memmove(mem, (char*)P2V(pa), PGSIZE);
    if(mappages(d, (void*)i, PGSIZE, V2P(mem), flags) < 0) {
      kfree(mem);
      goto bad;
    }
  }
  return d;

bad:
  freevm(d);
  return 0;
}

//PAGEBREAK!
// Map user virtual address to kernel address.
char*
uva2ka(pde_t *pgdir, char *uva)
{
  pte_t *pte;

  pte = walkpgdir(pgdir, uva, 0);
  if((*pte & PTE_P) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  return (char*)P2V(PTE_ADDR(*pte));
}

// Copy len bytes from p to user address va in page table pgdir.
// Most useful when pgdir is not the current page table.
// uva2ka ensures this only works for PTE_U pages.
int
copyout(pde_t *pgdir, uint va, void *p, uint len)
{
  char *buf, *pa0;
  uint n, va0;

  buf = (char*)p;
  while(len > 0){
    va0 = (uint)PGROUNDDOWN(va);
    pa0 = uva2ka(pgdir, (char*)va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (va - va0);
    if(n > len)
      n = len;
    memmove(pa0 + (va - va0), buf, n);
    len -= n;
    buf += n;
    va = va0 + PGSIZE;
  }
  return 0;
}

//added for assignment 3
struct sharedPage{
  int npages;
  int counter;
  void* phys_addr_[MAX_NPAGES]; //max no of pages is 8 here
};


struct sharedPage sp_array[MAX_KEYS];// array struct sharedPage for max number of keys

//-----------Helper functions -------------//

//initialize page - zero filled initially
void initialize_page(){
 for (int i=0;i<MAX_KEYS;i++){
    sp_array[i].counter=0;}
}

//add page--updates page array after allocating 
int add_page(int key, int n, void* phys_addr[MAX_NPAGES]){
  if(key<0 || MAX_KEYS<=key || n<0 || MAX_NPAGES<n){
	return -1;
  }
  sp_array[key].npages = n;
  sp_array[key].counter=1;
  for(int i=0; i<n; i++){
	sp_array[key].phys_addr_[i] = phys_addr[i];
  }
  return 0;
}
int get_counter(int key){
  return (key<0||MAX_KEYS<=key)?-1 :sp_array[key].counter;
}

//------------Added my versions of allocate, dealloacte and free to not mess up the allocvm, etc. codes above -----------------//

//allocate page
int allocate_page(pde_t *pgdir, uint oldsp, uint newsp, uint sz, void *phys_addr[MAX_NPAGES]){
  //kind of just based on allocvm code above

  char *mem;
  uint a;
  int i;
  //a = newsp;
  //attempt at fixing proc issue
  if(oldsp & 0xFFF || newsp & 0xFFF || oldsp > ENDOFSTACK || newsp <sz){
	return 0;}

  for(i=0,a = newsp; a<oldsp; a+= PGSIZE, i++){
    mem= kalloc();
    if(mem==0){
	cprintf("Error - allocate page out of memory\n");
	deallocate_page(pgdir, newsp, oldsp);
	return 0;
    }
    memset(mem, 0, PGSIZE);
    mappages(pgdir, (char*)a, PGSIZE, V2P(mem), PTE_W|PTE_U);
    phys_addr[i]=mem;
  }
  return newsp;
}
//deallocate page
int deallocate_page(pde_t *pgdir, uint oldsp, uint newsp){
  pte_t *pte;
  uint a;
  uint pa;

  if(newsp<=oldsp){
	return oldsp;}
  a = (uint)PGROUNDDOWN(newsp-PGSIZE);
  for(; oldsp<=a; a-=PGSIZE){
   pte = walkpgdir(pgdir, (char*)a, 0);
   if(pte && (*pte & PTE_P) != 0){
	pa = PTE_ADDR(*pte);
	if(pa ==0)
	  panic("kfree");
	kfree((char*)pa);
	*pte = 0;
   }
  }
  return newsp;
}

//map vp 
int map_page(pde_t *pgdir, uint oldsp, uint newsp, uint sz, void **phys_addr){
  uint a;
  int i;

  if(oldsp & 0xFFF || newsp & 0xfff || oldsp > ENDOFSTACK || newsp <sz){
	return 0;}

  for(i=0, a=newsp; a < oldsp; a+=PGSIZE, i++){
    mappages(pgdir, (char*)a, PGSIZE, V2P(phys_addr[i]), PTE_W|PTE_U);

  }
  return newsp;
}
//free page
int free_page(pde_t *pgdir){

	uint i;
	//pde_t *pgdir;
    struct proc *proc = myproc();
    
    pgdir = proc->pgdir;
 	uint sproc = proc-> sproc;

    if(pgdir == 0)
    	panic("free_page: no pgdir");
    deallocate_page(pgdir, sproc, 0);
    for(i = 0; i < MAX_NPAGES; i++){
    	if(pgdir[i] & PTE_P){
    		char * v = P2V(PTE_ADDR(pgdir[i]));
    		kfree(v);
      	}
    }
	kfree((char*)pgdir);
    
	
	return 0;
}

//remove page
int remove_page(){
	//to be implemented
	return -1;
}

void* getSharedPage(int key, int n){
  pde_t *pgdir;
  struct proc *proc = myproc();
  void *phys_addr[MAX_NPAGES];
  int i; 
  uint sproc;

  cprintf("Get Shared Page syscall(%d, %d)\n", key, n);

  if(key<0 || key>=MAX_KEYS || n<0 || MAX_NPAGES < n){
	cprintf("Key invalid\n\n");
	return (void*)-1;
  }
  //idead here -> allocate -> add page -> map to a virtual address -> return virtual address
  pgdir = proc->pgdir;
  sproc = proc-> sproc;
  if(proc->shm_key_mask >>key &1){
	return proc->shm_va[key];}


  if(sp_array[key].counter==0){
   sproc = allocate_page(pgdir, sproc, sproc-n * PGSIZE, proc->sz, phys_addr);
    if(sproc==0){
	return (void*)-1;}
    proc->shm_va[key] = (void*)sproc;
    add_page(key, n, phys_addr);
  } else{
    for(i=0;i<n;i++){
	phys_addr[i]=sp_array[key].phys_addr_[i];
    }

    n = sp_array[key].npages;

    if((sproc=map_page(pgdir, sproc, sproc-n *PGSIZE, proc->sz, phys_addr))==0){		return (void*)-1;}

    //sp_array[key].counter++;
    proc->shm_va[key] = (void*)sproc;
    sp_array[key].counter++;
  }
  proc->sproc = sproc;
  proc->shm_key_mask |= 1 <<key;  
  return (void*)sproc;
}


int freeSharedPage(int key){
    pde_t *pgdir;
    struct proc *proc = myproc();
    
    pgdir = proc->pgdir;
 	//uint sproc = proc-> sproc;
 	
 	//make sure key exists
 	/*if(key>=MAX_KEYS || key < 0){
 		cprintf("Key out of range\n");
 		return -1;
 	}*/
 	
 	//check if process is mapped to physical addresses.
 	// so shm_va[key] has the stack pointer/base register thing for the virtual memory
 	// and sproc is the beginning of the stack 
 	/*if(proc->shm_va[key] != (void*)sproc){ //proc->shm_va[key]==0){ //|| 
 		cprintf("process does not have access to key\n");
 		return -1;
	}*/
 	
 	sp_array[key].counter--;
 	
	if(sp_array[key].counter==0){
		/*for(int i = 0; i < MAX_NPAGES;i++){
			sp_array[key].phys_addr_[i]=0;
		}*/ //idk about this aha
		free_page(pgdir);
		return 0; //this means it's hella free, the shared page has no references
	}else if (sp_array[key].counter>0){
		//so now the bigger question, how do we dereference a singular process
		//idk we could just make
		proc->shm_va[key] = (void*)-1;
		return 0;
	}else{
		sp_array[key].counter++;
		return -1;
	}
	sp_array[key].counter++;
	return -1;
}

//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.
//PAGEBREAK!
// Blank page.


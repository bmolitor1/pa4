#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

/*struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // protects everything below here
  int valid;          // inode has been read from disk?

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};*/

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

//added for assignement 3 part 2... shared page

int sys_getSharedPage(void){
  int key;
  int n;

  if(argint(0, &key)<0){
	return -1;}
  if(argint(1, &n)<0){
	return -1;}
  
  return (int)getSharedPage(key, n);
}


int sys_freeSharedPage(void){
  int key;
  if(argint(0, &key)<0){
	return -1;}

  return freeSharedPage(key);
}

int sys_get_counter(void){
  int key;
  if(argint(0, &key)<0){
	return -1;
  }
  return get_counter(key);
}

int sys_inodeWalker(void){
	int dev;
	if(argint(0, &dev)<0){
		return -1;
  	}
	inodeWalker(dev);
	return 0;
}

int sys_directoryWalker(void){
	char *point;
	if(argstr(0, &point)<0){
		return -1;
  	}
  	return directoryWalker(point);
	//return 0;
}

int sys_eraser(void){
	int inum;
	if(argint(0, &inum)<0){
		return -1;
  	}
  	eraser(inum);
  	return 0;
}

int sys_repair_fs(void){
	void* ip = (void *)10;
	if(argptr(0,ip,10)<0){ //sizeof(struct inode)
		return -1;
	}
	repair_fs(ip);
	return 0;
}

int sys_comp_two_walkers(void){
	comp_two_walkers();
	return 0;
}

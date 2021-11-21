#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"
//#include "defs.h"
#define SHAREDMEMSIZE 4096U
#define MAXMEMORY 0xA00000U

//-------------TEST CASES--------------/
void test_case(void (*test_proc)(void)){
 int pid = fork();
  if(pid<0){
	printf(2, "error - failed to test(0)\n\n");
  }
  if (pid==0){
	test_proc();
	exit();
  }else{
	if (wait()!=pid){
	  printf(2, "error - failed to test(1)\n\n");
	}
	for(int i=0; i<8; i++){
	  if(get_counter(i)!=0){
	    printf(2, "error - count invalid for key %d = %d\n\n", i, get_counter(i));}
	}
  }
}

//test for one process
void proc1(void){
	//getsharedpage for one process
	//case 1: process p1 with key 0
	int *p1;
	printf(1, "CASE 1: testing for one process\n");
	p1 = (int*)getSharedPage(0,1);
	printf(1, "getsharedpage for process p1 =%x \n", p1);

	//case 2: key is above max key
	int *p2;
	printf(1, "CASE 2: testing for invalid key\n");
	p2 = (int*)getSharedPage(10, 3);
	printf(1, "process p2 =%x is invalid, key not added to sp_array\n", p2);

}

//test for 2 processes using same page # using fork
void proc2(void){
	char *p1; char *p2;
	printf(1, "testing for 2 processes using same page number\n");
	int pid; int count;
	p1 = getSharedPage(1, 2);
	//p2 = getSharedPage(0, 2);
	if((uint)p1!=MAXMEMORY-SHAREDMEMSIZE){
	  printf(2, "error - failed shared memory: address of p1 is %x, expected address is %x\n", p1, MAXMEMORY-SHAREDMEMSIZE);
	}

	pid = fork();
	if(pid<0){
	  printf(2, "error - fork failed\n");
	} else if (pid==0){
	  printf(1, "Sharing page with child process \n\n");
	  count = get_counter(2); //key is 2
	  if(count!=2){
	    printf(2, "error - sharing Process failed: count is %d, expected count is %d\n", count, 2);
	  }
	  p2 = getSharedPage(0, 2);
	  p2[0]='1'; p2[1]='2'; p2[2]=0;
	  sleep(2);
	  count = get_counter(1); //key is 1
	  if(count!=2){
  	    printf(2, "error - sharing Process failed: count is %d, expected count is %d\n", count, 2);
	  }
	  exit();
	} else{
	  //count is 2 and child process successfully gets shared page of parent process
	  printf(1, "parent process sharing page with child process\n\n");
	  count = get_counter(2);
	  //same as above, to check if counter is 2 and getCounter get the expected output after executing getSharedPage
	  if(count!=2){
	  printf(2, "error - sharing process failed: count is %d, expected count is %d\n", count, 2);
	  }
	  sleep(2);
	  p2 = getSharedPage(1, 2);
	  count = get_counter(1);
	  if(count!=2){
          printf(2, "error - sharing process failed: count is %d, expected count is %d\n", count, 2);}
	  if(!strcmp(p1, "12")){
	    printf(2, "error - fork process failed: p2 is %s, expected value of p1 is 12\n", p1);
  	  }
	  if(pid!=wait()){
	    printf(2, "error -sharing page failed: process exited\n"); 
	  }
	  count = get_counter(1);
	  if(count!=1){
	    printf(2, "error - fork process failed\n\n");
	  }
	}
}


int main(int argc, char *argv[]){
 // char* p1 = getSharedPage(0, 2); //key 0, page 1
  //printf(1, "\n\n"); printf(1, "parent get shared page\n"); printf(1, "\n\n");
  
 // printf("%p\n", &p1);

  test_case(proc1);
  test_case(proc2); 

  printf(1, "\n\n");
  printf(1, "free shared page test for key =0\n");
  int counter;
  //-------------free shared page test-------------------//
  //VALID TEST
  getSharedPage(2,2);
  counter= get_counter(2);
  printf(1, "Counter of Key 2 before freesharedpage() is %d\n", counter);
  int p;
  p = freeSharedPage(2); //free page with key 0 that was used in proc1
  if(p==0){
	printf(1, "Free Shared page for Key 2 - counter is now %d\n\n", counter);
  }
  else{
	printf(2, "Error - Free Shared Page failed - counter is now %d\n\n", counter);
  }
  //INVALID TEST - should throw -1 when function is called
  counter= get_counter(20); //invalid since max_keys is 8
  printf(1, "Invalid Counter of Key 20 before freesharedpage() is %d, and expected value is 0\n", counter);
  p = freeSharedPage(20); //free page with key 0 that was used in proc1
  if(p==0){
        printf(1, "Free Shared page for Key 20\n\n");
  }
  else{
        printf(2, "Error - Free Shared Page failed - counter is %d\n\n",counter);
  }

  wait();
  exit();
}


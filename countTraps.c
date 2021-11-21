#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main(int argc, char *argv[]){
  int n=23; int counts[23];


  getCountTraps(counts, n);
  printf(1, "\n\n"); printf(1, "Count Traps\n"); printf(1, "\n\n");

  for(int i=1;i<n;i++){
	printf(1, "SYSCALL #%d: %d\n", i, counts[i]);}
  printf(1, "\n\n");

  // test cases above

  // count traps for write to file
  int fd; int s;
  mkdir("testcase");
  char *c= (char *) malloc(sizeof("testcase"));
  fd = open("testfile.txt", O_CREATE | O_WRONLY);
  char toWrite[] = "Assignment 2";
  write(fd, toWrite, strlen(toWrite)+1);
  printf(1, "text written to file!\n");
  printf(1, "bytes written: %d\n\n",strlen(toWrite)+1);
  close(fd);

  getCountTraps(counts, n);

  printf(1, "test parent syscalls\n\n");
  for(int i=1; i<n;i++){
	printf(1, "SYSCALL #%d: %d\n", i, counts[i]);
  }
  printf(1, "\n\n");


  if(fork()==0){
    printf(1, "test child syscalls\n\n");

    //read testfile.txt
    fd = open("testfile.txt", O_RDONLY);
    s = read(fd, c, 10);
    printf(1, "file text:  %s\n",c);
    printf(1, "bytes read: %d\n\n",s);
    close(fd);

    getCountTraps(counts, n);
    for(int i=1;i<n;i++){
      printf(1, "SYSCALL #%d: %d\n", i, counts[i]);
    }
    printf(1, "\n\n");
  }

  wait();
  exit();
}

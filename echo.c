#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char *argv[])
{
  int i;
  inodeWalker(0);
  directoryWalker("/");
  //int a = 
  directoryWalker("/hello/");
  //printf(1, "this is the output of walker: %d \n", a);
  comp_two_walkers();
  eraser(22);
  //repairfs(); //we literally can't send an inode over since they don't exist here

  for(i = 1; i < argc; i++)
    printf(1, "%s%s", argv[i], i+1 < argc ? " " : "\n");
  exit();
}

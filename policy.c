#include "types.h"
#include "stat.h"
#include "user.h"

int
main(int argc, char **argv)
{
  if(argc != 2){
    printf(2, "cannot progress policy\n");
    exit(-1);
  }
   printf(2, "enter syscall policy\n");
    exit(policy(atoi(argv[1])));
}
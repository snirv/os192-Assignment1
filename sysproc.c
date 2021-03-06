#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  int status;
  if(argint(0, &status) < 0)
    return -1;

  exit(status);
  return 0;  // not reached
}

int
sys_wait(void)
{
  int* status = 0;
 if(argptr(0, (void*)&status, sizeof(int)) < 0)
        return -1;
    return wait(status);
}


//   if(argint(0, status) < 0)
//     return -1;
//   return wait(status);
// }

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

int
sys_detach(void)
{
    int pid;
    if(argint(0, &pid) < 0)
        return -1;

    return detach(pid);
}


int
sys_priority(void)
{
    int pri;
    if(argint(0, &pri) < 0)
        return -1;

    priority(pri);
    return 0;//sharon add
}

int
sys_policy(void)
{
  //cprintf("enter sys_policy\n");
    int pol;
    if(argint(0, &pol) < 0)
        return -1;
    return policy(pol);
}

int
sys_wait_stat(void)
{
    int* status = 0;
    struct perf * performance;
    if(argptr(0, (void*)&status, sizeof(int)) < 0)
        return -1;

    if(argptr(1, (void*)&performance, sizeof(*performance)) < 0)
        return -1;
    return wait_stat(status , performance);
}
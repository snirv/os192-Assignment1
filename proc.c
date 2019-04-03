#include "schedulinginterface.h"
#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

#define ROUND_ROBIN_POLICY 1
#define PRIORITY_POLICY 2
#define EX_PRIORITY_POLICY 3

extern PriorityQueue pq;
extern RoundRobinQueue rrq;
extern RunningProcessesHolder rpholder;

int scheduler_num = ROUND_ROBIN_POLICY;
long long time_quantums_passed = 0;

long long getAccumulator(struct proc *p) {
    return p->accumulator;
}

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  release(&ptable.lock);
    //3.2
    p->priority = 5;
    p->accumulator = get_min_accumulator();
    //3.3
    p->last_time_quantum = time_quantums_passed;
    p->last_go_to_runnable = 0 ;
    p->last_go_to_running = 0;
    p->last_go_to_sleep = 0;

      p->ctime = ticks; //3.5
    p->stime =0;      //3.5
    p->retime =0;     //3.5
    p->rutime=0;      //3.5
    p->ttime = 0;

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

    //3.5 init for pref sturct
//    sp -= sizeof *p->perf;
//    p->perf = (struct perf*)sp;
//    memset(p->perf, 0, sizeof *p->perf);

//    p->perf->ctime = ticks;

//    cprintf("context : %x\n",p->context);
//    cprintf("perf ttime: %d\n",p->ttime);
//    cprintf("perf stime: %d\n",p->stime);
//    cprintf("perf rutime: %d\n",p->rutime);
//    cprintf("perf retime: %d\n",p->retime);
//    cprintf("perf ctime: %d\n",p->ctime);

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");
  p->accumulator = 0;

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);
    move_to_runnable(p);//3.1
  p->state = RUNNABLE;
  p->last_go_to_runnable = ticks;
  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
//    cprintf("perf in frok() new proc : %x\n",np);
//    cprintf("perf ttime: %d\n",np->ttime);
//    cprintf("perf stime: %d\n",np->stime);
//    cprintf("perf rutime: %d\n",np->rutime);
//    cprintf("perf retime: %d\n",np->retime);
//    cprintf("perf ctime: %d\n",np->ctime);
  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  move_to_runnable(np);//3.1
  np->state = RUNNABLE;
  np->last_go_to_runnable = ticks;  //3.5
  release(&ptable.lock);

//    cprintf("perf in fork() curroroc: %x\n",curproc);
//    cprintf("perf ttime: %d\n",curproc->ttime);
//    cprintf("perf stime: %d\n",curproc->stime);
//    cprintf("perf rutime: %d\n",curproc->rutime);
//    cprintf("perf retime: %d\n",curproc->retime);
//    cprintf("perf ctime: %d\n",curproc->ctime);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(int status)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }


    begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }
   if(curproc->state == RUNNING){
       rpholder.remove(curproc);
       accumulate_time(curproc);
   }
  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  curproc->ttime = ticks;//3.5
  curproc->status =status;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(int* status)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
  
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
          if(status != null) {
              *status = p->status;
          }
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
/* //origin scheduler
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}
*/



void
scheduler(void)
{

        struct proc *p;
        struct proc *p_longet_time;
        struct cpu *c = mycpu();
        c->proc = 0;
//        boolean longest_init = false;

    for(;;) {
        // Enable interrupts on this processor.
        sti();
        acquire(&ptable.lock);
        if ((scheduler_num == EX_PRIORITY_POLICY) && ( (time_quantums_passed % 100) == 0)) {//3.4 fairness fix
          p_longet_time = null;
          for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//                cprintf("enter for\n");
            if (p->state != RUNNABLE){
              continue;
            }
            else {//p is runnable
                if(p_longet_time == null){//first loop iteration
                 p_longet_time = p;
//                 longest_init = true;
                // cprintf("enter init\n");
                }
                else if((p->last_time_quantum) < (p_longet_time->last_time_quantum)){//other iterations
                  //cprintf("enter switch p\n");
                  p_longet_time=p;
              }
            }
          }
          if(p_longet_time == null){//Didn't found runnable to run
              release(&ptable.lock);
              continue;
          }
          p = p_longet_time;
          p->last_time_quantum = time_quantums_passed;
//          if(!pq.isEmpty()){
//            pq.extractProc(p);
//            }
            if(pq.extractProc(p) == false){
                panic("extractProc fail\n");
            }
        }
        else {//Not EX_PRIORITY_POLICY or not % 100
            p = move_to_running();
        }

        //cprintf("p is: %d\n",p);


        if( p == null || p->state != RUNNABLE){
            release(&ptable.lock);
            continue;
        }
        //cprintf("proc name: %s\n",p->name);
        c->proc = p;
        switchuvm(p);
        accumulate_time(p);
        p->state = RUNNING;
        p->last_go_to_running = ticks;
        rpholder.add(p);
        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        release(&ptable.lock);
    }
}



boolean
move_to_runnable(struct proc* p) {
    boolean success = false;

    switch (scheduler_num) {
        case ROUND_ROBIN_POLICY:
           success = rrq.enqueue(p);
            if(!success){
                panic("fail to enqueue RR\n");
            }
            return success;
        case PRIORITY_POLICY:
            if(p->status == RUNNING){
                p->accumulator += p->priority;
            }
            success = pq.put(p);
            if(!success){
                panic("fail to enqueue PP\n");
            }
            break;
        case EX_PRIORITY_POLICY :
             if(p->status == RUNNING){
                p->accumulator += p->priority;
            }
            success = pq.put(p);
            if(!success){
                panic("fail to enqueue EPP\n");
            }
            break;
        default:
            return success;
    }
    return success;

}

struct proc*
move_to_running(void)
{
    struct proc* p;
    switch (scheduler_num) {
        case ROUND_ROBIN_POLICY:
            if (!rrq.isEmpty()){
                p = rrq.dequeue();
                return p;
            }
            break;

        case PRIORITY_POLICY:
            if(!pq.isEmpty()){
                p = pq.extractMin();
                return p;
            }
            break;
        case EX_PRIORITY_POLICY :
            if(!pq.isEmpty()){
                p = pq.extractMin();
                p->last_time_quantum = time_quantums_passed;
                return p;
            }
            break;
        default:
            return null;
    }
    return null;
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
    struct proc* p = myproc();
    if(p->state == RUNNING){
        rpholder.remove(p);
        time_quantums_passed++;//3.3
//        cprintf("time qunta is :%d\n",time_quantums_passed);
//        accumulate_time(p);
    }
    accumulate_time(p);
    move_to_runnable(p);//3.1
    p->state = RUNNABLE;
    p->last_go_to_runnable = ticks;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
    //if moving from running
    if(p->state == RUNNING){
        rpholder.remove(p);
    }
  accumulate_time(p);
  p->state = SLEEPING;
  p->last_go_to_sleep = ticks;


  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan) {
        p->accumulator = get_min_accumulator();//3.2
        move_to_runnable(p);//3.1
        accumulate_time(p);
        p->state = RUNNABLE;
        p->last_go_to_runnable =ticks;

    }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid) //TODO check if logit is correct- p->state = RUNNABLE; why only in if
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
        // remove process from rpholder if necessary
        if(p->state == RUNNING){
            rpholder.remove(p);
        }
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING) {
          p->accumulator = get_min_accumulator();//3.2
          move_to_runnable(p);//3.1
          accumulate_time(p);
          p->state = RUNNABLE;
          p->last_go_to_runnable =ticks;


      }

      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}


int
detach(int pid)
{
    struct proc *p;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
        // Scan through table looking for  children with pid.
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->parent != curproc)
                continue;
            if(p->pid == pid){
                // Found the pid.
                p->parent = initproc;
                release(&ptable.lock);
                cprintf("detach success\n");
                return 0;
            }
        }

        // No child with pid pid.
            release(&ptable.lock);
            cprintf("detach fail\n");
            return -1;


}


void 
priority(int priority)
{
    int lower_bound = 1;
    if (scheduler_num == EX_PRIORITY_POLICY){
        lower_bound = 0;
    }
    if(priority > 10 || priority < lower_bound){
        panic("illegal priority!\n");
    }
    struct proc *p;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    // Scan through table looking for curproc.
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p != curproc) {
            continue;
        }
            p->priority = priority;
            release(&ptable.lock);
            cprintf("changed priority success\n");
            return ;
        }


    //Proc not found
    panic("priority change proc not found");

}

long long
get_min_accumulator(){
    long long min1 ,min2;
    boolean bool1 ,bool2;
    bool1 = pq.getMinAccumulator(&min1);
    bool2 =rpholder.getMinAccumulator(&min2);
    if(bool1 && bool2){
        if(min1 > min2){
            return min2;
        }
        return min1;
    }
    else if(bool1){
        return min1;
    } else if(bool2){
        return min2;
    } else {
        return 0;
    }

}

int
policy(int pol){
  if ((pol < ROUND_ROBIN_POLICY) || (pol > EX_PRIORITY_POLICY)){
    panic("illigal policy num\n");
    return -1;
  }
  if (scheduler_num == pol){ //nothing to change 
    return 0;
  }
  if (pol == ROUND_ROBIN_POLICY){ //from 2 or 3 to 1
    if (pq.switchToRoundRobinPolicy() == false){
      panic("did not succseed to change data structure\n");
    }
    reset_accumulator();
    scheduler_num =pol;
    return 0;
  }
  else{ 
      if (scheduler_num == ROUND_ROBIN_POLICY) { // from 1 to 2 or 3
          if (rrq.switchToPriorityQueuePolicy() == false){
              panic("did not succseed to change data structure\n");
          }
      }

      if (pol == PRIORITY_POLICY){
        no_zero_priority();
      }
    scheduler_num= pol;
    return 0;
  }
}

int 
reset_accumulator(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    p->accumulator = 0;  
  } 
  release(&ptable.lock);
  return 0;
}

int
no_zero_priority(void){
  struct proc *p;
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->priority == 0){
      p->priority =1;
    }  
  } 
  release(&ptable.lock);
  return 0;
}


void
accumulate_time(struct proc* p){
  if (p->state == RUNNING){
    p->rutime += (ticks - p->last_go_to_running);
  }
  else if (p->state == RUNNABLE){
     p->retime += (ticks - p->last_go_to_runnable);
  }
  else if (p->state == SLEEPING){
    p->stime += (ticks - p->last_go_to_sleep);
  }
}


int
wait_stat(int* status, struct perf * performance)//3.5
{
    struct proc *p;
    int havekids, pid;
    struct proc *curproc = myproc();

    acquire(&ptable.lock);
    for(;;){
        // Scan through table looking for exited children.
        havekids = 0;
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
            if(p->parent != curproc)
                continue;
            havekids = 1;
            if(p->state == ZOMBIE){
                // Found one.
                pid = p->pid;
                kfree(p->kstack);
                p->kstack = 0;
                freevm(p->pgdir);
                p->pid = 0;
                p->parent = 0;
                p->name[0] = 0;
                p->killed = 0;
                p->state = UNUSED;
                if(status != null) {
                    *status = p->status;
                }
                if(performance !=null){
                    cprintf("perf in wait stat : %x\n",p);
                    cprintf("perf ttime: %d\n",p->ttime);
                    cprintf("perf stime: %d\n",p->stime);
                    cprintf("perf rutime: %d\n",p->rutime);
                    cprintf("perf retime: %d\n",p->retime);
                    cprintf("perf ctime: %d\n",p->ctime);
                    performance->ttime = p->ttime;
                    performance->stime = p->stime;
                    performance->rutime = p->rutime;
                    performance->retime = p->retime;
                    performance->ctime = p->ctime;
                }
                release(&ptable.lock);
                return pid;
            }
        }

        // No point waiting if we don't have any children.
        if(!havekids || curproc->killed){
            release(&ptable.lock);
            return -1;
        }

        // Wait for children to exit.  (See wakeup1 call in proc_exit.)
        sleep(curproc, &ptable.lock);  //DOC: wait-sleep
    }
}
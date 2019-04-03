//
// Created by snirv@wincs.cs.bgu.ac.il on 3/20/19.
//
#include "types.h"
#include "user.h"
struct perf {
    int ctime;                     // Creation time
    int ttime;                     // Termination time
    int stime;                     // The total time spent in the SLEEPING state
    int retime;                    // The total time spent in the RUNNABLE state
    int rutime;                    // The total time spent in the RUNNING state
};


int main(void) {
     int pid;
//    int first_status;
//    int second_status;
//    int third_status;
//    pid = fork(); // the child pid is 99
//    if (pid > 0) {
//        first_status =  detach(pid); // status = 0
//        printf(1,"firts status: %d\n",first_status);
//        second_status = detach(pid); // status = -1, because this process has already
//        printf(1,"second_status: %d\n",second_status);
//        // detached this child, and it doesn’t have
//        // this child anymore.
//        third_status =  detach(pid+1); // status = -1, because this process doesn’t
//        printf(1,"third_status: %d\n",third_status);
//        // have a child with this pid.
//    }
    printf(1,"start pref test\n");
    struct perf perf;
    printf(1,"perf1 : %x\n",&perf);
    int status;
    pid = fork();
    if(pid > 0 ){
       status = wait_stat(&status,&perf);

//        printf(1,"child pid : %d\n",pid);
//        printf(1,"status: %d\n",status);
        printf(1,"perf ttime: %d\n",perf.ttime);
        printf(1,"perf stime: %d\n",perf.stime);
        printf(1,"perf rutime: %d\n",perf.rutime);
        printf(1,"perf retime: %d\n",perf.retime);
        printf(1,"perf ctime: %d\n",perf.ctime);
    }
    if(pid == 0){
        sleep(100);
        for (int i = 0; i <1000000 ; ++i) {
            i++;
        }
    }
    exit(0);
}


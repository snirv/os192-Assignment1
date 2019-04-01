//
// Created by snirv@wincs.cs.bgu.ac.il on 3/20/19.
//
#include "types.h"
#include "user.h"


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
    struct perf* perf;
    int status;
    pid = fork();
    if(pid > 0 ){
        wait_stat(&status,perf);
    }
    if(pid == 0){
        sleep(1);
    }
    exit(0);
}


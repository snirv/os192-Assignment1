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


void test_detach () {
    int pid;
    int first_status;
    int second_status;
    int third_status;
    pid = fork(); // the child pid is 99
    if (pid > 0) {
        first_status = detach(pid); // status = 0
        printf(1, "firts status: %d\n", first_status);
        second_status = detach(pid); // status = -1, because this process has already
        printf(1, "second_status: %d\n", second_status);
        // detached this child, and it doesn’t have
        // this child anymore.
        third_status = detach(pid + 1); // status = -1, because this process doesn’t
        printf(1, "third_status: %d\n", third_status);
        // have a child with this pid.
    }
    if(pid == 0){
        exit(0);
    }

}


void print_perf(struct perf *performance, int policy) {
    printf(1, "pref performance result in policy: %d :\n", policy);
    printf(1, "\tcreation time: %d\n", performance->ctime);
    printf(1, "\ttermination time: %d\n", performance->ttime);
    printf(1, "\tsleeping time: %d\n", performance->stime);
    printf(1, "\tready time: %d\n", performance->retime);
    printf(1, "\trunning time: %d\n", performance->rutime);
    printf(1, "\n\tTurnaround time: %d\n\n", (performance->ttime - performance->ctime));
}


void test_performance(int priority_num , int policy_num) {
    int pid_1;
    struct perf perf_1;
    pid_1 = fork();
    if (pid_1 > 0) {
        int status_1;
        wait_stat(&status_1, &perf_1);
        print_perf(&perf_1 , policy_num);
    } else {
        for (int k = 0; k < 50 ; k++) {
            int pid_2;
            struct perf perf_2;

            pid_2 = fork();
            // the child pid is pid
            if (pid_2 > 0) {
                int status_2;
                sleep(5);
                wait_stat(&status_2, &perf_2);
            } else {
                if (priority_num)
                    priority(priority_num);
                int sum = 0;
                for (int i = 0; i < 100000; i++) {
                    sum++;
                }
                sleep(5);
                exit(0);
            }
        }
        exit(0);
    }
    return;
}



int main(void) {
    int priority;

    test_detach();

    test_performance(null , ROUND_ROBIN_POLICY); //round robin test

    //--------- priority test ---------------
    policy(PRIORITY_POLICY);
    priority = 2;
    test_performance(priority , PRIORITY_POLICY);

    //----------ex priority test--------------
    policy(EX_PRIORITY_POLICY);
    priority = 0;
    test_performance(priority, EX_PRIORITY_POLICY);


    exit(0);
}


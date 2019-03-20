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
    pid = fork(); // the child pid is 99
    if (pid > 0) {
        detach(pid); // status = 0
        detach(pid); // status = -1, because this process has already
        // detached this child, and it doesn’t have
        // this child anymore.
        detach(77); // status = -1, because this process doesn’t
        // have a child with this pid.
    }

    exit(0);
}


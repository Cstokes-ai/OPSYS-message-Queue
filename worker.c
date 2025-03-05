//
// Created by corne on 3/05/2025.
//
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <time.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>

/*worker, the children
The worker takes in two command line arguments, this time corresponding to how many seconds and nanoseconds it should
decide to stay around in the system. For example, if you were running it directly you might call it like (note that oss launches
this when you are actually running the project):
Linux System Calls 2
./worker 5 500000
The worker will start by attaching to shared memory. However, it will not examine it. It will then go into a loop.
do {
msgrcv(from oss);
check the clock
determine if it is time to terminate
msgsnd(to oss, saying if we are done or not)
} while (not done);
It determines the termination time by adding up the system clock time and the time passed to it in the initial call through
command line arguments (in our simulated system clock, not actual time). This is when the process should decide to leave the
system and terminate.
For example, if the system clock was showing 6 seconds and 100 nanoseconds and the worker was passed 5 and 500000 as
above, the target time to terminate in the system would be 11 seconds and 500100 nanoseconds. The worker would check this
termination time to see if it has passed each iteration of the loop. If it ever looks at the system clock and sees values over the
ones when it should terminate, it should output some information, send a message to oss and then terminate.
So what output should the worker send? Upon starting up, it should output the following information:
WORKER PID:6577 PPID:6576 SysClockS: 5 SysclockNano: 1000 TermTimeS: 11 TermTimeNano: 500100
--Just Starting
The worker should then go into a loop, waiting for a message from oss, checking the clcok and then sending a message back. It
should also do some periodic output.
In this project, unlike previous projects, you should output a message every iteration of the loop. Something like the following:
WORKER PID:6577 PPID:6576 SysClockS: 6 SysclockNano: 45000000 TermTimeS: 11 TermTimeNano: 500100
--1 iteration has passed since it started
and then the next iteration it would output:
WORKER PID:6577 PPID:6576 SysClockS: 7 SysclockNano: 500000 TermTimeS: 11 TermTimeNano: 500100
--2 iterations have passed since starting
Once its time has elapsed, supposing it ran for 10 iterations, it would send out one final message:
WORKER PID:6577 PPID:6576 SysClockS: 11 SysclockNano: 700000 TermTimeS: 11 TermTimeNano: 500100
--Terminating after sending message back to oss after 10 iterations.*/


// Shared memory structure for the simulated clock
typedef struct {
    int occupied;
    pid_t pid;
    int messagesent;
    int seconds;
    int nanoseconds;
};
struct PCB processtable[20];

void run_worker(int maxSec, int maxNano) {
    // Attach to the shared memory
    int shmid = shmget(IPC_PRIVATE, sizeof(SharedClock), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    SharedClock *simClock = (SharedClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    // Calculate the termination time
    int termSec = simClock->seconds + maxSec;
    int termNano = simClock->nanoseconds + maxNano;
    if (termNano >= 1000000000) {
        termSec++;
        termNano -= 1000000000;
    }

    // Initial output
    printf("WORKER PID:%d PPID:%d SysClock:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d\n",
           getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termSec, termNano);

    // Loop until the termination time is reached
    int lastSecond = simClock->seconds;
    while (simClock->seconds < termSec || (simClock->seconds == termSec && simClock->nanoseconds < termNano)) {
        if (simClock->seconds != lastSecond) {
            printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --%d seconds passed\n",
                   getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termSec, termNano, simClock->seconds - lastSecond);
            lastSecond = simClock->seconds;
        }
    }

    // Final output
    printf("WORKER PID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Terminating\n",
           getpid(), simClock->seconds, simClock->nanoseconds, termSec, termNano);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <maxSeconds> <maxNanoseconds\n", argv[0]);
        return EXIT_FAILURE;
    }

    int maxSec = atoi(argv[1]);
    int maxNano = atoi(argv[2]);
    if (maxSec < 0 || maxNano < 0) {
        fprintf(stderr, "Error: maxSeconds and maxNanoseconds must be non-negative integers.\n");
        return EXIT_FAILURE;
    }

    run_worker(maxSec, maxNano);
    return EXIT_SUCCESS;
}


}
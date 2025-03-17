#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/types.h>
#include <string.h>
#include <time.h>

#define SHM_KEY 12345  // Shared memory key
#define MSG_KEY 54321  // Message queue key
#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)

struct simulated_clock {
    int seconds;
    int nanoseconds;
};

struct msgbuf {
    long mtype;
    int mtext;
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <seconds> <nanoseconds>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    int termSeconds = atoi(argv[1]);
    int termNano = atoi(argv[2]);

    int shmid = shmget(SHM_KEY, sizeof(struct simulated_clock), 0666);
    if (shmid < 0) {
        perror("shmget");
        exit(EXIT_FAILURE);
    }
    struct simulated_clock* simClock = (struct simulated_clock*) shmat(shmid, NULL, 0);
    if (simClock == (void*)-1) {
        perror("shmat");
        exit(EXIT_FAILURE);
    }

    int msgQueueId = msgget(MSG_KEY, 0666);
    if (msgQueueId < 0) {
        perror("msgget");
        exit(EXIT_FAILURE);
    }

    struct simulated_clock termTime;
    termTime.seconds = simClock->seconds + termSeconds;
    termTime.nanoseconds = simClock->nanoseconds + termNano;
    if (termTime.nanoseconds >= 1000000000) {
        termTime.seconds += 1;
        termTime.nanoseconds -= 1000000000;
    }

    printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Just Starting\n",
           getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termTime.seconds, termTime.nanoseconds);

    struct msgbuf msg;

    while (1) {
        // Receive a message from oss
        if (msgrcv(msgQueueId, &msg, MSG_SIZE, 0, 0) == -1) {
            perror("msgrcv");
        }
        printf("WORKER: Received message from oss\n");

        if ((simClock->seconds > termTime.seconds) ||
            (simClock->seconds == termTime.seconds && simClock->nanoseconds >= termTime.nanoseconds)) {
            printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Terminating\n",
                   getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termTime.seconds, termTime.nanoseconds);
            msg.mtype = 1;
            msg.mtext = 0;  // Indicate termination
            if (msgsnd(msgQueueId, &msg, MSG_SIZE, 0) == -1) {
                perror("msgsnd");
            }
            break;
        }

        printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Running\n",
               getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termTime.seconds, termTime.nanoseconds);

        msg.mtype = 1;
        msg.mtext = 1;  // Indicate still running
        if (msgsnd(msgQueueId, &msg, MSG_SIZE, 0) == -1) {
            perror("msgsnd");
        }

        sleep(1); // Placeholder for actual time synchronization
    }

    shmdt(simClock);
    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/types.h>

#define SHM_KEY 12345
#define MSG_KEY 54321
#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)

typedef struct {
    int seconds;
    int nanoseconds;
} SharedClock;

struct msgbuf {
    long mtype;
    int mtext;
};

void run_worker(int maxSec, int maxNano) {
    int shmid = shmget(SHM_KEY, sizeof(SharedClock), 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    SharedClock *simClock = (SharedClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    int msqid = msgget(MSG_KEY, 0666);
    if (msqid == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }

    int termSec = simClock->seconds + maxSec;
    int termNano = simClock->nanoseconds + maxNano;
    if (termNano >= 1000000000) {
        termSec++;
        termNano -= 1000000000;
    }

    printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Just Starting\n",
           getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termSec, termNano);

    struct msgbuf msg;
    int iterations = 0;

    do {
        msgrcv(msqid, &msg, MSG_SIZE, getpid(), 0);
        if (simClock->seconds > termSec || (simClock->seconds == termSec && simClock->nanoseconds >= termNano)) {
            msg.mtext = 0;
            msgsnd(msqid, &msg, MSG_SIZE, 0);
            printf("WORKER PID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --Terminating after %d iterations\n",
                   getpid(), simClock->seconds, simClock->nanoseconds, termSec, termNano, iterations);
            break;
        } else {
            msg.mtext = 1;
            msgsnd(msqid, &msg, MSG_SIZE, 0);
            printf("WORKER PID:%d PPID:%d SysClockS:%d SysClockNano:%d TermTimeS:%d TermTimeNano:%d --%d iterations have passed since starting\n",
                   getpid(), getppid(), simClock->seconds, simClock->nanoseconds, termSec, termNano, ++iterations);
        }
    } while (1);

    shmdt(simClock);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <maxSeconds> <maxNanoseconds>\n", argv[0]);
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
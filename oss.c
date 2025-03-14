#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>

#define SHM_KEY 12345
#define MSG_KEY 54321
#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)
#define MAX_CHILDREN 20

typedef struct {
    int seconds;
    int nanoseconds;
} SharedClock;

struct msgbuf {
    long mtype;
    int mtext;
};

typedef struct {
    int occupied;
    pid_t pid;
    int startSec;
    int startNano;
    int messagesSent;
} ProcessTableEntry;

ProcessTableEntry processTable[MAX_CHILDREN];
SharedClock *simClock;
int shmid, msqid;
FILE *logFile;

void cleanup(int signum) {
    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msqid, IPC_RMID, NULL);
    if (logFile) {
        fclose(logFile);
    }
    exit(0);
}

void incrementClock(int childrenRunning) {
    int increment = 250000000 / (childrenRunning > 0 ? childrenRunning : 1);
    simClock->nanoseconds += increment;
    if (simClock->nanoseconds >= 1000000000) {
        simClock->seconds++;
        simClock->nanoseconds -= 1000000000;
    }
}

int main(int argc, char *argv[]) {
    int numProcs = 5;
    int simul = 2;
    int timeLimit = 5;
    int interval = 100;
    char *logFileName = "oss.log";

    int opt;
    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", argv[0]);
                exit(EXIT_SUCCESS);
            case 'n':
                numProcs = atoi(optarg);
                break;
            case 's':
                simul = atoi(optarg);
                break;
            case 't':
                timeLimit = atoi(optarg);
                break;
            case 'i':
                interval = atoi(optarg);
                break;
            case 'f':
                logFileName = optarg;
                break;
            default:
                fprintf(stderr, "Usage: %s [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren] [-f logfile]\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }

    logFile = fopen(logFileName, "w");
    if (!logFile) {
        perror("fopen failed");
        exit(EXIT_FAILURE);
    }

    shmid = shmget(SHM_KEY, sizeof(SharedClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(EXIT_FAILURE);
    }

    simClock = (SharedClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) {
        perror("shmat failed");
        exit(EXIT_FAILURE);
    }

    msqid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msqid == -1) {
        perror("msgget failed");
        exit(EXIT_FAILURE);
    }

    signal(SIGALRM, cleanup);
    signal(SIGINT, cleanup);
    alarm(60);

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    int childrenLaunched = 0;
    int childrenRunning = 0;

    while (childrenLaunched < numProcs || childrenRunning > 0) {
        if (childrenLaunched < numProcs && childrenRunning < simul) {
            pid_t pid = fork();
            if (pid == 0) {
                char maxSecStr[10], maxNanoStr[10];
                int maxSec = rand() % timeLimit + 1;
                int maxNano = rand() % 1000000000;
                snprintf(maxSecStr, 10, "%d", maxSec);
                snprintf(maxNanoStr, 10, "%d", maxNano);
                execl("./worker", "./worker", maxSecStr, maxNanoStr, (char *)NULL);
                perror("execl failed");
                exit(EXIT_FAILURE);
            } else if (pid > 0) {
                for (int i = 0; i < MAX_CHILDREN; i++) {
                    if (!processTable[i].occupied) {
                        processTable[i].occupied = 1;
                        processTable[i].pid = pid;
                        processTable[i].startSec = simClock->seconds;
                        processTable[i].startNano = simClock->nanoseconds;
                        processTable[i].messagesSent = 0;
                        break;
                    }
                }
                childrenLaunched++;
                childrenRunning++;
            } else {
                perror("fork failed");
                exit(EXIT_FAILURE);
            }
        }

        incrementClock(childrenRunning);

        struct msgbuf msg;
        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (processTable[i].occupied) {
                msg.mtype = processTable[i].pid;
                msgsnd(msqid, &msg, MSG_SIZE, 0);
                fprintf(logFile, "OSS: Sending message to worker %d PID %d at time %d:%d\n", i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);
                msgrcv(msqid, &msg, MSG_SIZE, processTable[i].pid, 0);
                fprintf(logFile, "OSS: Receiving message from worker %d PID %d at time %d:%d\n", i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);
                if (msg.mtext == 0) {
                    fprintf(logFile, "OSS: Worker %d PID %d is planning to terminate.\n", i, processTable[i].pid);
                    waitpid(processTable[i].pid, NULL, 0);
                    processTable[i].occupied = 0;
                    childrenRunning--;
                } else {
                    processTable[i].messagesSent++;
                }
            }
        }

        usleep(interval * 1000);
    }

    cleanup(0);
    return 0;
}
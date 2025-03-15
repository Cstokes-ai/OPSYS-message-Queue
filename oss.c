#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <string.h>

#define SHM_KEY 12345
#define MSG_KEY 54321
#define MAX_CHILDREN 20
#define MSG_SIZE sizeof(struct msgbuf) - sizeof(long)

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
    int startS;
    int startN;
    int messagesSent;
} PCBEntry;

PCBEntry processTable[MAX_CHILDREN];
int shmid, msqid;
SharedClock *simClock;
FILE *logfile;

void cleanup() {
    shmdt(simClock);
    shmctl(shmid, IPC_RMID, NULL);
    msgctl(msqid, IPC_RMID, NULL);
    if (logfile) fclose(logfile);
}

void sigint_handler(int sig) {
    fprintf(logfile, "Caught SIGINT, terminating...\n");
    cleanup();
    exit(0);
}

void incrementClock(int children) {
    int increment = 250000000 / (children > 0 ? children : 1);
    simClock->nanoseconds += increment;
    if (simClock->nanoseconds >= 1000000000) {
        simClock->seconds++;
        simClock->nanoseconds -= 1000000000;
    }
}

void printProcessTable() {
    fprintf(logfile, "OSS PID:%d SysClockS:%d SysClockNano:%d\n", getpid(), simClock->seconds, simClock->nanoseconds);
    fprintf(logfile, "Process Table:\n");
    fprintf(logfile, "Entry Occupied PID StartS StartN MessagesSent\n");
    for (int i = 0; i < MAX_CHILDREN; i++) {
        fprintf(logfile, "%d %d %d %d %d %d\n", i, processTable[i].occupied, processTable[i].pid,
                processTable[i].startS, processTable[i].startN, processTable[i].messagesSent);
    }
    fprintf(logfile, "\n");
    fflush(logfile);
}

int main(int argc, char *argv[]) {
    int opt, proc = 1, simul = 1, timelimit = 2, interval = 100;
    char *logfilename = "oss.log";

    while ((opt = getopt(argc, argv, "hn:s:t:i:f:")) != -1) {
        switch (opt) {
            case 'h':
                printf("Usage: %s [-h] [-n proc] [-s simul] [-t timelimit] [-i interval] [-f logfile]\n", argv[0]);
                exit(0);
            case 'n': proc = atoi(optarg); break;
            case 's': simul = atoi(optarg); break;
            case 't': timelimit = atoi(optarg); break;
            case 'i': interval = atoi(optarg); break;
            case 'f': logfilename = optarg; break;
        }
    }

    logfile = fopen(logfilename, "w");
    if (logfile == NULL) {
        perror("Error opening logfile");
        exit(1);
    }

    shmid = shmget(SHM_KEY, sizeof(SharedClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    simClock = (SharedClock *)shmat(shmid, NULL, 0);
    if (simClock == (void *)-1) {
        perror("shmat failed");
        exit(1);
    }

    simClock->seconds = 0;
    simClock->nanoseconds = 0;

    msqid = msgget(MSG_KEY, IPC_CREAT | 0666);
    if (msqid == -1) {
        perror("msgget failed");
        exit(1);
    }

    signal(SIGINT, sigint_handler);

    int childrenLaunched = 0;
    int activeChildren = 0;

    while (childrenLaunched < proc || activeChildren > 0) {
        incrementClock(activeChildren);

        if (childrenLaunched < proc && activeChildren < simul &&
            (simClock->nanoseconds % (interval * 1000000) == 0)) {
            pid_t child_pid = fork();
            if (child_pid == 0) {
                char maxSecStr[10], maxNanoStr[10];
                snprintf(maxSecStr, 10, "%d", timelimit);
                snprintf(maxNanoStr, 10, "%d", rand() % 1000000000);
                execl("./worker", "worker", maxSecStr, maxNanoStr, NULL);
                perror("execl failed");
                exit(1);
            } else if (child_pid > 0) {
                for (int i = 0; i < MAX_CHILDREN; i++) {
                    if (!processTable[i].occupied) {
                        processTable[i].occupied = 1;
                        processTable[i].pid = child_pid;
                        processTable[i].startS = simClock->seconds;
                        processTable[i].startN = simClock->nanoseconds;
                        processTable[i].messagesSent = 0;
                        break;
                    }
                }
                childrenLaunched++;
                activeChildren++;
                printProcessTable();
            }
        }

        for (int i = 0; i < MAX_CHILDREN; i++) {
            if (processTable[i].occupied) {
                struct msgbuf msg;
                msg.mtype = processTable[i].pid;
                msg.mtext = 1;

                fprintf(logfile, "OSS: Sending message to worker %d PID %d at time %d:%d\n",
                        i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);
                if (msgsnd(msqid, &msg, MSG_SIZE, 0) == -1) {
                    perror("msgsnd failed");
                    exit(1);
                }
                processTable[i].messagesSent++;

                if (msgrcv(msqid, &msg, MSG_SIZE, processTable[i].pid, 0) == -1) {
                    perror("msgrcv failed");
                    exit(1);
                }
                fprintf(logfile, "OSS: Receiving message from worker %d PID %d at time %d:%d\n",
                        i, processTable[i].pid, simClock->seconds, simClock->nanoseconds);

                if (msg.mtext == 0) {
                    fprintf(logfile, "OSS: Worker %d PID %d is planning to terminate.\n", i, processTable[i].pid);
                    waitpid(processTable[i].pid, NULL, 0);
                    processTable[i].occupied = 0;
                    activeChildren--;
                }
            }
        }

        printProcessTable();
    }

    cleanup();
    return 0;
}
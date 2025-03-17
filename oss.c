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

int findNextActiveWorker(int currentWorker) {
    for (int i = 1; i <= MAX_CHILDREN; i++) {
        int index = (currentWorker + i) % MAX_CHILDREN;
        if (processTable[index].occupied) {
            return index;
        }
    }
    return -1;  // No active workers found
}

void cleanup() {
    shmdt(simClock);
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
    int childrenLaunched = 0;
    int activeChildren = 0;
    int currentWorker = -1;
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

        currentWorker = findNextActiveWorker(currentWorker);
        if (currentWorker != -1) {
            struct msgbuf msg;
            msg.mtype = processTable[currentWorker].pid;
            msg.mtext = 1;

            fprintf(logfile, "OSS: Sending message to worker %d PID %d at time %d:%d\n",
                    currentWorker, processTable[currentWorker].pid, simClock->seconds, simClock->nanoseconds);
            fflush(logfile);

            if (msgsnd(msqid, &msg, MSG_SIZE, 0) == -1) {
                perror("msgsnd failed");
                exit(1);
            }
            processTable[currentWorker].messagesSent++;

            // Wait for response from that worker
            if (msgrcv(msqid, &msg, MSG_SIZE, processTable[currentWorker].pid, 0) == -1) {
                perror("msgrcv failed");
                exit(1);
            }

            fprintf(logfile, "OSS: Receiving message from worker %d PID %d at time %d:%d\n",
                    currentWorker, processTable[currentWorker].pid, simClock->seconds, simClock->nanoseconds);
            fflush(logfile);

            if (msg.mtext == 0) {
                fprintf(logfile, "OSS: Worker %d PID %d is planning to terminate.\n", currentWorker, processTable[currentWorker].pid);
                waitpid(processTable[currentWorker].pid, NULL, 0);
                processTable[currentWorker].occupied = 0;
                activeChildren--;
            }
        }

        printProcessTable();
    }

    cleanup();
    return 0;
}
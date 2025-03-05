#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/shm.h>
#include <sys/ipc.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define SHM_KEY 859047
#define MAX_PROCESSES 20

struct SystemClock {
    int seconds;
    int nanoseconds;
};

struct PCB {
    int occupied;
    pid_t pid;
    int startSeconds;
    int startNanoseconds;
};

struct PCB processTable[MAX_PROCESSES];

int shmid;
struct SystemClock *sysClock;

void usage() {
    fprintf(stderr, "Help: oss [-h] [-n proc] [-s simul] [-t timelimitForChildren] [-i intervalInMsToLaunchChildren]\n");
    exit(1);
}

void commandLine(int argc, char *argv[], int *n, int *s, int *t, int *i) {
    int c;
    while ((c = getopt(argc, argv, "hn:s:t:i:")) != -1) {
        switch (c) {
            case 'h':
                usage();
                break;
            case 'n':
                *n = atoi(optarg);
                break;
            case 's':
                *s = atoi(optarg);
                break;
            case 't':
                *t = atoi(optarg);
                break;
            case 'i':
                *i = atoi(optarg);
                break;
            default:
                usage();
        }
    }
}

void incrementClock(struct SystemClock *clock, int increment) {
    clock->nanoseconds += increment;
    while (clock->nanoseconds >= 1000000000) {
        clock->nanoseconds -= 1000000000;
        clock->seconds++;
    }
}

void handleSignal(int sig) {
    printf("Terminating due to signal %d\n", sig);
    shmdt(sysClock);
    shmctl(shmid, IPC_RMID, NULL);
    kill(0, SIGTERM);
    exit(0);
}

int main(int argc, char *argv[]) {
    int n = 0, s = 0, t = 0, i = 0;
    commandLine(argc, argv, &n, &s, &t, &i);
    if (n == 0 || s == 0 || t == 0 || i == 0) {
        usage();
    }

    printf("n = %d, s = %d, t = %d, i = %d\n", n, s, t, i);

    // Create shared memory for the system clock
    shmid = shmget(SHM_KEY, sizeof(struct SystemClock), IPC_CREAT | 0666);
    if (shmid == -1) {
        perror("shmget failed");
        exit(1);
    }

    // Attach to shared memory
    sysClock = (struct SystemClock *) shmat(shmid, NULL, 0);
    if (sysClock == (void *) -1) {
        perror("shmat failed");
        exit(1);
    }

    // Initialize system clock
    sysClock->seconds = 0;
    sysClock->nanoseconds = 0;

    // Initialize process table
    memset(processTable, 0, sizeof(processTable));

    printf("Shared Memory Created: shmid = %d\n", shmid);

    signal(SIGALRM, handleSignal);
    signal(SIGINT, handleSignal);
    alarm(60);

    int active_processes = 0;
    int next_launch_time = 0;
    int increment = 1000000; // 1 millisecond

    while (n > 0) {
        incrementClock(sysClock, increment);

        if (sysClock->nanoseconds >= next_launch_time && active_processes < s) {
            pid_t pid = fork();
            if (pid < 0) {
                perror("fork failed");
                exit(1);
            } else if (pid == 0) {
                // Child process
                char shmid_str[10], duration_str[10];
                sprintf(shmid_str, "%d", shmid);
                int duration = (rand() % t) + 1;
                sprintf(duration_str, "%d", duration);

                execl("./worker", "worker", shmid_str, duration_str, (char *) NULL);
                perror("execl failed");
                exit(1);
            } else {
                // Parent process
                for (int j = 0; j < MAX_PROCESSES; j++) {
                    if (!processTable[j].occupied) {
                        processTable[j].occupied = 1;
                        processTable[j].pid = pid;
                        processTable[j].startSeconds = sysClock->seconds;
                        processTable[j].startNanoseconds = sysClock->nanoseconds;
                        break;
                    }
                }
                active_processes++;
                n--;
                next_launch_time = sysClock->nanoseconds + (i * 1000000);
            }
        }

        int status;
        pid_t pid = waitpid(-1, &status, WNOHANG);
        if (pid > 0) {
            for (int j = 0; j < MAX_PROCESSES; j++) {
                if (processTable[j].pid == pid) {
                    processTable[j].occupied = 0;
                    active_processes--;
                    break;
                }
            }
        }

        if (sysClock->nanoseconds % 500000000 == 0) {
            printf("OSS PID:%d SysClockS: %d SysclockNano: %d\n", getpid(), sysClock->seconds, sysClock->nanoseconds);
            printf("Process Table:\n");
            printf("Entry Occupied PID StartS StartN\n");
            for (int j = 0; j < MAX_PROCESSES; j++) {
                printf("%d %d %d %d %d\n", j, processTable[j].occupied, processTable[j].pid, processTable[j].startSeconds, processTable[j].startNanoseconds);
            }
        }
    }

    // Cleanup shared memory
    shmdt(sysClock);
    shmctl(shmid, IPC_RMID, NULL);
    printf("Shared Memory Removed shmid = %d\n", shmid);

    return 0;
}
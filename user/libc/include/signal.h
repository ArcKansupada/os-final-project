#pragma once

#include <unistd.h>

#define SIGINT 2
#define SIGBUS 7
#define SIGKILL 9
#define SIGUSR1 10
#define SIGSEGV 11
#define SIGPIPE 13
#define SIGALRM 14
#define SIGTERM 15
#define SIG_DFL ((void (*)(int))0)
#define SIG_IGN ((void (*)(int))1)
#define SIG_ERR ((void (*)(int))-1)
#define SA_SIGINFO 4
#define SA_RESTART 0x10000000

typedef int sig_atomic_t;
typedef unsigned long sigset_t;
typedef struct {
    int si_signo, si_errno, si_code;
    union {
        struct { pid_t si_pid; unsigned si_uid; };
        void *si_addr;
        char __padding[112];
    };
} siginfo_t;

struct sigaction {
    union {
        void (*sa_handler)(int);
        void (*sa_sigaction)(int, siginfo_t *, void *);
    };
    sigset_t sa_mask;
    int sa_flags;
    void (*sa_restorer)(void);
};

int sigaction(int sig, const struct sigaction *action, struct sigaction *old);
void (*signal(int sig, void (*handler)(int)))(int);
int kill(pid_t pid, int sig);
int raise(int sig);
int sigemptyset(sigset_t *set);
int sigfillset(sigset_t *set);
int sigaddset(sigset_t *set, int sig);
int sigdelset(sigset_t *set, int sig);
int sigismember(const sigset_t *set, int sig);

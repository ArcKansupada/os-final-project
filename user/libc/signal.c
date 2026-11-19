#include <errno.h>
#include <signal.h>

extern void __restore_rt(void);

int sigaction(int sig, const struct sigaction *action, struct sigaction *old) {
    struct kernel_action {
        void (*handler)(int);
        unsigned long flags;
        void (*restorer)(void);
        sigset_t mask;
    } in, out;
    if (action) in = (struct kernel_action){action->sa_handler,
        (unsigned)action->sa_flags | 0x04000000UL, __restore_rt, action->sa_mask};
    int result = syscall(SYS_rt_sigaction, sig, action ? (long)&in : 0,
                         old ? (long)&out : 0, sizeof(sigset_t), 0, 0);
    if (!result && old) *old = (struct sigaction){.sa_handler = out.handler,
        .sa_flags = out.flags & ~0x04000000UL, .sa_restorer = out.restorer, .sa_mask = out.mask};
    return result;
}

void (*signal(int sig, void (*handler)(int)))(int) {
    struct sigaction action = {.sa_handler = handler, .sa_flags = SA_RESTART}, old = {0};
    return sigaction(sig, &action, &old) < 0 ? SIG_ERR : old.sa_handler;
}

int kill(pid_t pid, int sig) { return syscall(SYS_kill, pid, sig, 0, 0, 0, 0); }
int raise(int sig) { return kill(getpid(), sig); }
int sigemptyset(sigset_t *set) { *set = 0; return 0; }
int sigfillset(sigset_t *set) { *set = ~0UL; return 0; }
static int valid_signal(int sig) {
    if (sig > 0 && sig <= 64) return 1;
    errno = EINVAL;
    return 0;
}
int sigaddset(sigset_t *set, int sig) {
    if (!valid_signal(sig)) return -1;
    *set |= 1UL << (sig - 1);
    return 0;
}
int sigdelset(sigset_t *set, int sig) {
    if (!valid_signal(sig)) return -1;
    *set &= ~(1UL << (sig - 1));
    return 0;
}
int sigismember(const sigset_t *set, int sig) {
    return valid_signal(sig) ? !!(*set & (1UL << (sig - 1))) : -1;
}

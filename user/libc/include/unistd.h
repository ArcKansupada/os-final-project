/* Copyright (C) 2026 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#pragma once

#include <stddef.h>
#include <sys/syscall.h>
#include <sys/types.h>

// Like Linux: a failing call returns -1 and sets errno (errno.h)

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

// Makes system call n with up to six arguments (pass 0 for the ones it
// doesn't take). Returns its result, or -1 with errno set if it failed.
long syscall(long n, long a0, long a1, long a2, long a3, long a4, long a5);

ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);
int pipe(int fd[2]);
int pipe2(int fd[2], int flags);
int dup3(int oldfd, int newfd, int flags);
char *getcwd(char *buf, size_t size);
int chdir(const char *path);
unsigned alarm(unsigned seconds);
unsigned sleep(unsigned seconds);
int usleep(unsigned microseconds);
off_t lseek(int fd, off_t offset, int whence);

// The mode only matters with O_CREAT
int open(const char *path, int flags, ...);
int openat(int dirfd, const char *path, int flags, ...);

pid_t getpid(void);
pid_t getppid(void);
pid_t gettid(void);
pid_t fork(void);

int execve(const char *path, char *const argv[], char *const envp[]);
int execveat(int dirfd, const char *path, char *const argv[],
             char *const envp[], int flags);

struct rusage;
pid_t wait4(pid_t pid, int *status, int options, struct rusage *rusage);

// Reboots the machine (the kernel shuts down). Not for programs to call
// casually.
int reboot(int magic1, int magic2, int command, void *argument);

int sched_yield(void);

// Ends the process (every thread) right away
__attribute__((noreturn)) void _exit(int status);

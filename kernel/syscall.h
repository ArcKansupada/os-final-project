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

#include <cstdint>

/*
 * System calls
 *
 * Linux x86-64 conventions: syscall with the number in RAX, arguments in
 * RDI, RSI, RDX, R10, R8, R9, the result in RAX (-errno on failure). RCX and
 * R11 are clobbered, every other register is preserved.
 *
 * A system call runs as a sync coroutine. If it finishes without blocking
 * the thread goes right back to user mode, otherwise the core goes back to
 * the event loop and the thread is resumed when the call is done.
 */
namespace Syscall {

constexpr uint64_t READ = 0;         // read(fd, buf, len), files and console
constexpr uint64_t WRITE = 1;        // write(fd, buf, len), to the console
constexpr uint64_t OPEN = 2;         // open(path, flags, mode)
constexpr uint64_t CLOSE = 3;        // close(fd)
constexpr uint64_t LSEEK = 8;        // lseek(fd, offset, whence)
constexpr uint64_t MMAP = 9;         // mmap(addr, length, prot, flags, fd,
                                     //      offset), anonymous or MAP_PRIVATE
                                     //      file (MAP_FILE is 0)
constexpr uint64_t MUNMAP = 11;      // munmap(addr, length)
constexpr uint64_t RT_SIGACTION = 13;
constexpr uint64_t RT_SIGRETURN = 15;
constexpr uint64_t IOCTL = 16;
constexpr uint64_t DUP = 32;
constexpr uint64_t DUP2 = 33;
constexpr uint64_t FCNTL = 72;
constexpr uint64_t NANOSLEEP = 35;
constexpr uint64_t ALARM = 37;
constexpr uint64_t KILL = 62;
constexpr uint64_t SCHED_YIELD = 24; // sched_yield(), always blocks
constexpr uint64_t GETPID = 39;      // getpid()
constexpr uint64_t FORK = 57;        // fork(), the direct system call
constexpr uint64_t EXECVE = 59;      // execve(path, argv, envp)
constexpr uint64_t EXIT = 60;        // exit(code), the calling thread
constexpr uint64_t WAIT4 = 61;       // wait4(pid, status, options, rusage),
                                     //      specific pid and exit only,
                                     //      rusage zeroed
constexpr uint64_t GETCWD = 79;      // getcwd(buf, size), returns bytes including NUL
constexpr uint64_t CHDIR = 80;       // chdir(path)
constexpr uint64_t GETPPID = 110;    // getppid()
constexpr uint64_t REBOOT = 169;     // reboot(magic, magic2, cmd, arg), all
                                     //      actions shut down
constexpr uint64_t GETTID = 186;     // gettid()
constexpr uint64_t FUTEX = 202;      // futex(uaddr, op, val, timeout, uaddr2, val3)
constexpr uint64_t EXIT_GROUP = 231; // exit_group(code), the whole process
constexpr uint64_t OPENAT = 257;     // openat(dirfd, path, flags, mode),
                                     //      ignore permissions and write to
                                     //      files
constexpr uint64_t DUP3 = 292;       // dup3(oldFd, newFd, flags)
constexpr uint64_t PIPE2 = 293;      // pipe2(pipefd[2], flags)
constexpr uint64_t EXECVEAT = 322;   // execveat(dirfd, path, argv, envp,
                                     //      flags)
constexpr uint64_t CLONE3 = 435;     // clone3(args, size), threads only

// open/openat's flags (Linux's values). Ignores permissions and write to
// files.
constexpr uint64_t O_RDONLY     = 00000000;
constexpr uint64_t O_WRONLY     = 00000001;
constexpr uint64_t O_RDWR       = 00000002;

constexpr uint64_t O_CREAT      = 00000100;
constexpr uint64_t O_EXCL       = 00000200;
constexpr uint64_t O_TRUNC      = 00001000;
constexpr uint64_t O_APPEND     = 00002000;
constexpr uint64_t O_DIRECT     = 00040000;
constexpr uint64_t O_DIRECTORY  = 00200000;
constexpr uint64_t O_NOFOLLOW   = 00400000;
constexpr uint64_t O_NOATIME    = 01000000;
constexpr uint64_t O_CLOEXEC    = 02000000;
constexpr uint64_t O_NONBLOCK   = 00004000;
constexpr uint64_t O_ASYNC      = 00020000;
constexpr uint64_t O_ACCMODE    = 00000003;

constexpr uint64_t O_STATUS_FLAGS = O_APPEND | O_NONBLOCK | O_NOATIME | O_DIRECT | O_ASYNC;

constexpr uint64_t F_DUPFD = 0;
constexpr uint64_t F_GETFD = 1;
constexpr uint64_t F_SETFD = 2;
constexpr uint64_t F_GETFL = 3;
constexpr uint64_t F_SETFL = 4;
constexpr uint64_t F_GETLK = 5;
constexpr uint64_t F_SETLK = 6;
constexpr uint64_t F_SETLKW = 7;
constexpr uint64_t F_OFD_GETLK = 36;
constexpr uint64_t F_OFD_SETLK = 37;
constexpr uint64_t F_OFD_SETLKW = 38;
constexpr uint64_t F_DUPFD_CLOEXEC = 1030;
constexpr uint64_t FD_CLOEXEC = 1;
constexpr int16_t F_RDLCK = 0;
constexpr int16_t F_WRLCK = 1;
constexpr int16_t F_UNLCK = 2;

constexpr uint64_t TCGETS = 0x5401;
constexpr uint64_t TCSETS = 0x5402;
constexpr uint64_t TCSETSW = 0x5403;
constexpr uint64_t TCSETSF = 0x5404;
constexpr uint64_t TIOCGWINSZ = 0x5413;
constexpr uint64_t FIONREAD = 0x541B;
constexpr uint64_t FIONBIO = 0x5421;
constexpr uint64_t FIONCLEX = 0x5450;
constexpr uint64_t FIOCLEX = 0x5451;
constexpr uint64_t FIOASYNC = 0x5452;

constexpr uint32_t S_IFMT = 0170000;
constexpr uint32_t S_IFSOCK = 0140000;
constexpr uint32_t S_IFLNK = 0120000;
constexpr uint32_t S_IFREG = 0100000;
constexpr uint32_t S_IFBLK = 0060000;
constexpr uint32_t S_IFDIR = 0040000;
constexpr uint32_t S_IFCHR = 0020000;
constexpr uint32_t S_IFIFO = 0010000;

// openat/execveat's flags (Linux's values)
constexpr uint64_t AT_FDCWD = -100;
constexpr uint64_t AT_SYMLINK_NOFOLLOW = 0x100;
constexpr uint64_t AT_EMPTY_PATH = 0x1000;

constexpr uint32_t FUTEX_WAIT = 0;
constexpr uint32_t FUTEX_WAKE = 1;
constexpr uint32_t FUTEX_PRIVATE_FLAG = 128;

// lseek's whence (Linux's values)
constexpr uint64_t SEEK_SET = 0;
constexpr uint64_t SEEK_CUR = 1;
constexpr uint64_t SEEK_END = 2;

// clone3's flags (Linux's values). Threads need VM | SIGHAND | THREAD,
// FS, FILES, and SYSVSEM are accepted and mean nothing here, SETTLS,
// PARENT_SETTID, and CHILD_CLEARTID work. Anything else is -EINVAL.
constexpr uint64_t CLONE_VM = 0x100;
constexpr uint64_t CLONE_FS = 0x200;
constexpr uint64_t CLONE_FILES = 0x400;
constexpr uint64_t CLONE_SIGHAND = 0x800;
constexpr uint64_t CLONE_THREAD = 0x10000;
constexpr uint64_t CLONE_SYSVSEM = 0x40000;
constexpr uint64_t CLONE_SETTLS = 0x80000;
constexpr uint64_t CLONE_PARENT_SETTID = 0x100000;
constexpr uint64_t CLONE_CHILD_CLEARTID = 0x200000;

// error results
constexpr int64_t EPERM = -1;
constexpr int64_t ENOENT = -2;
constexpr int64_t ESRCH = -3;
constexpr int64_t ENXIO = -6;
constexpr int64_t EBADF = -9;
constexpr int64_t ECHILD = -10;
constexpr int64_t EAGAIN = -11;
constexpr int64_t ENOMEM = -12;
constexpr int64_t EACCES = -13;
constexpr int64_t EFAULT = -14;
constexpr int64_t EEXIST = -17;
constexpr int64_t ENODEV = -19;
constexpr int64_t ENOTDIR = -20;
constexpr int64_t EISDIR = -21;
constexpr int64_t EINVAL = -22;
constexpr int64_t EMFILE = -24;
constexpr int64_t ENOTTY = -25;
constexpr int64_t EFBIG = -27;
constexpr int64_t ENOSPC = -28;
constexpr int64_t ESPIPE = -29;
constexpr int64_t EPIPE = -32;
constexpr int64_t ERANGE = -34;
constexpr int64_t ENAMETOOLONG = -36;
constexpr int64_t ENOSYS = -38;
constexpr int64_t ELOOP = -40;
constexpr int64_t EOVERFLOW = -75;

constexpr uint64_t RUSAGE_STRUCT_SIZE = 152;


// Called on each core, after CCB::init_core and with EFER.SCE set
extern void init_core();

extern bool user_range(uintptr_t start, uint64_t len);
extern bool copy_from_user(void *dst, const void *usrc, uint64_t len);
extern bool copy_to_user(void *udst, const void *src, uint64_t len);

} // namespace Syscall

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include "testing.h"

static int failures = 0;

static void check(int ok, const char *what) {
  const char *tag = ok ? "ok: " : "FAIL: ";
  write(1, tag, strlen(tag));
  puts(what);
  if (!ok) failures++;
}

#define ACC_AND(flags) ((flags) & (O_ACCMODE | O_APPEND | O_NONBLOCK))

int main(void) {
  puts("*** t1 start");
  char buf[16];

  int fd = open("data.txt", O_RDONLY);
  check(fd >= 0, "open data.txt");
  int d = dup(fd);
  check(d == fd + 1, "dup returns the lowest free fd");
  check(read(fd, buf, 2) == 2 && read(d, buf + 2, 2) == 2 && memcmp(buf, "0123", 4) == 0,
        "dup'd fds share the file position");
  check(FAILS_WITH(dup(99), EBADF), "dup of a closed fd fails with EBADF");

  int cfd = open("data.txt", O_RDONLY | O_CLOEXEC);
  check(fcntl(cfd, F_GETFD) == FD_CLOEXEC, "O_CLOEXEC sets FD_CLOEXEC");
  int cd = dup(cfd);
  check(cd >= 0 && fcntl(cd, F_GETFD) == 0, "dup clears FD_CLOEXEC");
  close(cd);
  close(cfd);

  check(dup2(fd, fd) == fd, "dup2(fd, fd) returns fd");
  check(FAILS_WITH(dup2(99, 99), EBADF), "dup2(closed, same) fails with EBADF");
  check(FAILS_WITH(dup2(99, 5), EBADF), "dup2 of a closed fd fails with EBADF");
  check(dup2(fd, 50) == 50 && read(50, buf, 2) == 2 && memcmp(buf, "45", 2) == 0,
        "dup2 to a high fd shares the position");
  check(dup2(fd, d) == d, "dup2 onto an open fd replaces it");
  check(FAILS_WITH(dup2(fd, 5000), EBADF), "dup2 past the fd limit fails with EBADF");
  check(FAILS_WITH(dup3(fd, fd, 0), EINVAL), "dup3(fd, fd) fails with EINVAL");
  check(dup3(fd, 51, O_CLOEXEC) == 51 && fcntl(51, F_GETFD) == FD_CLOEXEC, "dup3 with O_CLOEXEC");
  close(50);
  close(51);

  check(fcntl(fd, F_DUPFD, 20) == 20, "F_DUPFD gives the lowest free fd >= arg");
  check(fcntl(fd, F_DUPFD, 20) == 21, "F_DUPFD skips fds in use");
  check(fcntl(fd, F_DUPFD_CLOEXEC, 20) == 22 && fcntl(22, F_GETFD) == FD_CLOEXEC,
        "F_DUPFD_CLOEXEC sets FD_CLOEXEC");
  check(fcntl(20, F_GETFD) == 0, "F_DUPFD doesn't");
  check(FAILS_WITH(fcntl(fd, F_DUPFD, -1), EINVAL), "F_DUPFD with a negative fd fails with EINVAL");
  close(20);
  close(21);
  close(22);
  check(fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fcntl(fd, F_GETFD) == FD_CLOEXEC, "F_SETFD sets FD_CLOEXEC");
  check(fcntl(fd, F_SETFD, 0) == 0 && fcntl(fd, F_GETFD) == 0, "F_SETFD clears it");
  check(ioctl(fd, FIOCLEX) == 0 && fcntl(fd, F_GETFD) == FD_CLOEXEC, "ioctl FIOCLEX");
  check(ioctl(fd, FIONCLEX) == 0 && fcntl(fd, F_GETFD) == 0, "ioctl FIONCLEX");
  check(FAILS_WITH(fcntl(99, F_GETFD), EBADF), "fcntl on a closed fd fails with EBADF");
  check(FAILS_WITH(fcntl(fd, 9999), EINVAL), "an unknown fcntl command fails with EINVAL");

  check(ACC_AND(fcntl(fd, F_GETFL)) == O_RDONLY, "F_GETFL of an O_RDONLY file");
  int w = open("made.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
  check(ACC_AND(fcntl(w, F_GETFL)) == O_WRONLY, "F_GETFL of an O_WRONLY file");
  int rw = open("made.txt", O_RDWR | O_APPEND);
  check(ACC_AND(fcntl(rw, F_GETFL)) == (O_RDWR | O_APPEND), "F_GETFL shows O_APPEND");

  check(fcntl(fd, F_SETFL, O_NONBLOCK) == 0 && ACC_AND(fcntl(d, F_GETFL)) == (O_RDONLY | O_NONBLOCK),
        "F_SETFL is shared by dup'd fds");
  int other = open("data.txt", O_RDONLY);
  check(ACC_AND(fcntl(other, F_GETFL)) == O_RDONLY, "but not by a separate open");
  check(fcntl(fd, F_SETFL, O_RDWR) == 0 && ACC_AND(fcntl(fd, F_GETFL)) == O_RDONLY,
        "F_SETFL doesn't change the access mode");
  close(other);

  check(write(w, "ab", 2) == 2, "write ab");
  check(lseek(w, 0, SEEK_SET) == 0 && fcntl(w, F_SETFL, O_APPEND) == 0 && write(w, "cd", 2) == 2,
        "F_SETFL O_APPEND, then write cd at offset 0");
  check(lseek(rw, 0, SEEK_SET) == 0 && read(rw, buf, 8) == 4 && memcmp(buf, "abcd", 4) == 0,
        "O_APPEND from F_SETFL appended");
  check(fcntl(w, F_SETFL, 0) == 0 && lseek(w, 0, SEEK_SET) == 0 && write(w, "X", 1) == 1 &&
            lseek(rw, 0, SEEK_SET) == 0 && read(rw, buf, 8) == 4 && memcmp(buf, "Xbcd", 4) == 0,
        "clearing O_APPEND writes at the position again");

  struct flock lock = {F_WRLCK, SEEK_SET, 0, 0, 0};
  check(FAILS_WITH(fcntl(fd, F_SETLK, &lock), EBADF), "F_WRLCK on a read-only fd fails with EBADF");
  lock.l_type = F_RDLCK;
  check(fcntl(fd, F_SETLK, &lock) == 0, "F_RDLCK on a read-only fd");
  lock.l_type = F_WRLCK;
  check(fcntl(rw, F_SETLK, &lock) == 0, "F_WRLCK on a read-write fd");
  check(fcntl(rw, F_GETLK, &lock) == 0 && lock.l_type == F_UNLCK, "F_GETLK finds no conflicting lock");
  lock.l_type = 7;
  check(FAILS_WITH(fcntl(rw, F_SETLK, &lock), EINVAL), "a bad l_type fails with EINVAL");
  lock.l_type = F_UNLCK;
  check(fcntl(rw, F_SETLKW, &lock) == 0, "F_UNLCK");
  lock.l_type = F_WRLCK;
  lock.l_pid = 1;
  check(FAILS_WITH(fcntl(rw, F_OFD_SETLK, &lock), EINVAL), "F_OFD_SETLK with l_pid != 0 fails with EINVAL");
  lock.l_pid = 0;
  check(fcntl(rw, F_OFD_SETLK, &lock) == 0, "F_OFD_SETLK");

  int n = -1;
  check(fcntl(fd, F_SETFL, 0) == 0 && lseek(fd, 3, SEEK_SET) == 3 && ioctl(fd, FIONREAD, &n) == 0 && n == 7,
        "FIONREAD on a file: the bytes after the position");
  char termios[36];
  check(FAILS_WITH(ioctl(fd, TCGETS, termios), ENOTTY), "TCGETS on a file fails with ENOTTY");
  check(FAILS_WITH(ioctl(fd, 0x1234, 0), ENOTTY), "an unknown ioctl fails with ENOTTY");
  check(FAILS_WITH(ioctl(99, FIONREAD, &n), EBADF), "ioctl on a closed fd fails with EBADF");
  check(isatty(1) == 1, "the console is a terminal");
  check(isatty(fd) == 0 && errno == ENOTTY, "a file isn't");
  struct winsize ws = {0};
  check(ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0, "TIOCGWINSZ on the console");

  int p[2];
  check(pipe2(p, O_NONBLOCK) == 0, "pipe2(O_NONBLOCK)");
  check(ACC_AND(fcntl(p[0], F_GETFL)) == (O_RDONLY | O_NONBLOCK) &&
            ACC_AND(fcntl(p[1], F_GETFL)) == (O_WRONLY | O_NONBLOCK),
        "both ends are O_NONBLOCK");
  check(FAILS_WITH(read(p[0], buf, 4), EAGAIN), "reading an empty O_NONBLOCK pipe fails with EAGAIN");
  check(write(p[1], "hello", 5) == 5 && ioctl(p[0], FIONREAD, &n) == 0 && n == 5, "FIONREAD on a pipe");
  check(read(p[0], buf, 16) == 5 && memcmp(buf, "hello", 5) == 0, "read it");
  static char chunk[1024];
  long total = 0, got;
  while ((got = write(p[1], chunk, sizeof(chunk))) > 0) total += got;
  check(got == -1 && errno == EAGAIN, "writing a full O_NONBLOCK pipe fails with EAGAIN");
  check(total == 65536, "it filled at 64 KiB");
  check(FAILS_WITH(lseek(p[0], 0, SEEK_SET), ESPIPE), "lseek on a pipe fails with ESPIPE");
  check(mmap(0, 4096, PROT_READ, MAP_PRIVATE, p[0], 0) == MAP_FAILED && errno == ENODEV, "mmap of a pipe fails with ENODEV");
  check(FAILS_WITH(openat(p[0], "data.txt", O_RDONLY), ENOTDIR), "openat with a pipe as dirfd fails with ENOTDIR");
  check(FAILS_WITH(ioctl(p[0], TCGETS, termios), ENOTTY), "TCGETS on a pipe fails with ENOTTY");
  close(p[0]);
  close(p[1]);

  check(pipe2(p, O_NONBLOCK) == 0 && close(p[1]) == 0 && read(p[0], buf, 4) == 0,
        "an O_NONBLOCK pipe with no writer reads 0, not EAGAIN");
  close(p[0]);

  check(pipe(p) == 0 && ACC_AND(fcntl(p[0], F_GETFL)) == O_RDONLY, "a plain pipe blocks");
  int on = 1;
  check(ioctl(p[0], FIONBIO, &on) == 0 && ACC_AND(fcntl(p[0], F_GETFL)) == (O_RDONLY | O_NONBLOCK) &&
            FAILS_WITH(read(p[0], buf, 1), EAGAIN),
        "FIONBIO 1 makes it O_NONBLOCK");
  on = 0;
  check(ioctl(p[0], FIONBIO, &on) == 0 && ACC_AND(fcntl(p[0], F_GETFL)) == O_RDONLY, "FIONBIO 0 clears it");

  fcntl(p[0], F_SETFL, O_NONBLOCK);
  pid_t child = fork();
  if (child == 0) {
    int saw = (fcntl(p[0], F_GETFL) & O_NONBLOCK) != 0;
    fcntl(p[0], F_SETFL, 0);
    _exit(saw ? 0 : 1);
  }
  int status = 0;
  check(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
        "the child sees the parent's O_NONBLOCK");
  check((fcntl(p[0], F_GETFL) & O_NONBLOCK) == 0, "the parent sees the child clear it");
  close(p[0]);
  close(p[1]);

  char *map = mmap(0, 4096, PROT_READ, MAP_PRIVATE, fd, 0);
  check(map != MAP_FAILED && map[0] == '0' && map[9] == '9', "mmap of a file");
  check(mmap(0, 4096, PROT_READ, MAP_PRIVATE, w, 0) == MAP_FAILED && errno == EACCES, "mmap of an O_WRONLY file fails with EACCES");
  int dir = open(".", O_RDONLY | O_DIRECTORY);
  check(dir >= 0 && FAILS_WITH(read(dir, buf, 4), EISDIR), "read of a directory fails with EISDIR");
  int viadir = openat(dir, "data.txt", O_RDONLY);
  check(viadir >= 0 && read(viadir, buf, 3) == 3 && memcmp(buf, "012", 3) == 0, "openat with a directory fd");
  close(viadir);
  close(dir);
  close(w);
  close(rw);
  close(d);
  close(fd);

  check(FAILS_WITH(lseek(0, 0, SEEK_CUR), ESPIPE), "lseek on the console fails with ESPIPE");
  check(read(0, buf, 1) == 1 && buf[0] == 'x', "read x from the console");
  check(fcntl(0, F_SETFL, O_NONBLOCK) == 0, "make the console O_NONBLOCK");
  long r;
  int tries = 0;
  while ((r = read(0, buf, 1)) == -1 && errno == EAGAIN && tries++ < 100000) sched_yield();
  check(r == 1 && buf[0] == 'y', "an O_NONBLOCK read gets y (EAGAIN until it arrives)");
  while ((r = read(0, buf, 1)) == -1 && errno == EAGAIN && tries++ < 100000) sched_yield();
  check(r == 0, "then the end of the input: 0");
  fcntl(0, F_SETFL, 0);

  puts(failures ? "*** t1 FAILED" : "*** t1 passed");
  return failures;
}

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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

int main(void) {
  puts("*** t0 start");

  check(write(1, "", 0) == 0, "write of 0 bytes returns 0");
  char in[8] = {0};
  long got = 0, n;
  while (got < 3 && (n = read(0, in + got, 3 - got)) > 0) got += n;
  check(got == 3 && memcmp(in, "abc", 3) == 0, "read the console's input");
  check(read(0, in, 1) == 0, "read at the console's end of input returns 0");

  int fd = open("/hello.txt", O_RDONLY);
  char buf[64] = {0};
  check(fd >= 0, "open /hello.txt");
  check(read(fd, buf, sizeof(buf)) == 15 && memcmp(buf, "hello from ext2", 15) == 0,
        "read /hello.txt");
  check(lseek(fd, 6, SEEK_SET) == 6 && read(fd, buf, 4) == 4 && memcmp(buf, "from", 4) == 0,
        "lseek then read");
  check(close(fd) == 0, "close");
  check(close(fd) == -1 && errno == EBADF, "close again fails with EBADF");
  check(FAILS_WITH(open("/no/such/file", O_RDONLY), ENOENT), "open a missing file fails with ENOENT");

  fd = open("/created.txt", O_RDWR | O_CREAT | O_EXCL, 0644);
  check(fd >= 0, "create /created.txt");
  check(write(fd, "written", 7) == 7, "write to it");
  check(lseek(fd, 0, SEEK_SET) == 0 && read(fd, buf, sizeof(buf)) == 7 && memcmp(buf, "written", 7) == 0,
        "read it back");
  check(close(fd) == 0, "close it");
  check(FAILS_WITH(open("/created.txt", O_RDWR | O_CREAT | O_EXCL, 0644), EEXIST),
        "O_EXCL on an existing file fails with EEXIST");

  pid_t child = fork();
  if (child == 0) {
    int f = open("/created.txt", O_RDONLY);
    char b[8];
    _exit(f >= 0 && read(f, b, 7) == 7 && memcmp(b, "written", 7) == 0 ? 42 : 1);
  }
  int status = 0;
  check(child > 0, "fork");
  check(waitpid(child, &status, 0) == child, "waitpid");
  check(WIFEXITED(status) && WEXITSTATUS(status) == 42, "the child saw the file and exited 42");

  check(preserved() == 0, "registers preserved across sched_yield");

  check(syscall(9999, 0, 0, 0, 0, 0, 0) == -1 && errno == ENOSYS, "unknown system call fails with ENOSYS");

  puts(failures ? "*** t0 FAILED" : "*** t0 passed");
  return failures;
}

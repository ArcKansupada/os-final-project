#include <fcntl.h>
#include <stdio.h>
#include <string.h>
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

static int reads(const char *path, int flags, const char *expect) {
  int fd = open(path, flags);
  if (fd < 0) return 0;
  char buf[32] = {0};
  size_t n = strlen(expect);
  int ok = read(fd, buf, n) == (long)n && memcmp(buf, expect, n) == 0;
  close(fd);
  return ok;
}

static int reads_at(int dirfd, const char *path, const char *expect) {
  int fd = openat(dirfd, path, O_RDONLY);
  if (fd < 0) return 0;
  char buf[32] = {0};
  size_t n = strlen(expect);
  int ok = read(fd, buf, n) == (long)n && memcmp(buf, expect, n) == 0;
  close(fd);
  return ok;
}

static int cwd_is(const char *expect) {
  char buf[256];
  if (getcwd(buf, sizeof(buf)) == NULL) return 0;
  return strlen(buf) == strlen(expect) && memcmp(buf, expect, strlen(expect)) == 0;
}

static int exits_with(pid_t child, int code) {
  int status = 0;
  return child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == code;
}

static char big[100000];

int main(int argc, char **argv) {
  if (argc > 1) return 7;

  puts("*** t2 start");
  char *child_argv[] = {"init", "child", NULL};
  char *envp[] = {NULL};

  check(reads("/data.txt", O_RDONLY, "0123"), "an absolute path");
  check(reads("data.txt", O_RDONLY, "0123"), "a relative path");
  check(reads("//sub///inner.txt", O_RDONLY, "inner"), "repeated slashes");
  check(reads("/sub/./deeper/../inner.txt", O_RDONLY, "inner"), ". and ..");
  check(reads("/../../data.txt", O_RDONLY, "0123"), ".. at the root stays at the root");
  check(FAILS_WITH(open("/data.txt/", O_RDONLY), ENOTDIR), "a file with a trailing slash fails with ENOTDIR");
  check(FAILS_WITH(open("/data.txt/x", O_RDONLY), ENOTDIR), "a file in the middle of a path fails with ENOTDIR");
  check(FAILS_WITH(open("/missing", O_RDONLY), ENOENT), "a missing file fails with ENOENT");
  check(FAILS_WITH(open("/missing/x", O_RDONLY), ENOENT), "a missing directory fails with ENOENT");
  check(FAILS_WITH(open("", O_RDONLY), ENOENT), "an empty path fails with ENOENT");

  char name[300];
  name[0] = '/';
  memset(name + 1, 'a', 256);
  name[257] = '\0';
  check(FAILS_WITH(open(name, O_RDONLY), ENAMETOOLONG), "a 256-byte name fails with ENAMETOOLONG");
  name[256] = '\0';
  check(FAILS_WITH(open(name, O_RDONLY), ENOENT), "a 255-byte name is just missing");

  int fd = open("/sub/", O_RDONLY | O_DIRECTORY);
  check(fd >= 0, "a directory with a trailing slash and O_DIRECTORY");
  close(fd);
  check(FAILS_WITH(open("/data.txt", O_RDONLY | O_DIRECTORY), ENOTDIR), "O_DIRECTORY on a file fails with ENOTDIR");
  check(FAILS_WITH(open("/sub", O_WRONLY), EISDIR), "writing a directory fails with EISDIR");

  check(reads("/rel_link", O_RDONLY, "0123"), "a relative symlink");
  check(reads("/abs_link", O_RDONLY, "inner"), "an absolute symlink");
  check(reads("/dir_link/inner.txt", O_RDONLY, "inner"), "a symlink in the middle of a path");
  check(reads("/dir_link/deeper/leaf.txt", O_RDONLY, "leaf"), "a symlink then more directories");
  check(reads("/sub/up_link", O_RDONLY, "0123"), "a symlink to ../");
  check(reads("/long_link", O_RDONLY, "leaf"), "a symlink too long to live in the inode");
  check(reads("/chain_link", O_RDONLY, "0123"), "a symlink to a symlink");
  check(FAILS_WITH(open("/loop_a", O_RDONLY), ELOOP), "a symlink loop fails with ELOOP");
  check(FAILS_WITH(open("/self_link", O_RDONLY), ELOOP), "a symlink to itself fails with ELOOP");
  check(FAILS_WITH(open("/missing_link", O_RDONLY), ENOENT), "a dangling symlink fails with ENOENT");
  check(FAILS_WITH(open("/rel_link", O_RDONLY | O_NOFOLLOW), ELOOP), "O_NOFOLLOW on a symlink fails with ELOOP");
  fd = open("/dir_link/", O_RDONLY | O_DIRECTORY);
  check(fd >= 0, "a symlink to a directory with a trailing slash");
  close(fd);
  check(FAILS_WITH(open("/rel_link/", O_RDONLY), ENOTDIR), "a symlink to a file with a trailing slash fails with ENOTDIR");

  fd = open("/null_link", O_WRONLY);
  check(fd >= 0 && write(fd, "hello", 5) == 5, "a symlink into /dev");
  close(fd);

  fd = open("/sub/new.txt", O_CREAT | O_RDWR, 0644);
  check(fd >= 0 && write(fd, "made", 4) == 4, "O_CREAT in a subdirectory");
  close(fd);
  check(reads("/sub/new.txt", O_RDONLY, "made"), "the new file is there");
  check(FAILS_WITH(open("/sub/new.txt", O_CREAT | O_EXCL | O_RDWR, 0644), EEXIST), "O_EXCL on it fails with EEXIST");
  check(FAILS_WITH(open("/nodir/new.txt", O_CREAT | O_RDWR, 0644), ENOENT), "O_CREAT in a missing directory fails with ENOENT");
  check(FAILS_WITH(open("/newdir/", O_CREAT | O_RDWR, 0644), EISDIR), "O_CREAT with a trailing slash fails with EISDIR");
  check(FAILS_WITH(open("/dev/newdev", O_CREAT | O_RDWR, 0644), EACCES), "O_CREAT in /dev fails with EACCES");
  fd = open("/missing_link", O_CREAT | O_WRONLY, 0644);
  check(fd >= 0, "O_CREAT through a dangling symlink");
  close(fd);
  fd = open("/nowhere.txt", O_RDONLY);
  check(fd >= 0, "it created the symlink's target");
  close(fd);
  check(FAILS_WITH(open("/loop_a", O_CREAT | O_EXCL | O_RDWR, 0644), EEXIST),
        "O_CREAT | O_EXCL doesn't follow a final symlink");

  int sub = open("/sub", O_RDONLY | O_DIRECTORY);
  check(sub >= 0 && reads_at(sub, "inner.txt", "inner"), "openat with a directory fd");
  check(reads_at(sub, "../data.txt", "0123"), "openat with .. from a directory fd");
  check(reads_at(sub, "/data.txt", "0123"), "openat with an absolute path ignores the directory fd");
  check(reads_at(AT_FDCWD, "data.txt", "0123"), "openat with AT_FDCWD");
  check(reads_at(99, "/data.txt", "0123"), "openat with an absolute path ignores a bad fd");
  check(FAILS_WITH(openat(99, "data.txt", O_RDONLY), EBADF), "openat with a bad fd fails with EBADF");
  int file = open("/data.txt", O_RDONLY);
  check(FAILS_WITH(openat(file, "x", O_RDONLY), ENOTDIR), "openat with a file as the directory fails with ENOTDIR");

  char buf[64];
  fd = open("/dev/null", O_RDWR);
  check(fd >= 0 && write(fd, "0123456789", 10) == 10, "write to /dev/null");
  check(read(fd, buf, sizeof(buf)) == 0, "read from /dev/null is the end of the file");
  check(lseek(fd, 100, SEEK_SET) == 0, "lseek on /dev/null");
  check(isatty(fd) == 0, "/dev/null isn't a terminal");
  close(fd);
  fd = open("/dev/zero", O_RDONLY);
  memset(buf, 0x55, sizeof(buf));
  check(fd >= 0 && read(fd, buf, 8) == 8 && buf[0] == 0 && buf[7] == 0 && buf[8] == 0x55, "read from /dev/zero");
  memset(big, 0x55, sizeof(big));
  check(read(fd, big, sizeof(big)) == sizeof(big) && big[0] == 0 && big[sizeof(big) - 1] == 0,
        "one big read from /dev/zero fills it");
  char *zeros = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd, 0);
  check(zeros != MAP_FAILED && zeros[0] == 0 && zeros[4095] == 0, "mmap of /dev/zero is zeroes");
  if (zeros != MAP_FAILED) zeros[0] = 1;
  check(zeros != MAP_FAILED && zeros[0] == 1, "and writable");
  close(fd);

  int dev = open("/dev", O_RDONLY | O_DIRECTORY);
  check(dev >= 0, "open /dev as a directory");
  check(FAILS_WITH(read(dev, buf, 4), EISDIR), "read of /dev fails with EISDIR");
  fd = openat(dev, "zero", O_RDONLY);
  check(fd >= 0 && read(fd, buf, 1) == 1 && buf[0] == 0, "openat from /dev");
  close(fd);
  check(reads_at(dev, "../data.txt", "0123"), "openat with .. from /dev leaves it");
  check(reads("/dev/../sub/inner.txt", O_RDONLY, "inner"), "/dev/.. is the root");
  check(FAILS_WITH(open("/dev/missing", O_RDONLY), ENOENT), "a missing device fails with ENOENT");
  check(FAILS_WITH(open("/dev/null/x", O_RDONLY), ENOTDIR), "a device in the middle of a path fails with ENOTDIR");
  check(FAILS_WITH(open("/dev", O_WRONLY), EISDIR), "writing /dev fails with EISDIR");
  check(FAILS_WITH(open("/dev/null", O_RDONLY | O_DIRECTORY), ENOTDIR), "O_DIRECTORY on a device fails with ENOTDIR");
  check(FAILS_WITH(open("/dev/fake.txt", O_RDONLY), ENOENT), "/dev hides the disk's own /dev");
  fd = open("/proc", O_RDONLY | O_DIRECTORY);
  check(fd >= 0, "/proc is a directory");
  close(fd);
  check(FAILS_WITH(open("/proc/nope", O_RDONLY), ENOENT), "a missing /proc file fails with ENOENT");

  check(cwd_is("/"), "getcwd starts at /");
  check(chdir("sub") == 0 && cwd_is("/sub") && reads("inner.txt", O_RDONLY, "inner"), "chdir to a subdirectory");
  check(chdir("deeper") == 0 && cwd_is("/sub/deeper"), "chdir again");
  check(chdir("../..") == 0 && cwd_is("/"), "chdir ../..");
  check(chdir("/dev") == 0 && cwd_is("/dev") && reads("zero", O_RDONLY, "\0"), "chdir into /dev");
  pid_t child = fork();
  if (child == 0) _exit(cwd_is("/dev") && reads("zero", O_RDONLY, "\0") ? 0 : 1);
  check(exits_with(child, 0), "a child inherits the /dev working directory");
  check(chdir("..") == 0 && cwd_is("/"), "chdir .. out of /dev");
  check(FAILS_WITH(chdir("/data.txt"), ENOTDIR), "chdir to a file fails with ENOTDIR");
  check(FAILS_WITH(chdir("/missing"), ENOENT), "chdir to a missing directory fails with ENOENT");
  check(chdir("/dir_link") == 0 && cwd_is("/sub"), "chdir through a symlink");
  check(getcwd(buf, 3) == NULL && errno == ERANGE, "getcwd into a small buffer fails with ERANGE");
  check(chdir("/") == 0 && cwd_is("/"), "chdir /");

  check(FAILS_WITH(execve("/dev/null", child_argv, envp), EACCES), "execve of a device fails with EACCES");
  check(FAILS_WITH(execve("/sub", child_argv, envp), EACCES), "execve of a directory fails with EACCES");
  check(FAILS_WITH(execve("/missing", child_argv, envp), ENOENT), "execve of a missing file fails with ENOENT");
  check(FAILS_WITH(execveat(AT_FDCWD, "/init_link", child_argv, envp, AT_SYMLINK_NOFOLLOW), ELOOP),
        "execveat with AT_SYMLINK_NOFOLLOW on a symlink fails with ELOOP");
  check(FAILS_WITH(execveat(file, "", child_argv, envp, 0), ENOENT), "execveat with an empty path fails with ENOENT");
  int null = open("/dev/null", O_RDONLY);
  check(FAILS_WITH(execveat(null, "", child_argv, envp, AT_EMPTY_PATH), EACCES),
        "execveat AT_EMPTY_PATH of a device fails with EACCES");

  child = fork();
  if (child == 0) _exit(execve("/init_link", child_argv, envp) == -1 ? 100 + errno : 1);
  check(exits_with(child, 7), "execve through a symlink");
  int sbin = open("/sbin", O_RDONLY | O_DIRECTORY);
  child = fork();
  if (child == 0) _exit(execveat(sbin, "init", child_argv, envp, 0) == -1 ? 100 + errno : 1);
  check(exits_with(child, 7), "execveat from a directory fd");
  int self = open("/sbin/init", O_RDONLY);
  child = fork();
  if (child == 0) _exit(execveat(self, "", child_argv, envp, AT_EMPTY_PATH) == -1 ? 100 + errno : 1);
  check(exits_with(child, 7), "execveat with AT_EMPTY_PATH");

  puts(failures ? "*** t2 FAILED" : "*** t2 passed");
  return failures;
}

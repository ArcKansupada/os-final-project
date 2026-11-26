#pragma once

#include <cstdint>

#include "async/spin_lock.h"
#include "async/sync.h"
#include "lib/shared.h"
#include "syscall.h"

class Node;

namespace vfs {
class PseudoNode;
}

namespace impl {

bool current_thread_dead();

constexpr uint32_t POLLIN = 0x001;
constexpr uint32_t POLLPRI = 0x002;
constexpr uint32_t POLLOUT = 0x004;
constexpr uint32_t POLLERR = 0x008;
constexpr uint32_t POLLHUP = 0x010;
constexpr uint32_t POLLNVAL = 0x020;
constexpr uint32_t POLLRDNORM = 0x040;
constexpr uint32_t POLLWRNORM = 0x100;

struct PollWaiter {
  PollWaiter *next = nullptr;
  PollWaiter *prev = nullptr;
  virtual void notify(uint32_t events) = 0;
  virtual ~PollWaiter() = default;
};

class PollQueue {
  SpinLock lock;
  PollWaiter *head = nullptr;

public:
  void add(PollWaiter *w);
  void remove(PollWaiter *w);
  void notify(uint32_t events);
};

struct FileStat {
  uint64_t dev = 0;
  uint64_t ino = 0;
  uint64_t nlink = 1;
  uint32_t mode = 0;
  uint32_t uid = 0;
  uint32_t gid = 0;
  uint64_t rdev = 0;
  int64_t size = 0;
  uint64_t blksize = 4096;
  uint64_t blocks = 0;
  int64_t atime = 0, mtime = 0, ctime = 0;
};

class OpenFile {
protected:
  OpenFile(bool rd, bool wr, uint32_t status = 0)
      : rd(rd), wr(wr), status_flags(status & Syscall::O_STATUS_FLAGS) {}

public:
  const bool rd;
  const bool wr;

  virtual ~OpenFile() = default;

  OpenFile(const OpenFile &) = delete;
  OpenFile &operator=(const OpenFile &) = delete;

  uint32_t status() const { return __atomic_load_n(&status_flags, __ATOMIC_ACQUIRE); }
  bool nonblocking() const { return status() & Syscall::O_NONBLOCK; }
  void set_status(uint32_t flags) {
    __atomic_store_n(&status_flags, flags & Syscall::O_STATUS_FLAGS, __ATOMIC_RELEASE);
  }
  uint32_t get_fl() const {
    uint32_t mode = rd && wr ? Syscall::O_RDWR : wr ? Syscall::O_WRONLY : Syscall::O_RDONLY;
    return mode | status();
  }

  virtual sync<int64_t> read(char *buf, uint64_t len);
  virtual sync<int64_t> write(const char *buf, uint64_t len);

  virtual sync<int64_t> read_user(char *ubuf, uint64_t len);
  virtual sync<int64_t> write_user(const char *ubuf, uint64_t len);

  virtual bool fill_reads() const { return false; }

  virtual sync<int64_t> seek(int64_t offset, uint64_t whence);
  virtual sync<int64_t> pread(char *buf, uint64_t len, uint64_t offset);
  virtual sync<int64_t> pwrite(const char *buf, uint64_t len, uint64_t offset);

  virtual sync<int64_t> ioctl(uint64_t cmd, uint64_t arg);

  virtual sync<int64_t> mmap(void *addr, uint64_t length, int prot, int flags, uint64_t offset);

  virtual sync<int64_t> stat(FileStat &out);
  virtual uint32_t mode() const { return 0; }

  virtual sync<int64_t> fsync();
  virtual sync<int64_t> truncate(uint64_t length);

  virtual StrongRef<Node> node();
  virtual StrongRef<vfs::PseudoNode> pseudo();

  virtual uint32_t poll_ready();
  virtual PollQueue *poll_queue() { return nullptr; }

private:
  uint32_t status_flags;
};

}

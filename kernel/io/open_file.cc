#include "io/open_file.h"

#include <algorithm>

#include "filesystem/ext2.h"
#include "memory/vmm.h"

namespace impl {

void PollQueue::add(PollWaiter *w) {
  LockGuard g{lock};
  w->prev = nullptr;
  w->next = head;
  if (head) head->prev = w;
  head = w;
}

void PollQueue::remove(PollWaiter *w) {
  LockGuard g{lock};
  if (w->prev) w->prev->next = w->next;
  else if (head == w) head = w->next;
  if (w->next) w->next->prev = w->prev;
  w->next = w->prev = nullptr;
}

void PollQueue::notify(uint32_t events) {
  LockGuard g{lock};
  for (auto *w = head; w; w = w->next) w->notify(events);
}

sync<int64_t> OpenFile::read(char *, uint64_t) { co_return Syscall::EINVAL; }
sync<int64_t> OpenFile::write(const char *, uint64_t) { co_return Syscall::EINVAL; }

sync<int64_t> OpenFile::read_user(char *ubuf, uint64_t len) {
  char bounce[FRAME_SIZE];
  uint64_t total = 0;
  while (total < len && !current_thread_dead()) {
    uint64_t n = std::min<uint64_t>(FRAME_SIZE, len - total);
    int64_t count = co_await read(bounce, n);
    if (count < 0) co_return total ? int64_t(total) : count;
    if (current_thread_dead()) break;
    uint64_t left = VMM::copy_user_(ubuf + total, bounce, count);
    total += count - left;
    if (left) co_return total ? int64_t(total) : Syscall::EFAULT;
    if (uint64_t(count) < n || !fill_reads()) break;
  }
  co_return total;
}

sync<int64_t> OpenFile::write_user(const char *ubuf, uint64_t len) {
  char bounce[FRAME_SIZE];
  uint64_t total = 0;
  while (total < len && !current_thread_dead()) {
    uint64_t n = std::min<uint64_t>(FRAME_SIZE, len - total);
    uint64_t left = VMM::copy_user_(bounce, ubuf + total, n);
    if (left == n) co_return total ? int64_t(total) : Syscall::EFAULT;
    int64_t count = co_await write(bounce, n - left);
    if (count < 0) co_return total ? int64_t(total) : count;
    total += count;
    if (left) co_return total ? int64_t(total) : Syscall::EFAULT;
    if (uint64_t(count) < n) break;
  }
  co_return total;
}

sync<int64_t> OpenFile::seek(int64_t, uint64_t) { co_return Syscall::ESPIPE; }
sync<int64_t> OpenFile::pread(char *, uint64_t, uint64_t) { co_return Syscall::ESPIPE; }
sync<int64_t> OpenFile::pwrite(const char *, uint64_t, uint64_t) { co_return Syscall::ESPIPE; }
sync<int64_t> OpenFile::ioctl(uint64_t, uint64_t) { co_return Syscall::ENOTTY; }
sync<int64_t> OpenFile::mmap(void *, uint64_t, int, int, uint64_t) { co_return Syscall::ENODEV; }

sync<int64_t> OpenFile::stat(FileStat &out) {
  out = FileStat{};
  out.mode = mode();
  co_return 0;
}

sync<int64_t> OpenFile::fsync() { co_return Syscall::EINVAL; }
sync<int64_t> OpenFile::truncate(uint64_t) { co_return Syscall::EINVAL; }

StrongRef<Node> OpenFile::node() { return {}; }

uint32_t OpenFile::poll_ready() {
  return (rd ? POLLIN | POLLRDNORM : 0) | (wr ? POLLOUT | POLLWRNORM : 0);
}

}

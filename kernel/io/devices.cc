#include "io/devices.h"

#include "filesystem/vfs.h"
#include "memory/vmm.h"

namespace {

class NullFile : public vfs::DeviceFile {
public:
  using DeviceFile::DeviceFile;

  sync<int64_t> read(char *, uint64_t) override { co_return 0; }
  sync<int64_t> write(const char *, uint64_t len) override { co_return len; }
  sync<int64_t> pread(char *, uint64_t, uint64_t) override { co_return 0; }
  sync<int64_t> pwrite(const char *, uint64_t len, uint64_t) override { co_return len; }
};

class ZeroFile : public vfs::DeviceFile {
public:
  using DeviceFile::DeviceFile;

  bool fill_reads() const override { return true; }
  sync<int64_t> read(char *buf, uint64_t len) override {
    memset(buf, 0, len);
    co_return len;
  }
  sync<int64_t> write(const char *, uint64_t len) override { co_return len; }
  sync<int64_t> pread(char *buf, uint64_t len, uint64_t) override {
    memset(buf, 0, len);
    co_return len;
  }
  sync<int64_t> pwrite(const char *, uint64_t len, uint64_t) override { co_return len; }
  sync<int64_t> mmap(void *addr, uint64_t length, int prot, int flags, uint64_t) override {
    void *p = co_await VMM::mmap(addr, length, prot, flags | VMM::MAP_ANONYMOUS, StrongRef<Node>{}, 0);
    if (p == VMM::MAP_FAILED) co_return Syscall::EINVAL;
    co_return (int64_t)(uintptr_t)p;
  }
};

template <typename T>
StrongRef<impl::OpenFile> opener(const vfs::CharDevice &device, bool rd, bool wr, uint32_t status) {
  return StrongRef<impl::OpenFile>{new T(device, rd, wr, status)};
}

}

void devices::init() {
  vfs::add_device("null", 0666, 1, 3, opener<NullFile>);
  vfs::add_device("zero", 0666, 1, 5, opener<ZeroFile>);
}

#pragma once

#include <cstdint>

#include "async/spin_lock.h"
#include "async/sync.h"
#include "filesystem/ext2.h"
#include "io/open_file.h"
#include "lib/shared.h"
#include "syscall.h"

namespace impl {
struct PCB;
}

namespace vfs {

constexpr uint64_t PATH_MAX = 4096;
constexpr uint64_t NAME_MAX = 255;
constexpr uint32_t MAX_SYMLINKS = 40;

constexpr uint64_t PSEUDO_DEV = 2;

class PseudoDir;

class PseudoNode {
public:
  const uint32_t mode;
  const uint64_t ino;
  const char *const name;
  PseudoDir *parent = nullptr;
  WeakRef<PseudoNode> self{};

  PseudoNode(uint32_t mode, const char *name);
  virtual ~PseudoNode() = default;

  PseudoNode(const PseudoNode &) = delete;
  PseudoNode &operator=(const PseudoNode &) = delete;

  bool is_dir() const { return (mode & Syscall::S_IFMT) == Syscall::S_IFDIR; }
  bool is_symlink() const { return (mode & Syscall::S_IFMT) == Syscall::S_IFLNK; }

  virtual sync<StrongRef<PseudoNode>> lookup(const char *name);
  virtual sync<int64_t> open(bool rd, bool wr, uint32_t status, StrongRef<impl::OpenFile> &out);
  virtual sync<int64_t> readlink(char *buf, uint64_t len);
  virtual sync<int64_t> stat(impl::FileStat &out);

  int64_t path(char *buf, uint64_t size) const;
};

class PseudoDir : public PseudoNode {
  struct Entry {
    StrongRef<PseudoNode> node;
    Entry *next;
  };
  SpinLock lock;
  Entry *head = nullptr;
  Entry *tail = nullptr;

public:
  explicit PseudoDir(const char *name, uint32_t perms = 0555);
  ~PseudoDir() override;

  bool add(StrongRef<PseudoNode> node, PseudoNode *raw);
  StrongRef<PseudoNode> at(uint64_t index);

  sync<StrongRef<PseudoNode>> lookup(const char *name) override;
  sync<int64_t> stat(impl::FileStat &out) override;
};

class CharDevice : public PseudoNode {
public:
  using Opener = StrongRef<impl::OpenFile> (*)(const CharDevice &device, bool rd, bool wr, uint32_t status);

  const uint32_t major;
  const uint32_t minor;
  const Opener opener;

  CharDevice(const char *name, uint32_t perms, uint32_t major, uint32_t minor, Opener opener)
      : PseudoNode(Syscall::S_IFCHR | perms, name), major(major), minor(minor), opener(opener) {}

  sync<int64_t> open(bool rd, bool wr, uint32_t status, StrongRef<impl::OpenFile> &out) override;
  sync<int64_t> stat(impl::FileStat &out) override;
};

class DeviceFile : public impl::OpenFile {
  const uint32_t device_mode;
  const uint64_t device_ino;
  const uint64_t rdev;

public:
  DeviceFile(const CharDevice &device, bool rd, bool wr, uint32_t status);

  uint32_t mode() const override { return device_mode; }
  sync<int64_t> seek(int64_t offset, uint64_t whence) override;
  sync<int64_t> stat(impl::FileStat &out) override;
};

class PseudoDirFile : public impl::OpenFile {
  StrongRef<PseudoNode> dir;

public:
  PseudoDirFile(StrongRef<PseudoNode> dir, uint32_t status) : OpenFile(true, false, status), dir(dir) {}

  sync<int64_t> read(char *buf, uint64_t len) override;
  sync<int64_t> stat(impl::FileStat &out) override;
  StrongRef<PseudoNode> pseudo() override { return dir; }
};

struct Location {
  StrongRef<Node> node{};
  StrongRef<PseudoNode> pseudo{};

  bool empty() const { return node == nullptr && pseudo == nullptr; }
};

constexpr uint32_t FOLLOW = 1;
constexpr uint32_t EMPTY_PATH = 2;

struct Lookup {
  Location found{};
  Location parent{};
  StrongRef<impl::OpenFile> file{};
  char name[NAME_MAX + 1]{};
  bool must_be_dir = false;
};

Location root();

void init();
bool mount(StrongRef<PseudoNode> top);
PseudoDir *dev();
PseudoDir *proc();
bool add_device(const char *name, uint32_t perms, uint32_t major, uint32_t minor, CharDevice::Opener opener);

uint64_t makedev(uint32_t major, uint32_t minor);

sync<bool> is_dir(const Location &at);
sync<bool> is_symlink(const Location &at);
sync<bool> is_file(const Location &at);

sync<int64_t> resolve(impl::PCB *process, uint64_t dirfd, const char *path, uint32_t flags, Lookup &out);

sync<int64_t> open(const Location &at, uint64_t flags, StrongRef<impl::OpenFile> &out);

sync<int64_t> path_of(const Location &at, char *buf, uint64_t size);

}

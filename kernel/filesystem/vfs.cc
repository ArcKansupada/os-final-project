#include "filesystem/vfs.h"

#include <utility>

#include "kernel_main.h"
#include "memory/vmm.h"
#include "structures/pcb.h"

namespace vfs {

namespace {

uint64_t next_ino = 1;

constexpr int MAX_MOUNTS = 8;
StrongRef<PseudoNode> mounts[MAX_MOUNTS];
int mount_count = 0;
SpinLock mount_lock;

PseudoDir *dev_dir = nullptr;
PseudoDir *proc_dir = nullptr;

bool streq(const char *a, const char *b) {
  while (*a && *a == *b) {
    a++;
    b++;
  }
  return *a == *b;
}

uint64_t length(const char *s) {
  uint64_t n = 0;
  while (s[n]) n++;
  return n;
}

StrongRef<PseudoNode> mounted(const char *name) {
  LockGuard g{mount_lock};
  for (int i = 0; i < mount_count; i++) {
    if (streq(mounts[i]->name, name)) return mounts[i];
  }
  return {};
}

bool is_root(const StrongRef<Node> &node) { return node->number == impl::fs->root->number; }

template <typename T, typename... Args>
StrongRef<PseudoNode> make(T *&raw, Args &&...args) {
  raw = new T(std::forward<Args>(args)...);
  StrongRef<PseudoNode> ref{static_cast<PseudoNode *>(raw)};
  raw->self = WeakRef<PseudoNode>{ref};
  return ref;
}

sync<Location> parent_of(const Location &at) {
  if (at.node != nullptr) {
    if (is_root(at.node)) co_return root();
    auto up = co_await impl::fs->find(at.node, "..", false);
    if (up.error) co_return root();
    co_return Location{up.file, {}};
  }
  if (at.pseudo->parent == nullptr) co_return root();
  co_return Location{{}, StrongRef<PseudoNode>{at.pseudo->parent->self}};
}

sync<Location> lookup_child(const Location &at, const char *name) {
  if (at.node != nullptr) {
    if (is_root(at.node)) {
      auto top = mounted(name);
      if (top != nullptr) co_return Location{{}, top};
    }
    auto entry = co_await at.node->find_entry_inode(name);
    if (entry == nullptr) co_return Location{};
    auto child = co_await impl::fs->get_node(entry->inode);
    co_return Location{child, {}};
  }
  co_return Location{{}, co_await at.pseudo->lookup(name)};
}

sync<int64_t> read_link(const Location &at, char *buf, uint64_t size) {
  if (at.pseudo != nullptr) co_return co_await at.pseudo->readlink(buf, size);
  uint32_t n = co_await at.node->size_in_bytes();
  if (n > size) co_return Syscall::ENAMETOOLONG;
  co_await at.node->get_symbol(buf);
  co_return n;
}

struct PathBuffers {
  char *path = new char[PATH_MAX + 1];
  char *next = new char[PATH_MAX + 1];
  char *link = new char[PATH_MAX + 1];
  ~PathBuffers() {
    delete[] path;
    delete[] next;
    delete[] link;
  }
};

sync<int64_t> walk(Location cur, const char *path, uint32_t flags, Lookup &out) {
  uint64_t len = length(path);
  if (len > PATH_MAX) co_return Syscall::ENAMETOOLONG;

  PathBuffers bufs;
  memcpy(bufs.path, path, len + 1);
  char *p = bufs.path;
  uint32_t links = 0;

  while (true) {
    if (*p == '/') {
      cur = root();
      while (*p == '/') p++;
    }
    if (*p == '\0') {
      out.found = cur;
      out.parent = cur;
      co_return 0;
    }
    if (!co_await is_dir(cur)) co_return Syscall::ENOTDIR;

    char *end = p;
    while (*end && *end != '/') end++;
    uint64_t n = end - p;
    if (n > NAME_MAX) co_return Syscall::ENAMETOOLONG;
    char name[NAME_MAX + 1];
    memcpy(name, p, n);
    name[n] = '\0';

    char *rest = end;
    while (*rest == '/') rest++;
    bool last = *rest == '\0';
    bool slash = *end == '/';

    Location child;
    if (streq(name, ".")) child = cur;
    else if (streq(name, "..")) child = co_await parent_of(cur);
    else child = co_await lookup_child(cur, name);

    if (child.empty()) {
      if (last) {
        out.parent = cur;
        memcpy(out.name, name, n + 1);
        out.must_be_dir = slash;
      }
      co_return Syscall::ENOENT;
    }

    if (co_await is_symlink(child) && (!last || slash || (flags & FOLLOW))) {
      if (++links > MAX_SYMLINKS) co_return Syscall::ELOOP;
      int64_t target = co_await read_link(child, bufs.link, PATH_MAX);
      if (target < 0) co_return target;
      if (target == 0) co_return Syscall::ENOENT;
      uint64_t rest_len = length(rest);
      uint64_t total = target + (slash ? 1 + rest_len : 0);
      if (total > PATH_MAX) co_return Syscall::ENAMETOOLONG;
      memcpy(bufs.next, bufs.link, target);
      uint64_t pos = target;
      if (slash) {
        bufs.next[pos++] = '/';
        memcpy(bufs.next + pos, rest, rest_len);
        pos += rest_len;
      }
      bufs.next[pos] = '\0';
      std::swap(bufs.path, bufs.next);
      p = bufs.path;
      continue;
    }

    if (last) {
      if (slash && !co_await is_dir(child)) co_return Syscall::ENOTDIR;
      out.found = child;
      out.parent = cur;
      memcpy(out.name, name, n + 1);
      out.must_be_dir = slash;
      co_return 0;
    }

    cur = child;
    p = rest;
  }
}

}

PseudoNode::PseudoNode(uint32_t mode, const char *name)
    : mode(mode), ino(__atomic_fetch_add(&next_ino, 1, __ATOMIC_RELAXED)), name(name) {}

sync<StrongRef<PseudoNode>> PseudoNode::lookup(const char *) { co_return StrongRef<PseudoNode>{}; }

sync<int64_t> PseudoNode::open(bool, bool, uint32_t, StrongRef<impl::OpenFile> &) { co_return Syscall::ENXIO; }

sync<int64_t> PseudoNode::readlink(char *, uint64_t) { co_return Syscall::EINVAL; }

sync<int64_t> PseudoNode::stat(impl::FileStat &out) {
  out = impl::FileStat{};
  out.dev = PSEUDO_DEV;
  out.ino = ino;
  out.mode = mode;
  out.nlink = is_dir() ? 2 : 1;
  out.blksize = FRAME_SIZE;
  co_return 0;
}

int64_t PseudoNode::path(char *buf, uint64_t size) const {
  constexpr int MAX_DEPTH = 32;
  const PseudoNode *chain[MAX_DEPTH];
  int depth = 0;
  for (const PseudoNode *node = this; node != nullptr; node = node->parent) {
    if (depth == MAX_DEPTH) return Syscall::ENAMETOOLONG;
    chain[depth++] = node;
  }
  uint64_t pos = 0;
  for (int i = depth - 1; i >= 0; i--) {
    uint64_t n = length(chain[i]->name);
    if (pos + 1 + n + 1 > size) return Syscall::ERANGE;
    buf[pos++] = '/';
    memcpy(buf + pos, chain[i]->name, n);
    pos += n;
  }
  buf[pos] = '\0';
  return pos;
}

PseudoDir::PseudoDir(const char *name, uint32_t perms) : PseudoNode(Syscall::S_IFDIR | perms, name) {}

PseudoDir::~PseudoDir() {
  while (head) {
    auto *entry = head;
    head = head->next;
    delete entry;
  }
}

bool PseudoDir::add(StrongRef<PseudoNode> node, PseudoNode *raw) {
  LockGuard g{lock};
  for (auto *e = head; e; e = e->next) {
    if (streq(e->node->name, raw->name)) return false;
  }
  raw->parent = this;
  auto *entry = new Entry{node, nullptr};
  if (tail) tail->next = entry;
  else head = entry;
  tail = entry;
  return true;
}

StrongRef<PseudoNode> PseudoDir::at(uint64_t index) {
  LockGuard g{lock};
  for (auto *e = head; e; e = e->next) {
    if (index-- == 0) return e->node;
  }
  return {};
}

sync<StrongRef<PseudoNode>> PseudoDir::lookup(const char *name) {
  StrongRef<PseudoNode> found{};
  {
    LockGuard g{lock};
    for (auto *e = head; e; e = e->next) {
      if (streq(e->node->name, name)) {
        found = e->node;
        break;
      }
    }
  }
  co_return found;
}

sync<int64_t> PseudoDir::stat(impl::FileStat &out) {
  co_await PseudoNode::stat(out);
  co_return 0;
}

sync<int64_t> CharDevice::open(bool rd, bool wr, uint32_t status, StrongRef<impl::OpenFile> &out) {
  out = opener(*this, rd, wr, status);
  co_return out == nullptr ? Syscall::ENOMEM : 0;
}

sync<int64_t> CharDevice::stat(impl::FileStat &out) {
  co_await PseudoNode::stat(out);
  out.rdev = makedev(major, minor);
  co_return 0;
}

DeviceFile::DeviceFile(const CharDevice &device, bool rd, bool wr, uint32_t status)
    : OpenFile(rd, wr, status), device_mode(device.mode), device_ino(device.ino),
      rdev(makedev(device.major, device.minor)) {}

sync<int64_t> DeviceFile::seek(int64_t, uint64_t) { co_return 0; }

sync<int64_t> DeviceFile::stat(impl::FileStat &out) {
  out = impl::FileStat{};
  out.dev = PSEUDO_DEV;
  out.ino = device_ino;
  out.mode = device_mode;
  out.rdev = rdev;
  out.blksize = FRAME_SIZE;
  co_return 0;
}

sync<int64_t> PseudoDirFile::read(char *, uint64_t) { co_return Syscall::EISDIR; }

sync<int64_t> PseudoDirFile::stat(impl::FileStat &out) { co_return co_await dir->stat(out); }

Location root() { return Location{impl::fs->root, {}}; }

bool mount(StrongRef<PseudoNode> top) {
  LockGuard g{mount_lock};
  if (mount_count == MAX_MOUNTS) return false;
  for (int i = 0; i < mount_count; i++) {
    if (streq(mounts[i]->name, top->name)) return false;
  }
  mounts[mount_count++] = top;
  return true;
}

void init() {
  mount(make(dev_dir, "dev", 0755u));
  mount(make(proc_dir, "proc", 0555u));
}

PseudoDir *dev() { return dev_dir; }
PseudoDir *proc() { return proc_dir; }

bool add_device(const char *name, uint32_t perms, uint32_t major, uint32_t minor, CharDevice::Opener opener) {
  CharDevice *raw;
  auto node = make(raw, name, perms, major, minor, opener);
  return dev_dir->add(node, raw);
}

uint64_t makedev(uint32_t major, uint32_t minor) {
  uint64_t ma = major, mi = minor;
  return ((ma & 0xfffff000ull) << 32) | ((ma & 0xfffull) << 8) | ((mi & 0xffffff00ull) << 12) | (mi & 0xffull);
}

sync<bool> is_dir(const Location &at) {
  if (at.pseudo != nullptr) co_return at.pseudo->is_dir();
  if (at.node == nullptr) co_return false;
  co_return co_await at.node->is_dir();
}

sync<bool> is_symlink(const Location &at) {
  if (at.pseudo != nullptr) co_return at.pseudo->is_symlink();
  if (at.node == nullptr) co_return false;
  co_return co_await at.node->is_symlink();
}

sync<bool> is_file(const Location &at) {
  if (at.pseudo != nullptr || at.node == nullptr) co_return false;
  co_return co_await at.node->is_file();
}

sync<int64_t> resolve(impl::PCB *process, uint64_t dirfd, const char *path, uint32_t flags, Lookup &out) {
  Location start{};
  if (path[0] != '/') {
    if (dirfd == Syscall::AT_FDCWD) {
      start = process->get_cwd();
    } else {
      auto file = process->fd_table.get(dirfd);
      if (file == nullptr) co_return Syscall::EBADF;
      start = Location{file->node(), file->pseudo()};
      if (path[0] == '\0') {
        if (!(flags & EMPTY_PATH)) co_return Syscall::ENOENT;
        out.file = file;
        out.found = start;
        co_return 0;
      }
      if (start.empty()) co_return Syscall::ENOTDIR;
    }
    if (path[0] == '\0') {
      if (!(flags & EMPTY_PATH)) co_return Syscall::ENOENT;
      out.found = start;
      co_return 0;
    }
  }
  co_return co_await walk(start, path, flags, out);
}

sync<int64_t> open(const Location &at, uint64_t flags, StrongRef<impl::OpenFile> &out) {
  bool rd = (flags & Syscall::O_ACCMODE) != Syscall::O_WRONLY;
  bool wr = (flags & Syscall::O_ACCMODE) != Syscall::O_RDONLY;
  uint32_t status = flags & Syscall::O_STATUS_FLAGS;
  bool dir = co_await is_dir(at);

  if ((flags & Syscall::O_DIRECTORY) && !dir) co_return Syscall::ENOTDIR;
  if (dir && wr) co_return Syscall::EISDIR;

  if (at.node != nullptr) {
    if ((flags & Syscall::O_TRUNC) && !dir) co_await VMM::truncate_file(at.node, 0);
    out = StrongRef<impl::OpenFile>{new impl::SeekingFile(at.node, rd, wr, status)};
    co_return 0;
  }
  if (dir) {
    out = StrongRef<impl::OpenFile>{new PseudoDirFile(at.pseudo, status)};
    co_return 0;
  }
  co_return co_await at.pseudo->open(rd, wr, status, out);
}

sync<int64_t> path_of(const Location &at, char *buf, uint64_t size) {
  if (at.pseudo != nullptr) co_return at.pseudo->path(buf, size);

  struct Heap {
    char *p = new char[PATH_MAX];
    ~Heap() { delete[] p; }
  } heap;
  char *path = heap.p;
  uint64_t pos = PATH_MAX - 1;
  path[pos] = '\0';
  auto dir = at.node;
  while (!is_root(dir)) {
    auto parent = co_await impl::fs->find(dir, "..", false);
    if (parent.error) co_return parent.error;
    auto entry = co_await parent.file->entry_it([number = dir->number](DirectoryEntryHeader *e, uint32_t) {
      return e->inode == number;
    });
    if (entry == nullptr) co_return Syscall::ENOENT;
    uint64_t n = entry->name_length_low;
    if (pos < n + 1) co_return Syscall::ENAMETOOLONG;
    pos -= n;
    memcpy(path + pos, entry.operator->() + 1, n);
    path[--pos] = '/';
    dir = parent.file;
  }
  if (pos == PATH_MAX - 1) path[--pos] = '/';
  uint64_t n = PATH_MAX - 1 - pos;
  if (n + 1 > size) co_return Syscall::ERANGE;
  memcpy(buf, path + pos, n + 1);
  co_return n;
}

}

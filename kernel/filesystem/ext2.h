#pragma once

#include <algorithm>

#include "block_io.h"
#include "syscall.h"
#include "system_main.h"
#include "lib/ChainingHashMap.h"
#include "lib/debug.h"
#include "lib/heap.h"
#include "async/mutex.h"
#include "lib/shared.h"
#include "memory/physmem.h"
#include "memory/vmm.h"
#pragma pack(push, 1)
struct Ext2Superblock {
  uint32_t total_inodes;             // Total number of inodes in file system
  uint32_t total_blocks;             // Total number of blocks in file system
  uint32_t superuser_blocks;         // Number of blocks reserved for superuser (see offset 80)
  uint32_t unallocated_blocks;       // Total number of unallocated blocks
  uint32_t unallocated_inodes;       // Total number of unallocated inodes
  uint32_t superblock_block_num;     // Block number of the block containing the superblock (also the starting block number, NOT always zero.)
  uint32_t block_size_shift;         // log2 (block size) - 10. (In other words, the number to shift 1,024 to the left by to obtain the block size)
  uint32_t fragment_size_shift;      // log2 (fragment size) - 10. (In other words, the number to shift 1,024 to the left by to obtain the fragment size)
  uint32_t blocks_per_group;         // Number of blocks in each block group
  uint32_t fragments_per_group;      // Number of fragments in each block group
  uint32_t inodes_per_group;         // Number of inodes in each block group
  uint32_t last_mount_time;          // Last mount time (in POSIX time)
  uint32_t last_write_time;          // Last written time (in POSIX time)
  uint16_t mount_count;              // Number of times the volume has been mounted since its last consistency check (fsck)
  uint16_t max_mount_count;          // Number of mounts allowed before a consistency check (fsck) must be done
  uint16_t magic_signature;          // Ext2 signature (0xef53), used to help confirm the presence of Ext2 on a volume
  uint16_t fs_state;                 // File system state (see below)
  uint16_t error_handling;           // What to do when an error is detected (see below)
  uint16_t minor_version;            // Minor portion of version (combine with Major portion below to construct full version field)
  uint32_t last_check_time;          // POSIX time of last consistency check (fsck)
  uint32_t check_interval;           // Interval (in POSIX time) between forced consistency checks (fsck)
  uint32_t creator_os_id;            // Operating system ID from which the filesystem on this volume was created (see below)
  uint32_t major_version;            // Major portion of version (combine with Minor portion above to construct full version field)
  uint16_t superuser_id;             // User ID that can use reserved blocks
  uint16_t superuser_group_id;       // Group ID that can use reserved blocks
  uint32_t first_non_reserved_inode; // First non-reserved inode in file system. (In versions < 1.0, this is fixed as 11)
  uint16_t inode_size;               // Size of each inode structure in bytes. (In versions < 1.0, this is fixed as 128)
  uint16_t block_group_num;          // Block group that this superblock is part of (if backup copy)
  uint32_t optional_features;        // Optional features present (features that are not required to read or write, but usually result in a performance increase. see below)
  uint32_t required_features;        // Required features present (features that are required to be supported to read or write. see below)
  uint32_t read_only_features;       // Features that if not supported, the volume must be mounted read-only see below)
  uint8_t  fs_id[16];                // File system ID (what is output by blkid)
  char     volume_name[16];          // Volume name (C-style string: characters terminated by a 0 byte)
  char     last_mounted_path[64];     // Path volume was last mounted to (C-style string: characters terminated by a 0 byte)
  uint32_t compression_algo;         // Compression algorithms used (see Required features above)
  uint8_t  prealloc_blocks_file;     // Number of blocks to preallocate for files
  uint8_t  prealloc_blocks_dir;      // Number of blocks to preallocate for directories
  uint8_t  unused_padding[2];        // (Unused)
  uint8_t  journal_id[16];           // Journal ID (same style as the File system ID above)
  uint32_t journal_inode;            // Journal inode
  uint32_t journal_device;           // Journal device
  uint32_t orphan_inode_list_head;   // Head of orphan inode list
  uint8_t  unused[788];              // (Unused)
};
#pragma pack(pop)
static_assert(sizeof(Ext2Superblock) == 1024, "Ext2Superblock size must be exactly 1024 bytes");

#pragma pack(push, 1)
struct BlockGroupDescriptor
{
  uint32_t block_bitmap;  // Block address of block usage bitmap
  uint32_t inode_bitmap;  // Block address of inode usage bitmap
  uint32_t inode_table;   // Starting block address of inode table
  uint16_t free_blocks;   // Number of unallocated blocks in group
  uint16_t free_inodes;   // Number of unallocated inodes in group
  uint16_t directories;   // Number of directories in group TODO
  uint8_t unused[14];     // (Unused)
};
#pragma pack(pop)
static_assert(sizeof(BlockGroupDescriptor) == 32, "BlockGroupDescriptor size must be exactly 32 bytes");

#pragma pack(push, 1)
struct Ext2Inode {
  enum InodeMode : uint16_t
  {
    // Permissions
    OX = 0x001, // Other execute
    OW = 0x002, // Other write
    OR = 0x004, // Other read
    GX = 0x008, // Group execute
    GW = 0x010, // Group write
    GR = 0x020, // Group read
    UX = 0x040, // User execute
    UW = 0x080, // User write
    UR = 0x100, // User read
    T  = 0x200, // Sticky bit
    GS = 0x400, // Set GUID
    US = 0x800, // Set UID
    // Type
    FIFO = 0x1000,
    CharacterDevice = 0x2000,
    Directory = 0x4000,
    BlockDevice = 0x6000,
    File = 0x8000,
    SymbolicLink = 0xA000,
    UnixSocket = 0xC000,
  };

  uint16_t mode;                     // Type and Permissions (see below)
  uint16_t uid;                      // User ID
  uint32_t size_low;                 // Lower 32 bits of size in bytes
  uint32_t atime;                    // Last Access Time (in POSIX time)
  uint32_t ctime;                    // Creation Time (in POSIX time)
  uint32_t mtime;                    // Last Modification time (in POSIX time)
  uint32_t dtime;                    // Deletion time (in POSIX time)
  uint16_t gid;                      // Group ID
  uint16_t links_count;              // Count of hard links (directory entries) to this inode. When this reaches 0, the data blocks are marked as unallocated.
  uint32_t sectors;                  // Count of disk sectors (not Ext2 blocks) in use by this inode, not counting the actual inode structure nor directory entries linking to the inode.
  uint32_t flags;                    // Flags (see below)
  uint32_t osd1;                     // Operating System Specific value #1
  uint32_t direct_block[12];         // Direct Block Pointers 0 through 11
  uint32_t singly_indirect_block;    // Singly Indirect Block Pointer (Points to a block that is a list of block pointers to data)
  uint32_t doubly_indirect_block;    // Doubly Indirect Block Pointer (Points to a block that is a list of block pointers to Singly Indirect Blocks)
  uint32_t triply_indirect_block;    // Triply Indirect Block Pointer (Points to a block that is a list of block pointers to Doubly Indirect Blocks)
  uint32_t generation;               // Generation number (Primarily used for NFS)
  uint32_t file_acl;                 // In Ext2 version 0, this field is reserved. In version >= 1, Extended attribute block (File ACL).
  uint32_t dir_acl_or_size_high;     // In Ext2 version 0, this field is reserved. In version >= 1, Upper 32 bits of file size (if feature bit set) if it's a file, Directory ACL if it's a directory
  uint32_t fragment_block_addr;      // Block address of fragment
  uint8_t  osd2[12];                 // Operating System Specific Value #2
};
#pragma pack(pop)
static_assert(sizeof(Ext2Inode) == 128, "Ext2Inode size must be exactly 128 bytes");

#pragma pack(push, 1)
struct DirectoryEntryHeader {
  uint32_t inode;                   // Inode
  uint16_t size;                    // Total size of this entry (Including all subfields)
  uint8_t name_length_low;          // Name Length least-significant 8 bits
  uint8_t type_or_name_length_high; // Type indicator (only if the feature bit for "directory entries have file type byte" is set, else this is the most-significant 8 bits of the Name Length)
};
#pragma pack(pop)
static_assert(sizeof(DirectoryEntryHeader) == 8, "DirectoryEntryHeader size must be exactly 8 bytes");

class Ext2;
// A wrapper around an i-node
class Node : public BlockIO { // we implement BlockIO because we
                              // represent data
  uint32_t inode_offset;

public:
  uint32_t number;
  StrongRef<Node> cache_pin; // retain unwritten ENOSPC pages across close/reopen
  uint32_t orphan_pages = 0; // cached pages with no PTE references (under mapped_blocks)
  Mutex<Ext2Inode> inode;
  BlockingLock dir_lock{};
  StrongRef<BlockIO> device;
  Ext2 *ext2;

  uint32_t ptrPerBlock, ptrPerBlockSq, ptrPerBlockCb;

  struct FileMapping
  {
    PPN ppn;
    uint32_t refcount;
    bool dirty = false; // kernel writes or dirty PTEs retired from this frame
  };
  Mutex<impl::ChainingHashMap<FileMapping*>> mapped_blocks{impl::ChainingHashMap<FileMapping*>{8}};

  Node(uint32_t number, uint32_t block_size, uint32_t inode_offset, Ext2Inode inode, StrongRef<BlockIO> device, Ext2 *ext2) :
    BlockIO(block_size), inode_offset(inode_offset), number(number), inode(inode), device(device), ext2(ext2)
  {
    ptrPerBlock = block_size / sizeof(uint32_t);
    ptrPerBlockSq = ptrPerBlock*ptrPerBlock;
    ptrPerBlockCb = ptrPerBlockSq*ptrPerBlock;
  }

  ~Node() override = default;

  // How many bytes does this i-node represent
  //    - for a file, the size of the file
  //    - for a directory, implementation dependent
  //    - for a symbolic link, the length of the name
  sync<uint32_t> size_in_bytes() override { co_return (co_await inode.readonly()).data().size_low; }
private:
  sync<uint32_t> get_block(const Ext2Inode& roProof, Mutex<Ext2Inode>::Proof& write, uint32_t number, bool force);
  sync<uint32_t> get_block(uint32_t number, bool force);
public:
  template <typename T> // lambda that takes Ext2Inode&
  sync<void> edit_inode(T edit) {
    auto proof = co_await inode;
    edit(proof.data());
    co_await device->write(inode_offset, proof.data());
  }

  // read the given block
  // remember that block size is defined by the file system not the device
  sync<void> read_block(uint32_t block, char *buffer) override // number is LBN
  {
    // if in shared map, read from that
    // FRAME_SIZE >= block_size, so this is fine
    uint32_t aligned = ((block*block_size)/FRAME_SIZE)*FRAME_SIZE;
    uint32_t frame_idx = (block*block_size) % FRAME_SIZE;
    auto map_proof = co_await mapped_blocks.readonly();
    auto cached = map_proof.data().get(aligned);
    if (cached.has_value()) {
      memcpy(buffer, (char*)(Sys::hhdm_offset + (*cached)->ppn.ppn() * FRAME_SIZE + frame_idx), block_size);
      co_return;
    }
    co_await read_device_block(block, buffer);
  }

  sync<void> read_device_block(uint32_t block, char *buffer)
  {
    uint32_t block_offset = co_await get_block(block, false);

    if (block_offset  == 0) memset(buffer, 0, block_size); // sparse file, all zeroes
    else co_await device->read_all(block_offset*block_size, block_size, buffer);
  }

  sync<void> write_block(uint32_t block, const char *buffer) override {
    // if in shared map, write to that
    // FRAME_SIZE >= block_size, so this is fine
    uint32_t aligned = ((block*block_size)/FRAME_SIZE)*FRAME_SIZE;
    uint32_t frame_idx = (block*block_size) % FRAME_SIZE;
    auto map_proof = co_await mapped_blocks;
    auto cached = map_proof.data().get(aligned);
    if (cached.has_value()) {
      (*cached)->dirty = true;
      memcpy((char*)(Sys::hhdm_offset + (*cached)->ppn.ppn() * FRAME_SIZE + frame_idx), buffer, block_size);
      co_return;
    }
    co_await write_device_block(block, buffer);
  }

  sync<bool> write_device_block(uint32_t block, const char *buffer)
  {
    uint32_t block_offset = co_await get_block(block, true);
    if (!block_offset) co_return false;
    co_await device->write_all(block_offset*block_size, block_size, buffer);
    co_return true;
  }

  // whole-range transfers that bypass the page cache (used to fill and flush cached frames)
  sync<void> read_uncached(uint32_t offset, uint32_t n, char *buffer)
  {
    uint32_t size = co_await size_in_bytes();
    for (uint32_t i = 0; i < n && offset + i < size; i += block_size)
      co_await read_device_block((offset + i)/block_size, buffer + i);
    if (offset < size && size - offset < n) memset(buffer + (size - offset), 0, n - (size - offset));
  }

  // false if the filesystem is full
  sync<bool> write_uncached(uint32_t offset, uint32_t n, const char *buffer)
  {
    while (n > 0) {
      uint32_t in_block = offset % block_size, count = std::min(n, block_size - in_block);
      char *temp = nullptr;
      const char *src = buffer;
      if (count < block_size) {
        src = temp = new char[block_size];
        co_await read_device_block(offset / block_size, temp);
        memcpy(temp + in_block, buffer, count);
      }
      bool ok = co_await write_device_block(offset / block_size, src);
      delete[] temp;
      if (!ok) co_return false;
      offset += count; buffer += count; n -= count;
    }
    co_return true;
  }

  sync<int64_t> read(uint32_t offset, uint32_t n, char *buffer) override {
    uint32_t size = co_await size_in_bytes();
    if (size < offset + n) {
      uint32_t to_overwrite = std::min(n , offset + n - size);
      memset(buffer + (n-to_overwrite), 0, to_overwrite);
      n -= to_overwrite;
    }
    if (n == 0) co_return 0;

    co_return co_await BlockIO::read(offset, n, buffer);
  }
  template <typename T> sync<void> read(uint32_t offset, T &thing) { co_await BlockIO::read(offset, thing); }

  sync<int64_t> write(uint32_t offset, uint32_t n, const char *buffer) override {
    if (n == 0) co_return 0;
    auto proof = co_await mapped_blocks;
    uint32_t count = std::min(n, block_size - offset % block_size);
    if (auto cached = proof.data().get(offset / FRAME_SIZE * FRAME_SIZE); cached.has_value()) {
      (*cached)->dirty = true;
      // Touch only the requested bytes: block-sized RMW loses concurrent mapped stores.
      memcpy((char*)(Sys::hhdm_offset + (*cached)->ppn.ppn() * FRAME_SIZE) + offset % FRAME_SIZE, buffer, count);
    } else if (!co_await write_uncached(offset, count, buffer)) co_return Syscall::ENOSPC;
    if (co_await size_in_bytes() < offset + count)
      co_await edit_inode([offset, count] (Ext2Inode& inode) { inode.size_low = std::max(inode.size_low, offset + count); });
    co_return count;
  }
  template <typename T> sync<void> write(uint32_t offset, T &thing) { co_await BlockIO::write(offset, thing); }

  sync<void> truncate(uint32_t length); // backing storage only; call via VMM::truncate_file

  // Returns the type of this node
  sync<uint16_t> get_type() { co_return (co_await inode.readonly()).data().mode & 0xF000; }

  // true if this node is a directory
  sync<bool> is_dir() { co_return co_await get_type() == Ext2Inode::Directory; }

  // true if this node is a file
  sync<bool> is_file() { co_return co_await get_type() == Ext2Inode::File; }

  // true if this node is a symbolic link
  sync<bool> is_symlink() { co_return co_await get_type() == Ext2Inode::SymbolicLink; }

  // If this node is a symbolic link, fill the buffer with
  // the name the link referes to.
  //
  // Panics if the node is not a symbolic link
  //
  // The buffer needs to be at least as big as the the value
  // returned by size_in_byte()
  sync<void> get_symbol(char *buffer)
  {
    ASSERT(co_await is_symlink());

    uint32_t size = co_await size_in_bytes();
    if (size < 60)
    {
      memcpy(buffer, (co_await inode.readonly()).data().direct_block, size);
    } else
    {
      char *block = new char[block_size];

      co_await read_block(0, block);
      memcpy(buffer, block, size);

      delete[] block;
    }

    // buffer[size] = '\x00';
  }

  // Returns the number of hard links to this node
  sync<uint32_t> n_links() { co_return (co_await inode.readonly()).data().links_count; }

  template <typename T> // callback needs to be a lambda (DirectoryEntryHeader*, uint32_t) -> bool
  sync<StrongRef<DirectoryEntryHeader>> entry_it(T callback)
  {
    uint32_t size = co_await size_in_bytes();
    uint32_t offset = 0;

    char *block = new char[block_size];

    uint32_t lbn = -1;
    uint16_t ptr = block_size;
    while (offset < size)
    {
      if (ptr == block_size)
      {
        ptr = 0;
        co_await read_block(++lbn, block);
      }
      ASSERT(ptr < block_size);

      auto *entry = (DirectoryEntryHeader*)(block + ptr);
      ASSERT(entry->size != 0); // a zero record length would never advance
      uint32_t start = offset;
      offset += entry->size;

      if (callback(entry, start)) {
        auto *ret = (DirectoryEntryHeader *)operator new(entry->size);
        memcpy(ret, entry, entry->size);
        delete[] block;
        co_return StrongRef{ret};
      }

      ptr += entry->size;
    }

    delete[] block;

    co_return StrongRef<DirectoryEntryHeader>{};
  }

  sync<StrongRef<DirectoryEntryHeader>> find_entry_inode(const char *name)
  {
    StrongRef<DirectoryEntryHeader> entry = co_await entry_it([&name](DirectoryEntryHeader* entry, uint32_t)
    {
      if (entry->inode == 0) return false;
      char* entryName = (char*) (entry+1);
      uint8_t len = entry->name_length_low;
      for (uint8_t i = 0; i < len; i++)
      {
        if (name[i] != entryName[i]) return false;
      }
      return name[len] == '\x00' || name[len] == '/';
    });

    if (entry == nullptr) co_return StrongRef<DirectoryEntryHeader>{};
    co_return entry;
  }

  // Returns the number of entries in a directory node
  // Panics if not a directory
  sync<uint32_t> entry_count()
  {
    uint32_t entries = 0;

    co_await entry_it([&entries](DirectoryEntryHeader*, uint32_t) { entries++; return false; });

    co_return entries;
  }
};

// just implements cache as a hash table .-.
class CachedBlockIO : public BlockIO
{
  static constexpr uint32_t CACHED_BLOCKS = 128;

  StrongRef<BlockIO> device;
  uint32_t entry_size;
  uint8_t* cache;
  BlockingLock lock;

  Atomic<uint32_t> reads;
  Atomic<uint32_t> writes;

  bool _flush_done = false;

public:
  CachedBlockIO(StrongRef<BlockIO> device) : BlockIO(device->block_size), device(device)
  {
    entry_size = block_size + sizeof(uint32_t);

    cache = (uint8_t*) malloc(CACHED_BLOCKS*entry_size);
    // mark every entry as invalid
    for (uint32_t i = 0; i < CACHED_BLOCKS; i++) *(uint32_t*)(cache + i*entry_size) = -1;
  }
  ~CachedBlockIO() override
  {
    auto handle = flush();
    while (!_flush_done) {}

    KPRINT("~~ CACHED IO READ ? WROTE ? ~~\n", Dec(reads.get()), Dec(writes.get()));
    free(cache);
  }

  sync<void> flush() override {
    for (uint32_t i = 0; i < CACHED_BLOCKS; i++) {
      auto *entry = (uint32_t*)(cache + i*entry_size);
      if (*entry == uint32_t(-1)) continue;

      co_await device->write_block(*entry, (char*)(entry+1));
    }

    _flush_done = true;
  }

  sync<uint32_t> size_in_bytes() override { return device->size_in_bytes(); }
  sync<uint32_t*> slot(uint32_t block_number) {
    auto* entry = (uint32_t*) (cache + (block_number % CACHED_BLOCKS)*entry_size);
    if (*entry != block_number)
    {
      if (*entry != uint32_t(-1)) {
        writes.fetch_add(1);
        co_await device->write_block(*entry, (char*)(entry+1));
      }
      reads.fetch_add(1);
      co_await device->read_block(block_number, (char*)(entry+1));
      *entry = block_number;
    }
    co_return entry;
  }

  sync<void> read_block(uint32_t block_number, char* buffer) override
  {
    auto proof = co_await lock;
    auto* entry = co_await slot(block_number);
    memcpy(buffer, entry+1, block_size);
  }

  sync<void> write_block(uint32_t block_number, const char *data) override {
    auto proof = co_await lock;
    auto* entry = co_await slot(block_number);
    memcpy(entry+1, data, block_size);
  }
};

// This class encapsulates the implementation of the Ext2 file system
class Ext2 {
  static constexpr uint32_t ROOT_INODE = 2;

  static constexpr uint32_t SUPERBLOCK_OFFSET = 1024;
  static constexpr uint32_t V0_INODE_SIZE = 128; // for versions <1.0

  // The device on which the file system resides
  StrongRef<BlockIO> device;
  Mutex<Ext2Superblock> superblock;
  BlockingLock bgdt_lock;

public:
  // The root directory for this file system
  StrongRef<Node> root;

private:
  Ext2(StrongRef<BlockIO> device) : device(device) { }

public:
  // Required (INCOMPAT) feature bits we support. FILETYPE is common and
  // compatible with our directory entry parsing (name length is 8 bits).
  static constexpr uint32_t INCOMPAT_FILETYPE = 0x2;
  static constexpr uint32_t INCOMPAT_SUPPORTED = INCOMPAT_FILETYPE;

  // Mount an existing file system residing on the given device
  // Panics if the file system is invalid
  static sync<StrongRef<Ext2>> mount(StrongRef<BlockIO> device)
  {
    auto ext2 = StrongRef{new Ext2{StrongRef<BlockIO>{new CachedBlockIO{device}}}};
    auto proof = co_await ext2->superblock.readonly();
    const Ext2Superblock& sb = proof.data();
    co_await device->read(SUPERBLOCK_OFFSET, sb); // should automatically handle reading two blocks

    const uint32_t BLOCK_SIZE = co_await ext2->get_block_size();

    ASSERT(sb.magic_signature == 0xEF53);
    ASSERT(sb.block_size_shift <= 2);

    ASSERT(sb.fragment_size_shift == sb.block_size_shift);

    ASSERT(sb.total_inodes > 0);
    ASSERT(sb.total_blocks > 0);
    ASSERT(sb.unallocated_inodes <= sb.total_inodes);
    ASSERT(sb.unallocated_blocks <= sb.total_blocks);
    ASSERT(sb.superuser_blocks <= sb.total_blocks);

    ASSERT(sb.inodes_per_group > 0 && sb.inodes_per_group <= sb.total_inodes);
    ASSERT(sb.blocks_per_group > 0);
    ASSERT(sb.blocks_per_group <= 8 * BLOCK_SIZE); // one block bitmap covers the group

    ASSERT(sb.superblock_block_num == (sb.block_size_shift == 0 ? 1u : 0u));

    // ceil((total blocks - superuser block)/blocks per group)
    const uint32_t groups_from_blocks = (sb.total_blocks - sb.superblock_block_num + sb.blocks_per_group - 1) / sb.blocks_per_group;
    // ceil(total inodes/inodes per group)
    const uint32_t groups_from_inodes = (sb.total_inodes + sb.inodes_per_group - 1) / sb.inodes_per_group;
    ASSERT(groups_from_blocks > 0);
    ASSERT(groups_from_blocks == groups_from_inodes);

    if (sb.major_version >= 1) {
      ASSERT(sb.inode_size >= V0_INODE_SIZE);
      ASSERT(sb.inode_size <= BLOCK_SIZE);
      ASSERT((sb.inode_size & (sb.inode_size - 1)) == 0); // power of two
      ASSERT(sb.first_non_reserved_inode >= 11);
    }
    ASSERT((uint64_t{sb.inodes_per_group} * co_await ext2->get_inode_size()) % BLOCK_SIZE == 0);

    ASSERT((sb.required_features & ~INCOMPAT_SUPPORTED) == 0);
    ASSERT(sb.fs_state == 1);

    ASSERT(co_await device->size_in_bytes() / BLOCK_SIZE >= sb.total_blocks);

    ext2->root = co_await ext2->get_node(ROOT_INODE);
    ASSERT(co_await ext2->root->is_dir());
    ASSERT(co_await ext2->root->find_entry_inode(".") != nullptr);
    ASSERT(co_await ext2->root->find_entry_inode("..") != nullptr);

    co_return ext2;
  }

  sync<void> flush() { co_await device->flush(); }

  ~Ext2() = default;

  // Returns the block size of the file system. Doesn't have
  // to match that of the underlying device
  sync<uint32_t> get_block_size() { co_return 1024 << (co_await superblock.readonly()).data().block_size_shift; }

  // Returns the actual size of an i-node. Ext2 specifies that
  // an i-node will have a minimum size of 128B but could have
  // more bytes for extended attributes
  sync<uint32_t> get_inode_size() {
    auto proof = co_await superblock.readonly();
    co_return proof.data().major_version < 1 ? V0_INODE_SIZE : proof.data().inode_size;
  }

  // dead entries are just misses: they get overwritten, so nodes need no free hook
  Mutex<impl::ChainingHashMap<WeakRef<Node>>> inode_cache{impl::ChainingHashMap<WeakRef<Node>>{1024}};
  // Returns the node with the given i-number
  sync<StrongRef<Node>> get_node(uint32_t number)
  {
    auto cache = co_await inode_cache;
    if (auto cached = cache.data().get(number); cached.has_value()) {
      StrongRef<Node> strong{*cached};
      if (strong != nullptr) co_return strong;
    }

    const uint32_t BLOCK_SIZE = co_await get_block_size();
    const uint32_t BGDT_OFFSET = (SUPERBLOCK_OFFSET/BLOCK_SIZE + 1)*BLOCK_SIZE; // the block after the superblock
    const uint32_t INODES_PER_GROUP = (co_await superblock.readonly()).data().inodes_per_group;

    uint32_t block_group = (number-1) / INODES_PER_GROUP;

    BlockGroupDescriptor descriptor{};
    co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);

    Ext2Inode inode{};
    uint32_t bg_index = (number-1) % INODES_PER_GROUP;
    uint32_t inode_offset = descriptor.inode_table * BLOCK_SIZE + bg_index * co_await get_inode_size();
    co_await device->read(inode_offset, inode);

    auto ret = StrongRef{new Node{number, BLOCK_SIZE, inode_offset, inode, device, this}};
    cache.data().put(number, WeakRef{ret});

    co_return ret;
  }

  sync<uint32_t> take_bit(uint32_t bitmap_block, uint32_t bits, uint32_t start) {
    const uint32_t BLOCK_SIZE = co_await get_block_size();
    auto *bitmap = (uint8_t*)malloc(BLOCK_SIZE);
    co_await device->read_all(bitmap_block*BLOCK_SIZE, BLOCK_SIZE, (char*)bitmap);

    int64_t found = -1;
    for (uint32_t i = 0; i < bits; i++) {
      uint32_t idx = (start + i) % bits;
      if (bitmap[idx/8] & (1 << (idx%8))) continue;

      found = idx;
      break;
    }
    ASSERT(found >= 0);

    bitmap[found/8] |= 1 << (found%8);
    co_await device->write_all(bitmap_block*BLOCK_SIZE + found/8, 1, (char*)&bitmap[found/8]);
    free(bitmap);
    co_return (uint32_t)found;
  }

  // gets a new block, preferring sequential, then in the same block group, then just linear scan
  struct BlockProvision { uint32_t block; int32_t error; };
  sync<BlockProvision> provision_block(uint32_t targetBlock, uint32_t targetInode) {
    if ((co_await superblock.readonly()).data().unallocated_blocks == 0) co_return {0, Syscall::ENOSPC}; // out of blocks!

    const uint32_t BLOCK_SIZE = co_await get_block_size();
    const uint32_t BGDT_OFFSET = (SUPERBLOCK_OFFSET/BLOCK_SIZE + 1)*BLOCK_SIZE; // the block after the superblock
    auto ro_proof = co_await superblock.readonly();
    const uint32_t FIRST_DATA_BLOCK = ro_proof.data().superblock_block_num;
    const uint32_t INODES_PER_GROUP = ro_proof.data().inodes_per_group;
    const uint32_t BLOCKS_PER_GROUP = ro_proof.data().blocks_per_group;

    uint32_t total_block_groups = (ro_proof.data().total_blocks + BLOCKS_PER_GROUP - 1) / BLOCKS_PER_GROUP;
    ro_proof.unlock();

    auto bgdt_proof = co_await bgdt_lock;
    uint32_t o_block_group = targetBlock != 0 ? (targetBlock - FIRST_DATA_BLOCK)/BLOCKS_PER_GROUP : (targetInode-1)/INODES_PER_GROUP;
    uint32_t block_group = o_block_group;
    uint32_t start_block_idx = targetBlock != 0 ? (targetBlock - FIRST_DATA_BLOCK + 1) % BLOCKS_PER_GROUP : 0;

    BlockGroupDescriptor descriptor{};
    co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);

    while (descriptor.free_blocks == 0) {
      block_group = (block_group+1) % total_block_groups;
      if (block_group == o_block_group) co_return BlockProvision{0, Syscall::ENOSPC};

      co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);
    }

    uint32_t block_idx = co_await take_bit(descriptor.block_bitmap, BLOCKS_PER_GROUP,
      block_group == o_block_group ? start_block_idx : 0);
    descriptor.free_blocks -= 1;
    co_await device->write(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor)
      + offsetof(BlockGroupDescriptor, free_blocks), descriptor.free_blocks);
    bgdt_proof.unlock();

    auto wr_proof = co_await superblock;
    wr_proof.data().unallocated_blocks -= 1;
    co_await device->write(SUPERBLOCK_OFFSET + offsetof(Ext2Superblock, unallocated_blocks), wr_proof.data().unallocated_blocks);

    uint32_t block = wr_proof.data().superblock_block_num + block_group * BLOCKS_PER_GROUP + block_idx;
    wr_proof.unlock();

    auto *empty = (char*)malloc(BLOCK_SIZE);
    memset(empty, 0, BLOCK_SIZE);
    co_await device->write_all(block * BLOCK_SIZE, BLOCK_SIZE, empty);
    free(empty);

    co_return {block, 0};
  }

  struct BlockFree { BlockFree *next; uint32_t block; };
  struct BlockGroupFree { BlockGroupFree *next; BlockFree *frees; uint32_t count; uint32_t block_group; };
  sync<void> free_blocks(BlockFree *frees, uint32_t count) {
    const uint32_t BLOCK_SIZE = co_await get_block_size();
    const uint32_t BGDT_OFFSET = (SUPERBLOCK_OFFSET/BLOCK_SIZE + 1)*BLOCK_SIZE; // the block after the superblock
    const uint32_t BLOCKS_PER_GROUP = (co_await superblock.readonly()).data().blocks_per_group;
    const uint32_t FIRST_DATA_BLOCK = (co_await superblock.readonly()).data().superblock_block_num;

    BlockGroupFree *block_groups = nullptr;

    while (frees != nullptr) {
      uint32_t block_group = (frees->block-FIRST_DATA_BLOCK) / BLOCKS_PER_GROUP;

      BlockGroupFree *bg = block_groups;
      while (bg && bg->block_group != block_group) bg = bg->next;
      if (!bg) {
        bg = block_groups = new BlockGroupFree{block_groups, nullptr, 0, block_group};
      }

      BlockFree *f = frees;
      frees = frees->next;

      f->next = bg->frees;
      bg->frees = f;
      bg->count++;
    }

    auto *bitmap = (char*)malloc(BLOCK_SIZE);
    while (block_groups) {
      uint32_t block_group = block_groups->block_group;
      auto *f = block_groups->frees;

      auto proof = co_await bgdt_lock;
      BlockGroupDescriptor descriptor{};
      co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);
      descriptor.free_blocks += block_groups->count;
      co_await device->write(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor)
        + offsetof(BlockGroupDescriptor, free_blocks), descriptor.free_blocks);

      co_await device->read_all(descriptor.block_bitmap*BLOCK_SIZE, BLOCK_SIZE, bitmap);

      while (f) {
        uint32_t idx = (f->block-FIRST_DATA_BLOCK) % BLOCKS_PER_GROUP;

        bitmap[idx/8] &= (uint8_t)~(1 << (idx%8));

        auto *old = f;
        f = f->next;
        delete old;
      }

      co_await device->write_all(descriptor.block_bitmap*BLOCK_SIZE, BLOCK_SIZE, bitmap);

      auto *old = block_groups;
      block_groups = block_groups->next;
      delete old;
    }
    free(bitmap);

    auto wr_proof = co_await superblock;
    wr_proof.data().unallocated_blocks += count;
    co_await device->write(SUPERBLOCK_OFFSET + offsetof(Ext2Superblock, unallocated_blocks), wr_proof.data().unallocated_blocks);
  }

  struct FileResult { StrongRef<Node> file; int64_t error; };
  sync<FileResult> create(StrongRef<Node> start, char *path, uint32_t mode) {
    if ((co_await superblock.readonly()).data().unallocated_inodes == 0) co_return {{}, Syscall::ENOSPC}; // out of inodes!

    struct Copy { char *p; ~Copy() { delete[] p; } } copy{new char[strlen(path) + 1]};
    memcpy(copy.p, path, strlen(path) + 1);
    char *slash = nullptr;
    for (char *p = copy.p; *p; p++) if (*p == '/') slash = p;
    char *name = slash ? slash + 1 : copy.p;
    uint32_t name_len = strlen(name);
    if (name_len == 0 || name_len > 255) co_return {{}, Syscall::EINVAL};

    StrongRef<Node> dir = start;
    if (slash) {
      const char *parent = slash == copy.p ? "/" : copy.p;
      *slash = '\0';
      auto found = co_await find(start, parent, true);
      if (found.error != 0) co_return {{}, found.error};
      dir = found.file;
    }
    if (!co_await dir->is_dir()) co_return {{}, Syscall::ENOTDIR};

    auto dir_proof = co_await dir->dir_lock; // one create at a time per directory
    if (auto exists = co_await dir->find_entry_inode(name); exists != nullptr)
      co_return {co_await get_node(exists->inode), 0};

    uint32_t now = Sys::unix_time();
    co_await dir->edit_inode([now](Ext2Inode& inode) { inode.ctime = std::max(inode.ctime, now); });

    const uint32_t BLOCK_SIZE = co_await get_block_size();
    const uint32_t BGDT_OFFSET = (SUPERBLOCK_OFFSET/BLOCK_SIZE + 1)*BLOCK_SIZE; // the block after the superblock
    auto ro_proof = co_await superblock.readonly();
    const uint32_t INODES_PER_GROUP = ro_proof.data().inodes_per_group;

    uint32_t total_block_groups = (ro_proof.data().total_inodes + INODES_PER_GROUP - 1) / INODES_PER_GROUP;
    uint32_t o_block_group = (dir->number-1) / INODES_PER_GROUP;
    uint32_t block_group = o_block_group;
    ro_proof.unlock();

    auto bgdt_proof = co_await bgdt_lock;

    BlockGroupDescriptor descriptor{};
    co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);

    while (descriptor.free_inodes == 0) {
      block_group = (block_group+1) % total_block_groups;
      ASSERT(block_group != o_block_group); // already checked above

      co_await device->read(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor), descriptor);
    }

    uint32_t inode_idx = co_await take_bit(descriptor.inode_bitmap, INODES_PER_GROUP, 0);
    uint32_t inode_number = 1 + inode_idx + INODES_PER_GROUP*block_group;

    descriptor.free_inodes -= 1;
    co_await device->write(BGDT_OFFSET + block_group * sizeof(BlockGroupDescriptor)
      + offsetof(BlockGroupDescriptor, free_inodes), descriptor.free_inodes);
    bgdt_proof.unlock();

    auto wr_proof = co_await superblock;
    wr_proof.data().unallocated_inodes -= 1;
    co_await device->write(SUPERBLOCK_OFFSET + offsetof(Ext2Superblock, unallocated_inodes), wr_proof.data().unallocated_inodes);
    wr_proof.unlock();

    Ext2Inode inode{
      .mode = (uint16_t)((mode & 0xFFF) | Ext2Inode::File),
      .atime = now,
      .ctime = now,
      .mtime = now,
      .links_count = 1,
    };
    uint32_t inode_offset = descriptor.inode_table * BLOCK_SIZE + inode_idx * co_await get_inode_size();
    co_await device->write(inode_offset, inode);

    // 4-byte aligned; reuse spare room
    uint16_t need = (name_len + sizeof(DirectoryEntryHeader) + 3) & ~3u;
    uint32_t offset = co_await dir->size_in_bytes();
    uint32_t rec_start = offset, used = 0;
    StrongRef<DirectoryEntryHeader> entry = co_await dir->entry_it([need, &offset, &rec_start, &used](DirectoryEntryHeader *entry, uint32_t off) {
      uint32_t u = entry->inode != 0 ? (entry->name_length_low + sizeof(DirectoryEntryHeader) + 3) & ~3u : 0;
      if (entry->size - u < need) return false;
      rec_start = off;
      used = u;
      offset = off + u;
      return true;
    });

    uint32_t rec = entry != nullptr ? entry->size - used : BLOCK_SIZE;
    if (entry != nullptr && used != 0) {
      uint16_t shrunk = used;
      co_await dir->write(rec_start + offsetof(DirectoryEntryHeader, size), shrunk);
    }
    auto *buffer = (DirectoryEntryHeader*)malloc(rec);
    memset(buffer, 0, rec);
    buffer->inode = inode_number;
    buffer->size = rec;
    buffer->name_length_low = (uint8_t)name_len;
    memcpy(buffer+1, name, name_len);
    co_await dir->write_all(offset, rec, (char*)buffer);
    free(buffer);

    auto ret = StrongRef{new Node{inode_number, BLOCK_SIZE, inode_offset, inode, device, this}};
    auto cache = co_await inode_cache;
    cache.data().put(inode_number, WeakRef{ret});

    co_return {ret, 0};
  }

  // Traverse "path" starting at the given node
  //
  // Path might be absolute or relative.
  //
  // Follows symbolic links for intermediate nodes
  //
  // Returns StrongPtr{} (null) in case of errors. For example:
  //   - node is not a directory
  //   - the path is invalid
  //   - too many symbolic link traversals (> 100)
  //
  static constexpr uint8_t MAX_SYMLINKS = 100;
  sync<FileResult> find(StrongRef<Node> dir, const char *path, bool followSyms = true)
  {
    uint8_t symlinks = 0;
    auto res = co_await find(dir, path, symlinks, followSyms);
    co_return {res.item, res.error};
  }

private:
  struct FindIntermediate { StrongRef<Node> item, parent; int64_t error; };
  sync<FindIntermediate> find(StrongRef<Node> dir, const char *path, uint8_t& symlinks, bool followFinal)
  {
    if (path[0] == '\x00') co_return {{}, {}, Syscall::ENOENT};
    if (path[0] == '/')
    {
      dir = root;
      while (path[0] == '/') path++;
      if (path[0] == '\x00') co_return {root, root, 0};
    }
    while (symlinks <= MAX_SYMLINKS)
    {
      if (dir == nullptr || !co_await dir->is_dir()) co_return {{}, {}, Syscall::ENOTDIR};

      StrongRef<DirectoryEntryHeader> entry = co_await dir->find_entry_inode(path);
      if (entry == nullptr) co_return {{}, {}, Syscall::ENOENT};

      path += entry->name_length_low;

      bool is_dir = path[0] == '/';
      while (path[0] == '/') path++;

      StrongRef<Node> node = co_await get_node(entry->inode);
      if (node == nullptr) co_return {{}, {}, Syscall::ENOENT};

      bool last = path[0] == '\x00';
      while (co_await node->is_symlink() && (!last || followFinal))
      {
        uint32_t size = co_await node->size_in_bytes();
        char *symPath = new char[size+1];
        co_await node->get_symbol(symPath);
        symPath[size] = '\x00';

        symlinks++;
        auto intermediate = co_await find(dir, symPath, symlinks, true);
        if (intermediate.item == nullptr) co_return {{}, {}, intermediate.error};

        node = intermediate.item;
        dir = intermediate.parent;

        delete[] symPath;
      }

      if (last)
      {
        if (is_dir && !co_await node->is_dir()) co_return {{}, {}, Syscall::ENOTDIR};
        co_return {node, dir, 0};
      }

      dir = node;
    }

    co_return {{}, {}, Syscall::ELOOP};
  }
};
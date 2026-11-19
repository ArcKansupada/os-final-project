#pragma once

#include "lib/debug.h"
#include "async/sync.h"
#include <cstdint>

//
// Base class for things that support block IO (disks, files, directories, etc)
//
// Assumptions:
//    - block size is a power of 2
//    - block size is fixed
//    - the caller is responsible for serializaing access (locking, etc)
//    - the caller is responsible for ensuring that buffers assed in
//      as arguments are large enough for the required operation
//    - the caller is also responsibile for ensuring that all offsets
//      and block numbers are in bounds:
//           block_number < number_of_blocks
//           byte offset < size_in_bytes
//
//
class BlockIO {
public:
  const uint32_t block_size;
  BlockIO(uint32_t block_size) : block_size(block_size) {}
  virtual ~BlockIO() {}

  // get number of bytes
  virtual sync<uint32_t> size_in_bytes() = 0;

  // get number of blocks
  sync<uint32_t> size_in_blocks() {
    co_return ((co_await size_in_bytes() + block_size - 1) / block_size);
  }

  // Read a block and put its bytes in the given buffer
  virtual sync<void> read_block(uint32_t block_number, char *buffer) = 0;

  // Writes buffer to block block_number.
  //   Q: In a BlockCache, when do the bytes reach the device?
  //   Q: Why doesn't ext2's Node support it yet?
  virtual sync<void> write_block(uint32_t block_number, const char *buffer) = 0;

  virtual sync<int64_t> write(uint32_t offset, uint32_t n, const char *buffer);

  virtual sync<int64_t> write_all(uint32_t offset, uint32_t n, const char *buffer);

  // Read up to "n" bytes starting at "offset" and put the restuls in "buffer".
  // returns:
  //   > 0  actual number of bytes read
  //   = 0  end (offset == size_in_bytes)
  //   -1   error (offset > size_in_bytes)
  virtual sync<int64_t> read(uint32_t offset, uint32_t n, char *buffer);

  // Read min(n,size_in_bytes - offset) bytes starting at "offset" and
  //      put the results in "buffer".
  // returns:
  //    > 0 actual number of bytes read
  //    = 0 end (offset == size_in_bytes)
  //    -1 error (offset > size_in_bytes)
  //
  virtual sync<int64_t> read_all(uint32_t offset, uint32_t n, char *buffer);

  virtual sync<void> flush() { co_return; }

  template <typename T> sync<void> read(uint32_t offset, T &thing) {
    auto cnt = co_await read_all(offset, sizeof(T), (char *)&thing);
    ASSERT(cnt == sizeof(T));
  }
  template <typename T> sync<void> write(uint32_t offset, T &thing) {
    auto cnt = co_await write_all(offset, sizeof(T), (char *)&thing);
    ASSERT(cnt == sizeof(T));
  }
};

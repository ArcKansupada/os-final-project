/* Copyright (C) 2026 Ahmed Gheith and contributors.
 *
 * Use restricted to classroom projects.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */


 // minimal ELF loader, just enough to load user programs.
 //   - statically linked
 //   - no relocation
 //   - x86_64 only
 //   - minimal sanity checks
 //   - expects all segments to be properly aligned
 //   - ...
 //
 // Modify to your liking 

#include "pcb.h"
#include "elf.h"

#include "memory/physmem.h"
#include "lib/shared.h"
#include "memory/vmm.h"

namespace {

uint64_t round_down(uint64_t x) { return x & ~(FRAME_SIZE - 1); }

// x <= USER_END, can't overflow
uint64_t round_up(uint64_t x) { return round_down(x + FRAME_SIZE - 1); }

struct ProgramHeader {

  static constexpr int RWX = VMM::PROT_READ | VMM::PROT_WRITE | VMM::PROT_EXEC;

  static constexpr uint32_t PT_LOAD = 1;
  struct [[gnu::packed]] Data {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t filesz;
    uint64_t memsz;
    uint64_t align;
  } data;

  static_assert(sizeof(Data) == 56);

  ProgramHeader(const uintptr_t start) : data(*(Data *)start) {}

  bool good_segment(const uint64_t file_size) const {
    return data.filesz <= data.memsz && data.vaddr >= VMM::VA_START.va() &&
           data.vaddr < VMM::VA_END && data.memsz <= VMM::VA_END - data.vaddr &&
           data.offset <= file_size && data.filesz <= file_size - data.offset &&
           // a file page lands on a virtual page, like mmap needs
           (data.vaddr % FRAME_SIZE) == (data.offset % FRAME_SIZE);
  }

  uint64_t first_page() const { return round_down(data.vaddr); }

  uint64_t end_page() const { return round_up(data.vaddr + data.memsz); }

  sync<bool> load(StrongRef<Node> file) const {

    if (data.type != PT_LOAD)
      co_return true;
    if (data.memsz == 0)
      co_return true;

    const uint64_t start = first_page();
    const uint64_t data_end = data.vaddr + data.filesz;
    const uint64_t file_pages_end = round_up(data_end);
    const uint64_t end = end_page();

    if (data.filesz != 0) {
      const uint64_t length = file_pages_end - start;
      void *actual = co_await VMM::mmap( (void *)start, length, RWX,
                                        VMM::MAP_PRIVATE | VMM::MAP_FIXED, file,
                                        round_down(data.offset));
      if (actual != (void *)start)
        co_return false;


      memset(VA(data_end), 0, file_pages_end-data_end);
    }

    const uint64_t anon_start = data.filesz != 0 ? file_pages_end : start;
    if (end > anon_start) {
      const uint64_t length = end - anon_start;
      void *actual = co_await VMM::mmap(
          (void *)anon_start, length, RWX,
          VMM::MAP_PRIVATE | VMM::MAP_ANONYMOUS | VMM::MAP_FIXED, {}, 0);
      if (actual != (void *)anon_start)
        co_return false;
    }

    co_return true;
  }
};

struct Header {

  static constexpr uint16_t ET_EXEC = 2;
  static constexpr uint16_t EM_X86_64 = 62;

  struct [[gnu::packed]] Data {
    uint8_t ident[16];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t phoff;
    uint64_t shoff;
    uint32_t flags;
    uint16_t ehsize;
    uint16_t phentsize;
    uint16_t phnum;
    uint16_t shentsize;
    uint16_t shnum;
    uint16_t shstrndx;
  } data;

  static_assert(sizeof(Data) == 64);

  uintptr_t start;

  Header(const uintptr_t start) : start(start) {
    memcpy((void *)&data, (const void *)start, sizeof(Data));
  }

  bool good_header() {
    return data.ident[0] == 0x7F && data.ident[1] == 'E' &&
           data.ident[2] == 'L' && data.ident[3] == 'F' &&
           data.ident[4] == 2 /* 64-bit */ &&
           data.ident[5] == 1 /* little-endian */ && data.type == ET_EXEC &&
           data.machine == EM_X86_64 &&
           data.phentsize == sizeof(ProgramHeader) && data.phnum > 0;
  }

  StrongRef<ProgramHeader> *get_program_headers() {
    if (data.phnum == 0)
      return {};

    auto phs = new StrongRef<ProgramHeader>[data.phnum]();
    for (uint64_t i = 0; i < data.phnum; i++) {
      phs[i] = StrongRef<ProgramHeader>::make(uintptr_t(start) + data.phoff +
                                              i * data.phentsize);
    }

    return phs;
  }
};

} // namespace

sync<uint64_t> ELF::load(StrongRef<Node> file) {
  const uint64_t file_size = co_await file->size_in_bytes();
  if (file_size < sizeof(Header)) {
    co_return 0;
  }

  // map the whole file so we can check the headers
  auto start = (uintptr_t)co_await VMM::mmap( nullptr, file_size,
                                             VMM::PROT_READ | VMM::PROT_WRITE,
                                             VMM::MAP_PRIVATE, file, 0);
  if (start == (uintptr_t)VMM::MAP_FAILED) {
    co_return 0;
  }
  const StrongRef<Header> h = StrongRef<Header>::make(start);
  if(!h->good_header())
  {
    co_await VMM::munmap(VA{start}, file_size);
    co_return 0;
  }

  const auto phs = h->get_program_headers();

  co_await VMM::munmap((void *)start, file_size);

  if (phs == nullptr) {
    co_await VMM::munmap(VA{start}, file_size);
    co_return 0;
  }

  bool ok = true;
  for (uint64_t i = 0; ok && (i < h->data.phnum); i++) {
    auto ph = phs[i];
    if (ph->data.type != ProgramHeader::PT_LOAD) continue;
    ok = ph->good_segment(file_size) && co_await ph->load(file);
  }

  delete[] phs;
  if (!ok) co_await VMM::munmap(VA{start}, file_size);
  co_return ok ? h->data.entry : 0;
}

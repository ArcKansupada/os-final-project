/* Copyright (C) 2025 Ahmed Gheith and contributors.
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

#include "physmem.h"

#include "machine/limine++.h"
#include "machine/limine.h"
#include "lib/print.h"
#include "vmm.h"
#include <cstdint>

using namespace limine;

namespace impl {

[[gnu::section(
    ".limine_requests")]] static constexpr volatile MemMap limine_memmap{};

#if 0
static char const *memmap_type_names[] = {"Usable",
                                          "Reserved",
                                          "AcpiReclaimable",
                                          "AcpiNvs",
                                          "BadMemory",
                                          "BootloaderReclaimable",
                                          "ExecutableAndModules",
                                          "Framebuffer",
                                          "AcpiTables"};
#endif

} // namespace impl

void do_print(const PPN &ppn) {
  do_print("PPN(");
  do_print(ppn.ppn());
  do_print(")");
}

void do_print(const PA &pa) {
  do_print("PA(");
  do_print(pa.pa());
  do_print(")");
}

void do_print(const Offset &offset) {
  do_print("Offset(");
  do_print(offset.offset());
  do_print(")");
}

PhysMem::PhysMem() {
  using namespace impl;

  free_ranges = nullptr;

  if (limine_memmap.response) {
    for (uint64_t i = 0; i < limine_memmap.response->entry_count; i++) {
      auto const *entry = limine_memmap.response->entries[i];

      if (entry->type == LIMINE_MEMMAP_USABLE) {
        auto range = new Range(entry->base, entry->length);
        range->next = free_ranges;
        free_ranges = range;

        auto snap = new Range(entry->base >> LOG_FRAME_SIZE, entry->length >> LOG_FRAME_SIZE);
        snap->next = ppn_ranges;
        ppn_ranges = snap;

        total_memory += range->length;
      }
    }
  }

  KPRINT("avaialble memory: ?\n", Dec(total_memory));

  uint64_t ptes_len = 2 * total_frames() * sizeof(uint16_t);
  ptes_len = (ptes_len + FRAME_SIZE - 1) & ~(FRAME_SIZE - 1);

  Range *p = free_ranges;
  Range **prev = &free_ranges;
  while (p) {
    if (ptes_len != 0 && p->length >= ptes_len)
    {
      VMM::present_ptes = VA(PA(p->base));
      VMM::frame_refs = VMM::present_ptes + total_frames();
      KPRINT("FOUND PTE AREA: ?\n", VMM::present_ptes);
      for (uint64_t i = 0; i < 2 * total_frames(); i++) VMM::present_ptes[i] = 0;
      p->base += ptes_len;
      p->length -= ptes_len;
      ptes_len = 0;
      if (p->length == 0) p = *prev = p->next;
    }
    KPRINT("    0x? ?\n", p->base, Dec(p->length / FRAME_SIZE));
    prev = &p->next;
    p = p->next;
  }
  if (ptes_len != 0) KPANIC("Not enough contiguous memory for PTE ref count array! (? bytes required)", ptes_len);
}

PPN PhysMem::alloc() {
  lock.lock();

  auto frame = avail;
  PA pa{0};

  if (frame != nullptr) {
    // recycled frames hold old data, zeroed below
    avail = frame->next;
    pa = PA{frame->ppn()};
  } else {
    if (free_ranges == nullptr) {
      lock.unlock();
      KPANIC("?\n", "out of frames");
    }

    pa = PA{free_ranges->base};

    free_ranges->base += FRAME_SIZE;
    free_ranges->length -= FRAME_SIZE;
    if (free_ranges->length == 0) {
      free_ranges = free_ranges->next;
    }
  }
  n_allocated += 1;

  lock.unlock();

  uint64_t *const ptr = VA(pa);

  for (uint64_t i = 0; i < FRAME_SIZE / sizeof(uint64_t); i++) {
    ptr[i] = 0;
  }
  return PPN{pa};
}

void PhysMem::free(PPN ppn) {
  impl::AvailFrame *frame = VA(PA(ppn));
  lock.lock();
  frame->next = avail;
  avail = frame;
  n_allocated -= 1;
  lock.unlock();
}

uint64_t PhysMem::ppn_index(uint64_t ppn) {
  uint64_t idx = 0;
  for (auto *r = ppn_ranges; r; r = r->next) {
    uint64_t base = r->base;
    uint64_t n = r->length;
    if (ppn >= base && ppn < base + n) return idx + (ppn - base);
    idx += n;
  }

  KPANIC("PPN not in useable memmap: ?", ppn);
}

uint64_t PhysMem::allocated()
{
  LockGuard g{lock};
  return n_allocated;
}

PhysMem physMem{};

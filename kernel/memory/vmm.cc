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

#include "vmm.h"

#include "structures/ccb.h"
#include "filesystem/ext2.h"
#include "structures/idt.h"
#include "machine/machine.h"
#include "structures/pcb.h"
#include "physmem.h"
#include "lib/print.h"
#include "lib/shared.h"
#include <cstdint>
#include <bits/ranges_base.h>

#include "async/async.h"
#include "structures/tlb.h"
#include "machine/x86_64.h"

namespace impl {

struct PageFaultTrapFrame {
  std::uint64_t rax;
  std::uint64_t rbx;
  std::uint64_t rcx;
  std::uint64_t rdx;
  std::uint64_t rdi;
  std::uint64_t rsi;
  std::uint64_t rbp;
  std::uint64_t r8;
  std::uint64_t r9;
  std::uint64_t r10;
  std::uint64_t r11;
  std::uint64_t r12;
  std::uint64_t r13;
  std::uint64_t r14;
  std::uint64_t r15;
  std::uint64_t error_code;
  std::uint64_t rip;
  std::uint64_t cs;
  std::uint64_t rflags;
  std::uint64_t rsp;
  std::uint64_t ss;
};

} // namespace impl

bool is_canonical(uint64_t va)
{
  int64_t sa = int64_t(va);
  return ((sa << 16) >> 16) == sa;
}

uint64_t VA::check_canonical(uint64_t va) {
  ASSERT(is_canonical(va));
  return va;
}




impl::AddressSpace::AddressSpace(uint64_t cr3) : cr3(cr3)
{
  // load upper half from kernel :)
  memcpy((char*)cr3 + HALF_FRAME,
         (char*)(CCB::current()->kernel_cr3 + Sys::hhdm_offset) + HALF_FRAME,
         HALF_FRAME);
}

static SpinLock address_spaces_lock;
static impl::AddressSpace *address_spaces = nullptr;
// Fork must not publish a copied page table halfway through an invalidation.
static BlockingLock fork_truncate_lock;

StrongRef<impl::AddressSpace> impl::AddressSpace::create() {
  StrongRef as{new AddressSpace(VA{physMem.alloc()}.va())};
  as->self = as;
  LockGuard g(address_spaces_lock);
  as->next = address_spaces;
  address_spaces = as.operator->();
  return as;
}

impl::AddressSpace::~AddressSpace() {
  LockGuard g(address_spaces_lock);
  auto **p = &address_spaces;
  while (*p != this) p = &(*p)->next;
  *p = next;
}


namespace VMM
{
class DeferredFreeList
{
  struct DeferredFree { DeferredFree *next; uint64_t ppn; };

  DeferredFree *deferred_frees = nullptr;
  DeferredFree *anonymous_releases = nullptr;

public:
  struct FileRelease { FileRelease *next; StrongRef<Node> file; uint64_t offset; uint64_t ppn; bool dirty; };

private:
  FileRelease *file_releases = nullptr;

public:
  void defer(uint64_t ppn) { deferred_frees = new DeferredFree{deferred_frees, ppn}; }
  void drop(uint64_t ppn) { anonymous_releases = new DeferredFree{anonymous_releases, ppn}; }
  DeferredFree *take_anonymous() { auto *r = anonymous_releases; anonymous_releases = nullptr; return r; }
  void release(StrongRef<Node> file, uint64_t offset, uint64_t ppn, bool dirty) { file_releases = new FileRelease{file_releases, file, offset, ppn, dirty}; }
  FileRelease *take_releases() { auto *r = file_releases; file_releases = nullptr; return r; }

  void flush()
  {
    while (deferred_frees)
    {
      auto *p = deferred_frees;
      deferred_frees = p->next;

      physMem.free(PPN{p->ppn});

      delete p;
    }
  }
};
} // namespace VMM

/*
 * A simplified mmap implementation
 */
static sync<int> munmap_impl(void *addr, std::size_t length,
                             const VMM::VirtualMemoryArea *replacement = nullptr);
static void *reserve_area(impl::AddressSpace *as, void *addr, uint64_t frames,
                         int prot, int flags, StrongRef<Node> file, uint64_t offset,
                         StrongRef<VMM::SharedAnonymous> anonymous);
static StrongRef<VMM::SharedAnonymous::Range> coverage(StrongRef<VMM::SharedAnonymous> backing,
                                                     uint64_t offset, uint64_t frames) {
  return backing == nullptr ? StrongRef<VMM::SharedAnonymous::Range>{}
      : StrongRef<VMM::SharedAnonymous::Range>::make(backing, offset, frames);
}

sync<void*> VMM::mmap(void *addr, std::size_t length, int prot, int flags, StrongRef<Node> file, uint64_t offset)
{
  auto *t = impl::CCB::current()->current_thread;
  ASSERT(t != nullptr /* mmap is only for lower half memory, shouldn't happen in kernel */);
  StrongRef<impl::PCB> process = t->process;

  if (!(bool(flags & MAP_SHARED) ^ bool(flags & MAP_PRIVATE))) co_return MAP_FAILED;
  if (file != nullptr && offset % FRAME_SIZE != 0) co_return MAP_FAILED;
  if (bool(flags & MAP_ANONYMOUS) != (file == nullptr)) co_return MAP_FAILED;
  if (length == 0 || length > VA_END - FRAME_SIZE) co_return MAP_FAILED;
  uint64_t frames = (length + FRAME_SIZE - 1)/FRAME_SIZE;
  if (file != nullptr && (offset > INT64_MAX || frames > (INT64_MAX - offset) / FRAME_SIZE))
    co_return MAP_FAILED;
  if (file == nullptr) offset = 0;
  if (addr != nullptr && (!is_canonical((uint64_t)addr) ||
      (uint64_t)addr >= VA_END || length > VA_END - (uint64_t)addr)) co_return MAP_FAILED;
  if ((flags & MAP_FIXED) && (addr == nullptr || (uint64_t)addr % FRAME_SIZE != 0)) co_return MAP_FAILED;
  StrongRef<SharedAnonymous> anonymous{};
  if ((flags & MAP_SHARED) && (flags & MAP_ANONYMOUS))
    anonymous = StrongRef<SharedAnonymous>::make();
  if (flags & MAP_FIXED) {
    VirtualMemoryArea replacement{nullptr, false, uint64_t(addr) >> LOG_FRAME_SIZE,
                                 frames, prot, flags, file, offset, anonymous};
    if (co_await munmap_impl(addr, length, &replacement) != 0) co_return MAP_FAILED;
    co_return addr;
  }

  auto proof = co_await process->as->vma_list_lock;
  co_return reserve_area(process->as.operator->(), addr, frames, prot, flags, file, offset, anonymous);
}

static void *reserve_area(impl::AddressSpace *as, void *addr, uint64_t frames,
                         int prot, int flags, StrongRef<Node> file, uint64_t offset,
                         StrongRef<VMM::SharedAnonymous> anonymous) {
  using namespace VMM;
  VirtualMemoryArea **memory_areas = &as->memory_areas;

  VirtualMemoryArea **prev = memory_areas;
  VirtualMemoryArea *area = *memory_areas;
  if (addr != nullptr)
  {
    const uint64_t req = VA((uint64_t)addr).vpn().vpn();

    while (area->next != nullptr && area->next->base <= req)
    {
      prev = &area->next;
      area = area->next;
    }

    const bool fits = area->free && req >= area->base &&
                      area->length - (req - area->base) >= frames;

    if (!fits)
    {
      if (flags & MAP_FIXED) return MAP_FAILED;

      // otherwise, the addr hint failed so we get to ignore it
      prev = memory_areas;
      area = *memory_areas;
    }
    else
    {
      // carve [area->base, req) back off so the allocation starts at req
      const uint64_t lead = req - area->base;
      if (lead > 0)
      {
        *prev = new VirtualMemoryArea{
          area, true, area->base, lead, PROT_NONE, 0, StrongRef<Node>{}, 0
        };
        prev = &(*prev)->next;
        area->base = req;
        area->length -= lead;
      }
    }
  }

  while (!area->free || area->length < frames)
  {
    prev = &area->next;
    area = area->next;
    if (area == nullptr)
    {
      area = *memory_areas;
      while (area != nullptr)
      {
        KPRINT("\t0x?: ? frames (?)\n", area->base, Dec(area->length), area->free ? "free" : "allocated");
        area = area->next;
      }
      KPRINT("!!! Insufficient memory to allocate ? frames !!!!\n", Dec(frames));
      return MAP_FAILED;
    }
  }

  void* ptr = VA(area->base << LOG_FRAME_SIZE);

  if (area->length == frames)
  {
    area->free = false;
    area->prot = prot;
    area->flags = flags;
    area->file = file;
    area->offset = offset;
    area->anonymous = anonymous;
    area->coverage = coverage(anonymous, offset, frames);
  } else
  {
    *prev = new VirtualMemoryArea{
      area, false, area->base, frames, prot, flags, file, offset, anonymous, coverage(anonymous, offset, frames)
    };
    area->base += frames;
    area->length -= frames;
  }

  return ptr;
}

static SpinLock page_table_lock;

static VMM::PageTableEntry* base_page_table()
{
  return (VMM::PageTableEntry*) VA(PA(get_cr3() & ~(FRAME_SIZE-1))).va();
}

static uint64_t PTE_ppn(VMM::PageTableEntry *layer)
{
  return PA((uint64_t)layer - Sys::hhdm_offset).ppn().ppn();
}
static uint16_t &pte_count(uint64_t ppn) { return VMM::present_ptes[physMem.ppn_index(ppn)]; }
static uint16_t &frame_ref(uint64_t ppn) { return VMM::frame_refs[physMem.ppn_index(ppn)]; }
static bool drop_ref(uint64_t ppn) { LockGuard g(page_table_lock); return --frame_ref(ppn) == 0; }

VMM::SharedAnonymous::Range::Range(StrongRef<SharedAnonymous> backing, uint64_t offset, uint64_t length)
    : backing(backing), offset(offset), length(length) {
  LockGuard g(backing->lock);
  next = backing->ranges;
  backing->ranges = this;
}

VMM::SharedAnonymous::Range::~Range() {
  LockGuard g(backing->lock);
  auto **r = &backing->ranges;
  while (*r != this) r = &(*r)->next;
  *r = next;
  for (auto **p = &backing->pages; *p;) {
    bool covered = false;
    for (auto *range = backing->ranges; range; range = range->next)
      if ((*p)->offset >= range->offset &&
          ((*p)->offset - range->offset) / FRAME_SIZE < range->length) { covered = true; break; }
    if (covered) { p = &(*p)->next; continue; }
    auto *page = *p;
    *p = page->next;
    if (drop_ref(page->ppn.ppn())) physMem.free(page->ppn);
    delete page;
  }
}

VMM::SharedAnonymous::~SharedAnonymous() {
  while (pages) {
    auto *p = pages;
    pages = p->next;
    if (drop_ref(p->ppn.ppn())) physMem.free(p->ppn);
    delete p;
  }
}

constexpr uint64_t PTE_PER_PAGE = FRAME_SIZE/sizeof(VMM::PageTableEntry); // 512

static uint64_t min(uint64_t a, uint64_t b) { return b < a ? b : a; }

static sync<bool> retain_cached_page(StrongRef<Node> file, uint64_t offset, uint64_t ppn, bool dirty = false)
{
  if (file == nullptr) co_return false;
  auto proof = co_await file->mapped_blocks;
  auto cached = proof.data().get(offset);
  if (!cached.has_value() || (*cached)->ppn.ppn() != ppn) co_return false;
  (*cached)->dirty |= dirty;

  if (--(*cached)->refcount == 0)
  {
    // Flush while still published and locked: a write after munmap's earlier
    // flush must not disappear when the last cached reference is removed.
    uint64_t size = co_await file->size_in_bytes();
    if ((*cached)->dirty && offset < size &&
        !co_await file->write_uncached(offset, min(FRAME_SIZE, size - offset), (char*)VA(PPN(ppn)).va())) {
      // A full backing filesystem must not panic or lose mapped stores. Keep
      // this page authoritative in memory until it can be flushed or truncated.
      ++file->orphan_pages;
      file->cache_pin = file;
      co_return true;
    }
    proof.data().remove(offset);
    proof.unlock();
    delete *cached;
    co_return false;
  }
  co_return true;
}

// runs after the page tables stop pointing at the frames, so it can take the cache locks
static sync<void> release_pages(VMM::DeferredFreeList *list)
{
  for (auto *r = list->take_anonymous(); r;) {
    auto *next = r->next;
    if (drop_ref(r->ppn)) list->defer(r->ppn);
    delete r;
    r = next;
  }
  for (auto *r = list->take_releases(); r;)
  {
    auto *next = r->next;
    if (!co_await retain_cached_page(r->file, r->offset, r->ppn, r->dirty)) list->defer(r->ppn);
    delete r;
    r = next;
  }
}

static uint64_t unmap(VMM::PageTableEntry *layer, uint8_t layer_idx, uint64_t base,
                      uint64_t frames, StrongRef<Node> &file, uint64_t file_offset, VMM::DeferredFreeList* list)
{
  uint64_t mask = (PTE_PER_PAGE-1) << 27;
  uint64_t index = (base & mask) >> 27;

  if (layer_idx == 3)
  {
    uint64_t to_delete = min(frames, PTE_PER_PAGE-index);

    uint64_t removed = 0;
    for (uint64_t i = index; i < index+to_delete; i++)
    {
      // Hardware updates A/D bits concurrently even under the software lock.
      uint64_t old = __atomic_exchange_n((uint64_t*)(void*)(layer+i), 0, __ATOMIC_SEQ_CST);
      if (!(old & 1)) continue;
      uint64_t ppn = (old >> LOG_FRAME_SIZE) & ((uint64_t(1) << 36) - 1);
      uint64_t page_off = file_offset + (i - index) * FRAME_SIZE;
      if (file == nullptr)
      {
        list->drop(ppn);
      }
      else list->release(file, page_off, ppn, old & (1 << 6));
      removed++;
    }

    ASSERT(pte_count(PTE_ppn(layer)) >= removed);
    pte_count(PTE_ppn(layer)) -= removed;

    return to_delete;
  }

  uint64_t covered = 0;
  while (covered < frames && index < PTE_PER_PAGE)
  {
    VMM::PageTableEntry *entry = layer + index;
    uint64_t pages_per_pte = 1l << (9 * (3 - layer_idx));
    uint64_t offset_in_entry = (base >> (9 * layer_idx)) & (pages_per_pte - 1);
    uint64_t want = min(frames - covered, pages_per_pte - offset_in_entry);

    if (!entry->present)
    {
      // Subtree was never mapped; skip only the rest of this entry.
      covered += want;
      base += want << (9 * layer_idx);
      index = (base & mask) >> 27;
      continue;
    }

    uint64_t to_free = unmap((VMM::PageTableEntry *)VA(PPN(entry->ppn)).va(), layer_idx + 1,
                             base << 9, want, file, file_offset + covered * FRAME_SIZE, list);
    if (pte_count(entry->ppn) == 0)
    {
      entry->present = false;
      list->defer(entry->ppn);
      if (layer_idx != 0) pte_count(PTE_ppn(layer))--;
    }

    covered += to_free;
    base += to_free << (9 * layer_idx);
    index = (base & mask) >> 27;
  }

  return covered;
}

static void unmap(VPN base, uint64_t frames, StrongRef<Node> file, uint64_t offset, VMM::DeferredFreeList* list)
{
  LockGuard g(page_table_lock);
  unmap(base_page_table(), 0, base.vpn(), frames, file, offset, list);
}

static sync<void> write_back(VPN base, uint64_t frames, StrongRef<Node> file, uint64_t offset,
                             VMM::PageTableEntry *root = nullptr);

static sync<int> munmap_impl(void *addr, std::size_t length,
                             const VMM::VirtualMemoryArea *replacement)
{
  using namespace VMM;
  auto *t = impl::CCB::current()->current_thread;
  ASSERT(t != nullptr /* munmap is only for lower half memory, shouldn't happen in kernel */);
  StrongRef<impl::PCB> process = t->process;

  if (!is_canonical((uint64_t)addr) || (uint64_t)addr % FRAME_SIZE != 0)
  {
    KPRINT("!!! Misaligned munmap !!!\n");
    co_return -1;
  }

  uint64_t base = VA((uint64_t)addr).vpn().vpn();
  uint64_t frames = (length + FRAME_SIZE - 1)/FRAME_SIZE;
  if (frames == 0)
  {
    KPRINT("!!! munmap len 0 !!!\n");
    co_return -1;
  }
  if (base == 0)
  {
    base = 1; // if we're starting at addr 0, we may as well just start at 1!
    frames -= 1; // already "freed" the first page
    if (frames == 0) co_return 0; // was only trying to free the null page apparently
  }
  uint64_t o_base = base;
  uint64_t o_frames = frames;

  auto proof = co_await process->as->vma_list_lock;

  VirtualMemoryArea *memory_areas = process->as->memory_areas;

  VirtualMemoryArea *area = find_vma(memory_areas, base);

  // when munmapping VMA, keep data to pass to PTE logic
  struct UnmapRange {
    UnmapRange *next;
    uint64_t base;   // VA
    uint64_t length; // pages
    StrongRef<Node> file;
    uint64_t offset;
    int flags;
  };
  UnmapRange *actually_freed = nullptr;

  while (!(area->free && area->base+area->length >= base + frames))
  {
    if (area->free) // if already free, we extend past this frame
    {
      frames -= area->length - (base - area->base);
      base = area->base + area->length;
      area = area->next;

      if (area == nullptr)
      {
        KPRINT("!!! Attempted to free beyond end of memory !!!\n");
        co_return -1;
      }
      if (area->free) continue; // we assume not free below
    }

    uint64_t before = base - area->base;
    bool contained = area->base+area->length >= base + frames;
    uint64_t after = (area->base+area->length) - (base+frames);

    if (before > 0) {
      area->next = new VirtualMemoryArea{
        area->next, false, area->base+before, area->length-before, area->prot, area->flags, area->file, area->offset+before*FRAME_SIZE, area->anonymous,
        coverage(area->anonymous, area->offset+before*FRAME_SIZE, area->length-before)
      };
      area->length = before;
      area->coverage = coverage(area->anonymous, area->offset, before);
      area = area->next;
    }

    if (contained && after > 0)
    {
      area->next = new VirtualMemoryArea{
        area->next, false, area->base+frames, after, area->prot, area->flags, area->file, area->offset+frames*FRAME_SIZE, area->anonymous,
        coverage(area->anonymous, area->offset+frames*FRAME_SIZE, after)
      };
      area->length = frames;
    }

    actually_freed = new UnmapRange{actually_freed, area->base << LOG_FRAME_SIZE,
                                    area->length, area->file, area->offset, area->flags};
    area->free = true;
    area->file = nullptr;
    area->coverage = nullptr;
    area->anonymous = nullptr;

    if (!contained)
    {
      base = area->next->base;
      frames -= area->length;
      area = area->next;

      if (area == nullptr)
      {
        KPRINT("!!! Attempted to free beyond end of memory !!!\n");
        co_return -1;
      }
    }
  }

  for (auto *a = memory_areas; a && a->next;)
  {
    if (a->free && a->next->free)
    {
      a->length += a->next->length;
      auto *n = a->next->next;
      delete a->next;
      a->next = n;
    } else a = a->next;
  }

  for (auto *r = actually_freed; r; r = r->next)
  {
    if (r->file != nullptr && (r->flags & VMM::MAP_SHARED)) co_await write_back(VA(r->base).vpn(), r->length, r->file, r->offset);
  }

  auto *list = new DeferredFreeList();
  for (auto *r = actually_freed; r;)
  {
    auto *next = r->next;
    unmap(VA(r->base).vpn(), r->length, r->file, r->offset, list);
    delete r;
    r = next;
  }
  // Publish a fixed replacement before releasing the VMA lock: neither another
  // mapper nor a fault may observe the temporary hole.
  if (replacement) {
    auto *r = replacement;
    ASSERT(reserve_area(process->as.operator->(), addr, r->length, r->prot,
                        r->flags, r->file, r->offset, r->anonymous) == addr);
  }
  // drop the VMA lock before sleeping on shootdown so other cores can fault
  proof.unlock();
  // IF=0 from syscall/page-fault gates; other cores need us to ack their
  // shootdowns while we wait, and they need IF=1 to ack ours
  sti();
  co_await TLB::Shootdown{VPN{o_base}, VPN{o_base + o_frames}};
  co_await release_pages(list);
  list->flush();
  delete list;

  co_return 0;
}

sync<int> VMM::munmap(void *addr, std::size_t length) {
  co_return co_await munmap_impl(addr, length);
}

static VMM::PageTableEntry* find_entry(VPN vpn, VMM::PageTableEntry *root = nullptr)
{
  LockGuard g(page_table_lock);
  auto* layer = root ? root : base_page_table();

  uint64_t mask = (PTE_PER_PAGE - 1) << 27;
  uint64_t index = vpn.vpn();

  for (int i = 0; i < 3; i++)
  {
    VMM::PageTableEntry *entry = layer + ((index & mask) >> 27);
    if (!entry->present) return nullptr;

    layer = (VMM::PageTableEntry*) VA(PPN(entry->ppn)).va();
    index <<= 9;
  }

  VMM::PageTableEntry *entry = layer + ((index & mask) >> 27);
  if (!entry->present) return nullptr;
  return entry;
}

uint64_t VMM::physical_address(uintptr_t addr) {
  auto *pte = find_entry(VA{addr}.vpn());
  return pte ? (pte->ppn << LOG_FRAME_SIZE) + (addr % FRAME_SIZE) : 0;
}

static sync<void> write_back(VPN base, uint64_t frames, StrongRef<Node> file, uint64_t offset,
                             VMM::PageTableEntry *root)
{
  for (uint64_t i = 0; i < frames; i++)
  {
    VMM::PageTableEntry *pte = find_entry(VPN{base.vpn() + i}, root);
    if (pte == nullptr) continue;
    uint64_t ppn = pte->ppn;

    uint64_t file_offset = offset + i * FRAME_SIZE;
    auto proof = co_await file->mapped_blocks;
    auto cached = proof.data().get(file_offset);
    bool mine = cached.has_value() && (*cached)->ppn.ppn() == ppn; // private copies aren't the file's page
    if (!mine) continue;
    (*cached)->dirty |= pte->dirty;
    if (!(*cached)->dirty) continue;

    uint64_t size = co_await file->size_in_bytes();
    if (file_offset >= size) continue; // don't expand file...
    co_await file->write_uncached(file_offset, min(FRAME_SIZE, size - file_offset), (char*)VA(PPN(ppn)).va());
  }
}

sync<void> VMM::copy_address_space(StrongRef<impl::AddressSpace> src, StrongRef<impl::AddressSpace> dst)
{
  auto fork_guard = co_await fork_truncate_lock;
  struct ChildMapping { ChildMapping *next; uint64_t vpn; uint64_t ppn; bool write; bool no_execute; };
  struct ShootRange { ShootRange *next; VPN start; VPN end; };

  ChildMapping *mappings = nullptr;
  ShootRange *shoots = nullptr;

  auto proof = co_await src->vma_list_lock;

  delete dst->memory_areas;
  VirtualMemoryArea **link = &dst->memory_areas;
  for (VirtualMemoryArea *a = src->memory_areas; a; a = a->next)
  {
    *link = new VirtualMemoryArea{nullptr, a->free, a->base, a->length, a->prot, a->flags, a->file, a->offset, a->anonymous, a->coverage};
    link = &(*link)->next;
  }

  // the parent's cr3 is active here, so gather what the child needs in one pass
  for (VirtualMemoryArea *a = src->memory_areas; a; a = a->next)
  {
    if (a->free) continue;

    bool shared = a->flags & MAP_SHARED;
    bool write = a->prot & PROT_WRITE;
    bool no_execute = !(a->prot & PROT_EXEC);
    bool downgraded = false;

    for (uint64_t i = 0; i < a->length; i++)
    {
      uint64_t vpn = a->base + i;
      VMM::PageTableEntry *pte = find_entry(VPN{vpn});
      if (pte == nullptr) continue;
      uint64_t ppn = pte->ppn;

      if (a->file == nullptr)
      {
        // private memory is CoW (the first write on either side copies it); shared memory stays shared
        LockGuard g(page_table_lock);
        ASSERT(frame_ref(ppn) < UINT16_MAX);
        frame_ref(ppn)++;
        if (!shared && pte->write)
        {
          pte->write = false;
          downgraded = true;
        }
        mappings = new ChildMapping{mappings, vpn, ppn, shared && pte->write, no_execute};
        continue;
      }

      // a file page still on its cached frame is shared; the child's first write to a private one copies it
      auto cache = co_await a->file->mapped_blocks;
      auto cached = cache.data().get(a->offset + i * FRAME_SIZE);
      if (cached.has_value() && (*cached)->ppn.ppn() == ppn)
      {
        (*cached)->refcount++;
        mappings = new ChildMapping{mappings, vpn, ppn, shared && write, no_execute};
        continue;
      }

      uint64_t child_ppn = physMem.alloc().ppn();
      memcpy((char*)VA(PPN(child_ppn)).va(), (char*)VA(PPN(ppn)).va(), FRAME_SIZE);
      mappings = new ChildMapping{mappings, vpn, child_ppn, write, no_execute};
    }

    if (downgraded) shoots = new ShootRange{shoots, VPN{a->base}, VPN{a->base + a->length}};
  }

  uint64_t parent_cr3 = get_cr3();
  set_cr3(dst->cr3_phys());
  for (ChildMapping *m = mappings; m;)
  {
    impl::map(VPN{m->vpn}, PPN{m->ppn}, true, m->write, m->no_execute);
    ChildMapping *next = m->next;
    delete m;
    m = next;
  }
  set_cr3(parent_cr3);

  // drop the VMA lock before the shootdown, same as munmap
  proof.unlock();
  sti();
  for (ShootRange *s = shoots; s;)
  {
    co_await TLB::Shootdown{s->start, s->end};
    ShootRange *next = s->next;
    delete s;
    s = next;
  }
}

sync<void> VMM::truncate_file(StrongRef<Node> file, uint32_t length) {
  auto fork_guard = co_await fork_truncate_lock;
  struct Space {
    Space *next;
    StrongRef<impl::AddressSpace> as;
    BlockingLock::Proof proof{nullptr};
  };
  Space *spaces = nullptr;
  {
    LockGuard g(address_spaces_lock);
    for (auto *as = address_spaces; as; as = as->next) {
      StrongRef<impl::AddressSpace> hold{as->self};
      if (hold != nullptr) spaces = new Space{spaces, hold};
    }
  }
  // Lock VMAs before the page cache, matching faults/unmap. Other VM operations
  // hold only one VMA lock; fork is excluded above. Keep faults out until shootdown.
  for (auto *s = spaces; s; s = s->next) s->proof = co_await s->as->vma_list_lock;
  auto cache = co_await file->mapped_blocks;
  uint64_t old_size = co_await file->size_in_bytes();
  if (length < old_size) {
    co_await file->truncate(length);
    auto *list = new DeferredFreeList();
    uint64_t first_removed = (uint64_t(length) + FRAME_SIZE - 1) / FRAME_SIZE;
    for (auto *s = spaces; s; s = s->next) {
      // reset() leaves no VMAs before destroy() frees the page-table root.
      auto *root = (PageTableEntry*)s->as->cr3;
      for (auto *a = s->as->memory_areas; a; a = a->next) {
        if (a->free || a->file != file) continue;
        uint64_t start = a->offset / FRAME_SIZE;
        uint64_t skip = first_removed > start ? min(a->length, first_removed - start) : 0;
        if (skip < a->length) {
          LockGuard g(page_table_lock);
          unmap(root, 0, a->base + skip, a->length - skip, file,
                a->offset + skip * FRAME_SIZE, list);
        }
        // The surviving last page keeps its prefix (including private COW data).
        if (length % FRAME_SIZE && length / FRAME_SIZE >= start &&
            length / FRAME_SIZE - start < a->length) {
          auto *pte = find_entry(VPN{a->base + length / FRAME_SIZE - start}, root);
          if (pte) memset((char*)VA(PPN{pte->ppn}).va() + length % FRAME_SIZE,
                          0, FRAME_SIZE - length % FRAME_SIZE);
        }
      }
    }
    sti();
    co_await TLB::Shootdown{VA_START.vpn(), VPN{VA_END >> LOG_FRAME_SIZE}};
    // No stale TLB can store into a removed page after this point.
    // Also clear cached pages whose last unmap/COW is awaiting release. A
    // subsequent write may regrow the file before those references disappear.
    for (uint64_t offset = uint64_t(length) / FRAME_SIZE * FRAME_SIZE;
         offset < old_size; offset += FRAME_SIZE) {
      auto cached = cache.data().get(offset);
      if (cached.has_value()) {
        uint64_t tail = offset < length ? length - offset : 0;
        memset((char*)VA((*cached)->ppn).va() + tail, 0, FRAME_SIZE - tail);
        if ((*cached)->refcount == 0 && offset >= length) {
          physMem.free((*cached)->ppn);
          delete *cached;
          cache.data().remove(offset);
          if (--file->orphan_pages == 0) file->cache_pin = nullptr;
        }
      }
    }
    cache.unlock();
    co_await release_pages(list);
    list->flush();
    delete list;
  }
  cache.unlock();
  while (spaces) {
    auto *s = spaces;
    spaces = s->next;
    delete s;
  }
}

void impl::map(VPN vpn, PPN ppn, bool user, bool write, bool no_execute, uint64_t root)
{
  LockGuard g(page_table_lock);
  auto* layer = root ? (VMM::PageTableEntry*)root : base_page_table();

  uint64_t mask = (PTE_PER_PAGE - 1) << 27;
  uint64_t index = vpn.vpn();

  for (int i = 0; i < 3; i++)
  {
    VMM::PageTableEntry *entry = layer + ((index & mask) >> 27);
    if (!entry->present)
    {
      // physMem.alloc zeros the frame
      *entry = { .present = true, .write = true, .user = true, .ppn = physMem.alloc().ppn(), .no_execute = false };
      if (i != 0) pte_count(PTE_ppn(layer))++;
    }

    layer = (VMM::PageTableEntry*) VA(PPN(entry->ppn)).va();
    index <<= 9;
  }

  VMM::PageTableEntry *entry = layer + ((index & mask) >> 27);
  ASSERT(!entry->present);

  *entry = { .present = true, .write = write, .user = user, .ppn = ppn.ppn(), .no_execute = no_execute };
  pte_count(PTE_ppn(layer))++;
  invlpg((void *)VA{vpn}.va());
}

struct FaultTableEntry
{
  int refs = 0; // 0 => free; owner + waiters hold refs (no heap)
  uint64_t vpn = 0;
  uint64_t cr3 = 0; // address space; VPN alone is not unique
  Future<bool> done{};
};
constexpr uint8_t FAULT_TABLE_SIZE = 32;
SpinLock fault_table_lock;
FaultTableEntry fault_table[FAULT_TABLE_SIZE];

// VA goes in cr2
static sync<void> pageFault(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame,
                            Atomic<bool>& done, int& signal, int& code)
{
  struct Completion { Atomic<bool> &done; ~Completion() { done.set(true); } } completion{done};
  bool is_write_fault = trap_frame->error_code & 0b10;

  VPN vpn = VA(cr2).vpn();

  ASSERT(impl::CCB::current()->current_thread != nullptr);

  auto as = impl::CCB::current()->current_thread->process->as;
  auto *root = (VMM::PageTableEntry*)as->cr3;

  auto vma_lock = co_await as->vma_list_lock;

  VMM::VirtualMemoryArea *area = as->memory_areas && cr2 >= VMM::VA_START.va() && cr2 < VMM::VA_END
      ? VMM::find_vma(as->memory_areas, vpn.vpn()) : nullptr;
  if (!area || area->free || vpn.vpn() < area->base || vpn.vpn() >= area->base + area->length)
  {
    signal = 11; code = 1;
    co_return;
  }
  int permission = (trap_frame->error_code & 16) ? VMM::PROT_EXEC :
                   is_write_fault ? VMM::PROT_WRITE : VMM::PROT_READ;
  if (!(area->prot & permission)) {
    signal = 11; code = 2;
    co_return;
  }
  bool is_private = area->flags & VMM::MAP_PRIVATE;
  bool priv = is_private && is_write_fault; // are we making a new private mapping and already triggering CoW
  uint64_t offset = area->offset + (vpn.vpn() - area->base) * FRAME_SIZE;
  if (area->file != nullptr && offset >= co_await area->file->size_in_bytes()) {
    signal = 7; code = 2;
    co_return;
  }

  fault_table_lock.lock();
  int8_t first_free = -1;
  for (int8_t i = 0; i < FAULT_TABLE_SIZE; i++)
  {
    if (first_free == -1 && fault_table[i].refs == 0) first_free = i;

    if (fault_table[i].refs > 0 && fault_table[i].vpn == vpn.vpn() &&
        fault_table[i].cr3 == as->cr3_phys())
    {
      fault_table[i].refs++;
      fault_table_lock.unlock();
      vma_lock.unlock();
      co_await fault_table[i].done;
      fault_table_lock.lock();
      fault_table[i].refs--;
      fault_table_lock.unlock();
      co_return;
    }
  }
  if (auto pte = find_entry(vpn, root); pte)
  {
    fault_table_lock.unlock();

    if (priv) // private write to a page that was mapped read-only
    {
      if (!(area->prot & VMM::PROT_WRITE))
      {
        KPANIC("Write fault: ? (page ?) (in area ?, len ? [?])\n", cr2, vpn.vpn(), area->base, area->length, area->free ? "free" : "alloc");
      }

      uint64_t oldPpn = 0;
      uint64_t newPpn = 0;
      bool did_cow = false;
      StrongRef<Node> cow_file = area->file;
      if (area->file == nullptr)
      {
        LockGuard g(page_table_lock);
        if (!pte->write)
        {
          oldPpn = pte->ppn;
          if (frame_ref(oldPpn) > 1)
          {
            newPpn = physMem.alloc().ppn();
            frame_ref(newPpn) = 1;
            memcpy((char*)VA(PPN(newPpn)).va(), (char*)VA(PPN(oldPpn)).va(), FRAME_SIZE);
            pte->ppn = newPpn;
            did_cow = true;
          }
          pte->write = true;
          impl::invlpg((void *)VA{vpn}.va());
        }
        else impl::invlpg((void *)VA{vpn}.va());
      }
      else
      {
        // cache lock before page_table_lock: nothing may suspend while holding the spinlock
        auto cache = co_await area->file->mapped_blocks;
        LockGuard g(page_table_lock);
        if (!pte->write)
        {
          oldPpn = pte->ppn;
          auto cached = cache.data().get(offset);
          ASSERT(cached.has_value() && (*cached)->ppn.ppn() == oldPpn);

          newPpn = physMem.alloc().ppn();
          memcpy((char*)VA(PPN(newPpn)).va(), (char*)VA(PPN(oldPpn)).va(), FRAME_SIZE);
          pte->ppn = newPpn;
          pte->write = true;
          impl::invlpg((void *)VA{vpn}.va());

          did_cow = true;
        }
        else impl::invlpg((void *)VA{vpn}.va());
      }
      vma_lock.unlock();

      if (did_cow)
      {
        sti();
        co_await TLB::Shootdown{vpn, VPN{vpn.vpn()+1}};
        if (cow_file != nullptr ? !co_await retain_cached_page(cow_file, offset, oldPpn) : drop_ref(oldPpn))
          physMem.free(PPN{oldPpn});
      }
    }
    else vma_lock.unlock();

    co_return;
  }

  if (first_free == -1) KPANIC("Fault table filled! (fault on ?)", vpn.vpn());
  auto &slot = fault_table[first_free];
  slot.done.~Future();
  new (&slot.done) Future<bool>();
  slot.vpn = vpn.vpn();
  slot.cr3 = as->cr3_phys();
  slot.refs = 1;
  fault_table_lock.unlock();

  PPN ppn = PA{0}.ppn();

  if (area->flags & VMM::MAP_ANONYMOUS)
  {
    ASSERT(area->file == nullptr);
    if (area->anonymous != nullptr) {
      LockGuard backing(area->anonymous->lock);
      auto *page = area->anonymous->pages;
      while (page && page->offset != offset) page = page->next;
      if (!page) {
        page = new VMM::SharedAnonymous::Page{area->anonymous->pages, offset, physMem.alloc()};
        area->anonymous->pages = page;
        frame_ref(page->ppn.ppn()) = 1; // backing object owns one reference
      }
      ppn = page->ppn;
      LockGuard g(page_table_lock);
      ASSERT(frame_ref(ppn.ppn()) < UINT16_MAX);
      frame_ref(ppn.ppn())++;
    } else {
      ppn = physMem.alloc(); // already zeroed
      frame_ref(ppn.ppn()) = 1;
    }
  } else
  {
    ASSERT(area->file != nullptr);

    // held across the disk read so no other fault can see a half-loaded frame
    auto cache = co_await area->file->mapped_blocks;
    if (auto cached = cache.data().get(offset); cached.has_value())
    {
      if (!priv)
      {
        if ((*cached)->refcount == 0 && --area->file->orphan_pages == 0) area->file->cache_pin = nullptr;
        (*cached)->refcount++;
        ppn = (*cached)->ppn;
      } else
      {
        ppn = physMem.alloc();
        memcpy((char*)VA(ppn).va(), (char*)VA((*cached)->ppn).va(), FRAME_SIZE);
      }
    }
    else
    {
      ppn = physMem.alloc();
      co_await area->file->read_uncached(offset, FRAME_SIZE, (char*)VA(ppn).va());
      if (!priv) // shared: publish into the page cache
        cache.data().put(offset, new Node::FileMapping{ppn, 1});
    }
  }

  bool writeable = (!is_private || priv || area->flags & VMM::MAP_ANONYMOUS) && (area->prot & VMM::PROT_WRITE);
  bool no_execute = !(area->prot & VMM::PROT_EXEC);
  impl::map(vpn, ppn, cr2 >> 47 == 0, writeable, no_execute, as->cr3);

  fault_table_lock.lock();
  fault_table[first_free].done.set(true);
  fault_table[first_free].refs--;
  fault_table_lock.unlock();

  co_return;
}

extern "C" void copy_user_access_();
extern "C" void store_user32_access_();
extern "C" void copy_user_fault_();

extern "C" [[gnu::force_align_arg_pointer]] void
pageFaultHandler(uintptr_t cr2, impl::PageFaultTrapFrame *trap_frame, const void *fpu) {
  using namespace impl;

  sti();

  TCB *t = CCB::current()->current_thread;
  uint64_t cr3 = get_cr3();
  bool gp = trap_frame->error_code >> 63;
  bool user = (trap_frame->cs & 3) == 3;
  bool copying = !user && (trap_frame->rip == uintptr_t(copy_user_access_) ||
                           trap_frame->rip == uintptr_t(store_user32_access_));
  if (copying && (gp || !t || t->zombie || cr3 != t->process->as->cr3_phys())) {
    trap_frame->rip = uintptr_t(copy_user_fault_);
    return;
  }
  auto fault_signal = [&](int signal, uintptr_t address, int code) {
    auto &c = t->context;
    c.rax=trap_frame->rax; c.rbx=trap_frame->rbx; c.rcx=trap_frame->rcx;
    c.rdx=trap_frame->rdx; c.rdi=trap_frame->rdi; c.rsi=trap_frame->rsi; c.rbp=trap_frame->rbp;
    c.r8=trap_frame->r8; c.r9=trap_frame->r9; c.r10=trap_frame->r10;
    c.r11=trap_frame->r11; c.r12=trap_frame->r12; c.r13=trap_frame->r13;
    c.r14=trap_frame->r14; c.r15=trap_frame->r15;
    c.rip=trap_frame->rip; c.rsp=trap_frame->rsp; c.rflags=trap_frame->rflags;
    c.fs_base=rdfsbase(); c.gs_base=Msr::KernelGSBase().read();
    memcpy(c.fpu, fpu, sizeof(c.fpu));
    if (!t->deliver_signal(signal, address, code)) {
      CCB::current()->current_thread = nullptr;
      drop_to_kernel_cr3();
      bounce_to_yield_();
    }
    t->resume();
  };
  if (gp && user && t) fault_signal(11, 0, 128);
  if (gp) KPANIC("?", "general protection fault in kernel");
  Atomic<bool> done{false};
  int signal = 0, code = 0;
  {
    auto handle = pageFault(cr2, trap_frame, done, signal, code);
    while (!done.get()) {
      TLB::poll();
      auto h = ready_queue.remove();
      if (h) h.resume();
      else pause();
    }
  }

  apply_saved_context(coroutine{{}, cr3, t});
  if (signal) {
    if (copying) trap_frame->rip = uintptr_t(copy_user_fault_);
    else if (user) fault_signal(signal, cr2, code);
    else KPANIC("Kernel access fault at ? (address ?)\n", trap_frame->rip, cr2);
  }
}

extern "C" void generalProtectionHandler_();
void VMM::init_system() {
  IDT::interrupt(14, uintptr_t(pageFaultHandler_));
  IDT::interrupt(13, uintptr_t(generalProtectionHandler_));
}

sync<void> VMM::reset(StrongRef<impl::AddressSpace> as) {
  // Teardown can block and resume on another core. Never leave a dying CR3
  // loaded on the scheduler's original core while its root may be freed elsewhere.
  auto *root = (PageTableEntry*)as->cr3;

  auto *list = new DeferredFreeList();

  auto proof = co_await as->vma_list_lock;
  for (VirtualMemoryArea *a = as->memory_areas; a;) {
    VirtualMemoryArea *next = a->next;
    if (!a->free)
    {
      if (a->file != nullptr && (a->flags & MAP_SHARED)) co_await write_back(VPN{a->base}, a->length, a->file, a->offset, root);
      LockGuard g(page_table_lock);
      unmap(root, 0, a->base, a->length, a->file, a->offset, list);
    }
    delete a;
    a = next;
  }
  as->memory_areas = nullptr;
  co_await release_pages(list);
  proof.unlock();

  list->flush();
  delete list;
}

async<void> VMM::destroy(StrongRef<impl::AddressSpace> as) {
  co_await reset(as);

  impl::drop_to_kernel_cr3();
  physMem.free(PA{as->cr3_phys()}.ppn());
}

namespace {
SpinLock map_device_lock{};
uint64_t map_device_used = 0;
} // namespace

VA VMM::map_device(PA pa, uint64_t bytes) {
  using namespace impl;
  ASSERT(pa.pa() % FRAME_SIZE == 0 && bytes % FRAME_SIZE == 0);

  // the next unused byte of the region
  LockGuard g{map_device_lock};

  ASSERT(bytes <= DEVICE_BYTES - map_device_used);
  const VA start{DEVICE_START.va() + map_device_used};
  map_device_used += bytes;
  for (uint64_t i = 0; i < bytes; i += FRAME_SIZE) {
    map(VPN{VA{start.va() + i}}, PPN{PA{pa.pa() + i}}, false, true, false);
  }
  return start;
}

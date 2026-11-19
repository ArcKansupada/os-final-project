#pragma once

#include <cstdint>

#include "async/async.h"
#include "filesystem/ext2.h"
#include "physmem.h"
#include "lib/shared.h"
#include "async/sync.h"

namespace impl { struct PCB; }
class Node;
class VA;

class VPN {
private:
  static constexpr uint64_t MASK = UINT64_C(0xFFFFFFFFF);
  uint64_t val;

public:
  explicit inline VPN(VA va);
  explicit constexpr inline VPN(uint64_t val) : val(val & MASK) {}
  inline constexpr uint64_t vpn() const { return val; }

  // +
  constexpr VPN operator+(const VPN n) const { return VPN{val + n.val}; }
  template <typename T> constexpr VPN operator+(const T n) const {
    return VPN{val + n};
  }

  // -
  constexpr VPN operator-(const VPN n) const { return VPN{val - n.val}; }
  template <typename T> constexpr VPN operator-(const T n) const {
    return VPN{val - n};
  }

  // ==, !=, <, <=, >, >= ordered by page number
  constexpr auto operator<=>(const VPN &) const = default;

  // same, against a raw page number/count, in either order
  constexpr auto operator<=>(const uint64_t n) const { return val <=> n; }
};

extern void do_print(const VPN &);

class VA {
private:
  uint64_t _va;
  static uint64_t check_canonical(uint64_t va);

public:
  explicit inline VA(uint64_t va) : _va(check_canonical(va)) {}
  // the start of the page, bit 47 sign-extended into a canonical address
  explicit inline VA(const VPN &vpn)
      : _va(uint64_t(int64_t(vpn.vpn() << (LOG_FRAME_SIZE + 16)) >> 16)) {}
  inline VA(const VA &rhs) : _va(rhs._va) {}
  explicit inline VA(const PA &pa)
      : _va(check_canonical(pa.pa() + Sys::hhdm_offset)) {}
  explicit inline VA(const PPN &ppn) : VA(PA(ppn)) {}
  inline uint64_t va() const { return _va; }

  template <typename T> operator T *() const { return (T *)_va; }

  inline VPN vpn() const { return VPN(*this); }

  // ==, !=, <, <=, >, >= ordered by address
  constexpr auto operator<=>(const VA &) const = default;
};

extern void do_print(const VA &);

inline VPN::VPN(VA va)
    : val((va.va() >> LOG_FRAME_SIZE) & MASK) {}

// low-mem is [0x1000, 0x8000_0000_0000)

namespace impl
{
void map(VPN vpn, PPN ppn, bool user, bool write, bool no_execute, uint64_t root = 0);

inline void invlpg(void *addr) {
  asm volatile("invlpg (%0)" : : "r"(addr) : "memory");
}
} // namespace impl
namespace VMM
{
// mmap's prot and flags, Linux's values
constexpr int PROT_NONE = 0;
constexpr int PROT_READ = 1;
constexpr int PROT_WRITE = 2;
constexpr int PROT_EXEC = 4;

constexpr int MAP_SHARED = 0x01;
constexpr int MAP_PRIVATE = 0x02;
constexpr int MAP_FIXED = 0x10;
constexpr int MAP_ANONYMOUS = 0x20;

const static VA VA_START = VA{VPN{1}};
constexpr uint64_t VA_END = uint64_t(1) << 47; // not a canonical VA

inline void *const MAP_FAILED = (void *)-1;

struct SharedAnonymous {
  struct Page { Page *next; uint64_t offset; PPN ppn; };
  struct Range {
    Range *next;
    StrongRef<SharedAnonymous> backing;
    uint64_t offset, length;
    Range(StrongRef<SharedAnonymous> backing, uint64_t offset, uint64_t length);
    ~Range();
  };
  SpinLock lock;
  Page *pages = nullptr;
  Range *ranges = nullptr;
  ~SharedAnonymous();
};

struct VirtualMemoryArea
{
  VirtualMemoryArea* next;

  bool free;

  uint64_t base;        // pages
  uint64_t length;      // pages

  int prot, flags;

  StrongRef<Node> file; // optional
  uint64_t offset;      // byte offset in file or shared anonymous backing
  StrongRef<SharedAnonymous> anonymous{};
  StrongRef<SharedAnonymous::Range> coverage{};
};

// Last VMA with base <= vpn (allocated region or free hole).
inline VirtualMemoryArea *find_vma(VirtualMemoryArea *area, uint64_t vpn) {
  while (area->next != nullptr && area->next->base <= vpn)
    area = area->next;
  return area;
}
} // namespace VMM

namespace impl {
/*
 * An address space
 *
 * Low-mem (user memory, [VA_START, VA_END)) is its own: its own page tables
 * and VMEs. High-mem is shared with the kernel: the upper half of its PML4 is
 * a copy of the kernel's, and that half of the kernel's PML4 doesn't change
 * once the first one is created (see create).
 *
 * The kernel itself always runs in kernel(), whose low-mem is the kernel's
 * own (e.g. kernel_main's mmaps). A process's address space is only loaded
 * while the process runs in user mode: every way into the kernel switches
 * back to kernel(). So the kernel never dereferences a process's addresses,
 * it uses copy_from_user and fill_user.
 */
class AddressSpace {
public:
  WeakRef<AddressSpace> self;
  AddressSpace *next = nullptr; // weak registry for file truncation
  static StrongRef<AddressSpace> create();
  ~AddressSpace();
  uint64_t const cr3;
  static VMM::VirtualMemoryArea *initial_areas() {
    return new VMM::VirtualMemoryArea{
      nullptr, true, VMM::VA_START.vpn().vpn(),
      (VMM::VA_END >> LOG_FRAME_SIZE) - VMM::VA_START.vpn().vpn(),
      VMM::PROT_NONE, 0, StrongRef<Node>{}, 0
    };
  }
  VMM::VirtualMemoryArea *memory_areas = initial_areas();
  BlockingLock vma_list_lock{};

  static constexpr uint64_t HALF_FRAME = FRAME_SIZE/2;
  explicit AddressSpace(uint64_t cr3);

  uint64_t cr3_phys() const { return cr3 - Sys::hhdm_offset; }

  AddressSpace(const AddressSpace &) = delete;
  AddressSpace &operator=(const AddressSpace &) = delete;
};
} // namespace impl


namespace VMM {
#pragma pack(push, 1)
struct PageTableEntry
{
  bool present : 1;
  bool write : 1;
  bool user : 1;
  uint16_t unused_1 : 3;
  bool dirty : 1;
  uint16_t unused_3 : 5;
  uint64_t ppn : 36;
  uint16_t unused_2 : 15;
  bool no_execute : 1;
};
#pragma pack(pop)
static_assert(sizeof(PageTableEntry) == 8, "PageTableEntry size must be exactly 64 bits");

inline uint16_t* present_ptes;
inline uint16_t* frame_refs;

extern void init_system();

extern sync<void *> mmap(void *addr, std::size_t length,
                         int prot, int flags, StrongRef<Node> file,
                         uint64_t offset);

/* read the munmap spec for details on corner cases */
extern sync<int> munmap(void *addr, std::size_t length);
extern sync<void> truncate_file(StrongRef<Node> file, uint32_t length);
// Returns the number of bytes not copied on a user-memory fault (zero on success).
extern "C" std::size_t copy_user_(void *dst, const void *src, std::size_t length);
extern "C" std::size_t store_user32_(uint32_t *dst, uint32_t value);

// Current address space; hold vma_list_lock to keep the mapping stable.
// Returns zero if the page is not resident.
extern uint64_t physical_address(uintptr_t addr);

// fully destroys it all
extern async<void> destroy(StrongRef<impl::AddressSpace> as);

// unmaps everything
extern sync<void> reset(StrongRef<impl::AddressSpace> as);

extern sync<void> copy_address_space(StrongRef<impl::AddressSpace> src, StrongRef<impl::AddressSpace> dst);

// The kernel heap's region, high-mem under one PML4 entry.
//   Q: Why must every address space see the same heap?
constexpr uint64_t HEAP_START_VAL = UINT64_C(0xFFFFC00000000000);
constexpr uint64_t HEAP_BYTES = UINT64_C(4) << 30;
const VA HEAP_START{HEAP_START_VAL};

// Device memory (PCI BARs), right after the heap.
//   Q: Why can't the kernel reach a BAR through the HHDM?
//   Q: Why does it matter that it doesn't share a PDPT entry with the heap?
constexpr uint64_t DEVICE_START_VAL = HEAP_START_VAL + HEAP_BYTES;
constexpr uint64_t DEVICE_BYTES = UINT64_C(4) << 30;
const VA DEVICE_START{DEVICE_START_VAL};
static_assert(HEAP_BYTES % (UINT64_C(1) << 30) == 0);
static_assert(((DEVICE_START_VAL + DEVICE_BYTES - 1) >> 39) ==
              (HEAP_START_VAL >> 39));

// Maps device memory at pa and returns where.
//   Q: Does anything ever unmap it?
extern VA map_device(PA pa, uint64_t bytes);

} // namespace VMM

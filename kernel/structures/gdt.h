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

#pragma once

#include <cstdint>

/*
 * Per-core GDT and TSS
 *
 * Each core has its own (in its CCB), so each core's TSS can sit at the same
 * selector, and ltr marking it busy doesn't affect the other cores.
 *
 * Entries 0..6 are copied from Limine's GDT, so the kernel segments keep
 * their selectors and values:
 * https://codeberg.org/Limine/limine-protocol/src/branch/trunk/PROTOCOL.md
 *
 *     0x00 null
 *     0x08 16-bit code    0x10 16-bit data
 *     0x18 32-bit code    0x20 32-bit data
 *     0x28 64-bit code    0x30 64-bit data
 *
 * followed by
 *
 *     0x38 user data      0x40 user code      0x48 TSS (two entries)
 *
 * syscall/sysret (see IA32_STAR) need kernel data right after kernel code,
 * and user data right before user code.
 */
class GDT {
public:
  static constexpr uint16_t KERNEL_CS = 0x28;
  static constexpr uint16_t KERNEL_DS = 0x30;
  static constexpr uint16_t USER_DS = 0x38 | 3;
  static constexpr uint16_t USER_CS = 0x40 | 3;
  static constexpr uint16_t TSS_SEL = 0x48;

  // 64-bit TSS
  struct [[gnu::packed]] TSS {
    uint32_t reserved0 = 0;
    uint64_t rsp[3] = {}; // stacks for entering rings 0..2
    uint64_t reserved1 = 0;
    uint64_t ist[7] = {}; // interrupt stack table
    uint64_t reserved2 = 0;
    uint16_t reserved3 = 0;
    uint16_t iopb = sizeof(TSS); // no I/O permission bitmap
  };
  static_assert(sizeof(TSS) == 104);

  static constexpr uint64_t N_LIMINE = 7; // entries copied from Limine

  // Only load() uses it. Public like everything else: the CCB contains a GDT
  // and has to stay standard-layout for offsetof (see syscallEntry_).
  uint64_t entries[N_LIMINE + 4];

  // rsp[0] is the stack the CPU switches to on an interrupt or exception in
  // user mode. Has to be set before entering user mode.
  TSS tss{};

  // Copies the kernel entries from the current (Limine's) GDT
  GDT();
  GDT(const GDT &) = delete;
  GDT &operator=(const GDT &) = delete;

  // Loads this GDT and the TSS on the current core. The segment registers
  // keep their (Limine) selectors, they mean the same thing here.
  void load();
};

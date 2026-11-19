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

#include "gdt.h"
#include "tlb.h"
#include <cstdint>
namespace impl
{
struct TCB;

/*
 * Core control block: per-core data. GS points to the current core's CCB.
 *
 */
struct CCB {
  CCB *self;        // has to be first, current() reads GS:0
  uint32_t id;      // 0 .. Sys::core_count - 1
  uint32_t apic_id; // the local APIC's ID

  // used by the syscallHandler_ (assembly). Be careful.

  // The top of this core's stack (Limine's). Entering user mode abandons the
  // stack, coming back (TSS RSP0, system calls) starts over at the top.
  uint64_t stack_top;
  // The thread this core is running in user mode, nullptr if none
  TCB *current_thread = nullptr;
  // scratch for the system call entry, the user's RSP
  uint64_t user_rsp = 0;

  // FS base for kernel code on this core (its thread_local variables)
  uint64_t kernel_fs;
  // The kernel's address space (AddressSpace::kernel().cr3), every way into
  // the kernel from user mode switches to it
  uint64_t kernel_cr3;

  TLB::Mailbox tlb_mailbox; // shootdown requests for this core

  GDT gdt; // this core's GDT and TSS

  CCB(uint32_t id, uint32_t apic_id, uint64_t stack_top);
  CCB() = delete;
  CCB(const CCB &) = delete;
  CCB &operator=(const CCB &) = delete;

  // The current core's CCB. Reads GS every time, fine in coroutines, but
  // don't keep the result across a co_await (it can resume on another core).
  static CCB *current() {
    CCB *ccb;
    asm volatile("mov %%gs:0, %0" : "=r"(ccb));
    return ccb;
  }

  // Called once on each core, before it touches its CCB and after
  // TLS::init_core. Also loads the core's GDT and points the TSS at
  // stack_top.
  static void init_core(uint32_t id, uint32_t apic_id, uint64_t stack_top);

};
} // namespace impl
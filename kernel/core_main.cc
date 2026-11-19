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

#include "core_main.h"
#include "structures/ccb.h"
#include "structures/idt.h"
#include "kernel_main.h"
#include "machine/limine.h"
#include "machine/machine.h"
#include "lib/print.h"
#include "syscall.h"
#include "structures/tlb.h"
#include "machine/x86_64.h"
#include "async/spin_barrier.h"

SpinBarrier *impl::start_barrier;

[[noreturn]] void impl::core_main(limine_mp_info *info) {

  // The part of Limine's stack below core_main's frame, everything after
  // this point can be thrown away (core_main never returns).
  uint64_t stack_top;
  asm volatile("mov %%rsp, %0" : "=r"(stack_top));
  stack_top &= ~UINT64_C(15);

  // KPRINT("bootstrapping core ?\n", info->lapic_id);

  // Check supported CPU features and enable required ones
  Features features{};
  ASSERT(features.hasSSE3);
  enable_sse();
  ASSERT(features.hasFSGSBASE);
  CR4().setFSGSBASE();

  // system_main passes the core's index in extra_argument
  CCB::init_core(uint32_t(info->extra_argument), info->lapic_id, stack_top);

  auto old_efer = Msr::EFER().read();
  KPRINT("old efer ?\n", old_efer);
  Msr::EFER().write(old_efer | 1);
  auto new_efer = Msr::EFER().read();
  KPRINT("new efer ?\n", new_efer);

  Syscall::init_core();

  IDT::init_core();
  TLB::init_core();

  // The APIC and IDT are ready. Shootdown IPIs sent before this stay pending
  // until now.
  sti();

  Sys::normal = true;

  KPRINT("bootstrapped core ? (lapic_id ?)\n", Dec(CCB::current()->id),
         Dec(info->lapic_id));

  impl::start_barrier->sync();

  if (info->lapic_id == 0) {
    /* run the async kernel_main */
    kernel_main();
  }

  impl::event_loop();
}

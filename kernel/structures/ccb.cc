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

#include "ccb.h"

#include "lib/debug.h"
#include "machine/machine.h"
#include "machine/x86_64.h"
#include <cstddef>
#include <cstdint>

using namespace impl;

static_assert(offsetof(CCB, self) == 0);
static_assert(offsetof(CCB, stack_top) == 16);
static_assert(offsetof(CCB, current_thread) == 24);
static_assert(offsetof(CCB, user_rsp) == 32);

CCB::CCB(uint32_t id, uint32_t apic_id, uint64_t stack_top) : self(this), id(id),
    apic_id(apic_id), stack_top(stack_top), kernel_cr3(get_cr3()) {}


void CCB::init_core(uint32_t id, uint32_t apic_id, uint64_t stack_top) {
  CCB *ccb = new CCB(id, apic_id, stack_top);
  ccb->gdt.tss.rsp[0] = stack_top;
  ccb->gdt.load();
  wrgsbase(uintptr_t(ccb));
  // While in kernel, KernelGSBase holds the user GS (0 until first resume).
  Msr::KernelGSBase().write(0);
}



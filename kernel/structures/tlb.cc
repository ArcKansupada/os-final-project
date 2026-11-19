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

#include "tlb.h"

#include "ccb.h"
#include "lib/handle_queue.h"
#include "idt.h"
#include "machine/machine.h"
#include "machine/x86_64.h"

namespace TLB {

using namespace impl;
namespace {

constexpr uint64_t MAX_CORES = 64;

// Ranges bigger than this are cheaper to flush by reloading CR3
constexpr uint64_t INVLPG_LIMIT = 32;

// Every core's mailbox, so other cores can post to it
Mailbox *mailboxes[MAX_CORES];

void flush(VPN start, VPN end) {
  if ((end - start).vpn() > INVLPG_LIMIT) {
    // low-mem mappings aren't global
    set_cr3(get_cr3());
    return;
  }
  for (VPN vpn = start; vpn < end; vpn = vpn + 1) {
    // VA{vpn} sign-extends, high-mem pages (e.g. QuickMem stacks) work too
    asm volatile("invlpg (%0)" : : "r"(VA{vpn}.va()) : "memory");
  }
}

} // namespace

// Services this core's pending requests
void poll() {
  Node *node = CCB::current()->tlb_mailbox.take_all();
  while (node != nullptr) {
    Node *next = node->next;
    node->request->ack();
    delete node;
    node = next;
  }
}

void init_system() { IDT::interrupt(VECTOR, uintptr_t(tlbHandler_)); }

// All cores register before the start barrier, nothing posts before that
void init_core() {
  ASSERT(Sys::core_count <= MAX_CORES);
  const uint32_t id = CCB::current()->id;
  ASSERT(id < Sys::core_count);
  mailboxes[id] = &CCB::current()->tlb_mailbox;
}

bool Shootdown::await_ready() {
  flush(start_, end_);
  return Sys::core_count == 1;
}

bool Shootdown::await_suspend(std::coroutine_handle<void> h) {
  waiter_.add(h);
  // The extra count keeps a fast core from resuming us before we're done
  // posting.
  pending_.set(Sys::core_count);
  Mailbox *const mine = &CCB::current()->tlb_mailbox;
  for (uint64_t i = 0; i < Sys::core_count; i++) {
    if (mailboxes[i] != mine) {
      mailboxes[i]->push(new Node{this, nullptr});
    }
  }
  // x2APIC MSR writes aren't serializing, make sure the other cores can see
  // the requests before the IPI gets there
  asm volatile("mfence; lfence" ::: "memory");
  // fixed delivery, assert, all excluding self
  Msr::InterruptCommandRegister().write((3 << 18) | (1 << 14) | VECTOR);
  // false: everyone already flushed, keep going without suspending
  return pending_.sub_fetch(1) != 0;
}

void Shootdown::ack() {
  flush(start_, end_);
  if (pending_.sub_fetch(1) == 0) {
    impl::ready_queue.transfer(waiter_);
  }
}

} // namespace TLB

// called by tlbHandler_ (machine.S)
extern "C" void tlbHandler() {
  TLB::poll();
  Msr::EOI().write(0);
}

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

#pragma once

#include "async/atomic.h"
#include "memory/vmm.h"
#include <coroutine>
#include <cstdint>

/*
 * TLB shootdown
 *
 * The initiator posts the request to every other core's mailbox, then sends
 * them an IPI. The handler empties the mailbox and flushes. A request is done
 * once every core has flushed.
 *
 * The handler runs whenever interrupts are enabled, so a core that isn't in
 * the event loop (or is stuck in a long computation) still services
 * requests. SpinLock disables interrupts, the handler can take spin locks.
 */

namespace TLB {

class Shootdown;

// A pending request in a core's mailbox
struct Node {
  Shootdown *request;
  Node *next;
};

// Each core's shootdown requests (CCB::tlb_mailbox). A lock-free stack, any
// core pushes and only the owner takes everything. Taking all at once avoids
// ABA.
struct Mailbox {
  Node *head = nullptr;

  void push(Node *node) {
    Node *old = __atomic_load_n(&head, __ATOMIC_RELAXED);
    do {
      node->next = old;
    } while (!__atomic_compare_exchange_n(&head, &old, node, true,
                                          __ATOMIC_RELEASE, __ATOMIC_RELAXED));
  }

  Node *take_all() {
    if (__atomic_load_n(&head, __ATOMIC_RELAXED) == nullptr) {
      return nullptr;
    }
    return __atomic_exchange_n(&head, nullptr, __ATOMIC_ACQUIRE);
  }
};

// The vector of the shootdown IPI
constexpr uint8_t VECTOR = 51;

// Called once, after IDT::init_system
extern void init_system();

// Called on each core, after TLS::init_core, CCB::init_core and
// IDT::init_core, and before it runs any coroutines
extern void init_core();

// Drain this core's shootdown mailbox (safe to call with IF=0)
extern void poll();

// Flushes [start, end) from the TLBs of all cores, this one included.
//
//     co_await TLB::Shootdown{start, end};
//
// Clear the PTEs first, and keep the frames they pointed to until this
// completes; other cores can use stale translations up to that point.
class Shootdown {
  const VPN start_;
  const VPN end_;
  // cores that haven't flushed yet, plus one for the initiator until it
  // suspends
  Atomic<uint64_t> pending_;
  HandleQueue<NoLock> waiter_; // should only ever be populated with one handle

public:
  Shootdown(VPN start, VPN end) : start_(start), end_(end), pending_(0) {}
  Shootdown(const Shootdown &) = delete;
  Shootdown &operator=(const Shootdown &) = delete;

  bool await_ready();
  bool await_suspend(std::coroutine_handle<void> h);
  void await_resume() {}

  // Flushes the local TLB and drops one pending core. The last one resumes
  // the waiter. Can't touch *this after it returns.
  void ack();
};

} // namespace TLB

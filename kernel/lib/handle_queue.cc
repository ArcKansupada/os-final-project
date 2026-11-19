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

#include "lib/handle_queue.h"

#include "structures/ccb.h"
#include "debug.h"
#include "machine/machine.h"
#include "structures/pcb.h"
#include "print.h"
#include "structures/tlb.h"

extern "C" {
  void *__dso_handle = nullptr;
  int __cxa_atexit(void (*)(void *), void *, void *) { return 0; }
}

namespace impl {
  coroutine make_coroutine(std::coroutine_handle<void> h) {
    return {h, get_cr3(), CCB::current()->current_thread};
  }

  void apply_saved_context(coroutine cr) {
    // A blocked syscall can finish after exit_group/exec destroyed its mm.
    if (cr.current_thread && cr.current_thread->zombie) cr.cr3 = CCB::current()->kernel_cr3;
    if (cr.cr3 != get_cr3()) set_cr3(cr.cr3);
    CCB::current()->current_thread = cr.current_thread;
  }

  HandleQueue<SpinLock> ready_queue{};
  ThreadQueue thread_queue{};

  static Atomic active_loops{0};
  static Atomic started{0};
  static Atomic finished{0};
  static Atomic shutdown_gate{0};
  static Atomic system_done{0};

  void mark_system_done() { system_done.set(1); }

  void ThreadQueue::add(StrongRef<TCB> t) {
    ASSERT(t != nullptr);
    ASSERT(t->next_ready == nullptr);
    ASSERT(t->queue_pin == nullptr);
    LockGuard g{lock};
    TCB *raw = t.operator->();
    raw->queue_pin = t;
    raw->ready_at = rdtsc();
    int q = raw->preempted;
    raw->preempted = false;
    if (tail[q] == nullptr) head[q] = raw;
    else tail[q]->next_ready = raw;
    tail[q] = raw;
    count += 1;
  }

  StrongRef<TCB> ThreadQueue::remove() {
    LockGuard g{lock};
    // the oldest ready thread wins, with preempted ones penalized by a 20 ms head start
    int q;
    if (head[0] && (!head[1] || head[0]->ready_at <= head[1]->ready_at + Sys::tsc_hz / 50)) q = 0;
    else if (head[1]) q = 1;
    else return {};

    TCB *raw = head[q];
    head[q] = raw->next_ready;
    if (head[q] == nullptr) tail[q] = nullptr;
    raw->next_ready = nullptr;
    count -= 1;
    StrongRef<TCB> t{raw->self};
    raw->queue_pin = nullptr;
    return t;
  }

  static bool work_pending() {
    return ready_queue.count != 0 || thread_queue.count != 0;
  }

  static bool quiescent() {
    if (system_done.get() == 0) return false;
    int s = started.get();
    int f = finished.get();
    int a = active_loops.get();
    uint32_t rq = ready_queue.count;
    uint32_t tq = thread_queue.count;
    if (s != started.get() || f != finished.get() || a != active_loops.get()) return false;
    if (rq != ready_queue.count || tq != thread_queue.count) return false;
    return a == 0 && rq == 0 && tq == 0 && s == f;
  }

  static bool run_one_coroutine(bool &active) {
    auto handle = ready_queue.remove();
    if (!handle) return false;
    if (!active) {
      active = true;
      active_loops.add_fetch(1);
    }
    started.fetch_add(1);
    handle.resume();
    finished.fetch_add(1);
    return true;
  }

  static bool run_one_thread(bool &active) {
    for (StrongRef<TCB> t = thread_queue.remove(); t != nullptr; t = thread_queue.remove()) {
      if (t->zombie) continue;
      if (!active) {
        active = true;
        active_loops.add_fetch(1);
      }
      TCB *raw = t.operator->();
      t = nullptr;
      raw->resume();
    }

    return false;
  }

  extern "C" [[noreturn]] void schedule_() {
    bool active = true;
    while (true) {
      poll_sleepers();
      if (run_one_coroutine(active)) continue;
      if (run_one_thread(active)) continue;

      if (active) {
        active = false;
        int left = active_loops.sub_fetch(1);
        ASSERT(left >= 0);
        if (left == 0) {
          if (run_one_coroutine(active)) continue;
          if (run_one_thread(active)) continue;
          if (!quiescent()) continue;
          break;
        }
      }
      drop_to_kernel_cr3();
      TLB::poll();
      pause();
    }

    if (shutdown_gate.exchange(1) != 0) {
      while (true) pause();
    }

    while (true) {
      while (!quiescent()) pause();
      if (quiescent()) break;
    }

    KPRINT("CORE EXIT (active: ?, queue: ?)\n", Dec(active_loops.get()),
           Dec(ready_queue.count));
    KPRINT("PROCESSOR SHUTDOWN\n");
    KPRINT("event_loop: all done started=? finished=? in_queue=?, shutting down\n",
           Dec(started.get()), Dec(finished.get()), Dec(ready_queue.count));
    shutdown(true);

    while (true) asm volatile("hlt");
  }

  [[noreturn]]
  void event_loop() {
    while (!work_pending()) {
      TLB::poll();
      pause();
    }

    KPRINT("CORE START (active: ?, queue: ?)\n", Dec(active_loops.add_fetch(1)),
           Dec(ready_queue.count));
    schedule_();
  }
} // namespace impl

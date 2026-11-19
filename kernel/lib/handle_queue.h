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

#include "debug.h"
#include "shared.h"
#include "async/spin_lock.h"
#include "machine/machine.h"
#include <coroutine>
#include <cstdint>

namespace impl {
struct TCB;

struct coroutine {
  std::coroutine_handle<void> handle = nullptr;
  uint64_t cr3 = 0;
  TCB *current_thread = nullptr;

  bool operator==(std::nullptr_t) const { return handle == nullptr; }
  bool operator!=(std::nullptr_t) const { return handle != nullptr; }
};

coroutine make_coroutine(std::coroutine_handle<void> h);
void apply_saved_context(coroutine);

class Segment {
public:
  static constexpr uint32_t LIMIT = 128;
  coroutine data[LIMIT];
  uint32_t head = 0;
  uint32_t count = 0;
  Segment *next = nullptr;

  [[gnu::noinline]]
  bool is_full() {
    return count == LIMIT;
  }

  [[gnu::noinline]]
  void add(const coroutine cr) {
    ASSERT(count < LIMIT);
    data[(head + count) % LIMIT] = cr;
    count += 1;
  }

  [[gnu::noinline]]
  inline coroutine remove() {
    if (count == 0) {
      return {};
    }
    uint32_t old_head = head;
    // data[head] = nullptr;
    head = (head + 1) % LIMIT;
    count -= 1;
    return data[old_head];
  }
};
} // namespace impl

template <typename LockType> class HandleQueue {
template <typename> friend class HandleQueue;
public:
  impl::Segment *head = nullptr;
  impl::Segment *tail = nullptr;
  uint32_t count = 0;
  LockType lock{};

  [[gnu::noinline]]
  void check() {
    if (head == nullptr) {
      ASSERT(tail == nullptr);
    }
    if (tail == nullptr) {
      ASSERT(head == nullptr);
    }
  }

public:
  HandleQueue() { check(); }

  ~HandleQueue()
  {
    while (head != nullptr)
    {
      auto h = head;
      head = head->next;
      delete h;
    }
  }

  // Racy by design: a best-effort hint for fast-path optimizations, not a
  // linearizable check.
  bool is_empty_hint() { return count == 0; }

public:
  [[gnu::noinline]]
  void add(impl::coroutine cr) {
    lock.lock();
    check();

    // fastpath
    if (tail != nullptr && !tail->is_full()) {
      tail->add(cr);
      count += 1;
      check();
      lock.unlock();
      return;
    }

    // slowpath
    lock.unlock();
    auto seg = new impl::Segment();
    lock.lock();
    check();

    if (tail == nullptr)
    {
      ASSERT(head == nullptr);
      head = tail = seg;
      seg = nullptr;
    } else if (tail->is_full())
    {
      tail = tail->next = seg;
      seg = nullptr;
    }

    tail->add(cr);
    count += 1;
    check();

    lock.unlock();
    delete seg;
  }
public:
  [[gnu::noinline]]
  void add(std::coroutine_handle<void> h) { add(impl::make_coroutine(h)); }

  [[gnu::noinline]]
  void add(HandleQueue<NoLock> &from) {
    from.check();
    if (from.head == nullptr) {
      return;
    }
    lock.lock();
    check();
    if (head == nullptr) {
      head = from.head;
      tail = from.tail;
      check();
    } else {
      ASSERT(tail != nullptr);
      tail->next = from.head;
      tail = from.tail;
      check();
    }
    count += from.count;
    check();
    lock.unlock();
    from.head = nullptr;
    from.tail = nullptr;
    from.count = 0;
    from.check();
  }

private:
  // INTERNAL USE ONLY: DOESN'T LOCK
  [[gnu::noinline]]
  impl::coroutine pop() {
    check();
    auto h = head;
    if (h == nullptr) {
      check();
      return {};
    }

    auto out = head->remove();
    ASSERT(out != nullptr);

    count -= 1;
    check();

    if (head->count == 0)
    {
      head = h->next;
      if (head == nullptr) {
        tail = nullptr;
      }
      check();
      delete h;
      return out;
    }

    return out;
  }
public:
  template <typename OtherLock>
  [[gnu::noinline]]
  void transfer(HandleQueue<OtherLock>& queue) {
    queue.lock.lock();
    auto cr = queue.pop();
    ASSERT(cr != nullptr);
    add(cr);
    queue.lock.unlock();
  }

  [[gnu::noinline]]
  std::coroutine_handle<void> remove() {
    lock.lock();

    auto cr = pop();
    if (cr == nullptr) {
      lock.unlock();
      return nullptr;
    }

    auto out = cr.handle;
    impl::apply_saved_context(cr);

    lock.unlock();
    return out;
  }

  [[gnu::noinline]]
  HandleQueue<NoLock> remove_all() {
    lock.lock();
    check();
    HandleQueue<NoLock> out;
    out.head = head;
    out.tail = tail;
    out.count = count;
    head = nullptr;
    tail = nullptr;
    count = 0;
    check();
    lock.unlock();
    return out;
  }
};

namespace impl {
struct TCB;

struct ThreadQueue {
  TCB *head[2] = {nullptr, nullptr};
  TCB *tail[2] = {nullptr, nullptr};
  uint32_t count = 0;
  SpinLock lock{};

  bool is_empty_hint() { return count == 0; }
  void add(StrongRef<TCB> t);
  StrongRef<TCB> remove();
};

extern HandleQueue<SpinLock> ready_queue;
extern ThreadQueue thread_queue;

[[noreturn]] void event_loop();
void mark_system_done();
// re-enter with fresh stack via bounce_to_yield_ after a blocking syscall
extern "C" [[noreturn]] void schedule_();

} // namespace impl

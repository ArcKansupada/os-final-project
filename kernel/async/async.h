/* Copyright (C) 2025-2026 Ahmed Gheith and contributors.
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

#include "future.h"
#include "lib/handle_queue.h"

#include "lib/shared.h"

namespace impl {

// An awaiter that suspends the caller and adds it to the ready queue.
struct YieldAwaiter {
  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle) const noexcept { impl::ready_queue.add(handle); }
  void await_resume() const noexcept {}
};
} // namespace impl

// A coroutine can suspend itself temporarily by doing:
//      - co_await yield();
//
// The exact return type is up to you but it must be awaitable.
[[nodiscard]]
inline impl::YieldAwaiter yield() noexcept {
  return {};
}

// An async function:
//    - adds itself to the ready queue.
//    - the caller resumes immediately.
//    - produces a value of type T when it does co_return.
//    - the caller can co_await the async to block until the value is ready.
//    - can be freely copied and moved around.
template <typename T = void> class async {
public:
  StrongRef<Future<T>> future;

  struct promise_type
  {
    StrongRef<Future<T>> future = StrongRef<Future<T>>(new Future<T>());

    impl::YieldAwaiter initial_suspend() const noexcept { return {}; }
    async<T> get_return_object() noexcept { return async{.future=future}; }
    void return_value(T v) noexcept { future->set(v); }
    void unhandled_exception() const noexcept;

    std::suspend_never final_suspend() noexcept { return {}; }
  };

  // The awaitable interface
  bool await_ready() const noexcept { return future->await_ready(); }
  bool await_suspend(std::coroutine_handle<> handle) const noexcept { return future->await_suspend(handle); }
  T await_resume() const noexcept { return future->await_resume(); }
};

// specialization for async<void>
// doesn't return a value
// the caller can't co_await on async<void>
// this is what we had in p0
template <> class async<void> {
public:
  struct promise_type {
    impl::YieldAwaiter initial_suspend() const noexcept { return {}; }
    async<void> get_return_object() noexcept { return {}; }
    void return_void() const noexcept {}
    void unhandled_exception() const noexcept;
    std::suspend_never final_suspend() const noexcept { return {}; }
  };

  impl::YieldAwaiter operator co_await() const noexcept { return {}; }
};

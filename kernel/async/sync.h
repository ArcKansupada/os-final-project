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

#include <coroutine>

#include "lib/print.h"
#include "spin_lock.h"

// A sync<T> is a coroutine that produces a value of type T and allows the
// caller to wait for the value to be produced.
//
// How does it differ from async<T>?
//  - never runs in parallel with its caller when used as recommended.
//  - runs in the caller's context as much as possible.
//  - reduces the number of heap operations.
//  - eliminates context switching overhead for coroutines that don't block.
//
// Recomended use:
//     sync<T> f(...) { ... }
//
//     auto v = co_await f(...);
//
// Used to create light-weight functions that can be block without the overhead
// of creating a new thread. Optimizes for the case where the function doesn't
// block.
template <typename T> struct [[nodiscard]] sync {

  struct promise_type {
    SpinLock spin;
    bool done{false}; // Either caller has dropped its sync object or the
                      // coroutine has finished and produced a value. Why is it
                      // sufficient to have a single boolean? because if one is
                      // looking, then it's not done.

    std::coroutine_handle<> wakeup{
        nullptr}; // who is waiting for the value to be produced, if any
                  // A single value is sufficient because the sync object is not
                  // copyable and the only way to to wait is through the sync
                  // object.

    T value{}; // the value produced by the coroutine
               // only valid if done is true and is being checked by the caller
               // after it has been woken up.

    sync<T> get_return_object() { return sync<T>{this}; }

    // a sync coroutine runs in its caller's context as much as possible
    std::suspend_never initial_suspend() const noexcept { return {}; }

    struct FinalAwaiter {
      promise_type *promise;

      explicit FinalAwaiter(promise_type *p) : promise(p) {}

      bool await_ready() const noexcept {
        promise->spin.lock();
        // If the caller has dropped its sync object, then we can just return
        // true and let the promise be destroyed
        if (promise->done) {
          promise->spin.unlock();
          return true;
        }

        // Current state:
        //    - The caller is still holding on to its sync object (done ==
        //    false)
        //    - The coroutine produced its value and will never run again
        // We need to keep the promise around to give the caller a chance to get
        // the value. Can't update the state here because await_ready is called
        // from the coroutine's context. Just return false and let await_suspend
        // handle the state transition.
        return false;
      }
      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
        // State:
        //   - still holding the lock
        //   - done == false
        //   - value is ready
        ASSERT(!promise->done);

        promise->done = true;

        auto w = promise->wakeup;
        if (w) {
          // The caller is waiting for the value
          promise->wakeup = nullptr;
          promise->spin.unlock();
          return w;
        } else {
          promise->spin.unlock();
          return std::noop_coroutine();
        }
      }
      void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return FinalAwaiter{this}; }

    void unhandled_exception() const noexcept {
      KPANIC("?", "Unhandled exception in sync<T>");
    }

    void return_value(const T &v) noexcept { value = v; }
  };

  // NOTE: must not be `T* const` -- the move constructor below has to be able
  // to null out a moved-from sync's pointer.
  promise_type *promise;
  sync(promise_type *p) : promise(p) { ASSERT(p != nullptr); }

  ~sync() {
    // A moved-from sync must remain safely destructible.
    if (promise == nullptr)
      return;

    promise->spin.lock();
    if (promise->done == 0) {
      // The coroutine has not finished yet, but the caller is dropping its sync
      // object. We need to let the coroutine know that it can clean up the
      // promise when it finishes.
      promise->done = 1;
      promise->spin.unlock();
    } else {
      ASSERT(promise->done == 1);
      ASSERT(promise->wakeup == nullptr);
      // The coroutine has finished and the caller is dropping its sync object.
      // We can clean up the promise now.
      promise->done = 2;
      promise->spin.unlock();
      std::coroutine_handle<promise_type>::from_promise(*promise).destroy();
    }
  }

  // can't copy
  sync(const sync &) = delete;
  sync &operator=(const sync &) = delete;

  // can move
  sync(sync &&other) noexcept : promise(other.promise) {
    other.promise = nullptr;
  }

  bool await_ready() noexcept {
    ASSERT(promise);
    LockGuard g{promise->spin};
    return promise->done == 1;
  }

  bool await_suspend(std::coroutine_handle<> h) const noexcept {
    ASSERT(promise);
    LockGuard g{promise->spin};
    if (promise->done == 0) {
      // The coroutine has not finished yet, so we need to wait for it to
      // finish.
      ASSERT(promise->wakeup == nullptr);
      promise->wakeup = h;
      return true;
    } else {
      return false;
    }
  }

  T await_resume() const noexcept {
    ASSERT(promise);
    return promise->value;
  }
};

// A sync<void> is a coroutine that produces no value and allows the caller to
// wait for the coroutine to finish. Specialized form if sync<T> for T=void that
// avoids the need

template <> struct [[nodiscard]] sync<void> {

  struct promise_type {
    SpinLock spin;
    bool done{false}; // Either caller has dropped its sync object or the
                      // coroutine has finished and produced a value. Why is it
                      // sufficient to have a single boolean? because if one is
                      // looking, then it's not done.

    std::coroutine_handle<> wakeup{
        nullptr}; // who is waiting for the value to be produced, if any
                  // A single value is sufficient because the sync object is not
                  // copyable and the only way to to wait is through the sync
                  // object.

    sync<void> get_return_object() { return sync<void>{this}; }

    // a sync coroutine runs in its caller's context as much as possible
    std::suspend_never initial_suspend() const noexcept { return {}; }

    struct FinalAwaiter {
      promise_type *promise;

      explicit FinalAwaiter(promise_type *p) : promise(p) {}

      bool await_ready() const noexcept {
        promise->spin.lock();
        // If the caller has dropped its sync object, then we can just return
        // true and let the promise be destroyed
        if (promise->done) {
          promise->spin.unlock();
          return true;
        }

        // Current state:
        //    - The caller is still holding on to its sync object (done ==
        //    false)
        //    - The coroutine produced its value and will never run again
        // We need to keep the promise around to give the caller a chance to get
        // the value. Can't update the state here because await_ready is called
        // from the coroutine's context. Just return false and let await_suspend
        // handle the state transition.
        return false;
      }
      std::coroutine_handle<>
      await_suspend(std::coroutine_handle<promise_type> h) const noexcept {
        // State:
        //   - still holding the lock
        //   - done == false
        //   - value is ready
        ASSERT(!promise->done);

        promise->done = true;

        auto w = promise->wakeup;
        if (w) {
          // The caller is waiting for the value
          promise->wakeup = nullptr;
          promise->spin.unlock();
          return w;
        } else {
          promise->spin.unlock();
          return std::noop_coroutine();
        }
      }
      void await_resume() const noexcept {}
    };

    FinalAwaiter final_suspend() noexcept { return FinalAwaiter{this}; }

    void unhandled_exception() const noexcept {
      KPANIC("?", "Unhandled exception in sync<T>");
    }

    void return_void() noexcept {}
  };

  // NOTE: must not be `T* const` -- the move constructor below has to be able
  // to null out a moved-from sync's pointer.
  promise_type *promise;
  sync(promise_type *p) : promise(p) { ASSERT(p != nullptr); }

  ~sync() {
    // A moved-from sync must remain safely destructible.
    if (promise == nullptr)
      return;

    promise->spin.lock();
    if (promise->done == 0) {
      // The coroutine has not finished yet, but the caller is dropping its sync
      // object. We need to let the coroutine know that it can clean up the
      // promise when it finishes.
      promise->done = 1;
      promise->spin.unlock();
    } else {
      ASSERT(promise->done == 1);
      ASSERT(promise->wakeup == nullptr);
      // The coroutine has finished and the caller is dropping its sync object.
      // We can clean up the promise now.
      promise->done = 2;
      promise->spin.unlock();
      std::coroutine_handle<promise_type>::from_promise(*promise).destroy();
    }
  }

  // can't copy
  sync(const sync &) = delete;
  sync &operator=(const sync &) = delete;

  // can move
  sync(sync &&other) noexcept : promise(other.promise) {
    other.promise = nullptr;
  }

  bool await_ready() noexcept {
    ASSERT(promise);
    LockGuard g{promise->spin};
    return promise->done == 1;
  }

  bool await_suspend(std::coroutine_handle<> h) const noexcept {
    ASSERT(promise);
    LockGuard g{promise->spin};
    if (promise->done == 0) {
      // The coroutine has not finished yet, so we need to wait for it to
      // finish.
      ASSERT(promise->wakeup == nullptr);
      promise->wakeup = h;
      return true;
    } else {
      return false;
    }
  }

  void await_resume() const noexcept {}
};

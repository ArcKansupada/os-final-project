#pragma once

#include <utility>

#include "lib/handle_queue.h"
#include "lib/print.h"
#include "spin_lock.h"

// A future is a value that will be produced at some point in the future.
template <typename T> class Future {
  HandleQueue<SpinLock> waiting;
  T result;
  SpinLock has_result_lock;
  volatile bool has_result;

public:
  Future(const T &initial_value = {}) { result = initial_value; has_result = false; }

  // the awaitable interface, blocks the caller until the value is ready and
  // returns that value
  bool await_ready() { return has_result; }

  bool await_suspend(std::coroutine_handle<> handle)
  {
    LockGuard g(has_result_lock);

    if (await_ready())
    {
      return false;
    }
    waiting.add(handle);
    return true;
  }

  T await_resume() { return result; }

  // sets the value and resumes all waiters
  // can be called at most once, after which the future is considered "ready"
  [[gnu::noinline]] void set(const T &new_value)
  {
    has_result_lock.lock();
    ASSERT(has_result == false);
    result = std::move(new_value);
    has_result = true;
    HandleQueue<NoLock> handles = waiting.remove_all();
    has_result_lock.unlock();
    impl::ready_queue.add(handles);
  }
};
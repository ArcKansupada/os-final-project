#pragma once

#include "lib/handle_queue.h"
#include "spin_lock.h"

// Condition variable: the caller holds the same lock for checking its
// predicate, waiting, and triggering. Waiting releases it and resumes with
// it reacquired, so a surrounding LockGuard works across the co_await.
class Event {
  HandleQueue<NoLock> waiting;

public:
  struct Wait {
    Event &event;
    SpinLock &lock;

    bool await_ready() { return false; }
    void await_suspend(std::coroutine_handle<> handle) {
      event.waiting.add(handle);
      lock.unlock();
    }
    void await_resume() { lock.lock(); }
  };

  Wait wait(SpinLock &lock) { return {*this, lock}; }

  // The predicate's lock must be held.
  void trigger() {
    HandleQueue<NoLock> handles = waiting.remove_all();
    impl::ready_queue.add(handles);
  }
};

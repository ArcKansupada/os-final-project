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

#pragma once

#include "lib/handle_queue.h"
#include "spin_lock.h"
#include <optional>

// A blocking mutual exclusion lock that protects a value of type T. The value
// can be accessed by doing co_await on the mutex, which will block until the
// mutex is available. The caller will receive a Proof object that can be used
// to access the value. The Proof object will automatically unlock the mutex
// when it is destroyed.

template <typename T> class Mutex {
  T data;
  bool wr_locked;
  uint32_t readers;
  SpinLock lock;
  HandleQueue<SpinLock> queue;
  HandleQueue<SpinLock> ro_queue;

  /* SEMANTICS
  * on normal co_await:
  *  - if `wr_locked` or `rd_locked`, go in `queue`
  *  - else, execute
  * on RO co_await:
  *  - if `wr_locked` or `queue` not empty, go in ro_queue
  *  - else, execute
  * on release:
  *  - if `queue` not empty, execute next in `queue`
  *  - else if `ro_queue` not empty, execute all in `ro_queue`, unlock `wr_locked`
  *  - else, unlock
  */

public:
  // Returned by co_awaiting on the mutex:
  //     - indicates that the caller has acquired the mutex
  //     - allows the caller to access the protected data
  class [[nodiscard]] IProof {
    friend class Mutex;
    bool readonly;

  protected:
    Mutex *mutex;
    IProof(Mutex *mutex, bool readonly) : readonly(readonly), mutex(mutex) {}

  public:
    // can't copy
    IProof(const IProof &) = delete;
    IProof &operator=(const IProof &) = delete;

    // can move
    IProof(IProof &&other) noexcept
    {
      mutex = other.mutex;
      readonly = other.readonly;
      other.mutex = nullptr;
    }

    IProof &operator=(IProof &&other) noexcept
    {
      if (this == &other) return *this;

      unlock();
      mutex = other.mutex;
      readonly = other.readonly;
      other.mutex = nullptr;
      return *this;
    }

    ~IProof() { unlock(); }

    // release the lock before the Proof is destroyed.
    void unlock()
    {
      if (!mutex) return;
      mutex->lock.lock();

      if (readonly && --mutex->readers > 0) {
        mutex->lock.unlock();
        mutex = nullptr;
        return;
      }

      if (!mutex->queue.is_empty_hint()) {
        mutex->wr_locked = true;
        mutex->readers = 0;
        impl::ready_queue.transfer(mutex->queue);
      } else {
        if (!mutex->ro_queue.is_empty_hint()) {
          mutex->wr_locked = false;
          HandleQueue<NoLock> handles = mutex->ro_queue.remove_all();
          mutex->readers = handles.count;
          impl::ready_queue.add(handles);
        } else {
          mutex->wr_locked = false;
          mutex->readers = 0;
        }
      }

      mutex->lock.unlock();
      mutex = nullptr;
    }
  };
  class [[nodiscard]] Proof : public IProof {
  public:
    Proof(Mutex *mutex) : IProof(mutex, false) {}
    T& data() { return this->mutex->data; }
  };
  class [[nodiscard]] ROProof : public IProof {
  public:
    ROProof(Mutex *mutex) : IProof(mutex, true) {}
    const T& data() { return this->mutex->data; }
  };

  explicit Mutex(T initial_value = {}) : data(initial_value), wr_locked(false), readers(0) { }

  // The awaitable interface, blocks the caller until the mutex is available and
  // returns a Proof object that can be used to access the protected data.
  bool await_ready()
  {
    LockGuard g(lock);

    if (!wr_locked && readers == 0)
    {
      wr_locked = true;
      return true;
    }

    return false;
  }

  bool await_suspend(std::coroutine_handle<> handle)
  {
    LockGuard g(lock);

    if (!wr_locked && readers == 0)
    {
      wr_locked = true;
      return false;
    }

    queue.add(handle);
    return true;
  }

  Proof await_resume() { return Proof{this}; }

  struct ReadOnly {
    friend class Mutex;

    Mutex *mutex;

    bool await_ready() {
      LockGuard g(mutex->lock);

      if (!mutex->wr_locked && mutex->queue.is_empty_hint())
      {
        mutex->readers += 1;
        return true;
      }

      return false;
    }
    bool await_suspend(std::coroutine_handle<> handle) {
      LockGuard g(mutex->lock);

      if (!mutex->wr_locked && mutex->queue.is_empty_hint())
      {
        mutex->readers += 1;
        return false;
      }

      mutex->ro_queue.add(handle);
      return true;
    }
    ROProof await_resume() { return ROProof{mutex}; }
  };

  ReadOnly readonly() { return {this}; }
};

typedef Mutex<bool> BlockingLock;

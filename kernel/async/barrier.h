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

#include "atomic.h"
#include "lib/print.h"
#include <cstdint>

#include "future.h"

// A blocking barrier
class Barrier {
    Atomic<uint64_t> counter;
    Future<bool> future;
public:
    // create a barrier that will block until 'count' awaiters have arrived
    explicit Barrier(std::uint64_t count) { counter = count; }

    // awaitable interface
    //     co_await barrier; // indicates that the caller has arrived at the
    //     barrier
    //                       // and blocks until all other awaiters have arrived
    bool await_ready() noexcept { return future.await_ready(); }
    bool await_suspend(std::coroutine_handle<> handle) noexcept
    {
        ASSERT(counter.get() > 0);
        if (counter.sub_fetch(1) > 0) return future.await_suspend(handle);

        future.set(true);
        return false;
    }
    void await_resume() noexcept { }
};

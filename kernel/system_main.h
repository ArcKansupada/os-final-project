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

#include <cstdint>

namespace impl {
[[noreturn]]
extern void event_loop();
}

namespace Sys {
extern bool normal;
extern uint64_t core_count;
extern volatile bool bootstraping;
extern uint64_t start_tsc;
extern uint64_t tsc_hz;
extern uint32_t quantum_ticks; // APIC timer ticks in one scheduling quantum (10 ms)
extern int64_t boot_unix_time;
extern uint64_t hhdm_offset;

// Unix time in seconds, advancing with the TSC from the boot timestamp
uint32_t unix_time();
} // namespace Sys

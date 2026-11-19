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

/*
 * Helpers for the user tests' init programs (tN.dir/src)
 */

#include <errno.h>

typedef unsigned long u64;
typedef unsigned short u16;
typedef unsigned int u32;

// Loads known values into every register a system call has to preserve
// (all but RAX, RCX, R11), makes a sched_yield (which blocks, the thread can
// come back on another core) and returns a nonzero value if any changed.
u64 preserved(void);

// Did call fail the libc way: -1 with errno set to error?
#define FAILS_WITH(call, error) ((call) == -1 && errno == (error))

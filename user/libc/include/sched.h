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

#include <stdint.h>

int sched_yield(void);

/*
 * clone3, for threads (pthread.h does it for you). Linux's values. The
 * kernel needs CLONE_VM | CLONE_SIGHAND | CLONE_THREAD, takes CLONE_FS,
 * CLONE_FILES, and CLONE_SYSVSEM (they mean nothing yet), and does
 * CLONE_SETTLS, CLONE_PARENT_SETTID, and CLONE_CHILD_CLEARTID.
 */

#define CLONE_VM 0x100
#define CLONE_FS 0x200
#define CLONE_FILES 0x400
#define CLONE_SIGHAND 0x800
#define CLONE_THREAD 0x10000
#define CLONE_SYSVSEM 0x40000
#define CLONE_SETTLS 0x80000
#define CLONE_PARENT_SETTID 0x100000
#define CLONE_CHILD_CLEARTID 0x200000

// Linux's struct clone_args, up to tls (what the kernel reads)
struct clone_args {
  uint64_t flags;
  uint64_t pidfd;
  uint64_t child_tid;   // CLONE_CHILD_CLEARTID: set to 0 when it ends
  uint64_t parent_tid;  // CLONE_PARENT_SETTID: gets its tid
  uint64_t exit_signal; // 0 for threads
  uint64_t stack;       // its stack: [stack, stack + stack_size)
  uint64_t stack_size;
  uint64_t tls; // CLONE_SETTLS: its FS base
};

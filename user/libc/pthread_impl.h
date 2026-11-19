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

// libc's own, not for programs

#include <stddef.h>

// A thread, what pthread_t points to. The FS base of every thread points to
// its own (pthread_self, errno): CLONE_SETTLS for the ones pthread_create
// makes, crt0 for the main thread.
struct pthread {
  void *(*start)(void *);
  void *arg;
  void *value; // its result
  // The kernel puts the tid here before the thread runs (PARENT_SETTID) and
  // sets it to 0 when the thread is gone (CHILD_CLEARTID)
  int tid;
  void *stack;
  int err; // its errno
};

// the main thread's (crt0)
extern struct pthread __main_thread;

// System call n, the kernel's raw result (-errno for a failure)
long __syscall(long n, long a0, long a1, long a2, long a3, long a4, long a5);

// A raw system call result the libc way: a failure (-4095 .. -1, like
// Linux) sets errno and becomes -1
long __syscall_result(long result);

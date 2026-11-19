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
 * Threads, a simplified pthreads: no attributes. A thread gets a stack of
 * PTHREAD_STACK_SIZE bytes (mmap'ed, faulted in as it's used).
 *
 * Returning from main (or exit) ends the process with every thread in it.
 * pthread_exit, and returning from a thread's function, end just that
 * thread. The process ends when its last thread does.
 *
 * Like POSIX: errors are returned (an errno value, e.g. EAGAIN), errno is
 * left alone. Every thread has its own errno.
 */

#define PTHREAD_STACK_SIZE (64 * 1024)

typedef struct pthread *pthread_t;

// Starts a thread running start(arg), puts it in *thread. Returns 0, or
// EAGAIN if there's no memory for it.
int pthread_create(pthread_t *thread, void *(*start)(void *), void *arg);

// Ends the calling thread, value is what pthread_join gets. In the main
// thread there's nobody to get it.
__attribute__((noreturn)) void pthread_exit(void *value);

// Waits for thread to end and puts what it returned (or passed to
// pthread_exit) in *value, unless value is NULL. Each thread can be joined
// once, its stack goes away then. Returns 0, or EINVAL for no thread (or
// the main thread, which can't be joined).
//
// There's no futex yet: it waits with sched_yield.
int pthread_join(pthread_t thread, void **value);

// The calling thread. The main thread has one too.
pthread_t pthread_self(void);

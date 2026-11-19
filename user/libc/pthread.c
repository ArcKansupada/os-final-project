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

#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#include "pthread_impl.h"

// clone3 for a thread: the new thread calls child(self), the caller gets the
// tid (or -errno). In pthread_asm.S, like pthread_self.
long __clone3(struct clone_args *args, size_t size,
              void (*child)(struct pthread *), struct pthread *self);

__attribute__((noreturn)) static void thread_start(struct pthread *self) {
  pthread_exit(self->start(self->arg));
}

int pthread_create(pthread_t *thread, void *(*start)(void *), void *arg) {
  // pthreads functions return their errors, errno stays as it was
  const int saved_errno = errno;
  struct pthread *t = malloc(sizeof(*t));
  if (t == NULL) {
    errno = saved_errno;
    return EAGAIN;
  }
  void *stack = mmap(NULL, PTHREAD_STACK_SIZE, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (stack == MAP_FAILED) {
    errno = saved_errno;
    return EAGAIN;
  }
  t->start = start;
  t->arg = arg;
  t->value = NULL;
  t->tid = 0;
  t->stack = stack;
  t->err = 0;

  struct clone_args args = {
      .flags = CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_SIGHAND |
               CLONE_THREAD | CLONE_SYSVSEM | CLONE_SETTLS |
               CLONE_PARENT_SETTID | CLONE_CHILD_CLEARTID,
      .child_tid = (uint64_t)&t->tid,
      .parent_tid = (uint64_t)&t->tid,
      .stack = (uint64_t)stack,
      .stack_size = PTHREAD_STACK_SIZE,
      .tls = (uint64_t)t, // pthread_self
  };
  const long tid = __clone3(&args, sizeof(args), thread_start, t);
  if (tid < 0) {
    munmap(stack, PTHREAD_STACK_SIZE);
    return (int)-tid;
  }
  *thread = t;
  return 0;
}

void pthread_exit(void *value) {
  pthread_self()->value = value;
  __syscall(SYS_exit, 0, 0, 0, 0, 0, 0);
  __builtin_unreachable();
}

int pthread_join(pthread_t thread, void **value) {
  // the main thread has no stack of ours, and nothing clears its tid
  if (thread == NULL || thread == &__main_thread) {
    return EINVAL;
  }
  // acquire: its writes (value) are visible once the kernel cleared tid
  while (__atomic_load_n(&thread->tid, __ATOMIC_ACQUIRE) != 0) {
    sched_yield();
  }
  if (value != NULL) {
    *value = thread->value;
  }
  munmap(thread->stack, PTHREAD_STACK_SIZE);
  return 0;
}

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
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static char heap[HEAP_SIZE] __attribute__((aligned(16)));
// bytes handed out so far, can go past HEAP_SIZE once it runs out
static size_t used;

void *malloc(size_t size) {
  if (size > HEAP_SIZE) {
    errno = ENOMEM;
    return NULL;
  }
  // malloc(0) gets a block of its own too
  const size_t rounded = size == 0 ? 16 : (size + 15) & ~(size_t)15;
  const size_t start = __atomic_fetch_add(&used, rounded, __ATOMIC_RELAXED);
  if (start > HEAP_SIZE - rounded) {
    errno = ENOMEM;
    return NULL;
  }
  return heap + start;
}

void *calloc(size_t n, size_t size) {
  if (size != 0 && n > HEAP_SIZE / size) {
    errno = ENOMEM;
    return NULL;
  }
  void *p = malloc(n * size);
  if (p != NULL) {
    // fresh .bss memory is zero, but don't count on how the heap works
    memset(p, 0, n * size);
  }
  return p;
}

void free(void *p) { (void)p; }

void exit(int status) { _exit(status); }

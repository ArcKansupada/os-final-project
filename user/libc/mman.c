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
#include <sys/mman.h>
#include <unistd.h>

#include "pthread_impl.h"

void *mmap(void *addr, size_t length, int prot, int flags, int fd,
           off_t offset) {
  // a mapping is a user address, never -4095 .. -1
  const long result = __syscall_result(
      __syscall(SYS_mmap, (long)addr, (long)length, prot, flags, fd, offset));
  return result == -1 ? MAP_FAILED : (void *)result;
}

int munmap(void *addr, size_t length) {
  return (int)syscall(SYS_munmap, (long)addr, (long)length, 0, 0, 0, 0);
}

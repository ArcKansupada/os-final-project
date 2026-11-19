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

// The parts of pthreads that have to be assembly (see pthread.c)

        .text

// long __clone3(struct clone_args *args, size_t size,
//               void (*child)(struct pthread *), struct pthread *self)
//
// The new thread comes back from the system call with the same registers on
// its own stack (16-byte aligned), and RAX = 0: it calls child(self), which
// doesn't return. The caller gets the tid (or -errno).
        .global __clone3
__clone3:
        mov %rdx, %r8           // the system call keeps them, the new
        mov %rcx, %r9           // thread needs them
        mov $435, %eax          // clone3
        syscall
        test %rax, %rax
        jnz 1f
        mov %r9, %rdi           // the new thread
        call *%r8
        ud2
1:
        ret

// pthread_t pthread_self(void)
//
// The FS base is the thread's struct pthread: CLONE_SETTLS for the threads
// pthread_create makes, crt0 for the main thread. User mode can read it
// (CR4.FSGSBASE).
        .global pthread_self
pthread_self:
        rdfsbase %rax
        ret

        .section .note.GNU-stack,"",@progbits

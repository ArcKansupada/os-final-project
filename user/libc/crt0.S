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

// The entry point
//
// main(argc, argv, envp); its result is the exit code. RSP is 16-byte
// aligned and points at argc (the kernel's initial stack layout).
//
// The FS base points to the main thread's struct pthread first
// (pthread_self, errno). User mode can set it (CR4.FSGSBASE).

        .text
        .global _start
_start:
        lea __main_thread(%rip), %rax
        wrfsbase %rax
        mov (%rsp), %rdi
        lea 8(%rsp), %rsi
        lea 16(%rsp,%rdi,8), %rdx
        call main
        mov %eax, %edi
        call exit
        ud2

        .section .note.GNU-stack,"",@progbits

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

// u64 preserved(void), see testing.h
//
// Loads known values into every register a system call has to preserve
// (all but RAX, RCX, R11), makes a sched_yield (which blocks, the thread can
// come back on another core) and returns a nonzero value if any changed.
// Keeps the callee-saved registers it uses, an ordinary function to C.

        .text
        .global preserved
preserved:
        push %rbx
        push %rbp
        push %r12
        push %r13
        push %r14
        push %r15
        mov $0x1001, %rbx
        mov $0x1002, %rbp
        mov $0x1003, %rdx
        mov $0x1004, %rsi
        mov $0x1005, %rdi
        mov $0x1006, %r8
        mov $0x1007, %r9
        mov $0x1008, %r10
        mov $0x1009, %r12
        mov $0x100a, %r13
        mov $0x100b, %r14
        mov $0x100c, %r15
        mov $0x2001, %eax
        movq %rax, %xmm0
        mov $0x2002, %eax
        movq %rax, %xmm15
        mov $24, %eax           // sched_yield
        syscall
        xor %ecx, %ecx          // RCX collects the differences
        xor $0x1001, %rbx
        or %rbx, %rcx
        xor $0x1002, %rbp
        or %rbp, %rcx
        xor $0x1003, %rdx
        or %rdx, %rcx
        xor $0x1004, %rsi
        or %rsi, %rcx
        xor $0x1005, %rdi
        or %rdi, %rcx
        xor $0x1006, %r8
        or %r8, %rcx
        xor $0x1007, %r9
        or %r9, %rcx
        xor $0x1008, %r10
        or %r10, %rcx
        xor $0x1009, %r12
        or %r12, %rcx
        xor $0x100a, %r13
        or %r13, %rcx
        xor $0x100b, %r14
        or %r14, %rcx
        xor $0x100c, %r15
        or %r15, %rcx
        movq %xmm0, %rax
        xor $0x2001, %rax
        or %rax, %rcx
        movq %xmm15, %rax
        xor $0x2002, %rax
        or %rax, %rcx
        mov %rcx, %rax
        pop %r15
        pop %r14
        pop %r13
        pop %r12
        pop %rbp
        pop %rbx
        ret

        .section .note.GNU-stack,"",@progbits

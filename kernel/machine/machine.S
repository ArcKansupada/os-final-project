/* Copyright (C) 2025 Ahmed Gheith and contributors.
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

	.global outb
outb:
	mov %di, %dx
	mov %sil, %al
	outb %al, %dx
	ret

	.global outl
outl:
	mov %di, %dx
	mov %esi, %eax
	outl %eax, %dx
	ret

	.global inb
inb:
	mov %di, %dx
	xor %rax, %rax
	inb %dx, %al
	ret

	.global inl
inl:
	mov %di, %dx
	xor %rax, %rax
	inl %dx, %eax
	ret

	.global get_cr0
get_cr0:
	mov %cr0, %rax
	ret

	.global get_cr4
get_cr4:
	mov %cr4, %rax
	ret

	.global set_cr4
set_cr4:
	mov %rdi, %rax
	mov %rax, %cr4
	ret

	.global enable_sse
enable_sse:
	mov %cr4, %rax
	and $0xfffb, %ax
	or $0x602, %ax
	mov %rax, %cr4
	ret

	.global rdfsbase
rdfsbase:
	rdfsbase %rax
	ret

	.global wrfsbase
wrfsbase:
	wrfsbase %rdi
	ret

	.global rdgsbase
rdgsbase:
	rdgsbase %rax
	ret

	.global wrgsbase
wrgsbase:
	wrgsbase %rdi
	ret

	.global memcpy
memcpy:
	cld
	mov %rdi, %rax
	mov %rdx, %rcx
	rep movsb
	ret

	.global memset
memset:
	cld
	mov %rdi, %r8
	mov %esi, %eax
	mov %rdx, %rcx
	rep stosb
	mov %r8, %rax
	ret

	.global strlen
strlen:
	mov %rdi, %rax
	mov $0, %rcx
1:
	movb (%rax), %r11b
	cmpb $0, %r11b
	je 1f
	inc %rax
	inc %rcx
	jmp 1b
1:
	mov %rcx, %rax
	ret

	.global streq
streq:
	movb (%rdi), %al
	cmpb (%rsi), %al
	
	je 1f
	// not equal, return 0
	xor %rax, %rax
	ret

	// equal
1:
	cmp $0, %al
	je 1f
	inc %rdi
	inc %rsi
	jmp streq     // keep going

1:
	movq $1, %rax     // equal, return 1
	ret

	.global context_switch_ // (uintptr_t fs)
context_switch_:

	rdfsbase %rax # current (will become previous) thread

	
	push %rbx
	push %rbp
	push %r12
	push %r13
	push %r14
	push %r15
	pushf             // IF=what A wants
	mov %cr2,%r15
	push %r15
	mov %cr3,%r14
	push %r14

	cli               // IF=0  
	                  // disable interrupts until we restore them correctly
	                  // below. Handles the case where B wants interrupts disabled
					  // but A has them enabled.

	mov %rsp, %fs:8   // IF=0

	# Thread A
	# ------>
	# Thread B

	wrfsbase %rdi     // IF=0

	mov %fs:8, %rsp   // IF=0

	pop %r14
	mov %r14, %cr3
	pop %r15
	mov %r15, %cr2
	popf              // IF=what B wants
	pop %r15
	pop %r14
	pop %r13
	pop %r12
	pop %rbp
	pop %rbx

	// prev is both the return value (for old threads) and
	// the first arguments (for new threads)
	mov %rax, %rdi

	ret

	.global pause
pause:
	pause
	ret

	.global get_flags
get_flags:
	pushf
	pop %rax
	ret

.global pageFaultHandler_
        .global generalProtectionHandler_
generalProtectionHandler_:
        btsq $63, (%rsp) // distinguish #GP from #PF, same hardware frame
        jmp pageFaultHandler_
pageFaultHandler_:
        // An interrupt gate: interrupts are disabled until the C++ code turns
        // them back on, nothing can come in before GS is sorted out.
        // From user mode (CS RPL 3): switch to the kernel's GS; swap back
        // before iretq when returning to CPL3.
        testb $3, 16(%rsp)
        jz 1f
        swapgs
1:
    // we're about to call C++ code
        //     - Strictly speaking, we only need to to save the caller-saved registers
        //     - but it is better for debugging purposes to save all of them
        //     - this will also allow signal handlers to manipulate the machine state in
        //       the future
        push %r15
        push %r14
        push %r13
        push %r12
        push %r11
        push %r10
        push %r9
        push %r8
        push %rbp
        push %rsi
        push %rdi
        push %rdx
        push %rcx
        push %rbx
        push %rax

        // The C++ code (and whatever the event loop runs while we wait) is free
        // to use the x87/MMX/XMM registers, but the interrupted code might have
        // live values in them. Save them below the trap frame.
        //     - fxsave needs a 512 byte, 16-byte aligned area
        //     - this also leaves %rsp 16-byte aligned for the call
        //     - %rbx is callee-saved, it survives the call
        mov %rsp, %rbx
        sub $512, %rsp
        and $-16, %rsp
        fxsave64 (%rsp)
        // the kernel's x87/SSE control settings, not the interrupted code's
        fninit
        ldmxcsr kernel_mxcsr(%rip)

        mov %cr2, %rdi
        mov %rbx, %rsi    // the trap frame
        mov %rsp, %rdx    // the saved x87/SSE state
        call pageFaultHandler

        fxrstor64 (%rsp)
        mov %rbx, %rsp

        pop %rax
        pop %rbx
        pop %rcx
        pop %rdx
        pop %rdi
        pop %rsi
        pop %rbp
        pop %r8
        pop %r9
        pop %r10
        pop %r11
        pop %r12
        pop %r13
        pop %r14
        pop %r15

        // error, rip, cs, ... — restore user GS if returning to CPL3
        testb $3, 16(%rsp)
        jz 2f
        swapgs
2:
        add $8, %rsp // pop error code
        iretq

	// void call_on_stack_(void *arg, void (*fn)(void *), void *stack_top)
	//     runs fn(arg) on the given stack then returns on the caller's stack
	//     stack_top must be 16-byte aligned
	.global call_on_stack_
call_on_stack_:
	push %rbp
	mov %rsp, %rbp      // callee-saved, survives the call
	mov %rdx, %rsp
	call *%rsi          // arg is already in %rdi
	mov %rbp, %rsp
	pop %rbp
	ret

	// TLB shootdown IPI, an interrupt gate so IF=0 in here. Saves the
	// caller-saved general registers and the x87/SSE state: the C++ code may
	// use the XMM registers, and the interrupted code (kernel or user) can
	// have live values in them. The kernel is built with -mno-red-zone.
	.global tlbHandler_
tlbHandler_:
        // from user mode (CS RPL 3): switch to the kernel's GS
        testb $3, 8(%rsp)
        jz 1f
        swapgs
1:
        push %rax
        push %rcx
        push %rdx
        push %rsi
        push %rdi
        push %r8
        push %r9
        push %r10
        push %r11
        push %rbx

        // fxsave needs a 512 byte, 16-byte aligned area, this also leaves
        // %rsp aligned for the call
        mov %rsp, %rbx    // callee-saved, survives the call
        sub $512, %rsp
        and $-16, %rsp
        fxsave64 (%rsp)
        // the kernel's x87/SSE control settings, not the interrupted code's
        fninit
        ldmxcsr kernel_mxcsr(%rip)
        call tlbHandler
        fxrstor64 (%rsp)
        mov %rbx, %rsp

        pop %rbx
        pop %r11
        pop %r10
        pop %r9
        pop %r8
        pop %rdi
        pop %rsi
        pop %rdx
        pop %rcx
        pop %rax

        testb $3, 8(%rsp)
        jz 1f
        swapgs
1:
        iretq

	// The MXCSR kernel code runs with: all SSE exceptions masked, round to
	// nearest. Loaded (with fninit for the x87 side) after saving the
	// interrupted code's state on every entry to the kernel that saves it.
	.section .rodata
	.balign 4
	.global kernel_mxcsr
kernel_mxcsr:
	.long 0x1F80
	.text

	    .global spuriousHandler_
spuriousHandler_:
        iretq

	// Kill IPI: leave dead-process CR3 on every core; if a zombie was in user,
	// bounce to the event loop instead of iretq. ACK is done in killHandler
	// before any bounce. rdi = from_user for killHandler(bool).
	.global killHandler_
killHandler_:
        testb $3, 8(%rsp)
        jnz 1f
        // from kernel — leave CR3 / ack, then iretq
        push %rax
        push %rcx
        push %rdx
        push %rsi
        push %rdi
        push %r8
        push %r9
        push %r10
        push %r11
        xor %edi, %edi
        call killHandler
        pop %r11
        pop %r10
        pop %r9
        pop %r8
        pop %rdi
        pop %rsi
        pop %rdx
        pop %rcx
        pop %rax
        iretq
1:
        swapgs
        push %rax
        push %rcx
        push %rdx
        push %rsi
        push %rdi
        push %r8
        push %r9
        push %r10
        push %r11
        push %rbx
        mov %rsp, %rbx
        and $-16, %rsp
        mov $1, %edi
        call killHandler
        mov %rbx, %rsp
        pop %rbx
        test %al, %al
        jnz 2f
        pop %r11
        pop %r10
        pop %r9
        pop %r8
        pop %rdi
        pop %rsi
        pop %rdx
        pop %rcx
        pop %rax
        swapgs
        iretq
2:
        // zombie in user — abandon frame; bounce_to_yield_ resets RSP
        jmp bounce_to_yield_

	// APIC timer. From kernel: acknowledge and return. From user: save the
	// interrupted state into the TCB (as syscallHandler_ does) and bounce to the scheduler.
	.global timerHandler_
timerHandler_:
	testb $3, 8(%rsp)
	jnz 1f
	push %rax
	push %rcx
	push %rdx
	push %rsi
	push %rdi
	push %r8
	push %r9
	push %r10
	push %r11
	xor %edi, %edi
	call timerHandler
	pop %r11
	pop %r10
	pop %r9
	pop %r8
	pop %rdi
	pop %rsi
	pop %rdx
	pop %rcx
	pop %rax
	iretq
1:
	swapgs
	push %r10
	mov %gs:24, %r10             // TCB*
	mov %rax, 0(%r10)
	mov %rbx, 8(%r10)
	mov %rcx, 16(%r10)
	mov %rdx, 24(%r10)
	mov %rsi, 32(%r10)
	mov %rdi, 40(%r10)
	mov %rbp, 48(%r10)
	mov %r8, 56(%r10)
	mov %r9, 64(%r10)
	mov %r11, 80(%r10)
	mov %r12, 88(%r10)
	mov %r13, 96(%r10)
	mov %r14, 104(%r10)
	mov %r15, 112(%r10)
	mov 8(%rsp), %rax            // interrupt frame: rip, cs, rflags, rsp, ss
	mov %rax, 120(%r10)
	mov 24(%rsp), %rax
	mov %rax, 136(%r10)
	mov 32(%rsp), %rax
	mov %rax, 128(%r10)
	pop %rax                     // user r10
	mov %rax, 72(%r10)
	rdfsbase %rax
	mov %rax, 144(%r10)
	mov $0xC0000102, %ecx
	rdmsr
	shl $32, %rdx
	or %rdx, %rax
	mov %rax, 152(%r10)
	fxsave64 160(%r10)
	and $-16, %rsp
	mov $1, %edi
	call timerHandler
	jmp bounce_to_yield_

// CCB layout:          0 self, 8 id, 12 apic_id, 16 stack_top, 24 current_thread, 32 user_rsp
	// UserContext layout:  0 rax .. 112 r15, 120 rip, 128 rsp, 136 rflags, 144 fs, 152 gs, 160 fpu

	.global bounce_to_yield_
bounce_to_yield_:
	mov %gs:0, %rax              // CCB*
	mov 16(%rax), %rsp           // stack_top
	and $-16, %rsp
	sub $8, %rsp
	sti
	jmp schedule_

	.global resume_user_
resume_user_:
	// %rdi = UserContext*
	cli
	mov %rdi, %rsi               // rsi = &UserContext while we load
	fxrstor64 160(%rsi)
	// IRET preserves RCX/R11 too (timer and signal returns are not syscalls).
	pushq $0x3b                 // GDT::USER_DS
	pushq 128(%rsi)
	pushq 136(%rsi)
	pushq $0x43                 // GDT::USER_CS
	pushq 120(%rsi)
	mov 0(%rsi), %rax
	mov 8(%rsi), %rbx
	mov 24(%rsi), %rdx
	mov 48(%rsi), %rbp
	mov 56(%rsi), %r8
	mov 64(%rsi), %r9
	mov 72(%rsi), %r10
	mov 88(%rsi), %r12
	mov 96(%rsi), %r13
	mov 104(%rsi), %r14
	mov 112(%rsi), %r15
	mov 16(%rsi), %rcx
	mov 80(%rsi), %r11
	mov 40(%rsi), %rdi           // user rdi
	mov 32(%rsi), %rsi           // user rsi (last; was context base)
	swapgs
	iretq

	.global syscallHandler_
syscallHandler_:
	// GS still holds the user GS base; KernelGSBase holds the CCB.
	swapgs
	mov %rsp, %gs:32             // CCB.user_rsp
	mov %gs:16, %rsp             // CCB.stack_top

	push %r10                    // preserve user r10
	mov %gs:24, %r10             // TCB* current_thread

	mov %rax, 0(%r10)
	mov %rbx, 8(%r10)
	mov %rcx, 120(%r10)          // user rip (syscall's rcx)
	mov %rcx, 16(%r10)
	mov %rdx, 24(%r10)
	mov %rsi, 32(%r10)
	mov %rdi, 40(%r10)
	mov %rbp, 48(%r10)
	mov %r8, 56(%r10)
	mov %r9, 64(%r10)
	pop %rax                     // user r10
	mov %rax, 72(%r10)
	mov %r11, 136(%r10)          // user rflags
	mov %r11, 80(%r10)
	mov %r12, 88(%r10)
	mov %r13, 96(%r10)
	mov %r14, 104(%r10)
	mov %r15, 112(%r10)

	mov %gs:32, %rax
	mov %rax, 128(%r10)          // context.rsp

	rdfsbase %rax
	mov %rax, 144(%r10)          // fs_base
	// user GS sits in KernelGSBase after swapgs
	mov $0xC0000102, %rcx
	rdmsr                        // edx:eax = user gs
	shl $32, %rdx
	or %rdx, %rax
	mov %rax, 152(%r10)          // gs_base

	fxsave64 160(%r10)
	fninit
	ldmxcsr kernel_mxcsr(%rip)

	and $-16, %rsp
	call syscallHandler
	// noreturn


	.section .note.GNU-stack,"",@progbits
	

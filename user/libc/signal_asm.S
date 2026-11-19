.text
.global __restore_rt
.type __restore_rt, @function
__restore_rt:
    mov $15, %eax
    syscall
    ud2
.size __restore_rt, .-__restore_rt
.section .note.GNU-stack,"",@progbits

.text
.global copy_user_
.type copy_user_, @function
copy_user_:
    mov %rdx, %rcx
.global copy_user_access_
copy_user_access_:
    rep movsb
    xor %eax, %eax
    ret
.global copy_user_fault_
copy_user_fault_:
    mov %rcx, %rax
    ret
.size copy_user_, .-copy_user_
.global store_user32_
.type store_user32_, @function
store_user32_:
    mov $4, %ecx
.global store_user32_access_
store_user32_access_:
    mov %esi, (%rdi)
    xor %eax, %eax
    ret
.size store_user32_, .-store_user32_
.section .note.GNU-stack,"",@progbits

.global _start_kernel
.extern kernel_main

.section .text.entry, "ax"
_start_kernel:
    cli
    cld
    // 16-Byte Stack-Alignment für System-V ABI
    andq $-16, %rsp
    call kernel_main

1:
    hlt
    jmp 1b
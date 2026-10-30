.section .text
.code64
.global _start
.type _start, @function

_start:
    xor %rbp, %rbp
    lea stack_top(%rip), %rsp
    call kernel_startup

.hang:
    cli
    hlt
    jmp .hang

.section .note.GNU-stack,"",@progbits

.section .bss
.align 16
stack_bottom:
    .skip 65536
stack_top:

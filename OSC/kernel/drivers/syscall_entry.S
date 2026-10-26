.code64
.globl syscall_entry
.align 16
/* int $0x80 entry. Caller convention: rax = number, rdi/rsi/rdx/
   r10/r8/r9 = args, return in rax. C dispatch convention:
   rdi = number, rsi/rdi... see syscall_dispatch. Shuffle in an
   order that does not lose a value: r8->r9 first, then r10->r8,
   then rdx->rcx, rsi->rdx, rdi->rsi, rax->rdi. */
syscall_entry:
    pushq %rbp
    movq %r8, %r9
    movq %r10, %r8
    movq %rdx, %rcx
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq %rax, %rdi
    callq syscall_dispatch
    popq %rbp
    iretq
.section .note.GNU-stack,"",@progbits

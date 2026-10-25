.code64

.globl idt_load
.globl irq0
.globl irq1
.globl irq2
.globl irq3
.globl irq4
.globl irq5
.globl irq6
.globl irq7
.globl irq8
.globl irq9
.globl irq10
.globl irq11
.globl irq12
.globl irq13
.globl irq14
.globl irq15

# ========================
# IDT LOAD
# ========================
.align 16
idt_load:
    lidt (%rdi)
    retq

# ========================
# IRQ HANDLERS
# ========================
.align 16
irq0:
    pushq $0
    pushq $0
    jmp irq_common

.align 16
irq1:
    pushq $0
    pushq $1
    jmp irq_common

.align 16
irq2:
    pushq $0
    pushq $2
    jmp irq_common

.align 16
irq3:
    pushq $0
    pushq $3
    jmp irq_common

.align 16
irq4:
    pushq $0
    pushq $4
    jmp irq_common

.align 16
irq5:
    pushq $0
    pushq $5
    jmp irq_common

.align 16
irq6:
    pushq $0
    pushq $6
    jmp irq_common

.align 16
irq7:
    pushq $0
    pushq $7
    jmp irq_common

.align 16
irq8:
    pushq $8
    jmp irq_common

.align 16
irq9:
    pushq $0
    pushq $9
    jmp irq_common

.align 16
irq10:
    pushq $10
    jmp irq_common

.align 16
irq11:
    pushq $11
    jmp irq_common

.align 16
irq12:
    pushq $12
    jmp irq_common

.align 16
irq13:
    pushq $13
    jmp irq_common

.align 16
irq14:
    pushq $14
    jmp irq_common

.align 16
irq15:
    pushq $0
    pushq $15
    jmp irq_common

.align 16
irq_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq 120(%rsp), %rax

    cmpq $0, %rax
    je .irq0
    cmpq $1, %rax
    je .irq1
    cmpq $2, %rax
    je .irq2
    cmpq $3, %rax
    je .irq3
    cmpq $4, %rax
    je .irq4
    cmpq $5, %rax
    je .irq5
    cmpq $6, %rax
    je .irq6
    cmpq $7, %rax
    je .irq7
    cmpq $8, %rax
    je .irq8
    cmpq $9, %rax
    je .irq9
    cmpq $10, %rax
    je .irq10
    cmpq $11, %rax
    je .irq11
    cmpq $12, %rax
    je .irq12
    cmpq $13, %rax
    je .irq13
    cmpq $14, %rax
    je .irq14
    cmpq $15, %rax
    je .irq15

    jmp .done

.irq0:
    movq %rsp, %rdi
    callq irq0_handler
    jmp .done
.irq1:
    movq %rsp, %rdi
    callq irq1_handler
    jmp .done
.irq2:
    movq %rsp, %rdi
    callq irq2_handler
    jmp .done
.irq3:
    movq %rsp, %rdi
    callq irq3_handler
    jmp .done
.irq4:
    movq %rsp, %rdi
    callq irq4_handler
    jmp .done
.irq5:
    movq %rsp, %rdi
    callq irq5_handler
    jmp .done
.irq6:
    movq %rsp, %rdi
    callq irq6_handler
    jmp .done
.irq7:
    movq %rsp, %rdi
    callq irq7_handler
    jmp .done
.irq8:
    movq %rsp, %rdi
    callq irq8_handler
    jmp .done
.irq9:
    movq %rsp, %rdi
    callq irq9_handler
    jmp .done
.irq10:
    movq %rsp, %rdi
    callq irq10_handler
    jmp .done
.irq11:
    movq %rsp, %rdi
    callq irq11_handler
    jmp .done
.irq12:
    movq %rsp, %rdi
    callq irq12_handler
    jmp .done
.irq13:
    movq %rsp, %rdi
    callq irq13_handler
    jmp .done
.irq14:
    movq %rsp, %rdi
    callq irq14_handler
    jmp .done
.irq15:
    movq %rsp, %rdi
    callq irq15_handler
    jmp .done

.done:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp
    iretq

.section .note.GNU-stack,"",@progbits

/* ring3_entry.S - ring-3 entry, SYSCALL handler, fault stubs.
 *
 * Design references: Asher's Rust OS project
 * kernel/src/elf.rs jump_to_user_mode (iretq, CS 0x23 / SS 0x1B)
 * and the Linux SYSCALL/SYSRET ABI (STAR/LSTAR/FMASK). Ported
 * to C + asm here; no code copied verbatim, no profanity.
 *
 * Stack discipline, read before touching this file:
 * - ring3_enter_user() runs on the kernel stack. It saves the
 *   callee-saved registers, records RSP in g_ring3_kernel_rsp,
 *   builds an iretq frame (SS 0x1B, user RSP, RFLAGS with IF,
 *   CS 0x23, entry RIP) and iretqs into ring 3.
 * - The SYSCALL handler swaps RSP with g_ring3_kernel_rsp, so
 *   it runs on that same kernel stack, below the saved
 *   registers. On a normal return it swaps back and SYSRETs.
 *   On process exit it jumps to ring3_return, which pops the
 *   saved registers and returns to ring3_enter_user()'s caller.
 * - Faults from ring 3 switch to the TSS rsp0 stack, so the
 *   fault stubs run there; to kill the process they load
 *   g_ring3_kernel_rsp into RSP and jump to ring3_return.
 * - g_ring3_kernel_rsp therefore always means "the kernel
 *   stack pointer just above ring3_enter_user()'s saved
 *   registers" while a process runs, and "the user RSP" while
 *   a syscall is being served. Single CPU, single process:
 *   no nesting is possible (SYSCALL clears IF via FMASK).
 */
.code64
.globl g_ring3_kernel_rsp
.globl ring3_load_gdt
.globl ring3_enter_user
.globl ring3_return
.globl linux_syscall_entry
.globl ring3_fault_pf
.globl ring3_fault_gp

.section .bss
.align 8
g_ring3_kernel_rsp:
    .quad 0

.section .text

/* void ring3_load_gdt(void *gdtr);
 * Loads the new GDT and reloads CS to the kernel code segment
 * (0x08) via a far return, then reloads the data segments. */
.align 16
ring3_load_gdt:
    lgdt (%rdi)
    pushq $0x08
    leaq .Lgdt_reloaded(%rip), %rax
    pushq %rax
    lretq
.Lgdt_reloaded:
    movq $0x10, %rax
    movq %rax, %ds
    movq %rax, %es
    movq %rax, %fs
    movq %rax, %gs
    movq %rax, %ss
    retq

/* void ring3_enter_user(uint64_t entry, uint64_t user_rsp);
 * rdi = entry RIP, rsi = user RSP. Does not return until the
 * process exits or faults; the return value is 0. */
.align 16
ring3_enter_user:
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    movq %rsp, g_ring3_kernel_rsp(%rip)
    pushq $0x1B                 /* SS */
    pushq %rsi                 /* RSP (user stack) */
    pushfq
    popq %rax
    orq $0x200, %rax           /* RFLAGS with IF set */
    pushq %rax                 /* RFLAGS */
    pushq $0x23                 /* CS */
    pushq %rdi                 /* RIP = entry */
    /* Zero every user general-purpose register before IRETQ.
     * The frame above is already on the stack, so the registers
     * are free now. This matters: _start copies the entry RDX
     * into R9 (rtld_fini), and __libc_start_main registers a
     * non-zero rtld_fini as an atexit handler. A leftover
     * kernel value (e.g. 0x3f8, the COM1 port, from serial I/O)
     * in RDX made the process jump to 0x3f8 at exit. A real
     * kernel starts a process with a clean register state. */
    xorq %rax, %rax
    xorq %rbx, %rbx
    xorq %rcx, %rcx
    xorq %rdx, %rdx
    xorq %rsi, %rsi
    xorq %rdi, %rdi
    xorq %rbp, %rbp
    xorq %r8, %r8
    xorq %r9, %r9
    xorq %r10, %r10
    xorq %r11, %r11
    xorq %r12, %r12
    xorq %r13, %r13
    xorq %r14, %r14
    xorq %r15, %r15
    iretq
    /* Control never falls through here; the exit/fault paths
     * jump to ring3_return below. */

/* Shared return path: RSP points at the six saved registers
 * pushed by ring3_enter_user; below them is the return
 * address of the call to ring3_enter_user. */
.align 16
ring3_return:
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    xorl %eax, %eax
    retq

/* Linux SYSCALL entry (LSTAR). On entry: RCX = user RIP,
 * R11 = user RFLAGS, RSP = user RSP, CS/SS = kernel.
 * Linux args: rax = number, rdi/rsi/rdx/r10/r8/r9 = args.
 * C dispatch: linux_syscall_dispatch(num, a1..a6), where a6 is
 * passed on the stack (7th C argument). Result in rax.
 * g_linux_exited (set by the dispatcher on exit/exit_group)
 * selects the exit path instead of SYSRET. */
.align 16
linux_syscall_entry:
    /* Switch to the kernel stack; g_ring3_kernel_rsp now holds
     * the user RSP. This must be the first instruction: the
     * user stack is untrusted and must not be used. */
    xchgq g_ring3_kernel_rsp(%rip), %rsp
    /* Save every user register (RCX/R11 first: they hold the
     * SYSRET return address and flags, and the shuffle below
     * clobbers RCX). */
    pushq %rax
    pushq %rdi
    pushq %rsi
    pushq %rdx
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %rcx
    pushq %r11
    pushq %rbx
    pushq %rbp
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15
    /* Shuffle Linux args into the C calling convention. Order
     * matters: r8 -> r9 first so nothing is lost.
     *   rdi=num(rax) rsi=a1(rdi) rdx=a2(rsi) rcx=a3(rdx)
     *   r8=a4(r10) r9=a5(r8); a6 (user r9) goes on the stack. */
    movq %r8, %r9
    movq %r10, %r8
    movq %rdx, %rcx
    movq %rsi, %rdx
    movq %rdi, %rsi
    movq %rax, %rdi
    /* a6 = saved user r9, at 72(%rsp) in the saved registers.
     * The dummy goes on FIRST: the 7th C argument must be the
     * topmost stack slot at the call (8(%rsp) in the callee),
     * and the dummy only fixes alignment. Pushing a6 first
     * then the dummy put the dummy (0) in the argument slot,
     * so the dispatcher saw a6 = 0 for every syscall -- mmap
     * got file offset 0, which is why ld.so's mapped libc had
     * the dynamic section bytes from file offset 0x3b60 instead
     * of 0x1d2b60. After the dummy push the saved r9 is at
     * 80(%rsp). */
    pushq $0
    pushq 80(%rsp)
    callq linux_syscall_dispatch
    addq $16, %rsp
    /* Stash the result in the saved-rax slot (112(%rsp)) so
     * the pops below restore it into rax. */
    movq %rax, 112(%rsp)
    /* The exit decision is read from memory; pops do not
     * touch EFLAGS, so the branch after the pops is valid. */
    cmpl $0, g_linux_exited(%rip)
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbp
    popq %rbx
    popq %r11
    popq %rcx
    popq %r10
    popq %r9
    popq %r8
    popq %rdx
    popq %rsi
    popq %rdi
    popq %rax
    jne .Lsyscall_exit
    /* Normal return: RSP is the kernel stack again; swap back
     * to the user RSP and SYSRET (RCX = RIP, R11 = RFLAGS). */
    xchgq g_ring3_kernel_rsp(%rip), %rsp
    sysretq
.Lsyscall_exit:
    /* RSP points at ring3_enter_user()'s saved registers. */
    jmp ring3_return

/* Fault stubs. The CPU pushes an error code for #PF and #GP.
 * Each stub pushes its vector number so the common handler
 * sees a uniform frame: vector, error code, RIP, CS, RFLAGS
 * (plus SS/RSP when the fault came from ring 3). */
.align 16
ring3_fault_pf:
    pushq $14
    jmp ring3_fault_common
.align 16
ring3_fault_gp:
    pushq $13
    jmp ring3_fault_common

.align 16
ring3_fault_common:
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
    /* 15 pushes = 120 bytes. Frame offsets from RSP:
     *   120 vector, 128 error code, 136 RIP, 144 CS, 152 RFLAGS.
     * The saved registers are at RSP+0..119 (r15 first, rax
     * last); pass their base as a 6th argument so the handler
     * can dump them for diagnosis. */
    movq 120(%rsp), %rdi        /* vector */
    movq 128(%rsp), %rsi        /* error code */
    movq %cr2, %rdx             /* fault address */
    movq 144(%rsp), %rcx        /* faulting CS */
    movq 136(%rsp), %r8         /* faulting RIP */
    movq %rsp, %r9              /* saved registers base */
    callq ring3_fault_handler
    /* Returns 1 = kill the user process, 0 = resume. */
    testq %rax, %rax
    jnz .Lfault_kill
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
    addq $16, %rsp              /* drop vector + error code */
    iretq
.Lfault_kill:
    /* Run on the TSS rsp0 stack; switch to the process kernel
     * stack and return through ring3_return. */
    movq g_ring3_kernel_rsp(%rip), %rsp
    jmp ring3_return

.section .note.GNU-stack,"",@progbits

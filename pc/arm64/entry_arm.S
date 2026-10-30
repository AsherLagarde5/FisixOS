    .section .text
    .global _start
    .type _start, @function
_start:
    /* QEMU -kernel enters at EL1 (virt machine, cortex-a57).
     * Set up a stack, then call C. No EL2->EL1 drop, no MMU,
     * no exception vectors yet -- this is a skeleton only. */
    ldr x30, =stack_top
    mov sp, x30
    bl arm_kernel_main
.hang:
    wfi
    b .hang

    .section .bss
    .align 16
stack_bottom:
    .skip 65536
stack_top:

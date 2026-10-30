#include "idt.h"
#include <stdint.h>

/* IRQ handler context structure */
struct irq_context {
    uint64_t r15, r14, r13, r12, r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    uint64_t irq_number;
    uint64_t error_code;
} __attribute__((packed));

/* Generic IRQ handler dispatcher */
static void irq_handler_generic(struct irq_context *ctx)
{
    /* Under UEFI, direct interrupt handling is not implemented yet.
       This is a stub for future interrupt service routines. */
    (void)ctx;
}

/* IRQ handler stubs - called from assembly interrupt handlers */
void irq0_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq1_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq2_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq3_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq4_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq5_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq6_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq7_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq8_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq9_handler(struct irq_context *ctx)  { irq_handler_generic(ctx); }
void irq10_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }
void irq11_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }
void irq12_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }
void irq13_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }
void irq14_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }
void irq15_handler(struct irq_context *ctx) { irq_handler_generic(ctx); }

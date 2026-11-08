#include "idt.h"

#define IDT_ENTRIES 256

struct idt_entry64 idt[IDT_ENTRIES];
struct idt_ptr64 idtp;

extern void idt_load(uint64_t);
extern void irq0(void);
extern void irq1(void);
extern void irq2(void);
extern void irq3(void);
extern void irq4(void);
extern void irq5(void);
extern void irq6(void);
extern void irq7(void);
extern void irq8(void);
extern void irq9(void);
extern void irq10(void);
extern void irq11(void);
extern void irq12(void);
extern void irq13(void);
extern void irq14(void);
extern void irq15(void);

void idt_set_gate(int vec, uint64_t handler, uint16_t selector, uint8_t flags)
{
    if (vec < 0 || vec >= IDT_ENTRIES) {
        return;
    }
    if (handler == 0) {
        /* Never install a null gate: a stray call must not turn a
           vector into a jump to address 0. */
        return;
    }
    if (selector == 0) {
        /* A null segment selector would fault on interrupt
           delivery, so refuse a gate that can never run. */
        return;
    }
    if (flags == 0) {
        /* A zero type/attribute byte leaves the gate not present,
           so it would fault on delivery just like a null handler. */
        return;
    }
    idt[vec].offset_low = handler & 0xFFFF;
    idt[vec].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vec].offset_high = (handler >> 32) & 0xFFFFFFFF;

    idt[vec].selector = selector;
    idt[vec].ist = 0;
    idt[vec].type_attr = flags;
    idt[vec].zero = 0;
}

uint16_t idt_current_cs(void)
{
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    return cs;
}
void idt_init(void)
{
    uint16_t cs = idt_current_cs();
    /* Zero every gate first, so unused vectors are null, not stale. */
    for (int i = 0; i < IDT_ENTRIES; i++) {
        idt[i].offset_low = 0;
        idt[i].selector = 0;
        idt[i].ist = 0;
        idt[i].type_attr = 0;
        idt[i].offset_mid = 0;
        idt[i].offset_high = 0;
        idt[i].zero = 0;
    }
    /* Initialize the IDT structure */
    idtp.limit = (sizeof(struct idt_entry64) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt[0];
    
    /* IRQ handlers 0-15, in vector order: one loop instead of 16
       copy-pasted idt_set_gate() calls, so a future vector change
       cannot drift out of sync between the two lists. */
    static void (*const irq_handlers[16])(void) = {
        irq0, irq1, irq2, irq3, irq4, irq5, irq6, irq7,
        irq8, irq9, irq10, irq11, irq12, irq13, irq14, irq15
    };
    for (int i = 0; i < 16; i++) {
        idt_set_gate(i, (uint64_t)irq_handlers[i], cs, 0x8E);
    }
    
    /* Load the IDT */
    idt_load((uint64_t)&idtp);
}
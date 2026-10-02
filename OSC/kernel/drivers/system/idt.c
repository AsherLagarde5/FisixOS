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
    idt[vec].offset_low = handler & 0xFFFF;
    idt[vec].offset_mid = (handler >> 16) & 0xFFFF;
    idt[vec].offset_high = (handler >> 32) & 0xFFFFFFFF;

    idt[vec].selector = selector;
    idt[vec].ist = 0;
    idt[vec].type_attr = flags;
    idt[vec].zero = 0;
}

void idt_init(void)
{
    /* Initialize the IDT structure */
    idtp.limit = (sizeof(struct idt_entry64) * IDT_ENTRIES) - 1;
    idtp.base = (uint64_t)&idt[0];
    
    /* Set up IRQ handlers (0-15) */
    idt_set_gate(0, (uint64_t)irq0, 0x10, 0x8E);
    idt_set_gate(1, (uint64_t)irq1, 0x10, 0x8E);
    idt_set_gate(2, (uint64_t)irq2, 0x10, 0x8E);
    idt_set_gate(3, (uint64_t)irq3, 0x10, 0x8E);
    idt_set_gate(4, (uint64_t)irq4, 0x10, 0x8E);
    idt_set_gate(5, (uint64_t)irq5, 0x10, 0x8E);
    idt_set_gate(6, (uint64_t)irq6, 0x10, 0x8E);
    idt_set_gate(7, (uint64_t)irq7, 0x10, 0x8E);
    idt_set_gate(8, (uint64_t)irq8, 0x10, 0x8E);
    idt_set_gate(9, (uint64_t)irq9, 0x10, 0x8E);
    idt_set_gate(10, (uint64_t)irq10, 0x10, 0x8E);
    idt_set_gate(11, (uint64_t)irq11, 0x10, 0x8E);
    idt_set_gate(12, (uint64_t)irq12, 0x10, 0x8E);
    idt_set_gate(13, (uint64_t)irq13, 0x10, 0x8E);
    idt_set_gate(14, (uint64_t)irq14, 0x10, 0x8E);
    idt_set_gate(15, (uint64_t)irq15, 0x10, 0x8E);
    
    /* Load the IDT */
    idt_load((uint64_t)&idtp);
}
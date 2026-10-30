#include "pic.h"

static void outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

void pic_init(void)
{
    /* No PIC initialization in pure UEFI mode; interrupt controllers are
       managed by firmware (or by APIC/MSI later in the kernel). */
}

void pic_send_eoi(uint8_t irq)
{
    (void)irq; /* No-op under UEFI */
}

void pic_disable(void)
{
    /* The legacy PIC may still be routing IRQs after firmware exits. */
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}

#include "uart_pl011.h"
static volatile uint32_t *reg(uint64_t off) { return (volatile uint32_t *)(PL011_BASE + off); }
void uart_pl011_init(void) {
    /* QEMU virt PL011 is already usable without full init; clear
     * FIFOs / errors conservatively, do not enable interrupts. */
    *reg(0x44) = 0; /* IMSC: no UART interrupts */
    *reg(0x48) = 0xFF; /* ICR: clear pending */
}
void uart_pl011_putc(char c) {
    if (c == '\n') uart_pl011_putc('\r');
    while ((*reg(0x18) & (1u << 5)) != 0) { /* FR TXFF */ }
    *reg(0x00) = (uint32_t)c; /* DR */
}
void uart_pl011_puts(const char *s) { while (s && *s) uart_pl011_putc(*s++); }
void uart_pl011_put_hex64(uint64_t v) {
    static const char *d = "0123456789ABCDEF";
    char b[19]; int i;
    b[0] = '0'; b[1] = 'x';
    for (i = 0; i < 16; i++) b[2 + i] = d[(v >> (60 - 4 * i)) & 0xF];
    b[18] = '\0';
    uart_pl011_puts(b);
}

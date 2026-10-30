#ifndef UART_PL011_H
#define UART_PL011_H
#include <stdint.h>
#define PL011_BASE 0x09000000ULL
void uart_pl011_init(void);
void uart_pl011_putc(char c);
void uart_pl011_puts(const char *s);
void uart_pl011_put_hex64(uint64_t v);
#endif

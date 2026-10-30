#ifndef GENERIC_H
#define GENERIC_H
#include <stdint.h>
/* Generic, not-perfect, working drivers for common hardware.
 * Each one is polling-based and QEMU-testable, no IRQ plumbing
 * required. If a device is absent, the init returns 0, it does
 * not hang. */
typedef struct { int year, month, day, hour, minute, second; } rtc_time_t;
int generic_rtc_read(rtc_time_t *out);
void generic_uart_init(void);
void generic_uart_putc(char c);
void generic_uart_puts(const char *s);
void generic_speaker_beep(uint32_t freq_hz, uint32_t approx_ms);
int generic_ps2_mouse_init(void);
void generic_init_all(void);
#endif

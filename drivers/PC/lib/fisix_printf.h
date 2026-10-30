/* fisix_printf.h - FisixOS-original tiny snprintf.
 *
 * License: GPLv2 (see LICENSE at tree root).
 * Original FisixOS code, freestanding, no libc. Inspired in concept
 * by MIT-licensed tiny printf implementations for embedded systems
 * (e.g. mpaland/printf, MIT, https://github.com/mpaland/printf),
 * but NOT copied from them. Supports: %d %i %u %x %X %s %c %%,
 * %p as 0x + hex, and a simple numeric field width (e.g. %5d, %08x).
 * No floating point, no dynamic allocation, no libc printf.
 * Use where kernel code needs formatted text without pulling in
 * newlib / libc.
 */
#ifndef FISIX_PRINTF_H
#define FISIX_PRINTF_H
#include <stdarg.h>
#include <stddef.h>
int fisix_vsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int fisix_snprintf(char *buf, size_t size, const char *fmt, ...);
#endif

#include "ps2kbd.h"
#include <stdint.h>
static int g_present = 0;
static void outb(uint16_t port, uint8_t v) { __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port)); }
static uint8_t inb(uint16_t port) { uint8_t v; __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port)); return v; }
static int wait_input_empty(void) { uint32_t t = 100000; while (t--) { if ((inb(0x64) & 0x02) == 0) return 1; } return 0; }
static int wait_output_full(void) { uint32_t t = 100000; while (t--) { if (inb(0x64) & 0x01) return 1; } return 0; }
int ps2kbd_init(void)
{
    uint8_t r;
    g_present = 0;
    if (!wait_input_empty()) return 0;
    outb(0x64, 0xAD); /* disable port 1 */
    if (!wait_input_empty()) return 0;
    outb(0x64, 0xA7); /* disable port 2 */
    while (inb(0x64) & 0x01) (void)inb(0x60); /* flush output */
    if (!wait_input_empty()) return 0;
    outb(0x64, 0xAA); /* controller self-test */
    if (!wait_output_full()) return 0;
    r = inb(0x60);
    if (r != 0x55) return 0;
    if (!wait_input_empty()) return 0;
    outb(0x64, 0xAE); /* enable port 1 */
    g_present = 1;
    return 1;
}
int ps2kbd_present(void) { return g_present; }

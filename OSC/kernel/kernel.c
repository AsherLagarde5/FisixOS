#include <stdint.h>
#include "boot_info.h"
#include "cpu.h"
#include "drivers/memory.h"
#include "drivers/acpi.h"
#include "drivers/system/pic.h"
#include "drivers/system/idt.h"

/* Serial debug output on COM1 (0x3F8), for early kernel bring-up. */
static void debug_outb(uint16_t port, uint8_t value) {
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}

static void debug_print(const char *s) {
    while (s && *s) {
        /* Wait for transmitter holding register to be empty (LSR bit 5),
           but give up instead of hanging forever if there is no COM1. */
        uint8_t lsr = 0;
        uint32_t timeout = 100000;
        do {
            __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5)));
            if (timeout-- == 0) {
                return;
            }
        } while ((lsr & 0x20) == 0);
        debug_outb(0x3F8, (uint8_t)*s);
        s++;
    }
}

static void debug_print_u64(uint64_t value) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (value == 0) {
        debug_print("0");
        return;
    }
    while (value > 0 && i > 0) {
        buf[--i] = (char)('0' + (value % 10));
        value /= 10;
    }
    debug_print(&buf[i]);
}

static void debug_print_u32(uint32_t value) {
    debug_print_u64(value);
}

static void debug_init_serial(void) {
    /* Disable interrupts */
    debug_outb(0x3F8 + 1, 0x00);

    /* Enable DLAB */
    debug_outb(0x3F8 + 3, 0x80);

    /* Set baud divisor to 3 -> 38400 baud */
    debug_outb(0x3F8 + 0, 0x03);
    debug_outb(0x3F8 + 1, 0x00);

    /* 8 bits, no parity, one stop */
    debug_outb(0x3F8 + 3, 0x03);

    /* Enable FIFO, clear, 14-byte threshold */
    debug_outb(0x3F8 + 2, 0xC7);

    /* IRQs enabled, RTS/DSR set */
    debug_outb(0x3F8 + 4, 0x0B);
}

/* --- MWAHAHAHA IM A DINOSAUR --- */

void kernel_startup(const kernel_boot_info_t *boot_info)
{
    __asm__ volatile ("cli");

    debug_init_serial();
    debug_print("[FisixOS] Serial initialized\n");

    if (!boot_info) {
        debug_print("[FisixOS] ERROR: boot_info is NULL! Halting.\n");
        goto halt;
    }

    debug_print("[FisixOS] Boot info OK\n");

    /* Checkpoint 1: Memory bytes */
    debug_print("[FisixOS] memory_bytes = ");
    debug_print_u64(boot_info->memory_bytes);
    debug_print("\n");

    memory_set_total_bytes(boot_info->memory_bytes);
    debug_print("[FisixOS] memory_set_total_bytes OK\n");

    acpi_set_rsdp(boot_info->rsdp);
    debug_print("[FisixOS] acpi_set_rsdp OK, acpi_available = ");
    debug_print_u32((uint32_t)acpi_is_available());
    debug_print("\n");

    /* Checkpoint 2: memory_init */
    debug_print("[FisixOS] Calling memory_init...\n");
    memory_init();
    debug_print("[FisixOS] memory_init OK\n");

    /* Checkpoint 3: acpi_init */
    debug_print("[FisixOS] Calling acpi_init...\n");
    acpi_init();
    debug_print("[FisixOS] acpi_init OK\n");

    /* Checkpoint 4: cpu_init + pic_disable */
    debug_print("[FisixOS] Calling cpu_init...\n");
    cpu_init();
    {
        cpu_info_t cpu;
        if (cpu_get_info(&cpu)) {
            debug_print("[FisixOS] cpu vendor = ");
            debug_print_u32(cpu.vendor);
            debug_print(", family = ");
            debug_print_u32(cpu.family);
            debug_print(", model = ");
            debug_print_u32(cpu.model);
            debug_print(", stepping = ");
            debug_print_u32(cpu.stepping);
            debug_print("\n");
        } else {
            debug_print("[FisixOS] cpu_get_info failed\n");
        }
    }
    debug_print("[FisixOS] cpu_init OK\n");

    debug_print("[FisixOS] Disabling PIC...\n");
    pic_disable();
    debug_print("[FisixOS] PIC disabled\n");

    /* Checkpoint 5: IDT */
    debug_print("[FisixOS] Calling idt_init...\n");
    idt_init();
    debug_print("[FisixOS] idt_init OK\n");

    debug_print("[FisixOS] Kernel early setup complete. Entering idle loop.\n");

halt:
    while (1) {
        __asm__ volatile ("hlt");
    }
}

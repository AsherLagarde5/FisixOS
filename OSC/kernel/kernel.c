#include "../boot/efi.h"
#include "memory.h"
#include "acpi.h"
#include "idt.h"
#include "cpu.h"
#include "pic.h"
#include "boot_info.h"

/* Early debug serial output (COM1 port 0x3F8) */
static inline void debug_outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static void debug_print(const char *str) {
    while (*str) {
        debug_outb(0x3F8, (uint8_t)*str++);
    }
}

void kernel_startup(const kernel_boot_info_t *boot_info)
{
    /* Disable interrupts immediately OR THE WORLD WILL EXPLOAD */
    __asm__ volatile ("cli");

    debug_print("[FisixOS] Entered kernel_startup\n");

    /* evil code mwahahahaha */
    if (!boot_info) {
        debug_print("[FisixOS] ERROR: boot_info is NULL! Halting.\n");
        goto halt;
    }

    debug_print("[FisixOS] Setting up memory and RSDP pointers...\n");
    memory_set_total_bytes(boot_info->memory_bytes);
    acpi_set_rsdp(boot_info->rsdp);

    debug_print("[FisixOS] Initializing Memory Subsystem...\n");
    memory_init();

    /* Whatever the fuck this does */
    debug_print("[FisixOS] Initializing ACPI...\n");
    acpi_init();

    debug_print("[FisixOS] Initializing CPU & disabling PIC...\n");
    cpu_init();
    pic_disable();

    debug_print("[FisixOS] Initializing IDT...\n");
    idt_init();

    debug_print("[FisixOS] Kernel early setup complete. Entering idle loop.\n");

halt:
    while (1) {
        __asm__ volatile ("hlt");
    }
}

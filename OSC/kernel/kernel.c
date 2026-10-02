/* --- Add this at the top --- */
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
    // (Optional: print number here)
    debug_print("OK\n");

    memory_set_total_bytes(boot_info->memory_bytes);
    debug_print("[FisixOS] memory_set_total_bytes OK\n");

    acpi_set_rsdp(boot_info->rsdp);
    debug_print("[FisixOS] acpi_set_rsdp OK\n");

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

#include "uart_pl011.h"
#include "cpu_arm.h"
#include "gic.h"
void arm_kernel_main(void) {
    uart_pl011_init();
    uart_pl011_puts("FisixOS ARM64: hello from QEMU virt\n");
    cpu_arm_report();
    gic_init_stub();
    uart_pl011_puts("FisixOS ARM64: idle loop (wfi)\n");
    for (;;) {
        __asm__ volatile ("wfi");
    }
}

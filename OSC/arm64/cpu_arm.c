#include "cpu_arm.h"
#include "uart_pl011.h"
uint64_t cpu_arm_read_midr(void) {
    uint64_t v;
    __asm__ volatile ("mrs %0, MIDR_EL1" : "=r"(v));
    return v;
}
void cpu_arm_report(void) {
    uint64_t midr = cpu_arm_read_midr();
    uint32_t implementer = (uint32_t)((midr >> 24) & 0xFF);
    uint32_t part = (uint32_t)((midr >> 4) & 0xFFF);
    uart_pl011_puts("ARM64 MIDR_EL1 = ");
    uart_pl011_put_hex64(midr);
    uart_pl011_puts("\nARM64 implementer = ");
    uart_pl011_put_hex64(implementer);
    uart_pl011_puts(" part = ");
    uart_pl011_put_hex64(part);
    uart_pl011_puts("\n");
}

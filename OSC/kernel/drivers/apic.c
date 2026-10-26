#include "apic.h"
static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
int apic_detect(apic_info_t *out)
{
    uint64_t base_msr;
    if (!out) return 0;
    base_msr = rdmsr(0x1B);
    out->base = base_msr & 0xFFFFF000ULL;
    out->enabled = (base_msr & 0x800) != 0;
    if (!out->base) { out->version = 0; return 0; }
    out->version = *(volatile uint32_t *)(uintptr_t)(out->base + 0x30);
    return 1;
}

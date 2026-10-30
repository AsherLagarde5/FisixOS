#include "apic.h"
static uint64_t rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static uint64_t apic_base_addr(void)
{
    uint64_t msr = rdmsr(0x1B);
    return msr & 0xFFFFF000ULL;
}
int apic_timer_init(void)
{
    uint64_t base = apic_base_addr();
    volatile uint32_t *regs;
    if (!base) return 0;
    regs = (volatile uint32_t *)(uintptr_t)base;
    regs[0x3E0 / 4] = 0x3u; /* divide configuration: divide by 16 */
    regs[0x320 / 4] = 0x10000u; /* LVT timer: masked, vector 0 */
    regs[0x380 / 4] = 0xFFFFFFFFu; /* initial count, one-shot */
    return 1;
}
uint32_t apic_timer_current(void)
{
    uint64_t base = apic_base_addr();
    if (!base) return 0;
    return *(volatile uint32_t *)(uintptr_t)(base + 0x390);
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

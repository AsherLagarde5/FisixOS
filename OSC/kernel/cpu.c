#include "cpu.h"

static cpu_info_t g_cpu_info = {
    .vendor = CPU_VENDOR_UNKNOWN,
    .family = 0,
    .model = 0,
    .stepping = 0,
    .features_ecx = 0,
    .features_edx = 0
};

static void cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile (
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf)
    );
}

int cpu_get_info(cpu_info_t *info)
{
    uint32_t eax, ebx, ecx, edx;
    uint32_t vendor_b, vendor_c, vendor_d;
    uint32_t family, model, stepping;

    if (info == 0) {
        return 0;
    }

    cpuid(0, &eax, &ebx, &ecx, &edx);
    vendor_b = ebx;
    vendor_c = ecx;
    vendor_d = edx;

    if (vendor_b == 0x756e6547u && vendor_d == 0x49656e69u && vendor_c == 0x6c65746eu) {
        g_cpu_info.vendor = CPU_VENDOR_INTEL;
    } else if (vendor_b == 0x68747541u && vendor_d == 0x69746e65u && vendor_c == 0x444d4163u) {
        g_cpu_info.vendor = CPU_VENDOR_AMD;
    } else {
        g_cpu_info.vendor = CPU_VENDOR_UNKNOWN;
    }

    /* Leaf 1 is only valid if the CPU reports it. */
    if (eax < 1) {
        return 0;
    }

    cpuid(1, &eax, &ebx, &ecx, &edx);
    stepping = eax & 0xFU;
    model = (eax >> 4) & 0xFU;
    family = (eax >> 8) & 0xFU;
    /* Extended family/model, per the Intel/AMD CPUID spec:
       family 0xF adds the extended family, and family 0x6/0xF
       add the extended model shifted by 4. Without this, modern
       CPUs all report the same truncated family/model. */
    if (family == 0x6U || family == 0xFU) {
        model += ((eax >> 16) & 0xFU) << 4;
    }
    if (family == 0xFU) {
        family += (eax >> 20) & 0xFFU;
    }

    g_cpu_info.family = family;
    g_cpu_info.model = model;
    g_cpu_info.stepping = stepping;
    g_cpu_info.features_ecx = ecx;
    g_cpu_info.features_edx = edx;

    *info = g_cpu_info;
    return 1;
}

int cpu_is_intel(void)
{
    return g_cpu_info.vendor == CPU_VENDOR_INTEL;
}

void cpu_init(void)
{
    cpu_info_t info;

    cpu_get_info(&info);

    if (info.vendor == CPU_VENDOR_INTEL) {
        /* Intel vendor-specific setup hooks can be added here later. */
    }
}

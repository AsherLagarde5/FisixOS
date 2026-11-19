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
    /* Brand string, CPUID 0x80000002-0x80000004, 48 bytes + NUL.
       This is what actually says "11th Gen Intel(R) Core(TM) i5-1145G7",
       rather than just family/model numbers. Only if the CPU reports it. */
    {
        uint32_t max_ext, a, b, c, d;
        cpuid(0x80000000, &max_ext, &b, &c, &d);
        if (max_ext >= 0x80000004) {
            uint32_t *out = (uint32_t *)g_cpu_info.brand;
            cpuid(0x80000002, &a, &b, &c, &d); out[0]=a; out[1]=b; out[2]=c; out[3]=d;
            cpuid(0x80000003, &a, &b, &c, &d); out[4]=a; out[5]=b; out[6]=c; out[7]=d;
            cpuid(0x80000004, &a, &b, &c, &d); out[8]=a; out[9]=b; out[10]=c; out[11]=d;
            g_cpu_info.brand[48] = 0;
        } else {
            g_cpu_info.brand[0] = 0;
        }
    }

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

int cpu_has_sse(void) { return (g_cpu_info.features_edx & (1u << 25)) != 0; }
int cpu_has_sse2(void) { return (g_cpu_info.features_edx & (1u << 26)) != 0; }
int cpu_has_avx(void) { return (g_cpu_info.features_ecx & (1u << 28)) != 0; }
int cpu_has_avx2(void) { return 0; /* leaf 7 not stored in cpu_info_t yet; amd_cpu.c checks leaf 7 directly. Do not assume AVX2. */ }

int cpu_is_tiger_lake(void)
{
    /* Tiger Lake (11th Gen, including i5-1145G7): Intel, family 6, model 140 (0x8C).
       i5-1145G7 is a Tiger Lake-UP3 part: 4 cores, 8 threads, Iris Xe GT2. */
    return g_cpu_info.vendor == CPU_VENDOR_INTEL && g_cpu_info.family == 6 && g_cpu_info.model == 140;
}

const char *cpu_brand(void)
{
    return g_cpu_info.brand;
}

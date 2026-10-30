#include "amd_cpu.h"

static void amd_cpuid(uint32_t leaf, uint32_t *eax, uint32_t *ebx,
                      uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf));
}
static void amd_cpuid_sub(uint32_t leaf, uint32_t sub,
                          uint32_t *eax, uint32_t *ebx,
                          uint32_t *ecx, uint32_t *edx)
{
    __asm__ volatile ("cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(sub));
}

const char *amd_generation_name(uint32_t family, uint32_t model)
{
    if (family == 23) {
        /* Family 17h: Zen (early models), Zen+ (mid), Zen 2 (0x30+).
           Heuristic only — check amd_cpu_info_t.model for the exact SKU. */
        if (model >= 0x30) return "Zen 2 (family 23, model 0x30+)";
        if (model >= 0x10) return "Zen+ / Zen (family 23, model 0x10+)";
        return "Zen (family 23)";
    }
    if (family == 25) {
        /* Family 19h: Zen 3 (early models) / Zen 4 (0x60+ range). */
        if (model >= 0x60) return "Zen 4 (family 25, model 0x60+)";
        return "Zen 3 (family 25)";
    }
    if (family == 26) return "Zen 5 (family 26)";
    return "Unknown AMD family";
}

int amd_cpu_detect(amd_cpu_info_t *out)
{
    uint32_t eax, ebx, ecx, edx, max_leaf;
    uint32_t family, model, stepping;
    amd_cpu_info_t info;
    int i;
    if (!out) return 0;
    for (i = 0; i < 49; i++) info.brand[i] = 0;
    info.present = 0; info.family = 0; info.model = 0; info.stepping = 0;
    info.svm_present = 0; info.tsc_present = 0; info.sse_present = 0;
    info.sse2_present = 0; info.avx_present = 0; info.avx2_present = 0;

    amd_cpuid(0, &max_leaf, &ebx, &ecx, &edx);
    /* Vendor string is EBX, EDX, ECX = "AuthenticAMD". */
    if (!(ebx == 0x68747541u && edx == 0x69746e65u && ecx == 0x444d4163u))
        { *out = info; return 0; }
    info.present = 1;
    if (max_leaf < 1) { *out = info; return 1; }

    amd_cpuid(1, &eax, &ebx, &ecx, &edx);
    stepping = eax & 0xFu;
    model = (eax >> 4) & 0xFu;
    family = (eax >> 8) & 0xFu;
    if (family == 0x6u || family == 0xFu)
        model += ((eax >> 16) & 0xFu) << 4;
    if (family == 0xFu)
        family += (eax >> 20) & 0xFFu;
    info.family = family; info.model = model; info.stepping = stepping;
    info.tsc_present = (edx & (1u << 4)) != 0;
    info.sse_present = (edx & (1u << 25)) != 0;
    info.sse2_present = (edx & (1u << 26)) != 0;
    info.avx_present = (ecx & (1u << 28)) != 0;
    if (max_leaf >= 7) {
        amd_cpuid_sub(7, 0, &eax, &ebx, &ecx, &edx);
        info.avx2_present = (ebx & (1u << 5)) != 0;
    }
    /* SVM: CPUID 0x80000001 ECX bit 2. Present only — never enabled here. */
    amd_cpuid(0x80000000, &eax, &ebx, &ecx, &edx);
    if (eax >= 0x80000001) {
        amd_cpuid(0x80000001, &eax, &ebx, &ecx, &edx);
        info.svm_present = (ecx & (1u << 2)) != 0;
        /* Brand string, only if the extended leaf range exists. */
        {
            uint32_t max_ext, a, b, c, d;
            amd_cpuid(0x80000000, &max_ext, &b, &c, &d);
            if (max_ext >= 0x80000004) {
                uint32_t *o = (uint32_t *)info.brand;
                amd_cpuid(0x80000002, &a, &b, &c, &d);
                o[0]=a; o[1]=b; o[2]=c; o[3]=d;
                amd_cpuid(0x80000003, &a, &b, &c, &d);
                o[4]=a; o[5]=b; o[6]=c; o[7]=d;
                amd_cpuid(0x80000004, &a, &b, &c, &d);
                o[8]=a; o[9]=b; o[10]=c; o[11]=d;
                info.brand[48] = 0;
            }
        }
    }
    *out = info;
    return 1;
}

int amd_cpu_is_present(void)
{
    amd_cpu_info_t info;
    return amd_cpu_detect(&info);
}

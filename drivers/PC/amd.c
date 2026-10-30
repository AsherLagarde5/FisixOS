#include "amd.h"

int amd_init(amd_system_t *out)
{
    if (!out) return 0;
    amd_cpu_detect(&out->cpu);
    amd_gpu_detect(&out->gpu);
    amd_chipset_detect(&out->chipset);
    out->amd_present = out->cpu.present || out->gpu.present ||
                       out->chipset.present;
    out->intel_fallback = !out->amd_present;
    return out->amd_present;
}

int amd_is_present(void)
{
    amd_system_t sys;
    return amd_init(&sys);
}

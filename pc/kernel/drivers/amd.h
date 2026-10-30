#ifndef AMD_H
#define AMD_H

#include "amd_cpu.h"
#include "amd_gpu.h"
#include "amd_chipset.h"

/* AMD integration hook — called from PCI enumeration / kernel boot
   logging, NOT from a driver init that changes hardware state.
   STATUS (2026-10-30): DETECTION ONLY, UNTESTED ON REAL AMD HARDWARE.
   If amd_present is 1, log "AMD detected" and use the AMD detection
   structs above; display still goes via UEFI GOP, storage via generic
   NVMe, USB via generic xHCI. If amd_present is 0, fall back to the
   existing Intel paths (Latitude 5420 / Tiger Lake / Iris Xe / e1000).
   Nothing here enables SVM, AMD-Vi, or GPU acceleration. */
typedef struct {
    amd_cpu_info_t cpu;
    amd_gpu_info_t gpu;
    amd_chipset_info_t chipset;
    int amd_present;
    int intel_fallback; /* 1 when no AMD CPU/GPU/chipset was found */
} amd_system_t;

int amd_init(amd_system_t *out);
int amd_is_present(void);

#endif

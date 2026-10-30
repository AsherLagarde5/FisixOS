#ifndef AMD_GPU_H
#define AMD_GPU_H

#include <stdint.h>

/* AMD GPU — Radeon, PCI vendor 0x1002.
   STATUS (2026-10-30): DETECTION + UEFI GOP FRAMEBUFFER FALLBACK ONLY,
   UNTESTED ON REAL AMD GPU HARDWARE.
   This is NOT a full amdgpu driver. No acceleration, no command
   submission, no power management is claimed. What is real:
   - PCI detection of a vendor-1002 display device (class 0x03)
   - BAR0 readout for logging
   - The UEFI GOP framebuffer (boot_info->framebuffer_*, set by
     efi_main.c) is the display path on any UEFI AMD system, Intel
     Iris Xe included. That GOP path works without an AMD GPU driver.
   Full AMD GPU acceleration = TODO, needs real AMD GPU testing. */
typedef struct {
    int present;
    uint16_t device_id;
    uint64_t bar0;
    int gop_fallback;   /* 1: display via UEFI GOP framebuffer */
    int accelerated;    /* always 0 in this version */
} amd_gpu_info_t;

int amd_gpu_detect(amd_gpu_info_t *out);
int amd_gpu_is_present(void);

#endif

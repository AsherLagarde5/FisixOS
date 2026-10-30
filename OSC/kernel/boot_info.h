#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H
#include <stdint.h>
typedef struct {
    uint64_t memory_bytes;
    void *rsdp;
    /* GOP framebuffer, if the loader found one. 0 = no GOP mode found.
       This is the UEFI framebuffer the kernel can draw to later;
       it is NOT a full Iris Xe driver, it is the firmware's framebuffer. */
    uint64_t framebuffer_base;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint32_t framebuffer_pitch;
    /* SMBIOS table pointer, if the loader found one in the UEFI
       configuration table. 0 = not found. Kernel parses Type 0 (BIOS). */
    void *smbios;
    /* 2026-10-30: 1 = user chose 'Without Wi-Fi' at the
       after-login prompt. Kernel must skip generic Wi-Fi
       detect entirely (it was hanging at GEN 8 on the
       Latitude). 0 = With Wi-Fi, detect as before. */
    uint32_t no_wifi;
    /* 2026-10-30: 1 = RAM Mode. Kernel + embedded .fse are
       already in RAM, nothing is saved, and the install drive
       may be removed after the GUI appears. Gone on reboot,
       because RAM is cleared. Set by the loader menu. */
    uint32_t ram_mode;
} kernel_boot_info_t;
#endif

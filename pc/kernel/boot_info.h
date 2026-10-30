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
} kernel_boot_info_t;
#endif

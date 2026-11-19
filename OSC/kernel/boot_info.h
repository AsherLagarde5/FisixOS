#ifndef KERNEL_BOOT_INFO_H
#define KERNEL_BOOT_INFO_H
#include <stdint.h>
/* UEFI memory types, copied here so the kernel does not need the
 * UEFI headers to read the map. Values are the EFI_MEMORY_TYPE
 * numbers (EfiConventionalMemory = 7). */
#define KERNEL_MEM_RESERVED 0
#define KERNEL_MEM_LOADER_CODE 1
#define KERNEL_MEM_LOADER_DATA 2
#define KERNEL_MEM_BOOT_CODE 3
#define KERNEL_MEM_BOOT_DATA 4
#define KERNEL_MEM_RUNTIME_CODE 5
#define KERNEL_MEM_RUNTIME_DATA 6
#define KERNEL_MEM_CONVENTIONAL 7
#define KERNEL_MEM_UNUSABLE 8
#define KERNEL_MEM_ACPI_RECLAIM 9
#define KERNEL_MEM_ACPI_NVS 10

/* One UEFI memory-map descriptor, trimmed to what the kernel
 * frame allocator needs. Filled by the EFI loader in
 * efi_leave_boot_services(), before ExitBootServices(). */
typedef struct {
    uint64_t base;   /* physical start address */
    uint64_t pages;  /* length in 4 KiB pages */
    uint32_t type;   /* KERNEL_MEM_* above */
} kernel_mem_range_t;

#define KERNEL_MAX_MEM_RANGES 32

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
    /* UEFI memory map, copied by the loader before
       ExitBootServices(). The ring-3 frame allocator
       (kernel/drivers/ring3.c) only hands out pages from
       KERNEL_MEM_CONVENTIONAL ranges. If the loader is too old
       to fill this in, mem_range_count is 0 and the allocator
       refuses to run. */
    kernel_mem_range_t mem_ranges[KERNEL_MAX_MEM_RANGES];
    uint32_t mem_range_count;
} kernel_boot_info_t;
#endif

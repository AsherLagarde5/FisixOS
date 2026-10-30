#ifndef GENERIC_STORAGE_H
#define GENERIC_STORAGE_H
#include <stdint.h>
#include "pci.h"
/* Class-based storage, not model-based. Priority: NVMe > AHCI SATA > IDE
 * > USB Mass Storage (xHCI controller present) > UEFI fallback.
 * Detection only here: the real NVMe init lives in nvme.c and is run
 * by the kernel's existing path, so this file never double-inits a
 * controller. AHCI read/write and IDE DMA are TODO; for those, the
 * fallback is UEFI block I/O / FAT in the loader, and boot continues
 * with a logged fallback, not a halt. USB kind here means an xHCI
 * controller was found — a mass-storage device itself is only proven
 * by usb_storage_init() (BOT, after xhci_init()), see usb_storage.h. */
#define GENERIC_STORAGE_NONE 0
#define GENERIC_STORAGE_NVME 1
#define GENERIC_STORAGE_AHCI 2
#define GENERIC_STORAGE_IDE 3
#define GENERIC_STORAGE_UEFI_FALLBACK 4
#define GENERIC_STORAGE_USB 5
typedef struct { int kind; pci_device_t pci; int found; int uefi_fallback; int usb_controller_present; } generic_storage_t;
int generic_storage_detect(generic_storage_t *out);
const char *generic_storage_kind_name(int kind);
#endif

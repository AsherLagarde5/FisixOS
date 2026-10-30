#ifndef AMD_CHIPSET_H
#define AMD_CHIPSET_H

#include <stdint.h>

/* AMD chipset — PCI vendor 0x1022.
   STATUS (2026-10-30): DETECTION ONLY, UNTESTED ON REAL AMD HARDWARE.
   - SMBus (class 0x0C, subclass 0x05), SATA (0x01/0x06), USB xHCI
     (0x0C/0x03/0x30), NVMe (0x01/0x08) are detected by PCI class.
   - Storage/USB themselves still use the generic drivers already in
     FisixOS: nvme.c for NVMe, xhci.c for xHCI. This file does not
     duplicate them, it only says "this AMD box has them".
   - AMD-Vi IOMMU: detection is NOT claimed. AMD-Vi is described by
     the ACPI IVRS table, not by a PCI class, and IVRS parsing is
     TODO. iommu_detected stays 0 and AMD-Vi is never enabled here. */
typedef struct {
    int present;            /* any vendor-1022 PCI device found */
    int device_count;       /* number of vendor-1022 PCI devices */
    int smbus_present;
    int sata_present;
    int usb_xhci_present;
    int nvme_present;       /* use generic nvme.c, not an AMD NVMe driver */
    int iommu_detected;     /* always 0 in this version — IVRS TODO */
} amd_chipset_info_t;

int amd_chipset_detect(amd_chipset_info_t *out);
int amd_chipset_is_present(void);

#endif

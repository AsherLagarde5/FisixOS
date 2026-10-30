#include "amd_chipset.h"
#include "pci.h"

int amd_chipset_detect(amd_chipset_info_t *out)
{
    pci_device_t list[256];
    int count = 0, i;
    if (!out) return 0;
    out->present = 0; out->device_count = 0; out->smbus_present = 0;
    out->sata_present = 0; out->usb_xhci_present = 0; out->nvme_present = 0;
    out->iommu_detected = 0; /* IVRS parsing TODO, never enabled */
    if (!pci_scan_all(list, 256, &count)) return 0;
    if (count > 256) count = 256; /* pci_scan_all reports total, list holds 256 */
    for (i = 0; i < count; i++) {
        if (list[i].vendor_id != 0x1022u) continue;
        out->present = 1;
        out->device_count++;
        if (list[i].class_code == 0x0Cu && list[i].subclass == 0x05u)
            out->smbus_present = 1;
        if (list[i].class_code == 0x01u && list[i].subclass == 0x06u)
            out->sata_present = 1;
        if (list[i].class_code == 0x0Cu && list[i].subclass == 0x03u &&
            list[i].prog_if == 0x30u)
            out->usb_xhci_present = 1;
        if (list[i].class_code == 0x01u && list[i].subclass == 0x08u)
            out->nvme_present = 1;
    }
    return out->present;
}

int amd_chipset_is_present(void)
{
    amd_chipset_info_t info;
    return amd_chipset_detect(&info);
}

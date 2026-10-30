#include "amd_gpu.h"
#include "pci.h"

int amd_gpu_detect(amd_gpu_info_t *out)
{
    pci_device_t list[256];
    int count = 0, i;
    if (!out) return 0;
    out->present = 0; out->device_id = 0; out->bar0 = 0;
    out->gop_fallback = 1; out->accelerated = 0;
    if (!pci_scan_all(list, 256, &count)) return 0;
    if (count > 256) count = 256; /* pci_scan_all reports total, list holds 256 */
    for (i = 0; i < count; i++) {
        if (list[i].vendor_id == 0x1002u && list[i].class_code == 0x03u) {
            out->present = 1;
            out->device_id = list[i].device_id;
            out->bar0 = pci_read_bar64(&list[i], 0);
            out->gop_fallback = 1;
            out->accelerated = 0;
            return 1;
        }
    }
    return 0;
}

int amd_gpu_is_present(void)
{
    amd_gpu_info_t info;
    return amd_gpu_detect(&info);
}

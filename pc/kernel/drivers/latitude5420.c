#include "latitude5420.h"
#include "pci.h"
#include "acpi.h"
#include "cpu.h"
#include "apic.h"
#include "nvme.h"
#include "xhci.h"
#include "ps2kbd.h"
#include "smbios.h"
void latitude5420_check(latitude5420_boot_t *out)
{
    pci_device_t dev;
    apic_info_t apic; nvme_info_t nv; xhci_info_t xh;
    if (!out) return;
    out->cpu_tiger_lake = cpu_is_tiger_lake();
    out->acpi = acpi_is_available();
    out->pci_count = 0;
    {
        pci_device_t list[64]; int count = 0;
        pci_scan_all(list, 64, &count);
        out->pci_count = count;
    }
    out->apic = apic_detect(&apic);
    out->gop = 0; /* filled by kernel from boot_info, see kernel.c */
    out->ps2kbd = ps2kbd_present();
    out->nvme = nvme_detect(&nv);
    out->xhci = xhci_detect(&xh);
    /* Intel Serial IO I2C: PCI class 0x0C, subclass 0x80.
       Do NOT count USB (0x0C/0x03) as Serial IO. */
    out->serial_io = 0;
    {
        pci_device_t list[64]; int count = 0, i;
        pci_scan_all(list, 64, &count);
        for (i = 0; i < count && i < 64; i++) {
            if (list[i].class_code == 0x0C && list[i].subclass == 0x80) { out->serial_io = 1; break; }
        }
    }
    out->ethernet = pci_find_device_class(0x02, &dev) != 0;
    /* Wi-Fi AX201 is CNVi, not a plain PCI network device on this model;
       the PCI network check above covers Ethernet, Wi-Fi needs CNVi
       enumeration separately and is NOT claimed here. */
    out->wifi = 0;
    out->smbios = smbios_is_available();
}

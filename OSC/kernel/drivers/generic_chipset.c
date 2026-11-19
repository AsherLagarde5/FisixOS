#include "generic_chipset.h"
#include "pci.h"
#include "acpi.h"
#include "smbios.h"
int generic_chipset_detect(generic_chipset_t *out){
    pci_device_t list[64]; int count=0;
    if(!out) return 0;
    pci_scan_all(list,64,&count);
    out->pci_count=count; out->acpi=acpi_is_available() ? 1 : 0; out->smbios=smbios_is_available() ? 1 : 0;
    return 1;
}

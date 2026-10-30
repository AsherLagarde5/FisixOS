#ifndef GENERIC_CHIPSET_H
#define GENERIC_CHIPSET_H
#include <stdint.h>
/* Generic chipset: PCI CF8/CFC enumeration, ACPI RSDP, SMBIOS.
 * No Latitude-5420-specific code in this path. If ACPI/SMBIOS are
 * absent, boot continues with defaults; they are info, not blockers. */
typedef struct { int pci_count; int acpi; int smbios; } generic_chipset_t;
int generic_chipset_detect(generic_chipset_t *out);
#endif

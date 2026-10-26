#ifndef SMBIOS_H
#define SMBIOS_H
#include <stdint.h>
/* SMBIOS / "BIOS driver", step 1: read-only BIOS information.
   The BIOS itself (e.g. Dell firmware) is proprietary firmware and
   is NOT replaced or rewritten by this. What an OS driver can do is
   read the SMBIOS table the firmware already publishes: Type 0
   (BIOS Information: vendor, version, release date) and Type 1
   (System Information). The loader passes the table pointer in
   boot_info->smbios; this parses it without writing to firmware. */
void smbios_set_table(void *table);
const char *smbios_bios_vendor(void);
const char *smbios_bios_version(void);
const char *smbios_bios_date(void);
int smbios_is_available(void);
#endif

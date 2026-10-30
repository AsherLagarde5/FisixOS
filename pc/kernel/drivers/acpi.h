#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

void acpi_init(void);
void acpi_set_rsdp(void *rsdp);
int acpi_is_available(void);
/* Real-hardware poweroff: returns 0 = not yet wired (FADT/S5 parsing TODO,
   RSDP only so far). Callers must fall back to halt, never assume S5 works. */
int acpi_poweroff(void);

#endif

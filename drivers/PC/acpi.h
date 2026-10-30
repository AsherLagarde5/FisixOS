#ifndef ACPI_H
#define ACPI_H

#include <stdint.h>

void acpi_init(void);
void acpi_set_rsdp(void *rsdp);
int acpi_is_available(void);

#endif

#include "acpi.h"

static void *rsdp_address;

void acpi_set_rsdp(void *rsdp)
{
    rsdp_address = rsdp;
}

void acpi_init(void)
{
    /* The UEFI loader supplies the RSDP before ExitBootServices(). */
}

int acpi_is_available(void)
{
    return rsdp_address != 0;
}

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

int acpi_poweroff(void)
{
    /* TODO (real hardware): parse ACPI FADT from RSDP, find SMI_CMD /
       PM1a_CNT_BLK, write SLP_TYP (S5) + SLP_EN. Until that parsing is
       tested on real firmware, returning 0 is safer than writing a
       guessed port — a wrong SLP_EN write can hang or reboot. */
    (void)rsdp_address;
    return 0;
}

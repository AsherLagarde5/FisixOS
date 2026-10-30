#include "calldrivers.h"
#include "../acpi.h"
#include "../battery.h"
#include "../memory.h"
#include "../mouse.h"
#include "../pci.h"
#include "../storage.h"
#include "../usb.h"
#include "idt.h"
#include "pic.h"

void drivers_init(void)
{
    /* Initialize all drivers */
    acpi_init();
    battery_init();
    memory_init();
    mouse_init();
    pci_init();
    storage_init();
    usb_init();
    pic_init();
    idt_init();
}

void drivers_update(void)
{
    /* Update driver states periodically */
    /* Stub - not yet implemented */
}

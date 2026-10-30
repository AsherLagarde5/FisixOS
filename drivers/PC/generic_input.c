#include "generic_input.h"
#include "pci.h"
#include "ps2kbd.h"
int generic_input_init(generic_input_t *out){
    pci_device_t dev;
    if(!out) return 0;
    out->ps2=ps2kbd_init(); /* bounded, idempotent: self-test again is safe */
    out->usb_hid_class=pci_find_device_class(0x0C,&dev) ? 1 : 0;
    out->present=out->ps2 || out->usb_hid_class;
    return 1; /* always succeeds as a boot stage */
}

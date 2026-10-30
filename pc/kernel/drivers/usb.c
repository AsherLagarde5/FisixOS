#include "usb.h"

static const char *controller_type = "EFI USB";
static int keyboard_present = 0;
static int mouse_present = 0;

void usb_init(void)
{
    /* USB controllers are managed by EFI until after ExitBootServices.
       Pure EFI mode should use EFI USB protocols instead of direct PCI probing. */
    controller_type = "EFI USB";
    keyboard_present = 0;
    mouse_present = 0;
}

const char *usb_controller_type(void)
{
    return controller_type;
}

int usb_keyboard_present(void)
{
    return keyboard_present;
}

int usb_mouse_present(void)
{
    return mouse_present;
}

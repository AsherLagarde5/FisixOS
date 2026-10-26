#include "touchscreen.h"

static int g_touch_expected = 1;
static int g_touch_x = 0, g_touch_y = 0, g_touch_down = 0;

void touchscreen_init(void)
{
    /* Two real touch paths, in order of what FisixOS can drive today:
       1. USB HID digitizer/tablet via xHCI (xhci_hid_init, kind
          XHCI_HID_TOUCH / XHCI_HID_TABLET): full enumeration and
          absolute reports, wired into the GUI in kernel_startup.
       2. HID over I2C on laptops (Intel Serial IO I2C, PCI class
          0x0C subclass 0x80): detected by latitude5420_check, but
          no I2C bus driver or ACPI HID enumeration yet, so a laptop
          panel cannot be claimed working until that bus exists and
          is tested on real hardware. PS/2 is not a touch path. */
    g_touch_expected = 1;
}

int touchscreen_is_hid_over_i2c_expected(void)
{
    return g_touch_expected;
}

void touchscreen_update_absolute(int x, int y, int down)
{
    g_touch_x = x; g_touch_y = y; g_touch_down = down;
}

int touchscreen_last_x(void) { return g_touch_x; }
int touchscreen_last_y(void) { return g_touch_y; }
int touchscreen_is_down(void) { return g_touch_down; }

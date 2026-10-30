#ifndef I2C_HID_H
#define I2C_HID_H
#include <stdint.h>
/* Generic HID-over-I2C driver, 2026-10-30.
 * NOT vendor-specific: it uses only public, standard pieces:
 *  - PCI Serial IO I2C controllers (class 0x0C, subclass 0x80),
 *    any vendor, DesignWare I2C register set (the register set
 *    Intel LPSS, AMD and most other laptop I2C controllers use)
 *  - HID-over-I2C (Microsoft HID-over-I2C spec): HID descriptor,
 *    report descriptor, input reports with a 2-byte length prefix
 *  - Generic HID report-descriptor parsing: X, Y, buttons / tip
 *    switch, by bit offset, not by a fixed vendor layout
 * So it should work on almost every laptop with an I2C touchpad
 * or touchscreen, not just the Latitude 5420, as long as the
 * controller is DesignWare-compatible and the device follows
 * HID-over-I2C. If no such device is found, init returns 0 and
 * the kernel keeps using PS/2, USB and keyboard - never hangs
 * the boot for input.
 * Two I2C HID devices in Windows Device Manager (common on
 * laptops) are usually touchpad + touchscreen. This driver
 * scans all controllers and all common slave addresses,
 * classifies each device, and prefers Touch Pad, then Mouse,
 * then Touch Screen, so a touchscreen does not steal the
 * touchpad's input.
 * Interrupt: v1 polls the input report (length 0 = no new
 * report) instead of requiring a GPIO interrupt driver, which
 * would need full ACPI GPIO parsing. Polling is slower than
 * interrupt, but it works without ACPI AML parsing.
 */
typedef struct {
    uint16_t vendor_id;
    uint16_t product_id;
    uint16_t version_id;
    uint8_t controller_index;
    uint8_t slave_addr;
    int kind; /* 1=relative mouse, 2=absolute touchpad, 3=absolute touchscreen, 4=absolute tablet */
    uint32_t logical_max_x;
    uint32_t logical_max_y;
} i2c_hid_info_t;
typedef struct {
    int kind;
    int dx;
    int dy;
    int abs_x;
    int abs_y;
    int buttons;
    int down;
    uint32_t logical_max_x;
    uint32_t logical_max_y;
} i2c_hid_report_t;
int i2c_hid_init(i2c_hid_info_t *out);
int i2c_hid_poll(i2c_hid_report_t *rep);
int i2c_hid_is_ready(void);
#endif

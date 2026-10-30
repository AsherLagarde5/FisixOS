#ifndef GENERIC_INPUT_H
#define GENERIC_INPUT_H
#include <stdint.h>
/* PS/2 + USB HID, polling, no model-specific tablet/trackball code.
 * Order: PS/2 keyboard first (works on almost every PC, incl. laptops
 * via EC), then USB HID via xHCI (tried later by the kernel, not
 * double-inited here beyond a PCI class check). No input = still boot,
 * GUI just has no pointer yet; never halt for input. */
typedef struct { int ps2; int usb_hid_class; int present; } generic_input_t;
int generic_input_init(generic_input_t *out);
#endif

#ifndef USB_H
#define USB_H

void usb_init(void);
const char *usb_controller_type(void);
int usb_keyboard_present(void);
int usb_mouse_present(void);

#endif

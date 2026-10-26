#ifndef XHCI_H
#define XHCI_H
#include <stdint.h>
#include "pci.h"
/* xHCI (USB 3) driver, working controller stage: detection + full
   operational bring-up. Latitude 5420 has an Intel xHCI controller
   (PCI class 0x0C, subclass 0x03, prog-if 0x30).
   xhci_init() halts and resets the controller, sets up the Device
   Context Base Address Array, command ring and event ring, sets
   MaxSlots, and starts the controller running (USBCMD RS=1,
   USBSTS HCHalted=0). It then reports port status, including
   connected devices. Device enumeration, slot/endpoint contexts,
   transfer rings and HID input are the next stage, not in v1. */
typedef struct {
    pci_device_t pci;
    uint64_t bar;
    uint16_t version;
    uint8_t ports;
    int running;
    uint8_t connected;
    uint32_t portsc[32];
} xhci_info_t;
int xhci_detect(xhci_info_t *out);
int xhci_init(xhci_info_t *out);
#endif
int xhci_enumerate_first(uint8_t *slot_out, uint32_t *portsc_out);
/* xHCI HID stage 2: full control-transfer enumeration (device/config/
   HID report descriptors), SET_CONFIGURATION, Configure Endpoint for
   the first HID interrupt-IN endpoint, and non-blocking interrupt-IN
   polling. Kinds: 1=relative mouse, 2=keyboard, 3=absolute pointer
   (USB tablet), 4=digitizer/touch screen. */
#define XHCI_HID_MOUSE 1
#define XHCI_HID_KEYBOARD 2
#define XHCI_HID_TABLET 3
#define XHCI_HID_TOUCH 4
typedef struct {
    uint8_t slot;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface_number;
    uint8_t interface_protocol;
    uint8_t endpoint_address;
    uint8_t dci;
    uint16_t max_packet;
    uint8_t interval;
    int kind;
    uint32_t logical_max_x;
    uint32_t logical_max_y;
} xhci_hid_t;
typedef struct {
    int kind;
    uint8_t len;
    uint8_t data[64];
    int dx;
    int dy;
    int buttons;
    int abs_x;
    int abs_y;
    int down;
    uint32_t logical_max_x;
    uint32_t logical_max_y;
} xhci_hid_report_t;
int xhci_hid_init(xhci_hid_t *out);
int xhci_hid_poll(xhci_hid_report_t *rep);

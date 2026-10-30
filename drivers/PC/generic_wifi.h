#ifndef GENERIC_WIFI_H
#define GENERIC_WIFI_H
#include <stdint.h>
#include "pci.h"
/* GENERIC Wi-Fi — honest version, NOT a Windows-Basic-Driver equivalent.
 *
 * Unlike generic_display (UEFI GOP): there is NO universal Wi-Fi
 * driver that works on all chips without firmware. GOP works because
 * firmware already set up a framebuffer; Wi-Fi has no such firmware
 * abstraction. Every Wi-Fi chip needs:
 *   - chip-specific firmware (usually a closed binary blob, loaded
 *     by the OS, e.g. Intel iwlwifi .ucode, Realtek .bin, etc.),
 *   - a chip-specific MAC/PHY driver (registers, TX/RX queues,
 *     calibration, regulatory, WPA key handling),
 *   - and, for Intel CNVi (e.g. AX201 on the Latitude 5420), a
 *     CNVi-specific path: the radio is integrated in the chipset,
 *     not a normal PCIe Wi-Fi card, so a generic PCIe scan is not
 *     enough and iwlwifi-style firmware + CNVi handling is required.
 *
 * So this file is DETECTION + STRUCTURE + PLUG-IN INTERFACE only:
 *   - detect a PCI Wi-Fi device (class 02, subclass 0x80, any
 *     prog-if — pci_find_device() requires an exact prog-if, so
 *     detection uses pci_scan_all() instead),
 *   - identify the vendor so a later specific driver can claim it,
 *   - define 802.11 management-frame layout and SSID-element parsing
 *     (pure structure, no radio transmission),
 *   - provide a plug-in table where a specific driver registers
 *     itself (probe/connect/send). Until one registers, Wi-Fi is
 *     UNAVAILABLE, boot continues on Ethernet or no-net, and boot
 *     NEVER fails for Wi-Fi. Wi-Fi is optional, like network/audio.
 *
 * DO NOT claim AX201 / CNVi works. DO NOT claim any real Wi-Fi
 * connection works without hardware test AND firmware. AX201 is
 * TODO, a separate driver, not this file.
 */
#define GENERIC_WIFI_VENDOR_UNKNOWN 0
#define GENERIC_WIFI_VENDOR_INTEL 1
#define GENERIC_WIFI_VENDOR_QUALCOMM 2
#define GENERIC_WIFI_VENDOR_REALTEK 3
#define GENERIC_WIFI_VENDOR_MEDIATEK 4
#define GENERIC_WIFI_VENDOR_BROADCOM 5
#define GENERIC_WIFI_VENDOR_MARVELL 6
#define GENERIC_WIFI_VENDOR_RALINK 7

#define GENERIC_WIFI_STATUS_NONE 0
#define GENERIC_WIFI_STATUS_DETECTED_NEEDS_DRIVER 1
#define GENERIC_WIFI_STATUS_SPECIFIC_DRIVER 2
#define GENERIC_WIFI_STATUS_CONNECTED 3

#define GENERIC_WIFI_MAX_DRIVERS 8
#define GENERIC_WIFI_SSID_MAX 32

typedef struct {
    int found;              /* any PCI Wi-Fi device seen */
    int pci_found;
    int usb_controller_found; /* xHCI class present; USB Wi-Fi enum is TODO */
    int cnvi_hint;          /* Intel device-id in AX201/CNVi range: hint only */
    int vendor_kind;
    int specific_driver_present;
    int connected;          /* always 0 until a specific driver connects */
    int scan_count;         /* always 0 until a specific driver scans */
    int status;
    pci_device_t pci;
    char ssid[GENERIC_WIFI_SSID_MAX + 1];
} generic_wifi_t;

/* 802.11 management frame header, on-air layout (little-endian).
 * No radio code here: this is structure only, for a future driver
 * that actually owns the chip's TX queue. */
typedef struct __attribute__((packed)) {
    uint16_t frame_control;
    uint16_t duration;
    uint8_t addr1[6];   /* destination / receiver */
    uint8_t addr2[6];   /* source / transmitter */
    uint8_t addr3[6];   /* BSSID */
    uint16_t seq_ctrl;
} wifi_mgmt_hdr_t;

/* Plug-in interface for a real Wi-Fi driver.
 * probe: return 1 if this driver owns the detected chip.
 * connect: return 1 only if association + 4-way handshake succeeded.
 * Until a driver registers and connects, generic code does nothing
 * but log. Registration is in-memory only, no firmware loading here. */
typedef struct {
    const char *name;
    uint16_t vendor_id;
    int (*probe)(generic_wifi_t *w);
    int (*connect)(generic_wifi_t *w, const char *ssid, const char *passphrase);
} generic_wifi_driver_t;

int generic_wifi_detect(generic_wifi_t *out);
int generic_wifi_register_driver(const generic_wifi_driver_t *drv);
int generic_wifi_try_registered(generic_wifi_t *w);
int generic_wifi_scan(generic_wifi_t *w);
int generic_wifi_parse_ssid_element(const uint8_t *ies, uint32_t ies_len,
                                    char *ssid_out, uint32_t ssid_max);
const char *generic_wifi_vendor_name(int vendor_kind);
const char *generic_wifi_status_name(int status);
/* DHCP hook point, documented, not a connection:
 * Once a specific driver delivers Ethernet-like frames, the existing
 * net.c DHCP path (net_init / net_poll, DHCP first, no static IP
 * assumed) is the path to run over it. This stub returns 0 (no link)
 * and deliberately does NOT call net_init(), to avoid double-init
 * before any radio link exists. */
int generic_wifi_dhcp_hook(const generic_wifi_t *w);
#endif

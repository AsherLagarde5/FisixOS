#include "generic_wifi.h"

static generic_wifi_driver_t g_wifi_drivers[GENERIC_WIFI_MAX_DRIVERS];
static int g_wifi_driver_count = 0;

static int wifi_vendor_kind(uint16_t vendor) {
    switch (vendor) {
        case 0x8086: return GENERIC_WIFI_VENDOR_INTEL;
        case 0x168c: return GENERIC_WIFI_VENDOR_QUALCOMM;
        case 0x10ec: return GENERIC_WIFI_VENDOR_REALTEK;
        case 0x14c3: return GENERIC_WIFI_VENDOR_MEDIATEK;
        case 0x14e4: return GENERIC_WIFI_VENDOR_BROADCOM;
        case 0x11ab: return GENERIC_WIFI_VENDOR_MARVELL;
        case 0x1814: return GENERIC_WIFI_VENDOR_RALINK;
        default: return GENERIC_WIFI_VENDOR_UNKNOWN;
    }
}

static int wifi_is_cnvi_hint(uint16_t vendor, uint16_t device) {
    /* Hint only, NOT a driver claim. AX201 / CNVi device-ids seen on
     * Intel 500-series chipsets (Latitude 5420 class). Even if this
     * matches, AX201 still needs CNVi handling + iwlwifi firmware,
     * which this file does NOT provide. */
    if (vendor != 0x8086) return 0;
    switch (device) {
        case 0x51f0: case 0x51f1: case 0x7af0: case 0x7a70:
        case 0xa0f0: case 0x2725: case 0x43f0:
            return 1;
        default: return 0;
    }
}

const char *generic_wifi_vendor_name(int k) {
    switch (k) {
        case GENERIC_WIFI_VENDOR_INTEL: return "Intel";
        case GENERIC_WIFI_VENDOR_QUALCOMM: return "Qualcomm/Atheros";
        case GENERIC_WIFI_VENDOR_REALTEK: return "Realtek";
        case GENERIC_WIFI_VENDOR_MEDIATEK: return "MediaTek";
        case GENERIC_WIFI_VENDOR_BROADCOM: return "Broadcom";
        case GENERIC_WIFI_VENDOR_MARVELL: return "Marvell";
        case GENERIC_WIFI_VENDOR_RALINK: return "Ralink";
        default: return "unknown";
    }
}

const char *generic_wifi_status_name(int s) {
    switch (s) {
        case GENERIC_WIFI_STATUS_DETECTED_NEEDS_DRIVER:
            return "detected-needs-driver";
        case GENERIC_WIFI_STATUS_SPECIFIC_DRIVER:
            return "specific-driver";
        case GENERIC_WIFI_STATUS_CONNECTED:
            return "connected";
        default: return "no-wifi";
    }
}

int generic_wifi_detect(generic_wifi_t *out) {
    pci_device_t all[64];
    int count = 0, i;
    if (!out) return 0;
    out->found = 0; out->pci_found = 0; out->usb_controller_found = 0;
    out->cnvi_hint = 0; out->vendor_kind = GENERIC_WIFI_VENDOR_UNKNOWN;
    out->specific_driver_present = 0; out->connected = 0;
    out->scan_count = 0; out->status = GENERIC_WIFI_STATUS_NONE;
    out->ssid[0] = 0;
    /* pci_scan_all, not pci_find_device(0x02,0x80,prog_if): prog-if
     * varies by chip, and an exact-match find would miss devices. */
    if (!pci_scan_all(all, 64, &count)) return 1; /* no PCI: still boot */
    for (i = 0; i < count && i < 64; i++) {
        if (all[i].class_code == 0x0c && all[i].subclass == 0x03)
            out->usb_controller_found = 1; /* xHCI present; USB Wi-Fi
                device enumeration via xHCI is TODO, not claimed. */
        if (all[i].class_code == 0x02 && all[i].subclass == 0x80) {
            out->pci = all[i];
            out->found = 1; out->pci_found = 1;
            out->vendor_kind = wifi_vendor_kind(all[i].vendor_id);
            out->cnvi_hint = wifi_is_cnvi_hint(all[i].vendor_id,
                                               all[i].device_id);
            break;
        }
    }
    if (out->found) {
        /* Fallback, honestly logged by the caller:
         * "specific firmware/driver required, Wi-Fi unavailable,
         *  boot continues on Ethernet / no-net" — NOT a connection,
         *  NOT a failure. Boot never halts for Wi-Fi. */
        out->status = GENERIC_WIFI_STATUS_DETECTED_NEEDS_DRIVER;
        (void)generic_wifi_try_registered(out);
    }
    return 1; /* detection never fails boot, even with no Wi-Fi */
}

int generic_wifi_register_driver(const generic_wifi_driver_t *drv) {
    if (!drv || g_wifi_driver_count >= GENERIC_WIFI_MAX_DRIVERS) return 0;
    g_wifi_drivers[g_wifi_driver_count++] = *drv;
    return 1;
}

int generic_wifi_try_registered(generic_wifi_t *w) {
    int i;
    if (!w || !w->found) return 0;
    for (i = 0; i < g_wifi_driver_count; i++) {
        if (g_wifi_drivers[i].vendor_id != w->pci.vendor_id) continue;
        if (g_wifi_drivers[i].probe && g_wifi_drivers[i].probe(w)) {
            w->specific_driver_present = 1;
            w->status = GENERIC_WIFI_STATUS_SPECIFIC_DRIVER;
            return 1;
        }
    }
    return 0;
}

int generic_wifi_scan(generic_wifi_t *w) {
    /* Stub, by design: scanning needs a radio driver + firmware.
     * Returning a fake SSID list would be a false claim. A specific
     * driver must implement scan; until then scan_count stays 0. */
    if (!w) return 0;
    w->scan_count = 0;
    return 0;
}

int generic_wifi_parse_ssid_element(const uint8_t *ies, uint32_t ies_len,
                                    char *ssid_out, uint32_t ssid_max) {
    uint32_t i = 0, n;
    if (!ies || !ssid_out || ssid_max == 0) return 0;
    ssid_out[0] = 0;
    while (i + 2 <= ies_len) {
        uint8_t id = ies[i], len = ies[i + 1];
        if (i + 2 + len > ies_len) break;
        if (id == 0) { /* SSID element */
            uint32_t data_off = i + 2, j;
            n = len;
            if (n > GENERIC_WIFI_SSID_MAX) n = GENERIC_WIFI_SSID_MAX;
            if (n >= ssid_max) n = ssid_max - 1;
            for (j = 0; j < n; j++) ssid_out[j] = (char)ies[data_off + j];
            ssid_out[n] = 0;
            return 1;
        }
        i += 2 + len;
    }
    return 0;
}

int generic_wifi_dhcp_hook(const generic_wifi_t *w) {
    /* See header: hook point only. No link without a specific driver,
     * so no DHCP is started here. Caller: after a specific driver
     * reports connected, run net_init()/net_poll() (DHCP first). */
    if (!w || !w->connected) return 0;
    return 1;
}

#include "wifi_cards.h"

/* 10 most popular + BE200 bonus. Order is priority order:
 * AX201 first, because it is Derek's Latitude 5420 card.
 * Device IDs are the PCI IDs named in the task. Where a card
 * has more than one known ID, all listed IDs are included.
 * Firmware: family / example, user supplies, NOT bundled.
 */
static const wifi_card_t g_cards[] = {
    /* 1. TOP PRIORITY — Intel Wi-Fi 6 AX201 160MHz, CNVi */
    { "Intel Wi-Fi 6 AX201 160MHz", 0x8086,
      { 0x51f0, 0x51f1, 0x43f0, 0x7af0 }, 4,
      "Wi-Fi 6", 160, 1, 1,
      "iwlwifi-Qu-c0-hr-b0-*.ucode (Qu family, e.g. -72.ucode)",
      "iwlwifi (Qu)", WIFI_CARD_STATUS_INIT_STUB },
    /* 2. Intel Wi-Fi 6 AX200 160MHz, PCIe (not CNVi) */
    { "Intel Wi-Fi 6 AX200 160MHz", 0x8086,
      { 0x2723, 0, 0, 0 }, 1,
      "Wi-Fi 6", 160, 0, 0,
      "iwlwifi-cc-a0-*.ucode (cc family, e.g. -72.ucode)",
      "iwlwifi (cc)", WIFI_CARD_STATUS_INIT_STUB },
    /* 3. Intel Wi-Fi 6E AX210 160MHz, PCIe */
    { "Intel Wi-Fi 6E AX210 160MHz", 0x8086,
      { 0x2725, 0, 0, 0 }, 1,
      "Wi-Fi 6E", 160, 0, 0,
      "iwlwifi-ty-a0-gf-a0-*.ucode (ty family)",
      "iwlwifi (ty)", WIFI_CARD_STATUS_INIT_STUB },
    /* 4. Intel Wireless-AC 9560, CNVi */
    { "Intel Wireless-AC 9560", 0x8086,
      { 0x9df0, 0xa370, 0, 0 }, 2,
      "Wi-Fi 5", 160, 1, 0,
      "iwlwifi-9000-pu-b0-jf-b0-*.ucode",
      "iwlwifi (9000)", WIFI_CARD_STATUS_INIT_STUB },
    /* 5. Intel Wireless-AC 9260, PCIe */
    { "Intel Wireless-AC 9260", 0x8086,
      { 0x2526, 0, 0, 0 }, 1,
      "Wi-Fi 5", 160, 0, 0,
      "iwlwifi-9260-th-b0-jf-b0-*.ucode",
      "iwlwifi (9260)", WIFI_CARD_STATUS_INIT_STUB },
    /* 6. Intel Wireless-AC 8265, PCIe */
    { "Intel Wireless-AC 8265", 0x8086,
      { 0x24fd, 0, 0, 0 }, 1,
      "Wi-Fi 5", 80, 0, 0,
      "iwlwifi-8265-*.ucode (e.g. -36.ucode)",
      "iwlwifi (8265)", WIFI_CARD_STATUS_INIT_STUB },
    /* 7. Qualcomm Atheros QCA9377 */
    { "Qualcomm Atheros QCA9377", 0x168c,
      { 0x0042, 0, 0, 0 }, 1,
      "Wi-Fi 5", 80, 0, 0,
      "ath10k/QCA9377/hw1.0/firmware-6.bin + board-2.bin",
      "ath10k", WIFI_CARD_STATUS_INIT_STUB },
    /* 8. Realtek RTL8821CE / RTL8852AE */
    { "Realtek RTL8821CE / RTL8852AE", 0x10ec,
      { 0xc821, 0xc852, 0, 0 }, 2,
      "Wi-Fi 5 / 6", 80, 0, 0,
      "rtw88/rtw8821c_fw.bin (8821CE); rtw89/rtw8852a_fw.bin (8852AE)",
      "rtw88 / rtw89", WIFI_CARD_STATUS_INIT_STUB },
    /* 9. MediaTek MT7921 / MT7922 (AMD RZ616 = MT7922) */
    { "MediaTek MT7921 / MT7922 (AMD RZ616)", 0x14c3,
      { 0x7961, 0x0616, 0, 0 }, 2,
      "Wi-Fi 6 / 6E", 160, 0, 0,
      "mediatek/WIFI_MT7961_patch_mcu_*.bin + WIFI_RAM_CODE_MT7961_*.bin (7921); WIFI_MT7922_patch_mcu_*.bin (7922)",
      "mt7921e / mt7922", WIFI_CARD_STATUS_INIT_STUB },
    /* 10. Broadcom BCM94360 / BCM4360 */
    { "Broadcom BCM94360 / BCM4360", 0x14e4,
      { 0x43a0, 0x43ba, 0, 0 }, 2,
      "Wi-Fi 5", 80, 0, 0,
      "brcm/brcmfmac43602-pcie.bin (94360); brcm/brcmfmac4360-pcie.bin (4360)",
      "brcmfmac", WIFI_CARD_STATUS_INIT_STUB },
    /* Bonus. Intel Wi-Fi 7 BE200 */
    { "Intel Wi-Fi 7 BE200", 0x8086,
      { 0x272b, 0x7e40, 0, 0 }, 2,
      "Wi-Fi 7", 320, 0, 0,
      "iwlwifi-gl-c0-fm-c0-*.ucode / iwlwifi-bz-a0-*.ucode (family varies by stepping)",
      "iwlwifi (gl/bz)", WIFI_CARD_STATUS_INIT_STUB },
};

int wifi_cards_count(void) {
    return (int)(sizeof(g_cards) / sizeof(g_cards[0]));
}

const wifi_card_t *wifi_cards_table(void) { return g_cards; }

const wifi_card_t *wifi_cards_find(uint16_t vendor_id, uint16_t device_id) {
    int i, j;
    for (i = 0; i < wifi_cards_count(); i++) {
        if (g_cards[i].vendor_id != vendor_id) continue;
        for (j = 0; j < g_cards[i].device_id_count; j++) {
            if (g_cards[i].device_ids[j] == device_id) return &g_cards[i];
        }
    }
    return 0;
}

int wifi_cards_probe(const generic_wifi_t *w, const wifi_card_t **card_out) {
    const wifi_card_t *c;
    if (!w || !w->found || !card_out) return 0;
    c = wifi_cards_find(w->pci.vendor_id, w->pci.device_id);
    if (!c) return 0;
    *card_out = c;
    return 1;
}

int wifi_cards_init_stub(const wifi_card_t *card) {
    /* Stub only: card is known, no firmware loaded, no radio work.
     * A real init must: load firmware (TODO loader), reset MAC/PHY,
     * calibrate, set regulatory, then scan/associate (all TODO).
     * Returning 1 here means "stub initialised", NOT "connected". */
    if (!card) return 0;
    return 1;
}

const char *wifi_cards_status_name(int status) {
    switch (status) {
        case WIFI_CARD_STATUS_INIT_STUB: return "init-stub-needs-firmware";
        case WIFI_CARD_STATUS_NEEDS_FIRMWARE: return "needs-firmware";
        default: return "detection-only";
    }
}

/* Vendor plug-ins for generic_wifi. Probe = table match.
 * Connect = always 0: association + WPA need firmware + MAC/PHY,
 * which do not exist yet. This is deliberate, not a bug. */
static int wifi_probe_intel(generic_wifi_t *w) {
    const wifi_card_t *c = 0;
    return w && w->pci.vendor_id == 0x8086 && wifi_cards_probe(w, &c);
}
static int wifi_probe_qualcomm(generic_wifi_t *w) {
    const wifi_card_t *c = 0;
    return w && w->pci.vendor_id == 0x168c && wifi_cards_probe(w, &c);
}
static int wifi_probe_realtek(generic_wifi_t *w) {
    const wifi_card_t *c = 0;
    return w && w->pci.vendor_id == 0x10ec && wifi_cards_probe(w, &c);
}
static int wifi_probe_mediatek(generic_wifi_t *w) {
    const wifi_card_t *c = 0;
    return w && w->pci.vendor_id == 0x14c3 && wifi_cards_probe(w, &c);
}
static int wifi_probe_broadcom(generic_wifi_t *w) {
    const wifi_card_t *c = 0;
    return w && w->pci.vendor_id == 0x14e4 && wifi_cards_probe(w, &c);
}
static int wifi_connect_stub(generic_wifi_t *w, const char *ssid, const char *pass) {
    (void)w; (void)ssid; (void)pass;
    return 0; /* No firmware / MAC/PHY yet — never claim connected. */
}

int wifi_cards_register_all(void) {
    static const generic_wifi_driver_t drivers[] = {
        { "wifi-intel-ax201-ax200-ax210", 0x8086, wifi_probe_intel, wifi_connect_stub },
        { "wifi-qualcomm-qca9377", 0x168c, wifi_probe_qualcomm, wifi_connect_stub },
        { "wifi-realtek-8821ce-8852ae", 0x10ec, wifi_probe_realtek, wifi_connect_stub },
        { "wifi-mediatek-mt7921-mt7922", 0x14c3, wifi_probe_mediatek, wifi_connect_stub },
        { "wifi-broadcom-bcm94360-4360", 0x14e4, wifi_probe_broadcom, wifi_connect_stub },
    };
    int i, ok = 0;
    for (i = 0; i < (int)(sizeof(drivers) / sizeof(drivers[0])); i++) {
        if (generic_wifi_register_driver(&drivers[i])) ok++;
    }
    return ok;
}

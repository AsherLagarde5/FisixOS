#ifndef WIFI_CARDS_H
#define WIFI_CARDS_H
#include <stdint.h>
#include "generic_wifi.h"

/* Wi-Fi cards — 10 most popular + Intel BE200 bonus.
 * STATUS, HONEST: DETECTION + INIT STUB ONLY.
 * This is NOT a full 802.11 association / WPA / data path.
 * Every card below needs its vendor firmware blob, loaded by the OS,
 * and chip-specific MAC/PHY init (TX/RX queues, calibration,
 * regulatory, WPA key handling) before it can scan/associate.
 * Firmware is NOT bundled in FisixOS — the user supplies it
 * (e.g. from Linux firmware, /lib/firmware), and FisixOS must
 * load it before any radio work. Until firmware is present and
 * a real MAC/PHY init runs, connect/scan return 0 and boot
 * continues on Ethernet / no-net. Wi-Fi never halts boot.
 *
 * TOP PRIORITY: Intel Wi-Fi 6 AX201 160MHz, Derek's Dell
 * Latitude 5420 card. CNVi: the radio is integrated in the
 * Intel 500-series chipset, not a normal PCIe Wi-Fi card, so
 * a generic PCIe scan is not enough — CNVi handling + iwlwifi
 * firmware are required, and are TODO beyond this stub.
 *
 * Firmware names below are the Linux-firmware family / known
 * example filenames, NOT a guarantee that that exact version
 * is in the user's tree. Check /lib/firmware for the version
 * your kernel/firmware package ships, and place the matching
 * .ucode/.bin in the FisixOS firmware location when a loader
 * exists (loader itself is TODO).
 */
#define WIFI_CARD_STATUS_DETECTION_ONLY 0
#define WIFI_CARD_STATUS_INIT_STUB 1
#define WIFI_CARD_STATUS_NEEDS_FIRMWARE 2
/* All entries in this table are INIT_STUB + NEEDS_FIRMWARE. */
#define WIFI_CARDS_MAX_DEVICE_IDS 4

typedef struct {
    const char *name;          /* e.g. "Intel Wi-Fi 6 AX201 160MHz" */
    uint16_t vendor_id;
    uint16_t device_ids[WIFI_CARDS_MAX_DEVICE_IDS];
    int device_id_count;
    const char *generation;    /* Wi-Fi 5 / 6 / 6E / 7 */
    int max_width_mhz;         /* 80 or 160 */
    int cnvi;                  /* 1 = Intel CNVi, radio in chipset */
    int is_ax201_priority;     /* 1 only for AX201 */
    const char *firmware;      /* family / example filename, user supplies */
    const char *driver_family; /* Linux driver family, for reference */
    int status;                /* WIFI_CARD_STATUS_* */
} wifi_card_t;

int wifi_cards_count(void);
const wifi_card_t *wifi_cards_table(void);
const wifi_card_t *wifi_cards_find(uint16_t vendor_id, uint16_t device_id);
/* Probe a generic_wifi_t: returns 1 and fills card_out if the
 * detected PCI device matches this table, else 0. Detection only. */
int wifi_cards_probe(const generic_wifi_t *w, const wifi_card_t **card_out);
/* Init stub: validates card + firmware presence is NOT checked
 * here (no firmware loader yet), logs via return code only.
 * Returns 1 if the card is known (stub initialised), 0 otherwise.
 * It does NOT associate, scan, or transmit. */
int wifi_cards_init_stub(const wifi_card_t *card);
/* Register vendor plug-ins into generic_wifi so
 * generic_wifi_try_registered() can find a specific driver.
 * Call BEFORE generic_wifi_detect(). Connect still returns 0
 * until firmware + MAC/PHY init exist. */
int wifi_cards_register_all(void);
const char *wifi_cards_status_name(int status);

#endif

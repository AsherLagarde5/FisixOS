#ifndef GENERIC_BOOT_H
#define GENERIC_BOOT_H
#include <stdint.h>
#include "generic_display.h"
#include "generic_storage.h"
#include "generic_input.h"
#include "generic_network.h"
#include "generic_audio.h"
#include "generic_cpu.h"
#include "generic_chipset.h"
#include "generic_wifi.h"
#include "usb_storage.h"
/* Boot fallback chain, in order. This is the Windows-basic-driver idea:
 *  1. Try the specific driver (Latitude, AMD, e1000, NVMe, etc.) where
 *     the kernel already has one.
 *  2. If it fails or is absent, use the generic driver in this file.
 *  3. If generic fails, use the UEFI firmware fallback (GOP, ESP/FAT,
 *     serial text) and continue.
 *  Essential = display + storage + input. Network/audio/Wi-Fi are
 *  optional. Wi-Fi in particular is detection-only (see generic_wifi.h:
 *  no universal Wi-Fi driver exists without firmware) and never halts
 *  boot. NOTHING non-essential may halt boot. Essential failure still logs
 *  and falls back to text/serial idle, it does not triple-fault.
 *  generic_boot_init() is deliberately detection + PS/2 only: it does
 *  NOT re-init NVMe/xHCI/e1000, those run once in kernel.c afterwards.
 *  Call it early in kernel_startup(), before the specific-driver block,
 *  and keep the specific block as the optimization pass. */
typedef struct {
    generic_display_t display; generic_storage_t storage; generic_input_t input;
    generic_network_t network; generic_audio_t audio; generic_cpu_t cpu; generic_chipset_t chipset;
    generic_wifi_t wifi;
    usb_storage_t usb; /* detection-only here: controller present or not.
                          usb_storage_init() (BOT) runs after xhci_init(). */
    int essential_ok;
} generic_boot_t;
int generic_boot_init(uint64_t fb_base, uint32_t fb_w, uint32_t fb_h, uint32_t fb_p, generic_boot_t *out);
#endif

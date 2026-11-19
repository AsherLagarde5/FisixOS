#include "generic_boot.h"
#include "gui.h"
/* Sub-stage debug, 2026-10-30, v2: the old sub-stages were
 * drawn in a side column at x=400, which was easy to miss
 * in a photo, and only marked the start of each detect.
 * Now every detect prints START and DONE, in the same
 * left-column list as the main stages, via gui_debug_line().
 * The last line on screen is therefore the exact detect
 * that started and never returned - e.g. "GEN 3 INPUT
 * START" with no DONE means the hang is inside input
 * (PS/2 / PCI USB HID), not just "somewhere in generic". */
int generic_boot_init(uint64_t fb_base, uint32_t fb_w, uint32_t fb_h, uint32_t fb_p, generic_boot_t *out, int skip_wifi){
    if(!out) return 0;
    gui_debug_line("GEN 1 DISPLAY START");
    generic_display_init(fb_base,fb_w,fb_h,fb_p,&out->display);
    gui_debug_line("GEN 1 DISPLAY DONE");
    gui_debug_line("GEN 2 STORAGE START");
    generic_storage_detect(&out->storage);
    gui_debug_line("GEN 2 STORAGE DONE");
    gui_debug_line("GEN 3 INPUT START");
    generic_input_init(&out->input);
    gui_debug_line("GEN 3 INPUT DONE");
    gui_debug_line("GEN 4 NET START");
    generic_network_detect(&out->network);
    gui_debug_line("GEN 4 NET DONE");
    gui_debug_line("GEN 5 AUDIO START");
    generic_audio_detect(&out->audio);
    gui_debug_line("GEN 5 AUDIO DONE");
    gui_debug_line("GEN 6 CPU START");
    generic_cpu_detect(&out->cpu);
    gui_debug_line("GEN 6 CPU DONE");
    gui_debug_line("GEN 7 CHIPSET START");
    generic_chipset_detect(&out->chipset);
    gui_debug_line("GEN 7 CHIPSET DONE");
    /* Wi-Fi is detection-only and optional: if no specific driver
       registered, status stays detected-needs-driver and boot
       continues on Ethernet / no-net. See generic_wifi.h. */
    if (skip_wifi) {
        /* User chose Without Wi-Fi after login: do not even
           run the PCI Wi-Fi scan. This is the bypass for the
           GEN 8 hang on the Latitude. */
        out->wifi.found = 0; out->wifi.pci_found = 0;
        out->wifi.status = GENERIC_WIFI_STATUS_NONE;
        gui_debug_line("GEN 8 WIFI SKIPPED (Without Wi-Fi)");
    } else {
        gui_debug_line("GEN 8 WIFI START");
        generic_wifi_detect(&out->wifi);
        gui_debug_line("GEN 8 WIFI DONE");
    }
    /* USB storage, detection-only in the generic pass: usb_storage_detect()
       checks for an xHCI controller only. Full BOT init (bulk endpoints,
       INQUIRY, READ CAPACITY) is usb_storage_init(), after xhci_init()
       in kernel.c's specific-driver stage. Boot never fails for USB. */
    gui_debug_line("GEN 9 USB START");
    usb_storage_detect(&out->usb);
    gui_debug_line("GEN 9 USB DONE");
    /* Essential: display (GOP or text) + storage (any class or UEFI
       fallback) + input (PS/2 or USB class). All three generic stages
       return success even when hardware is absent, by design, so
       essential_ok is almost always 1; it is a log flag, not a halt. */
    out->essential_ok = generic_display_present(&out->display) && out->storage.kind!=GENERIC_STORAGE_NONE;
    return 1;
}

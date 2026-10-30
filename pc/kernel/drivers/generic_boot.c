#include "generic_boot.h"
int generic_boot_init(uint64_t fb_base, uint32_t fb_w, uint32_t fb_h, uint32_t fb_p, generic_boot_t *out){
    if(!out) return 0;
    generic_display_init(fb_base,fb_w,fb_h,fb_p,&out->display);
    generic_storage_detect(&out->storage);
    generic_input_init(&out->input);
    generic_network_detect(&out->network);
    generic_audio_detect(&out->audio);
    generic_cpu_detect(&out->cpu);
    generic_chipset_detect(&out->chipset);
    /* Wi-Fi is detection-only and optional: if no specific driver
       registered, status stays detected-needs-driver and boot
       continues on Ethernet / no-net. See generic_wifi.h. */
    generic_wifi_detect(&out->wifi);
    /* USB storage, detection-only in the generic pass: usb_storage_detect()
       checks for an xHCI controller only. Full BOT init (bulk endpoints,
       INQUIRY, READ CAPACITY) is usb_storage_init(), after xhci_init()
       in kernel.c's specific-driver stage. Boot never fails for USB. */
    usb_storage_detect(&out->usb);
    /* Essential: display (GOP or text) + storage (any class or UEFI
       fallback) + input (PS/2 or USB class). All three generic stages
       return success even when hardware is absent, by design, so
       essential_ok is almost always 1; it is a log flag, not a halt. */
    out->essential_ok = generic_display_present(&out->display) && out->storage.kind!=GENERIC_STORAGE_NONE;
    return 1;
}

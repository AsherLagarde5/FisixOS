#include "generic_storage.h"
const char *generic_storage_kind_name(int k){
    switch(k){ case GENERIC_STORAGE_NVME: return "NVMe"; case GENERIC_STORAGE_AHCI: return "AHCI-SATA"; case GENERIC_STORAGE_IDE: return "IDE"; case GENERIC_STORAGE_USB: return "USB-Mass-Storage"; case GENERIC_STORAGE_UEFI_FALLBACK: return "UEFI-fallback"; default: return "none"; }
}
int generic_storage_detect(generic_storage_t *out){
    pci_device_t dev;
    if(!out) return 0;
    out->kind=GENERIC_STORAGE_NONE; out->found=0; out->uefi_fallback=0; out->usb_controller_present=0;
    if(pci_find_device(0x01,0x08,0x02,&dev)){ out->kind=GENERIC_STORAGE_NVME; out->pci=dev; out->found=1; return 1; }
    if(pci_find_device(0x01,0x06,0x01,&dev)){ out->kind=GENERIC_STORAGE_AHCI; out->pci=dev; out->found=1; return 1; }
    if(pci_find_device(0x01,0x01,0x00,&dev)){ out->kind=GENERIC_STORAGE_IDE; out->pci=dev; out->found=1; return 1; }
    /* USB after NVMe/AHCI/IDE, before UEFI fallback: controller
       presence only here — a device is proven by usb_storage_init()
       after xhci_init(), not by this detection pass. */
    if(pci_find_device(0x0C,0x03,0x30,&dev)){ out->kind=GENERIC_STORAGE_USB; out->pci=dev; out->found=1; out->usb_controller_present=1; return 1; }
    /* No native class found: UEFI loader already read the ESP/FAT,
       so boot can continue from firmware media. */
    out->kind=GENERIC_STORAGE_UEFI_FALLBACK; out->uefi_fallback=1; return 1;
}

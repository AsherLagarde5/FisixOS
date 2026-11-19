#include "generic_network.h"
int generic_network_detect(generic_network_t *out){
    if(!out) return 0;
    out->found=0; out->is_e1000=0; out->wifi_only_hint=0;
    if(!pci_find_device_class(0x02,&out->pci)) return 1; /* no NIC: still boot */
    out->found=1;
    if(out->pci.vendor_id==0x8086 && (out->pci.device_id==0x100E || out->pci.device_id==0x100F || out->pci.device_id==0x10D3)) out->is_e1000=1;
    if(out->pci.subclass==0x80) out->wifi_only_hint=1;
    return 1;
}

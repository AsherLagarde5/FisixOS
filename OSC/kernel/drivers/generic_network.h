#ifndef GENERIC_NETWORK_H
#define GENERIC_NETWORK_H
#include <stdint.h>
#include "pci.h"
/* PCI network class (02/00) detection only. DHCP first (see net.c),
 * e1000 generic if present, otherwise no-net boot. Network is
 * OPTIONAL: a PC with no NIC, or with Wi-Fi only (AX201/CNVi, no
 * driver yet), must still boot to desktop. Never fail boot for net. */
typedef struct { int found; pci_device_t pci; int is_e1000; int wifi_only_hint; } generic_network_t;
int generic_network_detect(generic_network_t *out);
#endif

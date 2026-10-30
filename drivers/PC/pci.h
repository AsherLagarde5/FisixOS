#ifndef PCI_H
#define PCI_H

#include <stdint.h>

#define PCI_MAX_DEVICES 256
#define PCI_VENDOR_INTEL 0x8086u

#define PCI_CLASS_NETWORK 0x02u
#define PCI_CLASS_DISPLAY 0x03u
#define PCI_CLASS_BRIDGE 0x06u
#define PCI_SUBCLASS_VGA 0x00u
#define PCI_SUBCLASS_PCI_BRIDGE 0x04u
#define PCI_PROGIF_VGA 0x00u

typedef struct {
    uint8_t bus;
    uint8_t device;
    uint8_t function;
    uint16_t vendor_id;
    uint16_t device_id;
    uint8_t class_code;
    uint8_t subclass;
    uint8_t prog_if;
    uint8_t header_type;
} pci_device_t;

void pci_init(void);
int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out);
int pci_find_device_class(uint8_t class_code, pci_device_t *out);
uint32_t pci_read_bar(pci_device_t *dev, int index);
uint64_t pci_read_bar64(pci_device_t *dev, int index);
int pci_scan_all(pci_device_t *out, int max, int *count);
int pci_is_intel_gpu_present(void);

#endif

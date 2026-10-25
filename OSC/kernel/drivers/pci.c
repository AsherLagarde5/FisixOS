#include "pci.h"

static inline uint32_t pci_config_address(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    return 0x80000000u |
        ((uint32_t)bus << 16) |
        ((uint32_t)device << 11) |
        ((uint32_t)function << 8) |
        ((uint32_t)offset & 0xFCu);
}

static inline uint32_t pci_read_config32(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value;
    uint32_t address = pci_config_address(bus, device, function, offset);

    __asm__ volatile ("outl %0, %1" : : "a"(address), "d"((uint16_t)0xCF8u));
    __asm__ volatile ("inl %1, %0" : "=a"(value) : "d"((uint16_t)0xCFCu));
    return value;
}

static inline uint16_t pci_read_config16(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read_config32(bus, device, function, offset);
    return (uint16_t)(value >> ((offset & 0x2u) * 8u));
}

static inline uint8_t pci_read_config8(uint8_t bus, uint8_t device, uint8_t function, uint8_t offset)
{
    uint32_t value = pci_read_config32(bus, device, function, offset);
    return (uint8_t)(value >> ((offset & 0x3u) * 8u));
}

void pci_init(void)
{
    /* PCI config space is directly addressable in the freestanding x86_64 EFI path. */
}

int pci_find_device(uint8_t class_code, uint8_t subclass, uint8_t prog_if, pci_device_t *out)
{
    uint16_t bus;
    uint8_t device;
    uint8_t function;

    if (out == 0) {
        return 0;
    }

    for (bus = 0; bus < 256u; ++bus) {
        for (device = 0; device < 32u; ++device) {
            int multifunction = 0;
            for (function = 0; function < 8u; ++function) {
                uint16_t vendor_id;
                uint8_t header_type;
                uint8_t class_device;
                uint8_t class_subclass;
                uint8_t class_prog_if;
                if (function > 0 && !multifunction) {
                    break;
                }
                vendor_id = pci_read_config16(bus, device, function, 0x00u);

                if (vendor_id == 0xFFFFu) {
                    /* No device here. A non-multifunction device has
                       no further functions to check, so skip them
                       instead of probing 7 empty slots. */
                    if (function == 0) {
                        break;
                    }
                    continue;
                }

                header_type = pci_read_config8(bus, device, function, 0x0Eu);
                if (function == 0) {
                    multifunction = (header_type & 0x80u) != 0;
                } else if (!multifunction) {
                    break;
                }
                class_device = pci_read_config8(bus, device, function, 0x0Bu);
                class_subclass = pci_read_config8(bus, device, function, 0x0Au);
                class_prog_if = pci_read_config8(bus, device, function, 0x09u);

                if (class_device == class_code && class_subclass == subclass && class_prog_if == prog_if) {
                    out->bus = bus;
                    out->device = device;
                    out->function = function;
                    out->vendor_id = vendor_id;
                    out->device_id = pci_read_config16(bus, device, function, 0x02u);
                    out->class_code = class_device;
                    out->subclass = class_subclass;
                    out->prog_if = class_prog_if;
                    out->header_type = header_type;
                    return 1;
                }
            }
        }
    }

    return 0;
}

int pci_is_intel_gpu_present(void)
{
    pci_device_t device;

    if (pci_find_device(PCI_CLASS_DISPLAY, PCI_SUBCLASS_VGA, PCI_PROGIF_VGA, &device)) {
        return device.vendor_id == PCI_VENDOR_INTEL;
    }

    return 0;
}

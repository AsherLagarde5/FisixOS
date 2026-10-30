#ifndef LATITUDE5420_H
#define LATITUDE5420_H
/* Dell Latitude 5420 boot checklist.
   This is the model on Derek's desk (confirmed by the PC itself:
   Latitude 5420, i5-1145G7 Tiger Lake, Iris Xe). "540" in chat is
   shorthand for this 5420, not a Latitude 5400 (which is 8th Gen).
   Boot-critical, in order:
     CPU Tiger Lake, memory, ACPI, PCI, Local APIC, timer (via APIC/PIT),
     GOP display, PS/2 keyboard, NVMe storage, xHCI USB, I2C Serial IO
     (touchpad/touchscreen), Ethernet I219-LM, Wi-Fi AX201 (CNVi).
   Not needed to boot: audio (Realtek / Intel SST), Bluetooth,
   Thunderbolt, TPM, camera. They are listed as optional, not boot.
   This file does not claim any of those drivers are complete - see
   each driver's own header for what step it actually implements. */
typedef struct {
    int cpu_tiger_lake;
    int acpi;
    int pci_count;
    int apic;
    int gop;
    int ps2kbd;
    int nvme;
    int xhci;
    int serial_io;
    int ethernet;
    int wifi;
    int smbios;
} latitude5420_boot_t;
void latitude5420_check(latitude5420_boot_t *out);
#endif

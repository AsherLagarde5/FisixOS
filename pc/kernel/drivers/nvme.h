#ifndef NVME_H
#define NVME_H
#include <stdint.h>
#include "pci.h"
/* NVMe driver, working version: controller init, admin queues,
   Identify controller + namespace, I/O queues, and sector reads.
   Latitude 5420 boots from an NVMe SSD (PCI class 0x01, subclass 0x08,
   prog-if 0x02). This is a real driver, not detection only: it
   enables the controller, creates queues, runs Identify, and can
   read LBAs into a caller buffer. Writes, multiple namespaces,
   interrupts (polling only) and power management are not in v1. */
typedef struct {
    pci_device_t pci;
    uint64_t bar;
    uint32_t version;
    uint64_t cap;
    uint32_t csts;
    int ready;
    /* Identify controller fields, copied out of the Identify buffer. */
    char model[41];
    char serial[21];
    uint32_t max_transfer_pages; /* MDTS: 0 = unlimited in practice here */
    /* Identify namespace fields for NSID 1. */
    uint64_t ns_size_lba;
    uint32_t lba_data_size; /* bytes per LBA, usually 512 */
    int ns_valid;
} nvme_info_t;
int nvme_detect(nvme_info_t *out);
/* Full init: detect + enable + admin queues + Identify + I/O queues.
   Returns 1 on success and fills out with controller/namespace info. */
int nvme_init(nvme_info_t *out);
/* Read one LBA (lba_data_size bytes, usually 512) into buf.
   Returns 1 on success, 0 on failure or if nvme_init was not run. */
int nvme_read_lba(uint64_t lba, uint8_t *buf);
#endif

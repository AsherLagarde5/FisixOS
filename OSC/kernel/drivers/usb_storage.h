#ifndef USB_STORAGE_H
#define USB_STORAGE_H
#include <stdint.h>
/* USB Mass Storage Class — Bulk-Only Transport (BOT), SCSI over USB.
 *
 * Scope, stated plainly:
 *  - BOT (interface class 08, subclass 06, protocol 50) is implemented:
 *    CBW -> data stage -> CSW, over xHCI bulk IN/OUT endpoints.
 *  - UASP (protocol 62) is DETECTION ONLY: if a device reports 62, this
 *    driver logs uasp_detected=1, sets bot_fallback=1, and still uses
 *    BOT. UASP command queuing / streams are NOT implemented and are
 *    NOT claimed to work. Most flash drives / HDD enclosures also
 *    offer BOT, which is why the fallback is useful; a UASP-only
 *    device will be detected but will not transfer.
 *  - SCSI: INQUIRY, TEST UNIT READY, REQUEST SENSE, READ CAPACITY (10),
 *    READ (10), WRITE (10). READ is the primary path; WRITE is
 *    implemented (bounded, see usb_storage_write_blocks) but was NOT
 *    destructively tested here — use it only on scratch media.
 *  - Transport: xHCI bulk endpoints, via xhci_bulk_init() /
 *    xhci_bulk_transfer() in xhci.c. usb_storage_detect() does NOT
 *    touch the controller beyond xhci_detect(); usb_storage_init()
 *    must be called AFTER xhci_init() in kernel.c's specific-driver
 *    stage, not from generic_boot_init() (which is detection-only
 *    and must not re-init xHCI).
 *  - FAT integration: fat.c today takes a whole volume image, not a
 *    block callback, so USB FAT is integrated at the block layer:
 *    usb_storage_read_blocks() / usb_storage_read_bpb() validate the
 *    BPB (0x55AA, bytes/sector), and a caller streams blocks with
 *    usb_storage_block_read(). A future fat-from-block-device reader
 *    can use that callback directly; do NOT claim fat.c now reads a
 *    USB volume without loading it.
 *  - UNTESTED on real hardware: no flash drive, external HDD, USB 2
 *    vs 3, or xHCI transfer was run in this task. See README notes.
 *  - Boot never fails for USB: no controller, no device, or a failed
 *    transfer leaves transport_ready=0 and boot continues on
 *    NVMe/AHCI/UEFI media.
 */
#define USB_STORAGE_CLASS_MSC 0x08
#define USB_STORAGE_SUBCLASS_SCSI 0x06
#define USB_STORAGE_PROTO_BOT 50
#define USB_STORAGE_PROTO_UASP 62

#define USB_SCSI_TEST_UNIT_READY 0x00
#define USB_SCSI_REQUEST_SENSE 0x03
#define USB_SCSI_INQUIRY 0x12
#define USB_SCSI_READ_CAPACITY_10 0x25
#define USB_SCSI_READ_10 0x28
#define USB_SCSI_WRITE_10 0x2A

#define USB_STORAGE_MAX_WRITE_BLOCKS 128

typedef struct __attribute__((packed)) {
    uint32_t signature; /* 0x43425355 "USBC" */
    uint32_t tag;
    uint32_t data_length;
    uint8_t flags; /* bit 7 = data IN (device->host) */
    uint8_t lun;
    uint8_t cb_length;
    uint8_t cb[16];
} usb_cbw_t;

typedef struct __attribute__((packed)) {
    uint32_t signature; /* 0x53425355 "USBS" */
    uint32_t tag;
    uint32_t residue;
    uint8_t status; /* 0=passed, 1=failed, 2=phase error */
} usb_csw_t;

typedef struct {
    int found;              /* xHCI controller present (detect stage) */
    int controller_present;
    int device_present;     /* bulk endpoints configured (init stage) */
    int transport_ready;    /* INQUIRY + READ CAPACITY succeeded */
    int uasp_detected;      /* protocol 62 seen */
    int bot_fallback;       /* 1 if UASP seen but BOT is used */
    uint8_t slot;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t interface_number;
    uint8_t bulk_in_ep;     /* e.g. 0x81 */
    uint8_t bulk_out_ep;    /* e.g. 0x02 */
    uint16_t bulk_in_mps;
    uint16_t bulk_out_mps;
    uint32_t block_size;    /* bytes, usually 512 */
    uint64_t block_count;   /* last LBA + 1 */
    int removable;
    char inquiry_vendor[9];
    char inquiry_product[17];
    char inquiry_revision[5];
    uint8_t last_sense_key;
    uint8_t last_asc;
    uint8_t last_ascq;
    uint8_t last_csw_status;
    uint32_t tag;
} usb_storage_t;

int usb_storage_detect(usb_storage_t *out);
int usb_storage_init(usb_storage_t *out);
int usb_storage_inquiry(usb_storage_t *dev);
int usb_storage_test_unit_ready(usb_storage_t *dev);
int usb_storage_request_sense(usb_storage_t *dev);
int usb_storage_read_capacity(usb_storage_t *dev);
int usb_storage_exec_scsi(usb_storage_t *dev, const uint8_t *cdb, uint8_t cdb_len,
                          uint8_t *data, uint32_t data_len, int data_in);
int usb_storage_read_blocks(usb_storage_t *dev, uint32_t lba, uint32_t count, uint8_t *buf);
int usb_storage_write_blocks(usb_storage_t *dev, uint32_t lba, uint32_t count, const uint8_t *buf);
int usb_storage_read_bpb(usb_storage_t *dev, uint8_t *sector512);
int usb_storage_block_read(usb_storage_t *dev, uint32_t lba, uint8_t *buf);
uint32_t usb_storage_block_size(const usb_storage_t *dev);
uint64_t usb_storage_block_count(const usb_storage_t *dev);
const char *usb_storage_status_name(int ready);
#endif

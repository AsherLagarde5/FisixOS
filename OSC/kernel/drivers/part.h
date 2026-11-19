/* part.h - GPT + MBR partition table parsing and editing.
 *
 * Original FisixOS code for the read/write stage. Works on any
 * fisix_block_dev_t (host file, RAM disk, UEFI Block I/O, NVMe),
 * so the same code runs in the host test, in the bootloader
 * terminal (`part` command) and, once a kernel block driver is
 * wired up, in the kernel.
 *
 * Scope, stated plainly:
 * - GPT: parse (primary header, fall back to backup header when
 *   the primary CRC is bad), create a fresh GPT, add / delete /
 *   resize partitions, set the legacy-BIOS-bootable attribute.
 *   128 entries, 128 bytes each, 512-byte sectors only (the
 *   installer has the same limit). Every edit rewrites BOTH
 *   the primary and the backup header + entries, with fresh
 *   CRC32s, so the table stays valid after every operation.
 * - MBR: parse, create a fresh MBR, add / delete / resize
 *   primary partitions (max 4), set the 0x80 boot flag on one.
 * - No filesystem resizing: part_resize only moves the
 *   partition's last LBA. Growing a partition does NOT grow
 *   the FAT/NTFS/ext filesystem inside it, and shrinking one
 *   can destroy filesystem data past the new end - the caller
 *   must shrink the filesystem first. The host test resizes
 *   empty / freshly-made partitions only.
 * - No LVM, RAID, dynamic disks, or encrypted volumes.
 * - OS detection (part_os_detect_on_volume) classifies a FAT
 *   volume image by the bootloader files it contains; it does
 *   not mount NTFS/ext4, so a Windows/Linux install whose ESP
 *   is separate is detected from its ESP, not from its data
 *   partition.
 */
#ifndef FISIX_PART_H
#define FISIX_PART_H
#include <stdint.h>
#include <stddef.h>
#include "block.h"

#define PART_OK 0
#define PART_ERR_GENERIC (-1)
#define PART_ERR_NO_TABLE (-2)
#define PART_ERR_BAD_CRC (-3)
#define PART_ERR_OVERLAP (-4)
#define PART_ERR_NO_SLOT (-5)
#define PART_ERR_OUT_OF_RANGE (-6)
#define PART_ERR_NOT_FOUND (-7)
#define PART_ERR_UNSUPPORTED (-8)
#define PART_ERR_NO_SPACE (-9)

#define PART_MAX_ENTRIES 128
#define PART_NAME_MAX 36

typedef enum { PART_SCHEME_NONE = 0, PART_SCHEME_GPT = 1, PART_SCHEME_MBR = 2 } part_scheme_t;

typedef struct {
    int index;               /* GPT entry index (0..127) or MBR slot (0..3) */
    uint64_t first_lba;
    uint64_t last_lba;       /* inclusive */
    uint8_t type_guid[16];   /* GPT type GUID (disk byte order); MBR: type in [0] */
    uint8_t unique_guid[16]; /* GPT unique GUID; MBR: zero */
    uint64_t attrs;          /* GPT attributes; MBR: boot flag in bit 0 */
    char name[PART_NAME_MAX + 1]; /* GPT name (ASCII, lossy); MBR: "" */
    uint8_t mbr_type;        /* MBR partition type byte (0x0C, 0x83, 0x07, ...) */
    int bootable;            /* MBR 0x80 flag, or GPT legacy-bootable attr bit */
} part_entry_t;

typedef struct {
    part_scheme_t scheme;
    uint64_t disk_sectors;
    uint64_t first_usable;   /* GPT only */
    uint64_t last_usable;    /* GPT only */
    int count;
    part_entry_t entries[PART_MAX_ENTRIES];
} part_table_t;

/* Well-known GPT type GUIDs (disk byte order: first three
 * fields little-endian, as stored on disk). */
extern const uint8_t PART_GUID_ESP[16];        /* EFI System Partition */
extern const uint8_t PART_GUID_MS_BASIC[16];   /* Microsoft basic data */
extern const uint8_t PART_GUID_LINUX_FS[16];   /* Linux filesystem */
extern const uint8_t PART_GUID_LINUX_SWAP[16]; /* Linux swap */
extern const uint8_t PART_GUID_FISIX_DATA[16]; /* FisixOS data partition */

/* Human-readable type name for a GPT type GUID or MBR type byte. */
void part_type_name(const uint8_t type_guid[16], uint8_t mbr_type, char *out, size_t cap);

/* Probe a disk: fills table (entries, scheme, usable range).
 * GPT is tried first; a protective MBR (0xEE) with no valid GPT
 * is an error, not an MBR table. Returns PART_OK or PART_ERR_*. */
int part_probe(const fisix_block_dev_t *dev, part_table_t *table);

/* Create a fresh, empty GPT on a disk (wipes MBR + primary and
 * backup GPT areas). The disk keeps its size; usable LBAs are
 * 34 .. sectors-34. */
int part_create_gpt(const fisix_block_dev_t *dev);
/* Create a fresh, empty MBR (one zeroed sector + 0x55AA). */
int part_create_mbr(const fisix_block_dev_t *dev);

/* Add a partition. first_lba 0 = auto-place in the largest free
 * gap, 1 MiB aligned (2048 sectors). size_sectors is used when
 * first_lba is 0, or ignored when first_lba is given (then
 * last_lba must be given instead: pass size_sectors 0 and set
 * last_lba). Simpler form: use part_add_size() below. */
int part_add(const fisix_block_dev_t *dev, const uint8_t type_guid[16],
             uint64_t first_lba, uint64_t last_lba, const char *name, uint64_t attrs,
             int *index_out);
/* Add a partition of size_sectors at the first fitting free gap. */
int part_add_size(const fisix_block_dev_t *dev, const uint8_t type_guid[16],
                  uint64_t size_sectors, const char *name, uint64_t attrs,
                  int *index_out);
/* Add an MBR primary partition (type byte, e.g. 0x0C FAT32 LBA,
 * 0x83 Linux, 0x07 NTFS). first_lba 0 = auto-place. */
int part_add_mbr(const fisix_block_dev_t *dev, uint8_t mbr_type, int bootable,
                 uint64_t first_lba, uint64_t size_sectors, int *index_out);

/* Delete a partition (GPT entry zeroed, MBR slot zeroed). */
int part_delete(const fisix_block_dev_t *dev, int index);
/* Resize: move a partition's last LBA. Growing is refused when
 * the space after it is not free; shrinking is always allowed
 * if new_last >= first. The filesystem inside is NOT resized. */
int part_resize(const fisix_block_dev_t *dev, int index, uint64_t new_last_lba);
/* Set the boot flag on one partition and clear it on the others
 * (MBR 0x80 flag; GPT legacy-BIOS-bootable attribute bit 2).
 * Note: UEFI does not boot from this flag - it boots from the
 * ESP's \EFI\BOOT\BOOTX64.EFI (or the firmware BootOrder). The
 * flag matters for legacy BIOS boot and for humans reading the
 * table. */
int part_set_boot(const fisix_block_dev_t *dev, int index);

/* Largest free gap on a GPT disk (1 MiB aligned start). */
int part_largest_free_gap(const fisix_block_dev_t *dev, uint64_t *first_out, uint64_t *last_out);

/* ---- multi-OS detection ---- */
typedef enum {
    PART_OS_UNKNOWN = 0,
    PART_OS_FISIX = 1,
    PART_OS_WINDOWS = 2,
    PART_OS_LINUX = 3,
    PART_OS_OTHER_UEFI = 4
} part_os_kind_t;
/* Classify a FAT volume image (starting at its BPB) by the
 * bootloader files it contains. label_out gets e.g. "Windows",
 * "Linux (Ubuntu)", "FisixOS", "Other UEFI", "Unknown".
 * Returns the kind, or PART_OS_UNKNOWN. */
part_os_kind_t part_os_detect_on_volume(const uint8_t *vol, size_t len, char *label_out, size_t label_cap);

#endif

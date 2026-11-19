/* ntfs.h - NTFS read AND write support for FisixOS.
 *
 * Clean-room FisixOS original, GPLv2 (same as FisixOS). NOT a copy
 * of ntfs-3g / libntfs: no ntfs-3g source was copied into this
 * file or into ntfs.c. The on-disk layout implemented here is the
 * public NTFS format (boot sector, MFT records, attributes,
 * runlists, index blocks), verified against volumes created by
 * mkntfs / ntfs-3g (see tools/ntfs_host_test.c and
 * docs/NTFS.md). ntfs-3g (GPLv2, Copyright (c) the ntfs-3g
 * authors) was used only as the independent host-side checker:
 * it created the test volumes and verified FisixOS-written files
 * afterwards. If a future stage ports ntfs-3g code, its GPLv2
 * header and attribution must be added here first.
 *
 * What works (v1, 2026-11-10):
 *  - Probe + mount of an NTFS volume (whole volume image or a
 *    partition, via caller-supplied block callbacks).
 *  - Read: files (resident and non-resident $DATA, sparse holes
 *    read as zeros), directories (INDEX_ROOT + INDEX_ALLOCATION /
 *    INDX blocks), nested paths, case-insensitive lookup.
 *  - Write: create, overwrite (grow, shrink, resident <->
 *    non-resident), delete files, create directories.
 *    Allocation goes through the real $Bitmap; MFT records are
 *    allocated through the $MFT $BITMAP; directory entries are
 *    inserted in $UpCase sort order.
 *  - Safety (see docs/NTFS.md for the full list): the volume is
 *    validated before any write (boot sector, $MFT mirror,
 *    backup boot sector); write mounts are REFUSED on a dirty
 *    volume, on a hibernated volume (hiberfil.sys present), and
 *    when there is no write transport; compressed, encrypted and
 *    sparse files are refused for writing; system files
 *    (MFT records 0-15) can never be written or deleted; the
 *    $Volume dirty flag is set before a mutation starts and
 *    cleared only after it completes, so a crash mid-write
 *    leaves the volume dirty and Windows runs chkdsk instead
 *    of trusting a half-written state. $LogFile journal replay
 *    is NOT implemented - that is why dirty volumes are
 *    refused instead of repaired.
 *
 * What does NOT work (stated plainly, not faked):
 *  - $LogFile replay, compressed files (read or write),
 *    encrypted files, alternate data streams (only the unnamed
 *    $DATA is read/written), files with an $ATTRIBUTE_LIST /
 *    extension MFT records, hard links, quotas, reparse points,
 *    volumes with cluster size > 16 KiB or MFT record > 4 KiB,
 *    directory insert when every index block is full (returns
 *    NTFS_ERR_DIR_FULL instead of splitting the B-tree),
 *    non-ASCII sort order is approximate (ASCII fold only).
 *  - In the kernel, NVMe has reads but no writes yet, so the
 *    kernel NTFS path (ntfs_kernel.c) is READ-ONLY for now:
 *    ntfs_mount(writable=1) there fails with
 *    NTFS_ERR_READ_ONLY. Full kernel read/write needs an NVMe
 *    write command first. The loader terminal `ntfs` command
 *    (boot/efi_main.c, EFI Block I/O) CAN write, because EFI
 *    Block I/O has WriteBlocks.
 *
 * Freestanding: no libc, no heap. All state is static. The
 * caller supplies block read/write callbacks; byte offsets
 * passed to them are absolute within the NTFS volume (the
 * caller applies any partition offset).
 */
#ifndef FISIX_NTFS_H
#define FISIX_NTFS_H
#include <stdint.h>
#include <stddef.h>

#define NTFS_OK 0
#define NTFS_ERR_NOT_NTFS (-1)
#define NTFS_ERR_IO (-2)
#define NTFS_ERR_CORRUPT (-3)
#define NTFS_ERR_NOT_FOUND (-4)
#define NTFS_ERR_NOT_DIR (-5)
#define NTFS_ERR_IS_DIR (-6)
#define NTFS_ERR_READ_ONLY (-7)
#define NTFS_ERR_UNSUPPORTED (-8)
#define NTFS_ERR_NO_SPACE (-9)
#define NTFS_ERR_TOO_BIG (-10)
#define NTFS_ERR_DIR_FULL (-11)
#define NTFS_ERR_EXISTS (-12)
#define NTFS_ERR_BAD_ARG (-13)
#define NTFS_ERR_SYSTEM_FILE (-14)
#define NTFS_ERR_DIR_NOT_EMPTY (-15)

/* Block callbacks. read/write return 0 on success, non-zero on
 * failure. byte_off is an absolute byte offset inside the NTFS
 * volume; len is in bytes. A NULL write callback means the
 * transport is read-only (kernel NVMe today). */
typedef struct {
    int (*read)(void *ctx, uint64_t byte_off, uint32_t len, uint8_t *buf);
    int (*write)(void *ctx, uint64_t byte_off, uint32_t len, const uint8_t *buf);
    void *ctx;
} ntfs_block_ops_t;

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint32_t cluster_bytes;
    uint64_t total_clusters;
    uint64_t total_sectors;
    uint64_t mft_lcn;
    uint64_t mftmirr_lcn;
    uint32_t mft_record_bytes;
    uint32_t index_block_bytes;
    uint64_t serial;
    int dirty;          /* $Volume dirty flag was set at mount */
    int hibernated;     /* hiberfil.sys present in the root */
    int backup_boot_ok; /* backup boot sector at end of volume sane */
    char volume_label[64];
} ntfs_info_t;

typedef struct {
    char name[256]; /* UTF-8 */
    uint64_t size;
    uint64_t alloc_size;
    int is_dir;
    uint64_t mft_no;
} ntfs_entry_t;

/* Validate the boot sector only; fills info, mounts nothing. */
int ntfs_probe(const ntfs_block_ops_t *ops, ntfs_info_t *info);
/* Mount one volume (static state; only one at a time).
 * writable=0: read-only mount, allowed even on a dirty volume.
 * writable=1: refused (NTFS_ERR_READ_ONLY) if the volume is
 * dirty, hibernated, or the ops have no write callback. */
int ntfs_mount(const ntfs_block_ops_t *ops, int writable, ntfs_info_t *info);
void ntfs_unmount(void);
int ntfs_is_mounted(void);
const char *ntfs_error_name(int err);

int ntfs_list(const char *path, ntfs_entry_t *out, int max_entries, int *count);
int ntfs_stat(const char *path, ntfs_entry_t *out);
int ntfs_read_file(const char *path, uint8_t *out, size_t out_cap, uint64_t *out_len);
int ntfs_write_file(const char *path, const uint8_t *data, uint64_t len);
int ntfs_delete(const char *path);
int ntfs_mkdir(const char *path);

#endif

/* block.h - block-device abstraction for FisixOS.
 *
 * One small interface that every disk speaks, so the FAT
 * writer (fat.c, which works on a volume image) and the
 * partition code (part.c) do not care whether the bytes live
 * in a host file, a RAM disk, UEFI Block I/O (loader, before
 * ExitBootServices) or NVMe (kernel, after ExitBootServices).
 *
 * A device is a sector array: sector_size is usually 512.
 * read/write move whole sectors only; block_read_bytes /
 * block_write_bytes handle unaligned byte ranges with a
 * read-modify-write of the edge sectors.
 *
 * Backends in this file: a RAM disk over a caller-owned
 * buffer. File-backed (host test) and UEFI Block I/O
 * (loader) backends live in their own files and fill in the
 * same struct. NVMe gets its constructor in nvme.c.
 */
#ifndef FISIX_BLOCK_H
#define FISIX_BLOCK_H
#include <stdint.h>
#include <stddef.h>

typedef struct fisix_block_dev fisix_block_dev_t;
struct fisix_block_dev {
    uint64_t sectors;      /* number of sectors on the device */
    uint32_t sector_size;  /* bytes per sector, usually 512 */
    void *ctx;             /* backend-private */
    /* Return 0 on success, negative on error. count is in sectors. */
    int (*read)(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf);
    int (*write)(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf);
    int (*flush)(void *ctx); /* may be NULL */
};

/* Byte-granular access over the sector interface. */
int block_read_bytes(const fisix_block_dev_t *dev, uint64_t byte_off, uint8_t *buf, size_t len);
int block_write_bytes(const fisix_block_dev_t *dev, uint64_t byte_off, const uint8_t *buf, size_t len);
int block_flush(const fisix_block_dev_t *dev);

/* RAM disk over a caller-owned buffer (buf must be
 * sectors*sector_size bytes). The device does not own buf. */
typedef struct { uint8_t *buf; uint64_t sectors; uint32_t sector_size; } fisix_ramdisk_t;
int block_ramdisk_init(fisix_block_dev_t *dev, fisix_ramdisk_t *rd, uint8_t *buf, uint64_t sectors, uint32_t sector_size);

#endif

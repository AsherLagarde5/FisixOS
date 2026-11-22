/* block.c - block-device abstraction (see block.h).
 * Original FisixOS code. Freestanding: no libc. */
#include "block.h"

int block_read_bytes(const fisix_block_dev_t *dev, uint64_t byte_off, uint8_t *buf, size_t len)
{
    uint64_t ss, first, last, s;
    uint8_t sec[4096];
    if (!dev || !dev->read || !buf) return -1;
    ss = dev->sector_size;
    if (ss==0 || ss>4096) return -1;
    if (len==0) return 0;
    /* Overflow-safe range check: byte_off + len can wrap past 2^64,
     * which would make the old `byte_off + len > sectors * ss` test
     * pass for an out-of-range request. Compare without adding. */
    if (len > dev->sectors * ss || byte_off > dev->sectors * ss - len) return -1;
    first = byte_off / ss; last = (byte_off + len - 1) / ss;
    for (s=first; s<=last; s++) {
        uint64_t sec_off = s * ss;
        size_t skip = (byte_off > sec_off) ? (size_t)(byte_off - sec_off) : 0;
        size_t take = (size_t)ss - skip;
        size_t done = (size_t)(byte_off + len - sec_off - skip);
        if (take > done) take = done;
        if (skip==0 && take==(size_t)ss) {
            if (dev->read(dev->ctx, s, 1, buf + (sec_off + skip - byte_off))!=0) return -1;
        } else {
            size_t i;
            if (dev->read(dev->ctx, s, 1, sec)!=0) return -1;
            for (i=0;i<take;i++) buf[(sec_off + skip - byte_off)+i] = sec[i+skip];
        }
    }
    return 0;
}

int block_write_bytes(const fisix_block_dev_t *dev, uint64_t byte_off, const uint8_t *buf, size_t len)
{
    uint64_t ss, first, last, s;
    uint8_t sec[4096];
    if (!dev || !dev->write || !buf) return -1;
    ss = dev->sector_size;
    if (ss==0 || ss>4096) return -1;
    if (len==0) return 0;
    /* Same overflow-safe range check as block_read_bytes. */
    if (len > dev->sectors * ss || byte_off > dev->sectors * ss - len) return -1;
    first = byte_off / ss; last = (byte_off + len - 1) / ss;
    for (s=first; s<=last; s++) {
        uint64_t sec_off = s * ss;
        size_t skip = (byte_off > sec_off) ? (size_t)(byte_off - sec_off) : 0;
        size_t take = (size_t)ss - skip;
        size_t done = (size_t)(byte_off + len - sec_off - skip);
        size_t i;
        if (take > done) take = done;
        if (skip==0 && take==(size_t)ss) {
            if (dev->write(dev->ctx, s, 1, buf + (sec_off + skip - byte_off))!=0) return -1;
        } else {
            if (dev->read(dev->ctx, s, 1, sec)!=0) return -1;
            for (i=0;i<take;i++) sec[skip+i] = buf[(sec_off + skip - byte_off)+i];
            if (dev->write(dev->ctx, s, 1, sec)!=0) return -1;
        }
    }
    return 0;
}

int block_flush(const fisix_block_dev_t *dev)
{
    if (!dev) return -1;
    if (dev->flush) return dev->flush(dev->ctx);
    return 0;
}

static int ramdisk_read(void *ctx, uint64_t lba, uint32_t count, uint8_t *buf)
{
    fisix_ramdisk_t *rd = (fisix_ramdisk_t *)ctx;
    uint64_t i;
    if (!rd || !buf) return -1;
    if (lba + count > rd->sectors) return -1;
    for (i=0;i<(uint64_t)count * rd->sector_size;i++) buf[i] = rd->buf[lba * rd->sector_size + i];
    return 0;
}
static int ramdisk_write(void *ctx, uint64_t lba, uint32_t count, const uint8_t *buf)
{
    fisix_ramdisk_t *rd = (fisix_ramdisk_t *)ctx;
    uint64_t i;
    if (!rd || !buf) return -1;
    if (lba + count > rd->sectors) return -1;
    for (i=0;i<(uint64_t)count * rd->sector_size;i++) rd->buf[lba * rd->sector_size + i] = buf[i];
    return 0;
}
int block_ramdisk_init(fisix_block_dev_t *dev, fisix_ramdisk_t *rd, uint8_t *buf, uint64_t sectors, uint32_t sector_size)
{
    if (!dev || !rd || !buf || sectors==0 || sector_size==0) return -1;
    rd->buf = buf; rd->sectors = sectors; rd->sector_size = sector_size;
    dev->sectors = sectors; dev->sector_size = sector_size;
    dev->ctx = rd; dev->read = ramdisk_read; dev->write = ramdisk_write; dev->flush = 0;
    return 0;
}

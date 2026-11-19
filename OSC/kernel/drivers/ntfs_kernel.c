/* Kernel-side NTFS scan (read-only) - see ntfs_kernel.h.
 * Freestanding, no libc, no heap: all state is static.
 * Runs once at kernel startup, after nvme_init(). */
#include <stdint.h>
#include <stddef.h>
#include "ntfs.h"
#include "ntfs_kernel.h"
#include "nvme.h"
#include "block.h"

/* Microsoft Basic Data partition type GUID, little-endian bytes
   as they appear in a GPT entry (first 16 bytes). */
static const uint8_t NTFS_K_MS_BASIC[16] = {
    0xA2, 0xA0, 0xD0, 0xEB, 0xE5, 0xB9, 0x33, 0x44,
    0x87, 0xC0, 0x68, 0xB6, 0xB7, 0x26, 0x99, 0xC7
};

typedef struct {
    fisix_block_dev_t dev;
    uint64_t part_first_lba; /* partition start, in disk LBAs */
    uint32_t sector_size;
} ntfs_k_ctx_t;

static ntfs_k_ctx_t g_kctx;
static char g_ktext[2048];
static int g_kfound = 0;

static uint64_t k_u64le(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* ntfs.c block ops over the NVMe block device. Byte offsets are
   absolute within the NTFS volume; the partition start LBA is
   added here. All ntfs.c I/O is in multiples of the sector size
   when the volume's bytes/sector is 512 (the only sector size
   this path supports). */
static int ntfs_k_read(void *ctx, uint64_t off, uint32_t len, uint8_t *buf)
{
    ntfs_k_ctx_t *c = (ntfs_k_ctx_t *)ctx;
    uint64_t lba;
    uint32_t count;
    if (!c || !buf) return -1;
    if (c->sector_size != 512) return -1;
    if ((off % 512) || (len % 512)) return -1;
    lba = c->part_first_lba + off / 512;
    count = len / 512;
    if (!c->dev.read) return -1;
    return c->dev.read(c->dev.ctx, lba, count, buf);
}

/* Append text to g_ktext, truncating instead of overflowing. */
static void k_append(const char *s)
{
    size_t n = 0, i = 0;
    while (g_ktext[n]) n++;
    while (s[i] && n + 1 < sizeof(g_ktext)) g_ktext[n++] = s[i++];
    g_ktext[n] = 0;
}
static void k_append_u64(uint64_t v)
{
    char tmp[24];
    int i = 0, j;
    if (v == 0) { k_append("0"); return; }
    while (v > 0 && i < 23) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    for (j = i - 1; j >= 0; j--) { char c[2]; c[0] = tmp[j]; c[1] = 0; k_append(c); }
}

/* Try one partition start LBA as an NTFS volume. On success the
   root listing is in g_ktext and g_kfound is set. */
static int ntfs_k_try(uint64_t first_lba)
{
    ntfs_block_ops_t ops;
    ntfs_info_t info;
    ntfs_entry_t entries[24];
    int count = 0, i, r;
    g_kctx.part_first_lba = first_lba;
    ops.read = ntfs_k_read;
    ops.write = 0; /* read-only: no write callback */
    ops.ctx = &g_kctx;
    r = ntfs_probe(&ops, &info);
    if (r != NTFS_OK) return 0;
    r = ntfs_mount(&ops, 0, &info);
    if (r != NTFS_OK) { ntfs_unmount(); return 0; }
    g_ktext[0] = 0;
    k_append("NTFS volume: ");
    k_append(info.volume_label[0] ? info.volume_label : "(no label)");
    k_append("\n");
    r = ntfs_list("/", entries, 24, &count);
    ntfs_unmount();
    if (r != NTFS_OK) { g_ktext[0] = 0; return 0; }
    for (i = 0; i < count; i++) {
        k_append(entries[i].name);
        if (entries[i].is_dir) k_append(" <dir>");
        else { k_append(" "); k_append_u64(entries[i].size); k_append(" bytes"); }
        k_append("\n");
    }
    if (count == 0) k_append("(empty)\n");
    g_kfound = 1;
    return 1;
}

void ntfs_kernel_scan(void)
{
    uint8_t hdr[512];
    uint8_t entries[16384];
    int i;
    g_ktext[0] = 0;
    g_kfound = 0;
    if (nvme_block_dev(&g_kctx.dev) != 0) return;
    g_kctx.sector_size = g_kctx.dev.sector_size;
    if (g_kctx.sector_size != 512) return; /* 4Kn disks: unsupported here */
    /* Whole disk first: superfloppy NTFS (no partition table). */
    if (ntfs_k_try(0)) return;
    /* GPT: read the header at LBA 1, then the entries at LBA 2. */
    if (!g_kctx.dev.read || g_kctx.dev.read(g_kctx.dev.ctx, 1, 1, hdr) != 0) return;
    if (!(hdr[0] == 'E' && hdr[1] == 'F' && hdr[2] == 'I' && hdr[3] == ' ' &&
          hdr[4] == 'P' && hdr[5] == 'A' && hdr[6] == 'R' && hdr[7] == 'T')) return;
    if (g_kctx.dev.read(g_kctx.dev.ctx, 2, 32, entries) != 0) return;
    for (i = 0; i < 128; i++) {
        const uint8_t *e = entries + i * 128;
        int j, match = 1, nonzero = 0;
        uint64_t first;
        for (j = 0; j < 16; j++) if (e[j]) nonzero = 1;
        if (!nonzero) continue;
        for (j = 0; j < 16; j++) if (e[j] != NTFS_K_MS_BASIC[j]) { match = 0; break; }
        if (!match) continue;
        first = k_u64le(e + 32);
        if (first == 0) continue;
        if (ntfs_k_try(first)) return;
    }
}

const char *ntfs_kernel_text(void)
{
    if (g_kfound) return g_ktext;
    return "No NTFS volume found on the NVMe disk.\n"
           "Use the loader `ntfs` command to scan all disks.\n";
}

int ntfs_kernel_found(void) { return g_kfound; }

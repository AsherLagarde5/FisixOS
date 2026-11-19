/* ntfs.c - NTFS read AND write for FisixOS. Clean-room FisixOS
 * original, GPLv2. See ntfs.h for scope, safety measures and the
 * list of what is NOT implemented. No ntfs-3g code was copied.
 * Format reference: the public NTFS on-disk layout, verified
 * against mkntfs/ntfs-3g volumes (docs/NTFS.md, probe in
 * tools/ntfs_test/probe.py).
 *
 * Layout facts used here (all verified on a real mkntfs volume):
 *  - $Boot: bytes/sector @11, sectors/cluster @13, total sectors
 *    @0x28, MFT LCN @0x30, MFTMirr LCN @0x38, clusters/record
 *    @0x40 (negative = 2^-n bytes), clusters/index block @0x44,
 *    serial @0x48, 0x55AA @0x1FE, OEM "NTFS    " @3.
 *  - MFT record: "FILE" @0, USA offset @4, USA count @6, LSN @8,
 *    sequence @16, links @18, first attr @20, flags @22
 *    (1 = in use, 2 = directory), bytes used @24, bytes alloc
 *    @28, base record @32, next attr id @40.
 *  - USA fixup: USA[0] is the sequence; USA[1..] hold the
 *    original last 2 bytes of each sector of the record/block.
 *  - Attribute: type @0, length @4, non-resident @8, name len
 *    @9, name offset @10, flags @12 (1 compressed, 0x4000
 *    encrypted, 0x8000 sparse), id @14. Resident: content size
 *    @16, content offset @20. Non-resident: lowest VCN @16,
 *    highest VCN @24, runlist offset @32, allocated @40,
 *    real @48, initialized @56.
 *  - $FILE_NAME value (66 bytes fixed): parent ref @0,
 *    creation @8, modification @16, MFT modification @24,
 *    access @32, allocated @40, real @48, flags @56 (bit 28 =
 *    directory), reparse @60, name length (chars) @64,
 *    namespace @65 (1 Win32, 2 DOS, 3 Win32&DOS), name UTF-16
 *    @66.
 *  - $VOLUME_INFORMATION value: reserved 8, major @8, minor
 *    @9, flags @10 (bit 0 = dirty).
 *  - $INDEX_ROOT value: indexed attr type @0, collation @4,
 *    index block size @8, clusters/block @12, then an index
 *    header @16: entries offset @0, index length @4,
 *    allocated @8, flags @12 (1 = large, has INDEX_ALLOCATION).
 *    Entries start at value + 16 + entries offset.
 *  - Index entry: file ref @0, entry length @8, stream length
 *    @10, flags @12 (1 = subnode VCN follows the stream,
 *    2 = last entry), stream (a $FILE_NAME value) @16.
 *  - INDX block: "INDX" @0, USA @4/@6, LSN @8, VCN @16, index
 *    header @24, entries at 24 + entries offset.
 *  - Runlist: pairs; header byte low nibble = length field
 *    size, high nibble = offset field size; length is
 *    unsigned LE, offset delta is signed LE relative to the
 *    previous LCN; offset size 0 = sparse hole; 0x00 ends it.
 */
#include "ntfs.h"
#ifdef FISIX_NTFS_HOST_DEBUG
#include <stdio.h>
#define NDBG(...) fprintf(stderr, "[ntfs] " __VA_ARGS__)
#else
#define NDBG(...) do { } while (0)
#endif

/* ---- local helpers (no libc: the kernel build is freestanding
 * and the loader links this file too; deb.c already defines the
 * global memcpy/memset, so these are static and prefixed) ---- */
static void n_memcpy(uint8_t *d, const uint8_t *s, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) d[i] = s[i]; }
static void n_memset(uint8_t *d, uint8_t v, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) d[i] = v; }
static int n_memcmp(const uint8_t *a, const uint8_t *b, uint32_t n) { uint32_t i; for (i = 0; i < n; i++) { if (a[i] != b[i]) return (int)a[i] - (int)b[i]; } return 0; }
static int n_strcmp(const char *a, const char *b) { while (*a && *a == *b) { a++; b++; } return (int)(unsigned char)*a - (int)(unsigned char)*b; }

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
static uint64_t rd64(const uint8_t *p) { uint64_t v = 0; int i; for (i = 7; i >= 0; i--) v = (v << 8) | p[i]; return v; }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr64(uint8_t *p, uint64_t v) { int i; for (i = 0; i < 8; i++) { p[i] = (uint8_t)v; v >>= 8; } }

#define NTFS_MAX_RUNS 32
#define NTFS_MAX_CLUSTER 16384
#define NTFS_MAX_RECORD 4096
#define NTFS_ATTR_STDINFO 0x10
#define NTFS_ATTR_FILENAME 0x30
#define NTFS_ATTR_SECURITY 0x50
#define NTFS_ATTR_VOLNAME 0x60
#define NTFS_ATTR_VOLINFO 0x70
#define NTFS_ATTR_DATA 0x80
#define NTFS_ATTR_INDEX_ROOT 0x90
#define NTFS_ATTR_INDEX_ALLOC 0xA0
#define NTFS_ATTR_BITMAP 0xB0
#define NTFS_ATTR_END 0xFFFFFFFFu
#define NTFS_MFT_ROOT 5
/* "$I30" in UTF-16: the name of a directory's index attributes. */
static const uint16_t NTFS_NAME_I30[4] = { (uint16_t)'$', (uint16_t)'I', (uint16_t)'3', (uint16_t)'0' };

typedef struct { uint64_t lcn; uint64_t len; int sparse; } ntfs_run_t;
typedef struct { ntfs_run_t runs[NTFS_MAX_RUNS]; int count; uint64_t total_clusters; } ntfs_runlist_t;

typedef struct {
    int mounted;
    int writable;
    ntfs_block_ops_t ops;
    ntfs_info_t info;
    ntfs_runlist_t mft_runs;
    ntfs_runlist_t bitmap_runs;     /* $Bitmap $DATA */
    ntfs_runlist_t mftbmp_runs;     /* $MFT $BITMAP attribute */
    uint64_t mftbmp_size;           /* bytes of $MFT bitmap data */
    uint8_t rec[NTFS_MAX_RECORD];   /* scratch MFT record */
    uint8_t rec2[NTFS_MAX_RECORD];  /* second scratch MFT record */
    uint8_t cluster[NTFS_MAX_CLUSTER]; /* scratch cluster / index block */
    uint8_t bmp_cluster[NTFS_MAX_CLUSTER]; /* bitmap cache cluster */
    int bmp_kind;                   /* 0 none, 1 volume bitmap, 2 MFT bitmap */
    uint64_t bmp_vcn;               /* VCN of cached bitmap cluster */
} ntfs_vol_t;

static ntfs_vol_t g_vol;

const char *ntfs_error_name(int err)
{
    switch (err) {
        case NTFS_OK: return "ok";
        case NTFS_ERR_NOT_NTFS: return "not an NTFS volume";
        case NTFS_ERR_IO: return "block I/O error";
        case NTFS_ERR_CORRUPT: return "NTFS metadata corrupt or inconsistent";
        case NTFS_ERR_NOT_FOUND: return "not found";
        case NTFS_ERR_NOT_DIR: return "not a directory";
        case NTFS_ERR_IS_DIR: return "is a directory";
        case NTFS_ERR_READ_ONLY: return "volume is read-only (dirty, hibernated, or no write transport)";
        case NTFS_ERR_UNSUPPORTED: return "unsupported (compressed, encrypted, sparse, ADS, or extension record)";
        case NTFS_ERR_NO_SPACE: return "no space left on volume";
        case NTFS_ERR_TOO_BIG: return "too big for this implementation";
        case NTFS_ERR_DIR_FULL: return "directory index full (B-tree split not implemented)";
        case NTFS_ERR_EXISTS: return "already exists";
        case NTFS_ERR_BAD_ARG: return "bad argument or not mounted";
        case NTFS_ERR_SYSTEM_FILE: return "system file (MFT 0-15) is protected";
        case NTFS_ERR_DIR_NOT_EMPTY: return "directory is not empty";
        default: return "unknown error";
    }
}

int ntfs_is_mounted(void) { return g_vol.mounted; }
void ntfs_unmount(void) { g_vol.mounted = 0; g_vol.writable = 0; g_vol.bmp_kind = 0; }

/* ---- volume byte I/O, bounds-checked against the volume ---- */
static int vol_read(uint64_t off, uint32_t len, uint8_t *buf)
{
    uint64_t total;
    if (!g_vol.ops.read || !buf) return NTFS_ERR_IO;
    total = g_vol.info.total_sectors * (uint64_t)g_vol.info.bytes_per_sector;
    if (total && (off > total || (uint64_t)len > total - off)) return NTFS_ERR_IO;
    return g_vol.ops.read(g_vol.ops.ctx, off, len, buf) == 0 ? NTFS_OK : NTFS_ERR_IO;
}
static int vol_write(uint64_t off, uint32_t len, const uint8_t *buf)
{
    uint64_t total;
    if (!g_vol.ops.write || !buf) return NTFS_ERR_IO;
    total = g_vol.info.total_sectors * (uint64_t)g_vol.info.bytes_per_sector;
    if (total && (off > total || (uint64_t)len > total - off)) return NTFS_ERR_IO;
    return g_vol.ops.write(g_vol.ops.ctx, off, len, buf) == 0 ? NTFS_OK : NTFS_ERR_IO;
}
static int cluster_read(uint64_t lcn, uint8_t *buf)
{
    if (lcn >= g_vol.info.total_clusters) return NTFS_ERR_IO;
    return vol_read(lcn * (uint64_t)g_vol.info.cluster_bytes, g_vol.info.cluster_bytes, buf);
}
static int cluster_write(uint64_t lcn, const uint8_t *buf)
{
    if (lcn >= g_vol.info.total_clusters) return NTFS_ERR_IO;
    return vol_write(lcn * (uint64_t)g_vol.info.cluster_bytes, g_vol.info.cluster_bytes, buf);
}

/* ---- runlists ---- */
static int parse_runs(const uint8_t *attr, uint32_t attr_len, ntfs_runlist_t *out)
{
    uint32_t roff, p;
    uint64_t lcn = 0, vcn = 0;
    int n = 0;
    if (!attr || attr_len < 64) return NTFS_ERR_CORRUPT;
    roff = rd16(attr + 32);
    if (roff >= attr_len) return NTFS_ERR_CORRUPT;
    p = roff;
    out->count = 0;
    out->total_clusters = 0;
    while (p < attr_len) {
        uint8_t hdr = attr[p++];
        uint8_t len_size = (uint8_t)(hdr & 0x0F);
        uint8_t off_size = (uint8_t)(hdr >> 4);
        uint64_t len = 0, i;
        int64_t delta = 0;
        if (hdr == 0) break;
        if (len_size == 0 || len_size > 8 || off_size > 8) return NTFS_ERR_CORRUPT;
        if (p + len_size + off_size > attr_len) return NTFS_ERR_CORRUPT;
        for (i = 0; i < len_size; i++) len |= (uint64_t)attr[p++] << (8 * i);
        if (len == 0) return NTFS_ERR_CORRUPT;
        if (off_size > 0) {
            uint64_t raw = 0;
            for (i = 0; i < off_size; i++) raw |= (uint64_t)attr[p++] << (8 * i);
            /* Sign-extend the delta. */
            if (off_size < 8 && (raw & ((uint64_t)1 << (8 * off_size - 1)))) raw |= (~(uint64_t)0) << (8 * off_size);
            delta = (int64_t)raw;
            lcn = (uint64_t)((int64_t)lcn + delta);
            if ((int64_t)lcn < 0 || lcn + len > g_vol.info.total_clusters) return NTFS_ERR_CORRUPT;
        }
        if (n >= NTFS_MAX_RUNS) return NTFS_ERR_TOO_BIG;
        out->runs[n].lcn = lcn;
        out->runs[n].len = len;
        out->runs[n].sparse = (off_size == 0);
        n++;
        vcn += len;
        /* A run must not overlap an earlier run (except sparse). */
        {
            int j;
            for (j = 0; j + 1 < n; j++) {
                if (!out->runs[j].sparse && !out->runs[n - 1].sparse) {
                    uint64_t a0 = out->runs[j].lcn, a1 = a0 + out->runs[j].len;
                    uint64_t b0 = out->runs[n - 1].lcn, b1 = b0 + out->runs[n - 1].len;
                    if (b0 < a1 && a0 < b1) return NTFS_ERR_CORRUPT;
                }
            }
        }
    }
    out->count = n;
    out->total_clusters = vcn;
    /* The runlist must cover exactly lowest..highest VCN. */
    if (vcn != (uint64_t)(rd64(attr + 24) - rd64(attr + 16) + 1)) return NTFS_ERR_CORRUPT;
    return NTFS_OK;
}

/* Map a VCN to an LCN through a runlist. Returns NTFS_OK and sets
 * *sparse for holes; NTFS_ERR_CORRUPT if the VCN is not covered. */
static int vcn_to_lcn(const ntfs_runlist_t *rl, uint64_t vcn, uint64_t *lcn, int *sparse)
{
    int i;
    uint64_t base = 0;
    for (i = 0; i < rl->count; i++) {
        if (vcn >= base && vcn < base + rl->runs[i].len) {
            *sparse = rl->runs[i].sparse;
            *lcn = rl->runs[i].lcn + (vcn - base);
            return NTFS_OK;
        }
        base += rl->runs[i].len;
    }
    return NTFS_ERR_CORRUPT;
}

/* Read len bytes at byte offset off through a runlist (sparse
 * holes read as zeros). */
static int read_runs(const ntfs_runlist_t *rl, uint64_t off, uint32_t len, uint8_t *out)
{
    uint32_t done = 0;
    uint32_t cb = g_vol.info.cluster_bytes;
    while (done < len) {
        uint64_t pos = off + done;
        uint64_t vcn = pos / cb;
        uint32_t within = (uint32_t)(pos % cb);
        uint32_t chunk = cb - within;
        uint64_t lcn;
        int sparse, r;
        if (chunk > len - done) chunk = len - done;
        r = vcn_to_lcn(rl, vcn, &lcn, &sparse);
        if (r != NTFS_OK) return r;
        if (sparse) {
            n_memset(out + done, 0, chunk);
        } else {
            r = cluster_read(lcn, g_vol.cluster);
            if (r != NTFS_OK) return r;
            n_memcpy(out + done, g_vol.cluster + within, chunk);
        }
        done += chunk;
    }
    return NTFS_OK;
}

/* Write len bytes at byte offset off through a runlist.
 * Sparse runs are never written (callers refuse sparse files
 * for writing); partial clusters are read-modify-written. */
static int write_runs(const ntfs_runlist_t *rl, uint64_t off, uint32_t len, const uint8_t *data)
{
    uint32_t done = 0;
    uint32_t cb = g_vol.info.cluster_bytes;
    while (done < len) {
        uint64_t pos = off + done;
        uint64_t vcn = pos / cb;
        uint32_t within = (uint32_t)(pos % cb);
        uint32_t chunk = cb - within;
        uint64_t lcn;
        int sparse, r;
        if (chunk > len - done) chunk = len - done;
        r = vcn_to_lcn(rl, vcn, &lcn, &sparse);
        if (r != NTFS_OK) return r;
        if (sparse) return NTFS_ERR_UNSUPPORTED;
        if (within != 0 || chunk != cb) {
            r = cluster_read(lcn, g_vol.cluster);
            if (r != NTFS_OK) return r;
        } else {
            n_memset(g_vol.cluster, 0, cb);
        }
        n_memcpy(g_vol.cluster + within, data + done, chunk);
        r = cluster_write(lcn, g_vol.cluster);
        if (r != NTFS_OK) return r;
        done += chunk;
    }
    return NTFS_OK;
}

/* Encode a runlist (runs must be in VCN order, LCN deltas may be
 * negative) into attr+roff. Returns the encoded byte count, or
 * -1 if it does not fit in the room given. */
static int encode_runs(const ntfs_run_t *runs, int count, uint8_t *out, uint32_t room)
{
    uint32_t p = 0;
    uint64_t prev_lcn = 0;
    int i;
    for (i = 0; i < count; i++) {
        uint64_t len = runs[i].len;
        int64_t delta = (int64_t)runs[i].lcn - (int64_t)prev_lcn;
        uint8_t len_bytes[8], off_bytes[8];
        int ls = 0, os = 0, k;
        uint64_t tmp = len;
        while (tmp) { len_bytes[ls++] = (uint8_t)tmp; tmp >>= 8; }
        if (ls == 0) ls = 1;
        if (!runs[i].sparse) {
            /* Smallest signed encoding of delta. */
            int64_t d = delta;
            do { off_bytes[os++] = (uint8_t)d; d >>= 8; } while (d != 0 && d != -1);
            /* The do-while above can under-encode when the top
             * byte's sign bit disagrees; fix by checking. */
            {
                int64_t check = 0;
                int j;
                for (j = os - 1; j >= 0; j--) check = (check << 8) | off_bytes[j];
                if (os < 8 && (off_bytes[os - 1] & 0x80)) check |= (int64_t)(0xFFFFFFFFFFFFFFFFULL << (8 * os));
                if (check != delta) { off_bytes[os++] = delta < 0 ? 0xFF : 0x00; }
            }
            (void)k;
        }
        if (p + 1u + (uint32_t)ls + (uint32_t)os > room) return -1;
        out[p++] = (uint8_t)((os << 4) | ls);
        {
            int j;
            for (j = 0; j < ls; j++) out[p++] = len_bytes[j];
            for (j = 0; j < os; j++) out[p++] = off_bytes[j];
        }
        prev_lcn = runs[i].lcn;
    }
    if (p + 1 > room) return -1;
    out[p++] = 0;
    return (int)p;
}

/* ---- USA fixup ---- */
static int fixup_apply(uint8_t *rec, uint32_t rec_bytes)
{
    uint16_t usa_off, usa_cnt, seq;
    uint32_t sectors, i;
    if (rec_bytes < 48 || rec_bytes % g_vol.info.bytes_per_sector) return NTFS_ERR_CORRUPT;
    usa_off = rd16(rec + 4);
    usa_cnt = rd16(rec + 6);
    sectors = rec_bytes / g_vol.info.bytes_per_sector;
    if (usa_off + 2u * usa_cnt > rec_bytes || usa_cnt != sectors + 1) return NTFS_ERR_CORRUPT;
    seq = rd16(rec + usa_off);
    for (i = 0; i < sectors; i++) {
        uint32_t tail = (i + 1) * g_vol.info.bytes_per_sector - 2;
        if (rd16(rec + tail) != seq) return NTFS_ERR_CORRUPT;
        wr16(rec + tail, rd16(rec + usa_off + 2 + 2 * i));
    }
    return NTFS_OK;
}

static int fixup_create(uint8_t *rec, uint32_t rec_bytes)
{
    uint16_t usa_off, usa_cnt, seq;
    uint32_t sectors, i;
    if (rec_bytes < 48 || rec_bytes % g_vol.info.bytes_per_sector) return NTFS_ERR_CORRUPT;
    usa_off = rd16(rec + 4);
    usa_cnt = rd16(rec + 6);
    sectors = rec_bytes / g_vol.info.bytes_per_sector;
    if (usa_off + 2u * usa_cnt > rec_bytes || usa_cnt != sectors + 1) return NTFS_ERR_CORRUPT;
    seq = rd16(rec + 16);
    if (seq == 0) seq = 1;
    wr16(rec + usa_off, seq);
    for (i = 0; i < sectors; i++) {
        uint32_t tail = (i + 1) * g_vol.info.bytes_per_sector - 2;
        wr16(rec + usa_off + 2 + 2 * i, rd16(rec + tail));
        wr16(rec + tail, seq);
    }
    return NTFS_OK;
}

/* ---- MFT record I/O ---- */
static int read_mft_record(uint64_t no, uint8_t *out)
{
    uint64_t off = no * (uint64_t)g_vol.info.mft_record_bytes;
    uint32_t rec_bytes = g_vol.info.mft_record_bytes;
    int r;
    if (no * (uint64_t)rec_bytes >= g_vol.mft_runs.total_clusters * (uint64_t)g_vol.info.cluster_bytes) return NTFS_ERR_CORRUPT;
    r = read_runs(&g_vol.mft_runs, off, rec_bytes, out);
    if (r != NTFS_OK) return r;
    if (n_memcmp(out, (const uint8_t *)"FILE", 4) != 0) return NTFS_ERR_CORRUPT;
    r = fixup_apply(out, rec_bytes);
    if (r != NTFS_OK) return r;
    if (rd64(out + 32) != 0) return NTFS_ERR_UNSUPPORTED; /* base record: extension records are not handled */
    if (rd16(out + 20) < 48 || rd16(out + 20) >= rec_bytes) return NTFS_ERR_CORRUPT;
    if (rd32(out + 24) > rec_bytes) return NTFS_ERR_CORRUPT;
    return NTFS_OK;
}

static int write_mft_record(uint64_t no, const uint8_t *in)
{
    uint64_t off = no * (uint64_t)g_vol.info.mft_record_bytes;
    uint32_t rec_bytes = g_vol.info.mft_record_bytes;
    uint8_t *tmp = g_vol.rec2;
    int r;
    if (no * (uint64_t)rec_bytes >= g_vol.mft_runs.total_clusters * (uint64_t)g_vol.info.cluster_bytes) return NTFS_ERR_CORRUPT;
    n_memcpy(tmp, in, rec_bytes);
    r = fixup_create(tmp, rec_bytes);
    if (r != NTFS_OK) return r;
    return write_runs(&g_vol.mft_runs, off, rec_bytes, tmp);
}

/* Compare two fixup-applied MFT records, ignoring the USA
 * array and the LSN: mkntfs (and Windows, after a record is
 * rewritten) can leave the mirror's USA sequence one behind
 * the $MFT copy, while the actual record content is identical.
 * A byte-exact compare would reject healthy volumes. */
static int records_equal_except_usa_lsn(const uint8_t *a, const uint8_t *b, uint32_t rec_bytes)
{
    uint16_t usa_off = rd16(a + 4);
    uint16_t usa_cnt = rd16(a + 6);
    uint32_t i;
    if (rd16(b + 4) != usa_off || rd16(b + 6) != usa_cnt) return 0;
    for (i = 0; i < rec_bytes; i++) {
        if (i >= 8 && i < 16) continue; /* LSN */
        if (i >= usa_off && i < (uint32_t)usa_off + 2u * usa_cnt) continue; /* USA array */
        if (a[i] != b[i]) return 0;
    }
    return 1;
}

/* Read one MFT record by raw offset (no fixup validation beyond
 * the record itself) - used for the $MFTMirr comparison, where
 * the mirror records sit contiguously at mftmirr_lcn. */
static int read_mirror_record(int idx, uint8_t *out)
{
    uint64_t off = (g_vol.info.mftmirr_lcn * (uint64_t)g_vol.info.cluster_bytes) + (uint64_t)idx * g_vol.info.mft_record_bytes;
    int r = vol_read(off, g_vol.info.mft_record_bytes, out);
    if (r != NTFS_OK) return r;
    if (n_memcmp(out, (const uint8_t *)"FILE", 4) != 0) return NTFS_ERR_CORRUPT;
    return fixup_apply(out, g_vol.info.mft_record_bytes);
}

/* ---- attributes ---- */
static int find_attr(const uint8_t *rec, uint32_t type, int want_named, const uint16_t *want_name, int want_name_len, uint32_t *attr_off)
{
    uint32_t p = rd16(rec + 20);
    uint32_t used = rd32(rec + 24);
    int guard = 0;
    if (used > g_vol.info.mft_record_bytes) used = g_vol.info.mft_record_bytes;
    while (p + 8 <= used && guard++ < 64) {
        uint32_t t = rd32(rec + p);
        uint32_t len = rd32(rec + p + 4);
        if (t == NTFS_ATTR_END) break;
        if (len < 8 || p + len > used) return NTFS_ERR_CORRUPT;
        if (t == type) {
            if (!want_named && rec[p + 9] == 0) { *attr_off = p; return NTFS_OK; }
            if (want_named && rec[p + 9] == want_name_len) {
                uint16_t noff = rd16(rec + p + 10);
                if (p + noff + 2u * (uint32_t)want_name_len <= p + len &&
                    n_memcmp(rec + p + noff, (const uint8_t *)want_name, 2u * (uint32_t)want_name_len) == 0) {
                    *attr_off = p; return NTFS_OK;
                }
            }
        }
        p += len;
    }
    return NTFS_ERR_NOT_FOUND;
}

/* Find the $INDEX_ROOT whose indexed attribute type is 0x30
 * (file names). Directories can carry several INDEX_ROOTs
 * ($Quota has $O/$Q); only the filename one lists files. */
static int find_filename_index_root(const uint8_t *rec, uint32_t *attr_off)
{
    uint32_t p = rd16(rec + 20);
    uint32_t used = rd32(rec + 24);
    int guard = 0;
    if (used > g_vol.info.mft_record_bytes) used = g_vol.info.mft_record_bytes;
    while (p + 8 <= used && guard++ < 64) {
        uint32_t t = rd32(rec + p);
        uint32_t len = rd32(rec + p + 4);
        if (t == NTFS_ATTR_END) break;
        if (len < 8 || p + len > used) return NTFS_ERR_CORRUPT;
        if (t == NTFS_ATTR_INDEX_ROOT && rec[p + 8] == 0) {
            uint16_t coff = rd16(rec + p + 20);
            if (p + coff + 4 <= p + len && rd32(rec + p + coff) == NTFS_ATTR_FILENAME) {
                *attr_off = p; return NTFS_OK;
            }
        }
        p += len;
    }
    return NTFS_ERR_NOT_FOUND;
}


/* ---- boot sector / probe ---- */
static int parse_boot(const uint8_t *boot, ntfs_info_t *info)
{
    int8_t rec_size_code, idx_size_code;
    if (boot[510] != 0x55 || boot[511] != 0xAA) return NTFS_ERR_NOT_NTFS;
    if (n_memcmp(boot + 3, (const uint8_t *)"NTFS    ", 8) != 0) return NTFS_ERR_NOT_NTFS;
    info->bytes_per_sector = rd16(boot + 11);
    if (info->bytes_per_sector != 512 && info->bytes_per_sector != 1024 &&
        info->bytes_per_sector != 2048 && info->bytes_per_sector != 4096) return NTFS_ERR_NOT_NTFS;
    info->sectors_per_cluster = boot[13];
    if (info->sectors_per_cluster == 0 || info->sectors_per_cluster > 128) return NTFS_ERR_NOT_NTFS;
    info->cluster_bytes = (uint32_t)info->bytes_per_sector * info->sectors_per_cluster;
    if (info->cluster_bytes > NTFS_MAX_CLUSTER) return NTFS_ERR_TOO_BIG;
    info->total_sectors = rd64(boot + 0x28);
    if (info->total_sectors == 0) return NTFS_ERR_NOT_NTFS;
    info->total_clusters = info->total_sectors / info->sectors_per_cluster;
    info->mft_lcn = rd64(boot + 0x30);
    info->mftmirr_lcn = rd64(boot + 0x38);
    if (info->mft_lcn >= info->total_clusters || info->mftmirr_lcn >= info->total_clusters) return NTFS_ERR_NOT_NTFS;
    rec_size_code = (int8_t)boot[0x40];
    if (rec_size_code < 0) {
        uint32_t s = (uint32_t)1 << (-rec_size_code);
        if (s > NTFS_MAX_RECORD) return NTFS_ERR_TOO_BIG;
        info->mft_record_bytes = s;
    } else {
        uint64_t s = (uint64_t)rec_size_code * info->cluster_bytes;
        if (s == 0 || s > NTFS_MAX_RECORD) return NTFS_ERR_TOO_BIG;
        info->mft_record_bytes = (uint32_t)s;
    }
    idx_size_code = (int8_t)boot[0x44];
    if (idx_size_code < 0) {
        uint32_t s = (uint32_t)1 << (-idx_size_code);
        if (s > NTFS_MAX_CLUSTER) return NTFS_ERR_TOO_BIG;
        info->index_block_bytes = s;
    } else {
        uint64_t s = (uint64_t)idx_size_code * info->cluster_bytes;
        if (s == 0 || s > NTFS_MAX_CLUSTER) return NTFS_ERR_TOO_BIG;
        info->index_block_bytes = (uint32_t)s;
    }
    info->serial = rd64(boot + 0x48);
    info->dirty = 0;
    info->hibernated = 0;
    info->backup_boot_ok = 0;
    info->volume_label[0] = 0;
    return NTFS_OK;
}

int ntfs_probe(const ntfs_block_ops_t *ops, ntfs_info_t *info)
{
    uint8_t boot[512];
    uint8_t tail[512];
    ntfs_info_t tmp;
    int r;
    if (!ops || !ops->read || !info) return NTFS_ERR_BAD_ARG;
    /* Temporarily install the ops so vol_read can bounds-check. */
    n_memset((uint8_t *)&g_vol, 0, sizeof(g_vol));
    g_vol.ops = *ops;
    g_vol.info.bytes_per_sector = 512;
    g_vol.info.total_sectors = 0; /* unknown yet: no bounds check */
    r = ops->read(ops->ctx, 0, 512, boot);
    if (r != 0) return NTFS_ERR_IO;
    r = parse_boot(boot, &tmp);
    if (r != NTFS_OK) return r;
    /* Backup boot sector: mkntfs/Windows put it in the last
     * sector of the device, i.e. one sector past the volume's
     * own last sector (total_sectors counts volume sectors
     * 0..total_sectors-1). It may be absent on a truncated
     * image; that is a warning, not a refusal. */
    g_vol.info = tmp;
    if (ops->read(ops->ctx, tmp.total_sectors * 512, 512, tail) == 0) {
        if (tail[510] == 0x55 && tail[511] == 0xAA &&
            n_memcmp(tail + 3, (const uint8_t *)"NTFS    ", 8) == 0 &&
            rd64(tail + 0x30) == tmp.mft_lcn) tmp.backup_boot_ok = 1;
    }
    *info = tmp;
    g_vol.mounted = 0;
    return NTFS_OK;
}

/* ---- bitmaps ($Bitmap for clusters, $MFT $BITMAP for records) ----
 * One-cluster write-through cache, shared by both bitmaps and
 * tagged by kind+VCN. Every set writes the cluster back at once,
 * so a crash can only lose the in-flight operation, never a
 * cached-but-unflushed batch. */
static int bmp_load(uint64_t vcn, int kind)
{
    const ntfs_runlist_t *rl;
    int r;
    if (g_vol.bmp_kind == kind && g_vol.bmp_vcn == vcn) return NTFS_OK;
    rl = (kind == 1) ? &g_vol.bitmap_runs : &g_vol.mftbmp_runs;
    r = read_runs(rl, vcn * (uint64_t)g_vol.info.cluster_bytes, g_vol.info.cluster_bytes, g_vol.bmp_cluster);
    if (r != NTFS_OK) return r;
    g_vol.bmp_kind = kind;
    g_vol.bmp_vcn = vcn;
    return NTFS_OK;
}

static int bmp_get(int kind, uint64_t bit, int *val)
{
    uint64_t byte_off = bit / 8;
    uint64_t vcn = byte_off / g_vol.info.cluster_bytes;
    uint32_t within = (uint32_t)(byte_off % g_vol.info.cluster_bytes);
    int r = bmp_load(vcn, kind);
    if (r != NTFS_OK) return r;
    *val = (g_vol.bmp_cluster[within] >> (bit % 8)) & 1;
    return NTFS_OK;
}

static int bmp_set(int kind, uint64_t bit, int val)
{
    uint64_t byte_off = bit / 8;
    uint64_t vcn = byte_off / g_vol.info.cluster_bytes;
    uint32_t within = (uint32_t)(byte_off % g_vol.info.cluster_bytes);
    const ntfs_runlist_t *rl;
    int r = bmp_load(vcn, kind);
    if (r != NTFS_OK) return r;
    if (val) g_vol.bmp_cluster[within] |= (uint8_t)(1u << (bit % 8));
    else g_vol.bmp_cluster[within] &= (uint8_t)~(1u << (bit % 8));
    rl = (kind == 1) ? &g_vol.bitmap_runs : &g_vol.mftbmp_runs;
    return write_runs(rl, vcn * (uint64_t)g_vol.info.cluster_bytes, g_vol.info.cluster_bytes, g_vol.bmp_cluster);
}

/* Allocate n clusters. Prefers one contiguous run; falls back to
 * fragmented runs (max NTFS_MAX_RUNS). On failure any bits set
 * here are cleared again. */
static int alloc_clusters(uint64_t n, ntfs_runlist_t *out)
{
    uint64_t total = g_vol.info.total_clusters;
    uint64_t i;
    int runs = 0;
    uint64_t run_start = 0, run_len = 0;
    if (n == 0) { out->count = 0; out->total_clusters = 0; return NTFS_OK; }
    /* First pass: one contiguous run. */
    run_start = 0; run_len = 0;
    for (i = 0; i < total; i++) {
        int v, r = bmp_get(1, i, &v);
        if (r != NTFS_OK) return r;
        if (v == 0) {
            if (run_len == 0) run_start = i;
            if (++run_len >= n) break;
        } else run_len = 0;
    }
    if (run_len >= n) {
        for (i = 0; i < n; i++) { int r = bmp_set(1, run_start + i, 1); if (r != NTFS_OK) return r; }
        out->runs[0].lcn = run_start; out->runs[0].len = n; out->runs[0].sparse = 0;
        out->count = 1; out->total_clusters = n;
        return NTFS_OK;
    }
    /* Second pass: fragmented. */
    out->count = 0; out->total_clusters = 0;
    run_start = 0; run_len = 0;
    for (i = 0; i < total && out->total_clusters < n; i++) {
        int v, r = bmp_get(1, i, &v);
        if (r != NTFS_OK) goto fail;
        if (v == 0) {
            if (run_len == 0) run_start = i;
            run_len++;
            if (run_len == 1) { /* placeholder, extended below */ }
            if (i + 1 == total || run_len > 0) {
                /* Peek ahead: extend while the next bit is free. */
                uint64_t j = i + 1;
                while (j < total && out->total_clusters + run_len < n) {
                    int nv, rr = bmp_get(1, j, &nv);
                    if (rr != NTFS_OK) goto fail;
                    if (nv != 0) break;
                    run_len++; j++;
                }
                if (runs >= NTFS_MAX_RUNS) goto fail;
                out->runs[runs].lcn = run_start; out->runs[runs].len = run_len; out->runs[runs].sparse = 0;
                runs++;
                out->total_clusters += run_len;
                i = j - 1;
                run_len = 0;
            }
        }
    }
    if (out->total_clusters < n) goto fail;
    out->count = runs;
    /* Mark the bits now that the runlist is complete. */
    {
        int k;
        for (k = 0; k < runs; k++) {
            uint64_t c;
            for (c = 0; c < out->runs[k].len; c++) {
                int r = bmp_set(1, out->runs[k].lcn + c, 1);
                if (r != NTFS_OK) goto fail;
            }
        }
    }
    return NTFS_OK;
fail:
    {
        int k;
        for (k = 0; k < runs; k++) {
            uint64_t c;
            for (c = 0; c < out->runs[k].len; c++) (void)bmp_set(1, out->runs[k].lcn + c, 0);
        }
    }
    out->count = 0; out->total_clusters = 0;
    return NTFS_ERR_NO_SPACE;
}

static int free_runs(const ntfs_runlist_t *rl)
{
    int i;
    for (i = 0; i < rl->count; i++) {
        uint64_t c;
        if (rl->runs[i].sparse) continue;
        for (c = 0; c < rl->runs[i].len; c++) {
            int r = bmp_set(1, rl->runs[i].lcn + c, 0);
            if (r != NTFS_OK) return r;
        }
    }
    return NTFS_OK;
}

static int find_free_mft_record(uint64_t *out_no)
{
    uint64_t max_bits = g_vol.mftbmp_size * 8;
    uint64_t i;
    if (max_bits < 16) return NTFS_ERR_CORRUPT;
    for (i = 16; i < max_bits; i++) {
        int v, r = bmp_get(2, i, &v);
        if (r != NTFS_OK) return r;
        if (v == 0) { *out_no = i; return NTFS_OK; }
    }
    return NTFS_ERR_NO_SPACE;
}

/* ---- $Volume dirty flag ---- */
static int set_dirty_flag(int dirty)
{
    uint8_t *rec = g_vol.rec;
    uint32_t off;
    int r = read_mft_record(3, rec);
    if (r != NTFS_OK) return r;
    r = find_attr(rec, NTFS_ATTR_VOLINFO, 0, 0, 0, &off);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = rec + off;
        uint32_t alen = rd32(attr + 4);
        uint16_t coff;
        uint16_t flags;
        if (attr[8] != 0) return NTFS_ERR_CORRUPT;
        coff = rd16(attr + 20);
        if ((uint32_t)coff + 12 > alen) return NTFS_ERR_CORRUPT;
        flags = rd16(attr + coff + 10);
        if (dirty) flags |= 0x0001; else flags &= (uint16_t)~0x0001;
        wr16(rec + off + coff + 10, flags);
    }
    r = write_mft_record(3, rec);
    if (r != NTFS_OK) return r;
    /* Keep $MFTMirr in sync: record 3 is one of the four
     * mirrored records. write_mft_record leaves the exact
     * on-disk (fixup-applied) bytes in g_vol.rec2; copy them
     * to the mirror slot for record 3. */
    {
        uint64_t mirror_off = g_vol.info.mftmirr_lcn * (uint64_t)g_vol.info.cluster_bytes
            + 3u * (uint64_t)g_vol.info.mft_record_bytes;
        return vol_write(mirror_off, g_vol.info.mft_record_bytes, g_vol.rec2);
    }
}

/* ---- UTF-8 / UTF-16 helpers (BMP + surrogate pairs) ---- */
static int utf8_to_utf16(const char *s, uint16_t *out, int max_chars)
{
    int n = 0;
    const uint8_t *p = (const uint8_t *)s;
    while (*p && n < max_chars) {
        uint32_t cp;
        if (*p < 0x80) { cp = *p++; }
        else if ((*p & 0xE0) == 0xC0 && (p[1] & 0xC0) == 0x80) { cp = ((uint32_t)(*p & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((*p & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80) { cp = ((uint32_t)(*p & 0x0F) << 12) | ((uint32_t)(p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else if ((*p & 0xF8) == 0xF0 && (p[1] & 0xC0) == 0x80 && (p[2] & 0xC0) == 0x80 && (p[3] & 0xC0) == 0x80) { cp = ((uint32_t)(*p & 0x07) << 18) | ((uint32_t)(p[1] & 0x3F) << 12) | ((uint32_t)(p[2] & 0x3F) << 6) | (p[3] & 0x3F); p += 4; }
        else { cp = '?'; p++; }
        if (cp >= 0x10000) {
            if (n + 2 > max_chars) break;
            cp -= 0x10000;
            out[n++] = (uint16_t)(0xD800 + (cp >> 10));
            out[n++] = (uint16_t)(0xDC00 + (cp & 0x3FF));
        } else {
            out[n++] = (uint16_t)cp;
        }
    }
    return n;
}

static int utf16_to_utf8(const uint16_t *s, int nchars, char *out, int cap)
{
    int o = 0, i = 0;
    while (i < nchars && o + 1 < cap) {
        uint32_t cp = s[i++];
        if (cp >= 0xD800 && cp < 0xDC00 && i < nchars && s[i] >= 0xDC00 && s[i] < 0xE000) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (s[i++] - 0xDC00);
        }
        if (cp < 0x80) { out[o++] = (char)cp; }
        else if (cp < 0x800 && o + 2 < cap) { out[o++] = (char)(0xC0 | (cp >> 6)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000 && o + 3 < cap) { out[o++] = (char)(0xE0 | (cp >> 12)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
        else if (o + 4 < cap) { out[o++] = (char)(0xF0 | (cp >> 18)); out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F)); out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F)); out[o++] = (char)(0x80 | (cp & 0x3F)); }
        else break;
    }
    out[o] = 0;
    return o;
}

static uint16_t fold16(uint16_t c)
{
    if (c >= 'a' && c <= 'z') return (uint16_t)(c - 32);
    return c;
}

static int utf16_equal_fold(const uint16_t *a, int an, const uint16_t *b, int bn)
{
    int i;
    if (an != bn) return 0;
    for (i = 0; i < an; i++) if (fold16(a[i]) != fold16(b[i])) return 0;
    return 1;
}

/* $UpCase-style compare for sort order: ASCII case-folded,
 * other code units compared raw. Approximate for non-ASCII;
 * exact for the ASCII names the tests use. */
static int utf16_compare_fold(const uint16_t *a, int an, const uint16_t *b, int bn)
{
    int i, n = an < bn ? an : bn;
    for (i = 0; i < n; i++) {
        uint16_t ca = fold16(a[i]), cb = fold16(b[i]);
        if (ca != cb) return ca < cb ? -1 : 1;
    }
    if (an == bn) return 0;
    return an < bn ? -1 : 1;
}

/* ---- directory scanning ---- */
/* One index entry found while scanning a directory. fn points
 * into the scan buffer (g_vol.rec for INDEX_ROOT, g_vol.rec2
 * for an INDX block) and is only valid until the callback
 * returns - the callback must copy what it needs. */
typedef struct {
    uint64_t file_ref;
    uint64_t mft_no;
    uint64_t fn_alloc;
    uint64_t fn_real;
    uint32_t fn_flags;
    int loc_kind;           /* 0 = INDEX_ROOT in the dir record, 1 = INDX block */
    uint64_t block_vcn;     /* INDX block VCN (loc_kind 1) */
    uint32_t entry_off;     /* offset of the entry in the scan buffer */
    uint32_t entry_len;
} ntfs_dir_hit_t;

typedef int (*ntfs_dir_cb_t)(const ntfs_dir_hit_t *hit, const uint8_t *fn, uint32_t fn_len, void *ctx);

/* Scan one index header's entries. buf is the record or INDX
 * block, hdr_off the offset of the index header in buf. */
static int scan_index_entries(const uint8_t *buf, uint32_t buf_len, uint32_t hdr_off,
                              int loc_kind, uint64_t block_vcn,
                              ntfs_dir_cb_t cb, void *ctx)
{
    uint32_t entries_off, index_len;
    uint32_t p, end;
    if (hdr_off + 16 > buf_len) return NTFS_ERR_CORRUPT;
    entries_off = rd32(buf + hdr_off);
    index_len = rd32(buf + hdr_off + 4);
    if (entries_off > index_len || hdr_off + entries_off + 16 > buf_len) return NTFS_ERR_CORRUPT;
    p = hdr_off + entries_off;
    end = hdr_off + index_len;
    if (end > buf_len) return NTFS_ERR_CORRUPT;
    while (p + 16 <= end) {
        uint64_t ref = rd64(buf + p);
        uint32_t elen = rd16(buf + p + 8);
        uint32_t slen = rd16(buf + p + 10);
        uint16_t eflags = rd16(buf + p + 12);
        ntfs_dir_hit_t hit;
        if (elen < 16 || (elen & 7) || p + elen > end) return NTFS_ERR_CORRUPT;
        if (eflags & 0x02) break; /* end entry */
        if (slen < 66 || p + 16 + slen > p + elen) return NTFS_ERR_CORRUPT;
        {
            const uint8_t *fn = buf + p + 16;
            int nlen = fn[64];
            if (66 + 2 * nlen > (int)slen) return NTFS_ERR_CORRUPT;
            hit.file_ref = ref;
            hit.mft_no = ref & 0x0000FFFFFFFFFFFFull;
            hit.fn_alloc = rd64(fn + 40);
            hit.fn_real = rd64(fn + 48);
            hit.fn_flags = rd32(fn + 56);
            hit.loc_kind = loc_kind;
            hit.block_vcn = block_vcn;
            hit.entry_off = p;
            hit.entry_len = elen;
            if (cb) {
                int r = cb(&hit, fn, slen, ctx);
                if (r != NTFS_OK) return r;
            }
        }
        p += elen;
    }
    return NTFS_OK;
}

/* Scan every entry of a directory (INDEX_ROOT, then every INDX
 * block of INDEX_ALLOCATION). */
static int dir_scan(uint64_t dir_mft, ntfs_dir_cb_t cb, void *ctx)
{
    uint8_t *rec = g_vol.rec;
    uint32_t root_off, alloc_off;
    int r = read_mft_record(dir_mft, rec);
    if (r != NTFS_OK) return r;
    if (!(rd16(rec + 22) & 0x02)) return NTFS_ERR_NOT_DIR;
    r = find_filename_index_root(rec, &root_off);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = rec + root_off;
        uint32_t alen = rd32(attr + 4);
        uint16_t coff;
        uint32_t vlen, hdr_flags;
        if (attr[8] != 0) return NTFS_ERR_CORRUPT;
        coff = rd16(attr + 20);
        if ((uint32_t)coff + 32 > alen) return NTFS_ERR_CORRUPT;
        vlen = rd32(attr + 16);
        if (coff + vlen > alen) return NTFS_ERR_CORRUPT;
        hdr_flags = rd32(attr + coff + 16 + 12);
        r = scan_index_entries(rec, g_vol.info.mft_record_bytes, root_off + coff + 16, 0, 0, cb, ctx);
        if (r != NTFS_OK) return r;
        if (hdr_flags & 0x01) {
            /* Large directory: entries also live in INDX blocks. */
            ntfs_runlist_t rl;
            uint64_t blocks, b;
            r = find_attr(rec, NTFS_ATTR_INDEX_ALLOC, 1, NTFS_NAME_I30, 4, &alloc_off);
            if (r != NTFS_OK) return r;
            {
                const uint8_t *aattr = rec + alloc_off;
                uint32_t aalen = rd32(aattr + 4);
                if (aattr[8] == 0) return NTFS_ERR_CORRUPT;
                r = parse_runs(aattr, aalen, &rl);
                if (r != NTFS_OK) return r;
            }
            blocks = rl.total_clusters * (uint64_t)g_vol.info.cluster_bytes / g_vol.info.index_block_bytes;
            for (b = 0; b < blocks; b++) {
                uint8_t *blk = g_vol.rec2;
                uint64_t byte_off = b * (uint64_t)g_vol.info.index_block_bytes;
                r = read_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
                if (r != NTFS_OK) return r;
                if (n_memcmp(blk, (const uint8_t *)"INDX", 4) != 0) return NTFS_ERR_CORRUPT;
                r = fixup_apply(blk, g_vol.info.index_block_bytes);
                if (r != NTFS_OK) return r;
                r = scan_index_entries(blk, g_vol.info.index_block_bytes, 24, 1, b * (g_vol.info.index_block_bytes / g_vol.info.cluster_bytes), cb, ctx);
                if (r != NTFS_OK) return r;
            }
        }
    }
    return NTFS_OK;
}

typedef struct {
    const uint16_t *want;
    int want_len;
    ntfs_dir_hit_t hit;
    uint8_t fn_copy[66 + 2 * 255];
    uint32_t fn_len;
    int found;
} find_ctx_t;

static int find_cb(const ntfs_dir_hit_t *hit, const uint8_t *fn, uint32_t fn_len, void *ctx)
{
    find_ctx_t *c = (find_ctx_t *)ctx;
    int nlen = fn[64];
    uint16_t name[255];
    int i;
    if (fn[65] == 2) return NTFS_OK; /* DOS 8.3 alias: skip, the Win32/POSIX entry names the same file */
    for (i = 0; i < nlen && i < 255; i++) name[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
    if (utf16_equal_fold(name, nlen, c->want, c->want_len)) {
        c->hit = *hit;
        if (fn_len > sizeof(c->fn_copy)) return NTFS_ERR_TOO_BIG;
        n_memcpy(c->fn_copy, fn, fn_len);
        c->fn_len = fn_len;
        c->found = 1;
        return 1; /* stop scanning */
    }
    return NTFS_OK;
}

/* NTFS_OK + found=1, NTFS_OK + found=0, or an error. The value
 * 1 returned by the callback is the "stop" signal and is
 * translated back to NTFS_OK here. */
static int dir_find(uint64_t dir_mft, const uint16_t *name, int name_len, find_ctx_t *out)
{
    int r;
    out->want = name;
    out->want_len = name_len;
    out->found = 0;
    out->fn_len = 0;
    r = dir_scan(dir_mft, find_cb, out);
    if (r == 1) return NTFS_OK;
    return r;
}

typedef struct {
    ntfs_entry_t *out;
    int max;
    int count;
} list_ctx_t;

static int list_cb(const ntfs_dir_hit_t *hit, const uint8_t *fn, uint32_t fn_len, void *ctx)
{
    list_ctx_t *c = (list_ctx_t *)ctx;
    int nlen = fn[64];
    uint16_t name[255];
    int i;
    (void)fn_len;
    if (fn[65] == 2) return NTFS_OK; /* DOS 8.3 alias: skip */
    if (c->count >= c->max) return NTFS_ERR_TOO_BIG;
    for (i = 0; i < nlen && i < 255; i++) name[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
    {
        ntfs_entry_t *e = &c->out[c->count];
        utf16_to_utf8(name, nlen, e->name, sizeof(e->name));
        e->size = hit->fn_real;
        e->alloc_size = hit->fn_alloc;
        e->is_dir = (hit->fn_flags & 0x10000000u) != 0;
        e->mft_no = hit->mft_no;
    }
    c->count++;
    return NTFS_OK;
}

/* Resolve a path. On success *mft_no is the file/dir. On
 * NTFS_ERR_NOT_FOUND, *parent_mft and the leaf name (UTF-16)
 * are filled in so the caller can create it. */
static int resolve_path(const char *path, uint64_t *mft_no, uint64_t *parent_mft, uint16_t *leaf, int *leaf_len)
{
    uint64_t cur = NTFS_MFT_ROOT;
    const char *p = path;
    if (!path || !mft_no) return NTFS_ERR_BAD_ARG;
    while (*p == '/' || *p == '\\') p++;
    if (*p == 0) { *mft_no = NTFS_MFT_ROOT; if (parent_mft) *parent_mft = NTFS_MFT_ROOT; if (leaf_len) *leaf_len = 0; return NTFS_OK; }
    while (1) {
        char comp[256];
        int clen = 0;
        uint16_t comp16[255];
        int c16len;
        while (*p && *p != '/' && *p != '\\') {
            if (clen < 255) comp[clen++] = *p;
            p++;
        }
        comp[clen] = 0;
        while (*p == '/' || *p == '\\') p++;
        if (clen == 0) break;
        if (n_strcmp(comp, ".") == 0) continue;
        if (n_strcmp(comp, "..") == 0) {
            /* Parent from the current record's $FILE_NAME. */
            uint8_t *rec = g_vol.rec;
            uint32_t off;
            int r = read_mft_record(cur, rec);
            if (r != NTFS_OK) return r;
            r = find_attr(rec, NTFS_ATTR_FILENAME, 0, 0, 0, &off);
            if (r != NTFS_OK) return r;
            {
                const uint8_t *attr = rec + off;
                uint16_t coff = rd16(attr + 20);
                uint64_t parent = rd64(attr + coff) & 0x0000FFFFFFFFFFFFull;
                if (parent == 0) parent = NTFS_MFT_ROOT;
                cur = parent;
            }
            continue;
        }
        c16len = utf8_to_utf16(comp, comp16, 255);
        {
            find_ctx_t fc;
            int r = dir_find(cur, comp16, c16len, &fc);
            if (r != NTFS_OK) return r;
            if (!fc.found) {
                if (*p == 0) {
                    /* Leaf missing: caller may create it. */
                    if (parent_mft) *parent_mft = cur;
                    if (leaf && leaf_len) { int i; for (i = 0; i < c16len; i++) leaf[i] = comp16[i]; *leaf_len = c16len; }
                    return NTFS_ERR_NOT_FOUND;
                }
                return NTFS_ERR_NOT_FOUND;
            }
            cur = fc.hit.mft_no;
        }
        if (*p == 0) break;
    }
    *mft_no = cur;
    if (parent_mft) *parent_mft = cur;
    if (leaf_len) *leaf_len = 0;
    return NTFS_OK;
}

/* Get the unnamed $DATA of a record: resident flag, resident
 * pointer/length, or the runlist + sizes for non-resident. */
typedef struct {
    int resident;
    const uint8_t *res_data;
    uint32_t res_len;
    ntfs_runlist_t runs;
    uint64_t real_size;
    uint64_t alloc_size;
    uint16_t attr_flags;
    uint32_t attr_off;
} data_info_t;

static int get_data_info(const uint8_t *rec, data_info_t *di)
{
    uint32_t off;
    int r = find_attr(rec, NTFS_ATTR_DATA, 0, 0, 0, &off);
    const uint8_t *attr;
    uint32_t alen;
    if (r != NTFS_OK) return r;
    attr = rec + off;
    alen = rd32(attr + 4);
    di->attr_off = off;
    di->attr_flags = rd16(attr + 12);
    if (attr[8] == 0) {
        uint32_t csize = rd32(attr + 16);
        uint16_t coff = rd16(attr + 20);
        if (coff + csize > alen) return NTFS_ERR_CORRUPT;
        di->resident = 1;
        di->res_data = attr + coff;
        di->res_len = csize;
        di->real_size = csize;
        di->alloc_size = csize;
        return NTFS_OK;
    }
    di->resident = 0;
    di->alloc_size = rd64(attr + 40);
    di->real_size = rd64(attr + 48);
    r = parse_runs(attr, alen, &di->runs);
    return r;
}

int ntfs_list(const char *path, ntfs_entry_t *out, int max_entries, int *count)
{
    uint64_t mft_no;
    list_ctx_t c;
    int r;
    if (!g_vol.mounted || !out || max_entries <= 0 || !count) return NTFS_ERR_BAD_ARG;
    r = resolve_path(path, &mft_no, 0, 0, 0);
    if (r != NTFS_OK) return r;
    c.out = out; c.max = max_entries; c.count = 0;
    r = dir_scan(mft_no, list_cb, &c);
    if (r != NTFS_OK) return r;
    *count = c.count;
    return NTFS_OK;
}

int ntfs_stat(const char *path, ntfs_entry_t *out)
{
    uint64_t mft_no, parent;
    uint16_t leaf[255];
    int leaf_len = 0;
    uint8_t *rec = g_vol.rec;
    uint32_t off;
    int r;
    if (!g_vol.mounted || !out) return NTFS_ERR_BAD_ARG;
    r = resolve_path(path, &mft_no, &parent, leaf, &leaf_len);
    if (r != NTFS_OK) return r;
    r = read_mft_record(mft_no, rec);
    if (r != NTFS_OK) return r;
    r = find_attr(rec, NTFS_ATTR_FILENAME, 0, 0, 0, &off);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = rec + off;
        uint16_t coff = rd16(attr + 20);
        const uint8_t *fn = attr + coff;
        int nlen = fn[64];
        uint16_t name[255];
        int i;
        for (i = 0; i < nlen && i < 255; i++) name[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
        utf16_to_utf8(name, nlen, out->name, sizeof(out->name));
        out->size = rd64(fn + 48);
        out->alloc_size = rd64(fn + 40);
        out->is_dir = (rd32(fn + 56) & 0x10000000u) != 0;
        out->mft_no = mft_no;
    }
    return NTFS_OK;
}

int ntfs_read_file(const char *path, uint8_t *out, size_t out_cap, uint64_t *out_len)
{
    uint64_t mft_no;
    uint8_t *rec = g_vol.rec;
    data_info_t di;
    int r;
    if (!g_vol.mounted || !out) return NTFS_ERR_BAD_ARG;
    r = resolve_path(path, &mft_no, 0, 0, 0);
    if (r != NTFS_OK) return r;
    if (mft_no < 16) return NTFS_ERR_SYSTEM_FILE;
    r = read_mft_record(mft_no, rec);
    if (r != NTFS_OK) return r;
    if (rd16(rec + 22) & 0x02) return NTFS_ERR_IS_DIR;
    if (rd16(rec + 22) & 0x01) { /* in use, ok */ } else return NTFS_ERR_NOT_FOUND;
    r = get_data_info(rec, &di);
    if (r != NTFS_OK) return r;
    if (di.attr_flags & 0x0001) return NTFS_ERR_UNSUPPORTED; /* compressed */
    if (di.attr_flags & 0x4000) return NTFS_ERR_UNSUPPORTED; /* encrypted */
    if (di.real_size > out_cap) return NTFS_ERR_TOO_BIG;
    if (di.resident) {
        n_memcpy(out, di.res_data, di.res_len);
    } else {
        /* get_data_info's runlist points into rec; read_runs
         * uses g_vol.cluster, which is safe here. */
        r = read_runs(&di.runs, 0, (uint32_t)di.real_size, out);
        if (r != NTFS_OK) return r;
    }
    if (out_len) *out_len = di.real_size;
    return NTFS_OK;
}

/* ---- mount ---- */
int ntfs_mount(const ntfs_block_ops_t *ops, int writable, ntfs_info_t *info)
{
    uint8_t boot[512];
    ntfs_info_t tmp;
    int r;
    uint32_t off;
    if (!ops || !ops->read) return NTFS_ERR_BAD_ARG;
    n_memset((uint8_t *)&g_vol, 0, sizeof(g_vol));
    g_vol.ops = *ops;
    g_vol.info.bytes_per_sector = 512;
    g_vol.info.total_sectors = 0;
    r = ops->read(ops->ctx, 0, 512, boot);
    if (r != 0) return NTFS_ERR_IO;
    r = parse_boot(boot, &tmp);
    if (r != NTFS_OK) return r;
    g_vol.info = tmp;
    /* $MFT $DATA runs (record 0). */
    r = read_mft_record(0, g_vol.rec);
    /* read_mft_record needs mft_runs; bootstrap it from the
     * boot sector: the $MFT data starts at mft_lcn, and record
     * 0 itself is at the start of that run, so read it raw. */
    if (r != NTFS_OK) {
        /* Fallback bootstrap: read record 0 directly from
         * its LCN (it is the first record of the $MFT). */
        uint64_t off0 = tmp.mft_lcn * (uint64_t)tmp.cluster_bytes;
        r = vol_read(off0, tmp.mft_record_bytes, g_vol.rec);
        if (r != NTFS_OK) return r;
        if (n_memcmp(g_vol.rec, (const uint8_t *)"FILE", 4) != 0) return NTFS_ERR_CORRUPT;
        r = fixup_apply(g_vol.rec, tmp.mft_record_bytes);
        if (r != NTFS_OK) return r;
    }
    r = find_attr(g_vol.rec, NTFS_ATTR_DATA, 0, 0, 0, &off);
    NDBG("mount: record0 $DATA find r=%d off=%u\n", r, off);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = g_vol.rec + off;
        if (attr[8] == 0) return NTFS_ERR_CORRUPT;
        r = parse_runs(attr, rd32(attr + 4), &g_vol.mft_runs);
        NDBG("mount: mft runs r=%d count=%d total=%llu\n", r, g_vol.mft_runs.count, (unsigned long long)g_vol.mft_runs.total_clusters);
        if (r != NTFS_OK) return r;
    }
    /* Re-read record 0 through the runlist (validates it). */
    r = read_mft_record(0, g_vol.rec);
    NDBG("mount: re-read record0 r=%d\n", r);
    if (r != NTFS_OK) return r;
    /* $MFTMirr check: the first 4 records must match. */
    {
        int i;
        for (i = 0; i < 4; i++) {
            uint8_t *a = g_vol.rec;
            uint8_t *b = g_vol.rec2;
            r = read_mft_record((uint64_t)i, a);
            if (r != NTFS_OK) return r;
            r = read_mirror_record(i, b);
            NDBG("mount: mirror %d r=%d cmp=%d\n", i, r, r == NTFS_OK ? !records_equal_except_usa_lsn(a, b, tmp.mft_record_bytes) : -99);
            if (r != NTFS_OK) return NTFS_ERR_CORRUPT;
            if (!records_equal_except_usa_lsn(a, b, tmp.mft_record_bytes)) return NTFS_ERR_CORRUPT;
        }
    }
    /* $Bitmap $DATA runs (record 6). */
    r = read_mft_record(6, g_vol.rec);
    NDBG("mount: record6 r=%d\n", r);
    if (r != NTFS_OK) return r;
    r = find_attr(g_vol.rec, NTFS_ATTR_DATA, 0, 0, 0, &off);
    NDBG("mount: record6 $DATA find r=%d\n", r);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = g_vol.rec + off;
        if (attr[8] == 0) return NTFS_ERR_CORRUPT;
        r = parse_runs(attr, rd32(attr + 4), &g_vol.bitmap_runs);
        if (r != NTFS_OK) return r;
    }
    /* $MFT $BITMAP attribute runs (record 0, type 0xB0). */
    r = read_mft_record(0, g_vol.rec);
    if (r != NTFS_OK) return r;
    r = find_attr(g_vol.rec, NTFS_ATTR_BITMAP, 0, 0, 0, &off);
    NDBG("mount: record0 $BITMAP find r=%d\n", r);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = g_vol.rec + off;
        uint32_t alen = rd32(attr + 4);
        if (attr[8] == 0) {
            /* Resident MFT bitmap (tiny volumes): keep it in
             * memory by treating it as one runless blob is not
             * possible here, so refuse - real volumes use a
             * non-resident bitmap. */
            (void)alen;
            return NTFS_ERR_UNSUPPORTED;
        }
        g_vol.mftbmp_size = rd64(attr + 48);
        r = parse_runs(attr, alen, &g_vol.mftbmp_runs);
        if (r != NTFS_OK) return r;
    }
    /* $Volume: dirty flag + label (record 3). */
    r = read_mft_record(3, g_vol.rec);
    NDBG("mount: record3 r=%d\n", r);
    if (r != NTFS_OK) return r;
    r = find_attr(g_vol.rec, NTFS_ATTR_VOLINFO, 0, 0, 0, &off);
    NDBG("mount: record3 $VOLINFO find r=%d\n", r);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = g_vol.rec + off;
        uint16_t coff = rd16(attr + 20);
        if (attr[8] == 0 && (uint32_t)coff + 12 <= rd32(attr + 4)) {
            uint16_t flags = rd16(attr + coff + 10);
            g_vol.info.dirty = (flags & 0x0001) != 0;
        }
    }
    r = find_attr(g_vol.rec, NTFS_ATTR_VOLNAME, 0, 0, 0, &off);
    if (r == NTFS_OK) {
        const uint8_t *attr = g_vol.rec + off;
        uint32_t csize;
        uint16_t coff;
        if (attr[8] == 0) {
            csize = rd32(attr + 16);
            coff = rd16(attr + 20);
            if (csize <= 62 * 2) {
                uint16_t uname[64];
                int i, n = (int)(csize / 2);
                for (i = 0; i < n; i++) uname[i] = rd16(attr + coff + 2 * i);
                utf16_to_utf8(uname, n, g_vol.info.volume_label, sizeof(g_vol.info.volume_label));
            }
        }
    }
    /* Hibernation check: hiberfil.sys in the root means Windows
     * is hibernated (or Fast Startup is on) - writing would
     * lose the hibernated session's filesystem state. */
    {
        uint16_t hib[16];
        int hn = utf8_to_utf16("hiberfil.sys", hib, 16);
        find_ctx_t fc;
        r = dir_find(NTFS_MFT_ROOT, hib, hn, &fc);
        NDBG("mount: hiberfil dir_find r=%d found=%d\n", r, fc.found);
        if (r == NTFS_OK && fc.found) g_vol.info.hibernated = 1;
        else if (r != NTFS_OK) return r;
    }
    if (writable) {
        if (!ops->write) return NTFS_ERR_READ_ONLY;
        if (g_vol.info.dirty) return NTFS_ERR_READ_ONLY;
        if (g_vol.info.hibernated) return NTFS_ERR_READ_ONLY;
    }
    g_vol.mounted = 1;
    g_vol.writable = writable ? 1 : 0;
    if (info) *info = g_vol.info;
    return NTFS_OK;
}

/* ---- record building (create / overwrite) ---- */
static uint32_t align8(uint32_t v) { return (v + 7u) & ~7u; }

static void n_memmove(uint8_t *d, const uint8_t *s, uint32_t n)
{
    uint32_t i;
    if (d == s) return;
    if (d < s) { for (i = 0; i < n; i++) d[i] = s[i]; }
    else { for (i = n; i > 0; i--) d[i - 1] = s[i - 1]; }
}

/* Write one resident attribute header + value at p. Returns the
 * total attribute length (8-byte aligned). */
static uint32_t build_res_attr(uint8_t *p, uint32_t type, uint16_t id, const uint8_t *val, uint32_t val_len)
{
    uint32_t total = align8(24 + val_len);
    wr32(p, type);
    wr32(p + 4, total);
    p[8] = 0; p[9] = 0;
    wr16(p + 10, 0);
    wr16(p + 12, 0);
    wr16(p + 14, id);
    wr32(p + 16, val_len);
    wr16(p + 20, 24);
    p[22] = 0; p[23] = 0;
    n_memcpy(p + 24, val, val_len);
    if (total > 24 + val_len) n_memset(p + 24 + val_len, 0, total - 24 - val_len);
    return total;
}

/* Write a non-resident $DATA attribute at p. Returns its total
 * length (8-byte aligned), or 0 if the runlist does not fit. */
static uint32_t build_nonres_data_attr(uint8_t *p, uint16_t id, const ntfs_runlist_t *rl, uint64_t real, uint64_t alloc)
{
    uint8_t *runbuf = p + 64;
    int runlen = encode_runs(rl->runs, rl->count, runbuf, g_vol.info.mft_record_bytes - 64);
    uint32_t total;
    uint64_t highest;
    if (runlen < 0) return 0;
    total = align8(64u + (uint32_t)runlen);
    highest = rl->total_clusters ? rl->total_clusters - 1 : 0;
    wr32(p, NTFS_ATTR_DATA);
    wr32(p + 4, total);
    p[8] = 1; p[9] = 0;
    wr16(p + 10, 0);
    wr16(p + 12, 0);
    wr16(p + 14, id);
    wr64(p + 16, 0);            /* lowest VCN */
    wr64(p + 24, highest);      /* highest VCN */
    wr16(p + 32, 64);           /* runlist offset */
    wr16(p + 34, 0);            /* compression unit */
    wr32(p + 36, 0);
    wr64(p + 40, alloc);
    wr64(p + 48, real);
    wr64(p + 56, real);         /* initialized = real (data fully written) */
    if (total > 64u + (uint32_t)runlen) n_memset(p + 64 + runlen, 0, total - 64 - runlen);
    return total;
}

/* Build a complete file record in out (record_bytes). si/fn are
 * the $STANDARD_INFORMATION / $FILE_NAME values to store. For
 * a regular file, data is resident (res_data/res_len) or
 * non-resident (runs + real/alloc). */
static int build_file_record(uint8_t *out, uint64_t rec_no, uint16_t seq, int is_dir,
                             const uint8_t *si, uint32_t si_len,
                             const uint8_t *fn, uint32_t fn_len,
                             const uint8_t *res_data, uint32_t res_len,
                             const ntfs_runlist_t *runs, uint64_t real, uint64_t alloc)
{
    uint32_t rec_bytes = g_vol.info.mft_record_bytes;
    uint32_t p;
    uint32_t data_len;
    (void)rec_no;
    n_memset(out, 0, rec_bytes);
    n_memcpy(out, (const uint8_t *)"FILE", 4);
    wr16(out + 4, 48); /* USA offset */
    wr16(out + 6, (uint16_t)(1 + rec_bytes / g_vol.info.bytes_per_sector));
    wr64(out + 8, 0);  /* LSN */
    wr16(out + 16, seq ? seq : 1);
    wr16(out + 18, 1); /* links */
    wr16(out + 20, 56); /* first attr */
    wr16(out + 22, (uint16_t)(is_dir ? 0x03 : 0x01));
    wr32(out + 28, rec_bytes);
    wr64(out + 32, 0); /* base record */
    wr16(out + 40, 3); /* next attr id */
    p = 56;
    p += build_res_attr(out + p, NTFS_ATTR_STDINFO, 0, si, si_len);
    p += build_res_attr(out + p, NTFS_ATTR_FILENAME, 1, fn, fn_len);
    if (runs) {
        data_len = build_nonres_data_attr(out + p, 2, runs, real, alloc);
        if (data_len == 0) return NTFS_ERR_TOO_BIG;
    } else {
        data_len = build_res_attr(out + p, NTFS_ATTR_DATA, 2, res_data, res_len);
    }
    p += data_len;
    if (p + 8 > rec_bytes) return NTFS_ERR_TOO_BIG;
    wr32(out + p, NTFS_ATTR_END);
    wr32(out + p + 4, 0);
    p += 8;
    wr32(out + 24, p); /* bytes used */
    return NTFS_OK;
}

/* Build a complete directory record (SI + FN + empty INDEX_ROOT). */
static int build_dir_record(uint8_t *out, uint64_t rec_no, uint16_t seq,
                            const uint8_t *si, uint32_t si_len,
                            const uint8_t *fn, uint32_t fn_len)
{
    uint32_t rec_bytes = g_vol.info.mft_record_bytes;
    uint32_t p;
    uint8_t idxroot[48];
    uint32_t idx_len;
    (void)rec_no;
    n_memset(out, 0, rec_bytes);
    n_memcpy(out, (const uint8_t *)"FILE", 4);
    wr16(out + 4, 48);
    wr16(out + 6, (uint16_t)(1 + rec_bytes / g_vol.info.bytes_per_sector));
    wr64(out + 8, 0);
    wr16(out + 16, seq ? seq : 1);
    wr16(out + 18, 1);
    wr16(out + 20, 56);
    wr16(out + 22, 0x03);
    wr32(out + 28, rec_bytes);
    wr64(out + 32, 0);
    wr16(out + 40, 3);
    p = 56;
    p += build_res_attr(out + p, NTFS_ATTR_STDINFO, 0, si, si_len);
    p += build_res_attr(out + p, NTFS_ATTR_FILENAME, 1, fn, fn_len);
    /* INDEX_ROOT value: attr type 0x30, collation 1, index block
     * size, clusters/block, index header (entries off 16, length
     * 32, alloc 32, flags 0 = small), one end entry. */
    n_memset(idxroot, 0, sizeof(idxroot));
    wr32(idxroot, NTFS_ATTR_FILENAME);
    wr32(idxroot + 4, 1);
    wr32(idxroot + 8, g_vol.info.index_block_bytes);
    idxroot[12] = (uint8_t)(g_vol.info.index_block_bytes / g_vol.info.cluster_bytes);
    wr32(idxroot + 16, 16); /* entries offset */
    wr32(idxroot + 20, 32); /* index length */
    wr32(idxroot + 24, 32); /* allocated */
    wr32(idxroot + 28, 0);  /* flags: small */
    wr64(idxroot + 32, 0);  /* end entry ref */
    wr16(idxroot + 40, 16); /* end entry length */
    wr16(idxroot + 42, 0);  /* stream length */
    wr16(idxroot + 44, 0x02); /* last entry */
    {
        /* Named resident attribute: header 24 + name "$I30"
         * (4 UTF-16 chars) + value at offset 32. */
        uint32_t total = align8(32 + 48);
        uint8_t *a = out + p;
        static const uint16_t iname[4] = { '$', 'I', '3', '0' };
        int i;
        wr32(a, NTFS_ATTR_INDEX_ROOT);
        wr32(a + 4, total);
        a[8] = 0; a[9] = 4;
        wr16(a + 10, 24);
        wr16(a + 12, 0);
        wr16(a + 14, 2);
        wr32(a + 16, 48);
        wr16(a + 20, 32);
        a[22] = 0; a[23] = 0;
        for (i = 0; i < 4; i++) wr16(a + 24 + 2 * i, iname[i]);
        n_memcpy(a + 32, idxroot, 48);
        idx_len = total;
    }
    p += idx_len;
    if (p + 8 > rec_bytes) return NTFS_ERR_TOO_BIG;
    wr32(out + p, NTFS_ATTR_END);
    wr32(out + p + 4, 0);
    p += 8;
    wr32(out + 24, p);
    return NTFS_OK;
}

/* Extract the SI and FN values of a record into caller buffers. */
static int extract_si_fn(const uint8_t *rec, uint8_t *si, uint32_t *si_len, uint8_t *fn, uint32_t *fn_len)
{
    uint32_t off;
    int r = find_attr(rec, NTFS_ATTR_STDINFO, 0, 0, 0, &off);
    const uint8_t *attr;
    if (r != NTFS_OK) return r;
    attr = rec + off;
    if (attr[8] != 0) return NTFS_ERR_CORRUPT;
    *si_len = rd32(attr + 16);
    if (*si_len > 72) return NTFS_ERR_CORRUPT;
    n_memcpy(si, attr + rd16(attr + 20), *si_len);
    r = find_attr(rec, NTFS_ATTR_FILENAME, 0, 0, 0, &off);
    if (r != NTFS_OK) return r;
    attr = rec + off;
    if (attr[8] != 0) return NTFS_ERR_CORRUPT;
    *fn_len = rd32(attr + 16);
    if (*fn_len > 66 + 510) return NTFS_ERR_CORRUPT;
    n_memcpy(fn, attr + rd16(attr + 20), *fn_len);
    return NTFS_OK;
}

static void make_si(uint8_t *si, uint32_t *si_len, int is_dir)
{
    n_memset(si, 0, 72);
    wr32(si + 32, is_dir ? 0x10u : 0x20u);
    *si_len = 72;
}

static void make_fn(uint8_t *fn, uint32_t *fn_len, uint64_t parent_mft, uint16_t parent_seq,
                    uint64_t alloc, uint64_t real, int is_dir,
                    const uint16_t *name, int name_len)
{
    int i;
    n_memset(fn, 0, 66 + 2 * 255);
    wr64(fn, parent_mft | ((uint64_t)parent_seq << 48));
    wr64(fn + 40, alloc);
    wr64(fn + 48, real);
    wr32(fn + 56, is_dir ? 0x10000000u : 0x20u);
    fn[64] = (uint8_t)name_len;
    fn[65] = 1; /* Win32 namespace */
    for (i = 0; i < name_len; i++) wr16(fn + 66 + 2 * i, name[i]);
    *fn_len = 66 + 2u * (uint32_t)name_len;
}

/* ---- directory entry insert / remove / update ---- */
static int fn_namespace(const uint8_t *fn) { return fn[65]; }

/* Insert one entry (16 + FN stream, padded to 8) into an index
 * header at hdr_off in buf (buf_len bytes). The index's
 * allocated size is hdr_alloc (bytes from hdr_off). Inserts in
 * $UpCase sort order (ASCII fold). Returns NTFS_OK,
 * NTFS_ERR_DIR_FULL, or a corruption error. */
static int insert_into_index(uint8_t *buf, uint32_t buf_len, uint32_t hdr_off, uint32_t hdr_alloc,
                             const uint8_t *fn, uint32_t fn_len, uint64_t file_ref)
{
    uint32_t entries_off = rd32(buf + hdr_off);
    uint32_t index_len = rd32(buf + hdr_off + 4);
    uint32_t entry_len = align8(16u + fn_len);
    uint32_t p, insert_at = 0;
    int found_spot = 0;
    uint16_t new_name[255];
    int new_nlen;
    if (hdr_off + index_len > buf_len || hdr_off + hdr_alloc > buf_len) return NTFS_ERR_CORRUPT;
    if (index_len + entry_len > hdr_alloc) return NTFS_ERR_DIR_FULL;
    if (fn_len < 66) return NTFS_ERR_CORRUPT;
    new_nlen = fn[64];
    {
        int i;
        for (i = 0; i < new_nlen && i < 255; i++) new_name[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
    }
    p = hdr_off + entries_off;
    while (p + 16 <= hdr_off + index_len) {
        uint32_t elen = rd16(buf + p + 8);
        uint32_t slen = rd16(buf + p + 10);
        uint16_t eflags = rd16(buf + p + 12);
        if (elen < 16 || (elen & 7) || p + elen > hdr_off + index_len) return NTFS_ERR_CORRUPT;
        if (eflags & 0x02) { insert_at = p; found_spot = 1; break; } /* end entry */
        if (slen >= 66 && fn_namespace(buf + p + 16) != 2) {
            const uint8_t *efn = buf + p + 16;
            int enlen = efn[64];
            uint16_t ename[255];
            int i, cmp;
            if (66 + 2 * enlen > (int)slen) return NTFS_ERR_CORRUPT;
            for (i = 0; i < enlen && i < 255; i++) ename[i] = (uint16_t)(efn[66 + 2 * i] | (efn[67 + 2 * i] << 8));
            cmp = utf16_compare_fold(new_name, new_nlen, ename, enlen);
            if (cmp == 0) return NTFS_ERR_EXISTS;
            if (cmp < 0) { insert_at = p; found_spot = 1; break; }
        }
        p += elen;
    }
    if (!found_spot) return NTFS_ERR_CORRUPT;
    n_memmove(buf + insert_at + entry_len, buf + insert_at, (hdr_off + index_len) - insert_at);
    wr64(buf + insert_at, file_ref);
    wr16(buf + insert_at + 8, (uint16_t)entry_len);
    wr16(buf + insert_at + 10, (uint16_t)fn_len);
    wr16(buf + insert_at + 12, 0);
    wr16(buf + insert_at + 14, 0);
    n_memcpy(buf + insert_at + 16, fn, fn_len);
    if (entry_len > 16 + fn_len) n_memset(buf + insert_at + 16 + fn_len, 0, entry_len - 16 - fn_len);
    wr32(buf + hdr_off + 4, index_len + entry_len);
    return NTFS_OK;
}

/* Remove the entry at entry_off (entry_len bytes) from an index
 * header at hdr_off. The end entry shifts up with the rest. */
static int remove_from_index(uint8_t *buf, uint32_t buf_len, uint32_t hdr_off, uint32_t entry_off, uint32_t entry_len)
{
    uint32_t index_len = rd32(buf + hdr_off + 4);
    uint32_t end;
    if (hdr_off + index_len > buf_len) return NTFS_ERR_CORRUPT;
    if (entry_off < hdr_off + 16 || entry_off + entry_len > hdr_off + index_len) return NTFS_ERR_CORRUPT;
    end = hdr_off + index_len;
    n_memmove(buf + entry_off, buf + entry_off + entry_len, end - (entry_off + entry_len));
    wr32(buf + hdr_off + 4, index_len - entry_len);
    return NTFS_OK;
}

/* Update the $FILE_NAME sizes of the index entry for a file
 * inside its parent directory (used after an overwrite). */
static int dir_update_fn(uint64_t parent_mft, const uint16_t *name, int name_len, uint64_t alloc, uint64_t real)
{
    find_ctx_t fc;
    int r = dir_find(parent_mft, name, name_len, &fc);
    if (r != NTFS_OK) return r;
    if (!fc.found) return NTFS_ERR_NOT_FOUND;
    if (fc.hit.loc_kind == 0) {
        uint8_t *rec = g_vol.rec;
        uint8_t *fn;
        r = read_mft_record(parent_mft, rec);
        if (r != NTFS_OK) return r;
        if (fc.hit.entry_off + 16 + 66 > g_vol.info.mft_record_bytes) return NTFS_ERR_CORRUPT;
        fn = rec + fc.hit.entry_off + 16;
        wr64(fn + 40, alloc);
        wr64(fn + 48, real);
        return write_mft_record(parent_mft, rec);
    } else {
        uint8_t *rec = g_vol.rec;
        uint8_t *blk = g_vol.rec2;
        uint32_t alloc_off;
        ntfs_runlist_t rl;
        uint64_t byte_off;
        r = read_mft_record(parent_mft, rec);
        if (r != NTFS_OK) return r;
        r = find_attr(rec, NTFS_ATTR_INDEX_ALLOC, 1, NTFS_NAME_I30, 4, &alloc_off);
        if (r != NTFS_OK) return r;
        {
            const uint8_t *attr = rec + alloc_off;
            if (attr[8] == 0) return NTFS_ERR_CORRUPT;
            r = parse_runs(attr, rd32(attr + 4), &rl);
            if (r != NTFS_OK) return r;
        }
        byte_off = fc.hit.block_vcn * (uint64_t)g_vol.info.cluster_bytes;
        r = read_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
        if (r != NTFS_OK) return r;
        if (n_memcmp(blk, (const uint8_t *)"INDX", 4) != 0) return NTFS_ERR_CORRUPT;
        r = fixup_apply(blk, g_vol.info.index_block_bytes);
        if (r != NTFS_OK) return r;
        if (fc.hit.entry_off + 16 + 66 > g_vol.info.index_block_bytes) return NTFS_ERR_CORRUPT;
        {
            uint8_t *fn = blk + fc.hit.entry_off + 16;
            wr64(fn + 40, alloc);
            wr64(fn + 48, real);
        }
        r = fixup_create(blk, g_vol.info.index_block_bytes);
        if (r != NTFS_OK) return r;
        return write_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
    }
}

/* Insert a directory entry for a new file/dir into its parent. */
static int dir_insert(uint64_t parent_mft, uint64_t file_mft, uint16_t file_seq, const uint8_t *fn, uint32_t fn_len)
{
    uint8_t *rec = g_vol.rec;
    uint32_t root_off;
    int r = read_mft_record(parent_mft, rec);
    uint64_t file_ref;
    if (r != NTFS_OK) return r;
    if (!(rd16(rec + 22) & 0x02)) return NTFS_ERR_NOT_DIR;
    file_ref = file_mft | ((uint64_t)file_seq << 48);
    r = find_filename_index_root(rec, &root_off);
    if (r != NTFS_OK) return r;
    {
        const uint8_t *attr = rec + root_off;
        uint32_t alen = rd32(attr + 4);
        uint16_t coff;
        uint32_t hdr_off, hdr_alloc, hdr_flags;
        if (attr[8] != 0) return NTFS_ERR_CORRUPT;
        coff = rd16(attr + 20);
        if ((uint32_t)coff + 32 > alen) return NTFS_ERR_CORRUPT;
        hdr_off = root_off + coff + 16;
        hdr_alloc = rd32(attr + coff + 24);
        hdr_flags = rd32(attr + coff + 28);
        if (!(hdr_flags & 0x01)) {
            /* Small directory: everything lives in INDEX_ROOT.
             * A fresh directory's index only has room for its
             * end entry, so grow the INDEX_ROOT attribute first
             * (shift the record tail forward) when the new
             * entry does not fit in the allocated index. */
            {
                uint32_t index_len = rd32(rec + hdr_off + 4);
                uint32_t entry_len = align8(16u + fn_len);
                uint32_t need_vlen = 16 + index_len + entry_len;
                if (need_vlen > hdr_alloc + 16) {
                    uint32_t new_attr_len = align8(coff + need_vlen);
                    uint32_t used = rd32(rec + 24);
                    uint32_t delta;
                    if (new_attr_len <= alen) {
                        /* Room already inside the attribute. */
                    } else {
                        delta = new_attr_len - alen;
                        if (used + delta > g_vol.info.mft_record_bytes) return NTFS_ERR_DIR_FULL;
                        n_memmove(rec + root_off + alen + delta, rec + root_off + alen, used - (root_off + alen));
                        wr32(rec + root_off + 4, new_attr_len);
                        wr32(rec + 24, used + delta);
                        alen = new_attr_len;
                    }
                    hdr_alloc = need_vlen - 16;
                    wr32(rec + hdr_off + 8, hdr_alloc);
                }
            }
            r = insert_into_index(rec, g_vol.info.mft_record_bytes, hdr_off, hdr_alloc, fn, fn_len, file_ref);
            if (r != NTFS_OK) return r;
            /* INDEX_ROOT value length must cover the new index
             * length. */
            {
                uint32_t new_index_len = rd32(rec + hdr_off + 4);
                uint32_t new_vlen = 16 + new_index_len;
                wr32(rec + root_off + 16, new_vlen);
                {
                    uint32_t used = rd32(rec + 24);
                    uint32_t end = root_off + coff + new_vlen;
                    if (end + 8 > used) wr32(rec + 24, end + 8);
                }
            }
            return write_mft_record(parent_mft, rec);
        }
        /* Large directory: pick the INDX block the name belongs
         * in (first block with an entry greater than the name,
         * else the last block), insert there. */
        {
            uint32_t alloc_off;
            ntfs_runlist_t rl;
            uint64_t blocks, b;
            r = find_attr(rec, NTFS_ATTR_INDEX_ALLOC, 1, NTFS_NAME_I30, 4, &alloc_off);
            if (r != NTFS_OK) return r;
            {
                const uint8_t *aattr = rec + alloc_off;
                if (aattr[8] == 0) return NTFS_ERR_CORRUPT;
                r = parse_runs(aattr, rd32(aattr + 4), &rl);
                if (r != NTFS_OK) return r;
            }
            blocks = rl.total_clusters * (uint64_t)g_vol.info.cluster_bytes / g_vol.info.index_block_bytes;
            if (blocks == 0) return NTFS_ERR_CORRUPT;
            for (b = 0; b < blocks; b++) {
                uint8_t *blk = g_vol.rec2;
                uint64_t byte_off = b * (uint64_t)g_vol.info.index_block_bytes;
                uint32_t hdr_len, hdr_alloc2;
                int is_target = 0;
                r = read_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
                if (r != NTFS_OK) return r;
                if (n_memcmp(blk, (const uint8_t *)"INDX", 4) != 0) return NTFS_ERR_CORRUPT;
                r = fixup_apply(blk, g_vol.info.index_block_bytes);
                if (r != NTFS_OK) return r;
                hdr_len = rd32(blk + 24 + 4);
                hdr_alloc2 = rd32(blk + 24 + 8);
                /* Decide: does this block contain an entry
                 * greater than the new name, or is it the last
                 * block? Scan its entries. */
                {
                    uint32_t p = 24 + rd32(blk + 24);
                    uint32_t end = 24 + hdr_len;
                    int has_greater = 0, has_any = 0;
                    uint16_t new_name[255];
                    int new_nlen = fn[64], i;
                    for (i = 0; i < new_nlen && i < 255; i++) new_name[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
                    while (p + 16 <= end) {
                        uint32_t elen = rd16(blk + p + 8);
                        uint32_t slen = rd16(blk + p + 10);
                        uint16_t eflags = rd16(blk + p + 12);
                        if (elen < 16 || p + elen > end) return NTFS_ERR_CORRUPT;
                        if (eflags & 0x02) break;
                        if (slen >= 66) {
                            const uint8_t *efn = blk + p + 16;
                            int enlen = efn[64];
                            uint16_t ename[255];
                            int j;
                            for (j = 0; j < enlen && j < 255; j++) ename[j] = (uint16_t)(efn[66 + 2 * j] | (efn[67 + 2 * j] << 8));
                            has_any = 1;
                            if (utf16_compare_fold(new_name, new_nlen, ename, enlen) < 0) { has_greater = 1; break; }
                        }
                        p += elen;
                    }
                    (void)has_any;
                    if (has_greater || b + 1 == blocks) is_target = 1;
                }
                if (!is_target) continue;
                r = insert_into_index(blk, g_vol.info.index_block_bytes, 24, hdr_alloc2, fn, fn_len, file_ref);
                if (r != NTFS_OK) return r;
                r = fixup_create(blk, g_vol.info.index_block_bytes);
                if (r != NTFS_OK) return r;
                return write_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
            }
            return NTFS_ERR_DIR_FULL;
        }
    }
}

/* Remove a directory entry located by a previous dir_find hit. */
static int dir_remove(uint64_t parent_mft, const ntfs_dir_hit_t *hit)
{
    uint8_t *rec = g_vol.rec;
    int r = read_mft_record(parent_mft, rec);
    if (r != NTFS_OK) return r;
    if (hit->loc_kind == 0) {
        uint32_t root_off;
        r = find_filename_index_root(rec, &root_off);
        if (r != NTFS_OK) return r;
        {
            const uint8_t *attr = rec + root_off;
            uint16_t coff = rd16(attr + 20);
            uint32_t hdr_off = root_off + coff + 16;
            r = remove_from_index(rec, g_vol.info.mft_record_bytes, hdr_off, hit->entry_off, hit->entry_len);
            if (r != NTFS_OK) return r;
            /* Shrink the resident value length to match. */
            wr32(rec + root_off + 16, 16 + rd32(rec + hdr_off + 4));
        }
        return write_mft_record(parent_mft, rec);
    } else {
        uint8_t *blk = g_vol.rec2;
        uint32_t alloc_off;
        ntfs_runlist_t rl;
        uint64_t byte_off;
        r = find_attr(rec, NTFS_ATTR_INDEX_ALLOC, 1, NTFS_NAME_I30, 4, &alloc_off);
        if (r != NTFS_OK) return r;
        {
            const uint8_t *attr = rec + alloc_off;
            if (attr[8] == 0) return NTFS_ERR_CORRUPT;
            r = parse_runs(attr, rd32(attr + 4), &rl);
            if (r != NTFS_OK) return r;
        }
        byte_off = hit->block_vcn * (uint64_t)g_vol.info.cluster_bytes;
        r = read_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
        if (r != NTFS_OK) return r;
        if (n_memcmp(blk, (const uint8_t *)"INDX", 4) != 0) return NTFS_ERR_CORRUPT;
        r = fixup_apply(blk, g_vol.info.index_block_bytes);
        if (r != NTFS_OK) return r;
        r = remove_from_index(blk, g_vol.info.index_block_bytes, 24, hit->entry_off, hit->entry_len);
        if (r != NTFS_OK) return r;
        r = fixup_create(blk, g_vol.info.index_block_bytes);
        if (r != NTFS_OK) return r;
        return write_runs(&rl, byte_off, g_vol.info.index_block_bytes, blk);
    }
}

/* Roll back a half-created file: free its clusters, clear its
 * MFT bitmap bit, mark the record unused. Best effort. */
static void rollback_create(uint64_t mft_no, const ntfs_runlist_t *runs, int has_runs)
{
    uint8_t *rec = g_vol.rec;
    if (has_runs && runs) (void)free_runs(runs);
    (void)bmp_set(2, mft_no, 0);
    if (read_mft_record(mft_no, rec) == NTFS_OK) {
        uint16_t flags = rd16(rec + 22);
        wr16(rec + 22, (uint16_t)(flags & ~0x0001));
        (void)write_mft_record(mft_no, rec);
    }
}

/* True (1) if a directory has any real entry, 0 if empty,
 * negative on error. */
static int has_entry_cb(const ntfs_dir_hit_t *hit, const uint8_t *fn, uint32_t fn_len, void *ctx)
{
    (void)hit; (void)fn; (void)fn_len; (void)ctx;
    return 1;
}
static int ntfs_dir_has_entry(uint64_t dir_mft)
{
    int r = dir_scan(dir_mft, has_entry_cb, 0);
    if (r == 1) return 1;
    if (r == NTFS_OK) return 0;
    return r;
}

/* ---- write / create / delete / mkdir ---- */
int ntfs_write_file(const char *path, const uint8_t *data, uint64_t len)
{
    uint64_t mft_no, parent_mft;
    uint16_t leaf[255];
    int leaf_len = 0;
    int exists = 0;
    uint8_t si[72], fn[66 + 2 * 255];
    uint32_t si_len = 0, fn_len = 0;
    uint8_t *rec = g_vol.rec;
    uint8_t *newrec = g_vol.rec2;
    data_info_t old_di;
    int has_old_runs = 0;
    ntfs_runlist_t old_runs;
    uint16_t parent_seq = 0;
    uint16_t file_seq = 1;
    uint64_t file_alloc;
    int resident;
    ntfs_runlist_t new_runs;
    int has_new_runs = 0;
    int r;
    if (!g_vol.mounted || !g_vol.writable || !path) return NTFS_ERR_BAD_ARG;
    if (data == 0 && len != 0) return NTFS_ERR_BAD_ARG;
    if (len > 0x7FFFFFFFull) return NTFS_ERR_TOO_BIG;
    r = resolve_path(path, &mft_no, &parent_mft, leaf, &leaf_len);
    if (r == NTFS_OK) exists = 1;
    else if (r == NTFS_ERR_NOT_FOUND && leaf_len > 0) exists = 0;
    else return r;
    if (exists) {
        if (mft_no < 16) return NTFS_ERR_SYSTEM_FILE;
        r = read_mft_record(mft_no, rec);
        if (r != NTFS_OK) return r;
        if (rd16(rec + 22) & 0x02) return NTFS_ERR_IS_DIR;
        file_seq = rd16(rec + 16);
        r = get_data_info(rec, &old_di);
        if (r != NTFS_OK) return r;
        if (old_di.attr_flags & 0x0001) return NTFS_ERR_UNSUPPORTED; /* compressed */
        if (old_di.attr_flags & 0x4000) return NTFS_ERR_UNSUPPORTED; /* encrypted */
        if (old_di.attr_flags & 0x8000) return NTFS_ERR_UNSUPPORTED; /* sparse: refuse, do not rewrite holes as data */
        if (!old_di.resident) { old_runs = old_di.runs; has_old_runs = 1; }
        r = extract_si_fn(rec, si, &si_len, fn, &fn_len);
        if (r != NTFS_OK) return r;
        parent_mft = rd64(fn) & 0x0000FFFFFFFFFFFFull;
        parent_seq = (uint16_t)(rd64(fn) >> 48);
        (void)parent_seq;
    } else {
        /* New file: parent must be a directory. */
        r = read_mft_record(parent_mft, rec);
        if (r != NTFS_OK) return r;
        if (!(rd16(rec + 22) & 0x02)) return NTFS_ERR_NOT_DIR;
        parent_seq = rd16(rec + 16);
        make_si(si, &si_len, 0);
        /* FN alloc/real are filled in below, once the layout
         * (resident vs non-resident) is known. */
        make_fn(fn, &fn_len, parent_mft, parent_seq, 0, 0, 0, leaf, leaf_len);
    }
    /* Resident if it fits in the record with SI + FN + $DATA. */
    {
        uint32_t room = g_vol.info.mft_record_bytes - 56
            - align8(24 + si_len) - align8(24 + fn_len) - 24 - 8;
        resident = (len <= room);
    }
    file_alloc = resident ? 0 : (uint64_t)((len + g_vol.info.cluster_bytes - 1) / g_vol.info.cluster_bytes) * g_vol.info.cluster_bytes;
    /* FN sizes: resident files carry alloc 0 and the real size;
     * non-resident files carry the allocated cluster bytes. */
    wr64(fn + 40, file_alloc);
    wr64(fn + 48, len);
    /* From here on we mutate the volume: set the dirty flag
     * first, so a crash leaves the volume dirty and Windows
     * runs chkdsk instead of trusting a half-written state. */
    r = set_dirty_flag(1);
    if (r != NTFS_OK) return r;
    if (!resident) {
        uint64_t need_clusters = (len + g_vol.info.cluster_bytes - 1) / g_vol.info.cluster_bytes;
        r = alloc_clusters(need_clusters, &new_runs);
        if (r != NTFS_OK) { (void)set_dirty_flag(0); return r; }
        has_new_runs = 1;
        if (len > 0) {
            r = write_runs(&new_runs, 0, (uint32_t)len, data);
            if (r != NTFS_OK) { (void)free_runs(&new_runs); (void)set_dirty_flag(0); return r; }
        }
        /* Zero the tail of the last cluster past the file end,
         * so the initialized area never leaks old disk bytes. */
        if (len % g_vol.info.cluster_bytes) {
            uint64_t last_vcn = (len / g_vol.info.cluster_bytes);
            uint64_t lcn;
            int sparse;
            r = vcn_to_lcn(&new_runs, last_vcn, &lcn, &sparse);
            if (r == NTFS_OK && !sparse) {
                uint32_t tail = (uint32_t)(len % g_vol.info.cluster_bytes);
                r = cluster_read(lcn, g_vol.cluster);
                if (r == NTFS_OK) {
                    n_memset(g_vol.cluster + tail, 0, g_vol.info.cluster_bytes - tail);
                    r = cluster_write(lcn, g_vol.cluster);
                }
                if (r != NTFS_OK) { (void)free_runs(&new_runs); (void)set_dirty_flag(0); return r; }
            }
        }
    }
    if (exists) {
        r = build_file_record(newrec, mft_no, file_seq, 0, si, si_len, fn, fn_len,
                              resident ? data : 0, resident ? (uint32_t)len : 0,
                              resident ? 0 : &new_runs, len, file_alloc);
        if (r != NTFS_OK) goto fail_new;
        r = write_mft_record(mft_no, newrec);
        if (r != NTFS_OK) goto fail_new;
        /* Old clusters are freed only now that the new record
         * (and data) are safely on disk. From here on the new
         * record owns the new runs: later failures must NOT
         * free them (fail_after, not fail_new). */
        if (has_old_runs) {
            r = free_runs(&old_runs);
            if (r != NTFS_OK) goto fail_after;
        }
        /* Keep the parent directory's $FILE_NAME copy in sync. */
        {
            uint16_t lname[255];
            int ln = fn[64], i;
            for (i = 0; i < ln && i < 255; i++) lname[i] = (uint16_t)(fn[66 + 2 * i] | (fn[67 + 2 * i] << 8));
            r = dir_update_fn(parent_mft, lname, ln, file_alloc, len);
            if (r != NTFS_OK) goto fail_after;
        }
    } else {
        uint64_t new_no;
        uint16_t seq = 1;
        r = find_free_mft_record(&new_no);
        if (r != NTFS_OK) goto fail_new;
        r = bmp_set(2, new_no, 1);
        if (r != NTFS_OK) goto fail_new;
        r = build_file_record(newrec, new_no, seq, 0, si, si_len, fn, fn_len,
                              resident ? data : 0, resident ? (uint32_t)len : 0,
                              resident ? 0 : &new_runs, len, file_alloc);
        if (r != NTFS_OK) { rollback_create(new_no, has_new_runs ? &new_runs : 0, has_new_runs); (void)set_dirty_flag(0); return r; }
        r = write_mft_record(new_no, newrec);
        if (r != NTFS_OK) { rollback_create(new_no, has_new_runs ? &new_runs : 0, has_new_runs); (void)set_dirty_flag(0); return r; }
        r = dir_insert(parent_mft, new_no, seq, fn, fn_len);
        if (r != NTFS_OK) { rollback_create(new_no, has_new_runs ? &new_runs : 0, has_new_runs); (void)set_dirty_flag(0); return r; }
    }
    return set_dirty_flag(0);
fail_after:
    (void)set_dirty_flag(0);
    return r;
fail_new:
    if (has_new_runs) (void)free_runs(&new_runs);
    (void)set_dirty_flag(0);
    return r;
}

int ntfs_delete(const char *path)
{
    uint64_t mft_no, parent_mft;
    uint16_t leaf[255];
    int leaf_len = 0;
    uint8_t *rec = g_vol.rec;
    uint8_t fn_copy[66 + 2 * 255];
    uint32_t fn_len = 0;
    uint16_t lname[255];
    int ln = 0, i;
    data_info_t di;
    ntfs_runlist_t runs;
    int has_runs = 0;
    int is_dir = 0;
    int r;
    if (!g_vol.mounted || !g_vol.writable || !path) return NTFS_ERR_BAD_ARG;
    r = resolve_path(path, &mft_no, &parent_mft, leaf, &leaf_len);
    if (r != NTFS_OK) return r;
    if (mft_no < 16) return NTFS_ERR_SYSTEM_FILE;
    r = read_mft_record(mft_no, rec);
    if (r != NTFS_OK) return r;
    is_dir = (rd16(rec + 22) & 0x02) != 0;
    if (is_dir) {
        /* A directory is empty iff its first index entry is
         * the end entry: scan and stop at the first real one. */
        int has = ntfs_dir_has_entry(mft_no);
        if (has < 0) return has;
        if (has > 0) return NTFS_ERR_DIR_NOT_EMPTY;
    } else {
        r = get_data_info(rec, &di);
        if (r != NTFS_OK) return r;
        if (di.attr_flags & 0x0001) return NTFS_ERR_UNSUPPORTED;
        if (di.attr_flags & 0x4000) return NTFS_ERR_UNSUPPORTED;
        if (!di.resident) { runs = di.runs; has_runs = 1; }
    }
    {
        uint8_t si[72];
        uint32_t si_len = 0;
        r = extract_si_fn(rec, si, &si_len, fn_copy, &fn_len);
        if (r != NTFS_OK) return r;
        parent_mft = rd64(fn_copy) & 0x0000FFFFFFFFFFFFull;
        ln = fn_copy[64];
        for (i = 0; i < ln && i < 255; i++) lname[i] = (uint16_t)(fn_copy[66 + 2 * i] | (fn_copy[67 + 2 * i] << 8));
    }
    r = set_dirty_flag(1);
    if (r != NTFS_OK) return r;
    /* Remove the directory entry first: if we crash after
     * this, clusters leak (chkdsk-able); if we freed clusters
     * first, the directory would point at a free record. */
    {
        find_ctx_t fc;
        r = dir_find(parent_mft, lname, ln, &fc);
        if (r != NTFS_OK) goto fail_del;
        if (!fc.found) { r = NTFS_ERR_NOT_FOUND; goto fail_del; }
        r = dir_remove(parent_mft, &fc.hit);
        if (r != NTFS_OK) goto fail_del;
    }
    if (has_runs) {
        r = free_runs(&runs);
        if (r != NTFS_OK) goto fail_del;
    }
    r = bmp_set(2, mft_no, 0);
    if (r != NTFS_OK) goto fail_del;
    r = read_mft_record(mft_no, rec);
    if (r != NTFS_OK) goto fail_del;
    {
        uint16_t flags = rd16(rec + 22);
        uint16_t seq = rd16(rec + 16);
        wr16(rec + 22, (uint16_t)(flags & ~0x0001));
        wr16(rec + 16, (uint16_t)(seq + 1));
        r = write_mft_record(mft_no, rec);
        if (r != NTFS_OK) goto fail_del;
    }
    return set_dirty_flag(0);
fail_del:
    (void)set_dirty_flag(0);
    return r;
}

int ntfs_mkdir(const char *path)
{
    uint64_t mft_no, parent_mft;
    uint16_t leaf[255];
    int leaf_len = 0;
    uint8_t si[72], fn[66 + 2 * 255];
    uint32_t si_len = 0, fn_len = 0;
    uint8_t *rec = g_vol.rec;
    uint8_t *newrec = g_vol.rec2;
    uint64_t new_no;
    uint16_t parent_seq;
    int r;
    if (!g_vol.mounted || !g_vol.writable || !path) return NTFS_ERR_BAD_ARG;
    r = resolve_path(path, &mft_no, &parent_mft, leaf, &leaf_len);
    if (r == NTFS_OK) return NTFS_ERR_EXISTS;
    if (r != NTFS_ERR_NOT_FOUND || leaf_len == 0) return r;
    r = read_mft_record(parent_mft, rec);
    if (r != NTFS_OK) return r;
    if (!(rd16(rec + 22) & 0x02)) return NTFS_ERR_NOT_DIR;
    parent_seq = rd16(rec + 16);
    make_si(si, &si_len, 1);
    make_fn(fn, &fn_len, parent_mft, parent_seq, 0, 0, 1, leaf, leaf_len);
    r = set_dirty_flag(1);
    if (r != NTFS_OK) return r;
    r = find_free_mft_record(&new_no);
    if (r != NTFS_OK) { (void)set_dirty_flag(0); return r; }
    r = bmp_set(2, new_no, 1);
    if (r != NTFS_OK) { (void)set_dirty_flag(0); return r; }
    r = build_dir_record(newrec, new_no, 1, si, si_len, fn, fn_len);
    if (r != NTFS_OK) { rollback_create(new_no, 0, 0); (void)set_dirty_flag(0); return r; }
    r = write_mft_record(new_no, newrec);
    if (r != NTFS_OK) { rollback_create(new_no, 0, 0); (void)set_dirty_flag(0); return r; }
    r = dir_insert(parent_mft, new_no, 1, fn, fn_len);
    if (r != NTFS_OK) { rollback_create(new_no, 0, 0); (void)set_dirty_flag(0); return r; }
    return set_dirty_flag(0);
}

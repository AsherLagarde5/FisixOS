/* part.c - GPT + MBR parsing and editing (see part.h).
 * Original FisixOS code. Freestanding: no libc.
 * CRC32 is the standard IEEE polynomial, as used by GPT.
 */
#include "part.h"
#include "fat.h"

const uint8_t PART_GUID_ESP[16]        = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
const uint8_t PART_GUID_MS_BASIC[16]   = {0xA2,0xA0,0xD0,0xEB,0xE5,0xB9,0x33,0x44,0x87,0xC0,0x68,0xB6,0xB7,0x26,0x99,0xC7};
const uint8_t PART_GUID_LINUX_FS[16]   = {0xAF,0x3D,0xC6,0x0F,0x83,0x84,0x72,0x47,0x8E,0x79,0x3D,0x69,0xD8,0x47,0x7D,0xE4};
const uint8_t PART_GUID_LINUX_SWAP[16] = {0x6D,0xFD,0x57,0x06,0xAB,0xA4,0xC4,0x43,0x84,0xE5,0x09,0x33,0xC8,0x4B,0x4F,0x4F};
const uint8_t PART_GUID_FISIX_DATA[16] = {0x46,0x49,0x53,0x49,0x58,0x4F,0x53,0x44,0x41,0x54,0x41,0x00,0x00,0x00,0x00,0x01};

static uint32_t p_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu; size_t i; int b;
    for (i=0;i<len;i++) { crc ^= data[i]; for (b=0;b<8;b++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1); }
    return crc ^ 0xFFFFFFFFu;
}
static uint16_t p_rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t p_rd32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t p_rd64(const uint8_t *p){ uint64_t v=0; int i; for (i=0;i<8;i++) v |= (uint64_t)p[i] << (8*i); return v; }
static void p_wr16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void p_wr32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void p_wr64(uint8_t *p, uint64_t v){ int i; for (i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void p_memset(uint8_t *p, uint8_t v, size_t n){ size_t i; for (i=0;i<n;i++) p[i]=v; }
static void p_memcpy(uint8_t *d, const uint8_t *s, size_t n){ size_t i; for (i=0;i<n;i++) d[i]=s[i]; }
static int p_guid_eq(const uint8_t a[16], const uint8_t b[16]){ int i; for (i=0;i<16;i++) if (a[i]!=b[i]) return 0; return 1; }
static int p_guid_zero(const uint8_t g[16]){ int i; for (i=0;i<16;i++) if (g[i]) return 0; return 1; }
static void p_strcpy(char *d, const char *s, size_t cap){ size_t i=0; if (!cap) return; while (s && s[i] && i+1<cap) { d[i]=s[i]; i++; } d[i]=0; }

/* Deterministic unique GUIDs: xorshift PRNG seeded from the disk
 * size + entry index. Not cryptographic and not random across
 * boots on the same disk size - uniqueness within one disk is
 * what GPT needs, and that holds (different index -> different
 * stream position). A real random source (RDRAND / UEFI RNG)
 * would be better on hardware; this keeps host + loader
 * behaviour identical and testable. */
static uint64_t p_rng_state;
static uint64_t p_rng_next(void){ uint64_t x=p_rng_state; x^=x<<13; x^=x>>7; x^=x<<17; p_rng_state=x; return x; }
static void p_make_guid(uint64_t seed, uint8_t out[16])
{
    int i;
    p_rng_state = seed ? seed : 0x9E3779B97F4A7C15ULL;
    for (i=0;i<16;i+=8) { uint64_t v=p_rng_next(); int k; for (k=0;k<8 && i+k<16;k++) out[i+k]=(uint8_t)(v>>(8*k)); }
    out[6] = (uint8_t)((out[6] & 0x0F) | 0x40); /* version 4 */
    out[8] = (uint8_t)((out[8] & 0x3F) | 0x80); /* variant */
}

void part_type_name(const uint8_t type_guid[16], uint8_t mbr_type, char *out, size_t cap)
{
    const char *n = "Unknown";
    if (type_guid) {
        if (p_guid_eq(type_guid, PART_GUID_ESP)) n = "EFI System";
        else if (p_guid_eq(type_guid, PART_GUID_MS_BASIC)) n = "Microsoft basic data";
        else if (p_guid_eq(type_guid, PART_GUID_LINUX_FS)) n = "Linux filesystem";
        else if (p_guid_eq(type_guid, PART_GUID_LINUX_SWAP)) n = "Linux swap";
        else if (p_guid_eq(type_guid, PART_GUID_FISIX_DATA)) n = "FisixOS data";
        else if (p_guid_zero(type_guid)) n = "Unused";
        /* No GPT GUID matched: fall through to the MBR type byte
         * below (MBR entries carry their type in mbr_type). */
        else if (mbr_type == 0) { p_strcpy(out, n, cap); return; }
    }
    if (mbr_type != 0 || !type_guid) {
        switch (mbr_type) {
            case 0x00: n = "Unused"; break;
            case 0x0B: case 0x0C: n = "FAT32"; break;
            case 0x06: n = "FAT16"; break;
            case 0x07: n = "NTFS/exFAT"; break;
            case 0x82: n = "Linux swap"; break;
            case 0x83: n = "Linux"; break;
            case 0xEE: n = "GPT protective"; break;
            case 0xEF: n = "EFI System (MBR)"; break;
            default: n = "Unknown"; break;
        }
    }
    p_strcpy(out, n, cap);
}

static int dev_is_512(const fisix_block_dev_t *dev)
{
    return dev && dev->sector_size==512 && dev->sectors>=67;
}
static int read_sec(const fisix_block_dev_t *dev, uint64_t lba, uint8_t *buf)
{
    return dev->read(dev->ctx, lba, 1, buf)==0 ? 0 : -1;
}
static int write_sec(const fisix_block_dev_t *dev, uint64_t lba, const uint8_t *buf)
{
    return dev->write(dev->ctx, lba, 1, buf)==0 ? 0 : -1;
}
/* Read/write n sectors starting at lba (buf is n*512 bytes). */
static int read_secs(const fisix_block_dev_t *dev, uint64_t lba, uint32_t n, uint8_t *buf)
{
    uint32_t i;
    for (i=0;i<n;i++) if (read_sec(dev, lba+i, buf+i*512)!=0) return -1;
    return 0;
}
static int write_secs(const fisix_block_dev_t *dev, uint64_t lba, uint32_t n, const uint8_t *buf)
{
    uint32_t i;
    for (i=0;i<n;i++) if (write_sec(dev, lba+i, buf+i*512)!=0) return -1;
    return 0;
}

/* GPT header fields we need. */
typedef struct {
    uint64_t my_lba, alt_lba, first_usable, last_usable, entries_lba;
    uint32_t entry_count, entry_size, entries_crc;
} gpt_hdr_t;

static int gpt_header_ok(const uint8_t *h, uint64_t expect_lba, gpt_hdr_t *out)
{
    uint32_t hdr_size, crc_stored, crc_calc;
    uint8_t tmp[512];
    if (!(h[0]=='E'&&h[1]=='F'&&h[2]=='I'&&h[3]==' '&&h[4]=='P'&&h[5]=='A'&&h[6]=='R'&&h[7]=='T')) return -1;
    if (p_rd32(h+8)!=0x00010000u) return -1;
    hdr_size = p_rd32(h+12);
    if (hdr_size<92 || hdr_size>512) return -1;
    if (p_rd64(h+24)!=expect_lba) return -1;
    crc_stored = p_rd32(h+16);
    p_memcpy(tmp, h, 512);
    p_wr32(tmp+16, 0);
    crc_calc = p_crc32(tmp, hdr_size);
    if (crc_calc!=crc_stored) return -1;
    out->my_lba = p_rd64(h+24); out->alt_lba = p_rd64(h+32);
    out->first_usable = p_rd64(h+40); out->last_usable = p_rd64(h+48);
    out->entries_lba = p_rd64(h+72); out->entry_count = p_rd32(h+80); out->entry_size = p_rd32(h+84);
    out->entries_crc = p_rd32(h+88);
    if (out->entry_count==0 || out->entry_count>PART_MAX_ENTRIES) return -1;
    if (out->entry_size<128) return -1;
    return 0;
}

static void gpt_entry_to_part(const uint8_t *e, int idx, part_entry_t *p)
{
    int i;
    p->index = idx;
    p_memcpy(p->type_guid, e, 16);
    p_memcpy(p->unique_guid, e+16, 16);
    p->first_lba = p_rd64(e+32); p->last_lba = p_rd64(e+40);
    p->attrs = p_rd64(e+48);
    p->mbr_type = 0;
    p->bootable = (p->attrs & 0x4) ? 1 : 0;
    for (i=0;i<PART_NAME_MAX;i++) {
        uint16_t ch = p_rd16(e+56+i*2);
        p->name[i] = (ch>=32 && ch<127) ? (char)ch : 0;
        if (!ch) break;
    }
    p->name[PART_NAME_MAX]=0;
    /* Trim trailing NULs / spaces already handled by the loop. */
    { int n=0; while (p->name[n]) n++; while (n>0 && p->name[n-1]==' ') p->name[--n]=0; }
}

/* Parse GPT entries at entries_lba into table. entries_buf must
 * hold entry_count*entry_size bytes. Returns 0 or -1 (CRC). */
static int gpt_parse_entries(const uint8_t *buf, const gpt_hdr_t *h, part_table_t *t)
{
    uint32_t i;
    if (p_crc32(buf, (size_t)h->entry_count * h->entry_size)!=h->entries_crc) return -1;
    t->count = 0;
    for (i=0;i<h->entry_count && t->count<PART_MAX_ENTRIES;i++) {
        const uint8_t *e = buf + (size_t)i*h->entry_size;
        if (p_guid_zero(e)) continue;
        gpt_entry_to_part(e, (int)i, &t->entries[t->count++]);
    }
    return 0;
}

int part_probe(const fisix_block_dev_t *dev, part_table_t *table)
{
    uint8_t sec[512];
    int i;
    if (!dev || !table || !dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    p_memset((uint8_t *)table, 0, sizeof(*table));
    table->disk_sectors = dev->sectors;
    if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
    if (sec[510]!=0x55 || sec[511]!=0xAA) return PART_ERR_NO_TABLE;
    /* GPT? Try primary header, then backup. */
    if (read_sec(dev, 1, sec)==0) {
        gpt_hdr_t h;
        if (gpt_header_ok(sec, 1, &h)==0) {
            uint32_t nsec = (h.entry_count * h.entry_size + 511)/512;
            static uint8_t entries[PART_MAX_ENTRIES * 128];
            if (nsec > 32) return PART_ERR_UNSUPPORTED;
            if (read_secs(dev, h.entries_lba, nsec, entries)==0) {
                table->scheme = PART_SCHEME_GPT;
                table->first_usable = h.first_usable; table->last_usable = h.last_usable;
                if (gpt_parse_entries(entries, &h, table)==0) return PART_OK;
                /* Primary entries CRC bad: fall through to backup. */
            }
        }
    }
    if (read_sec(dev, dev->sectors-1, sec)==0) {
        gpt_hdr_t h;
        if (gpt_header_ok(sec, dev->sectors-1, &h)==0) {
            uint32_t nsec = (h.entry_count * h.entry_size + 511)/512;
            static uint8_t entries[PART_MAX_ENTRIES * 128];
            if (nsec <= 32 && read_secs(dev, h.entries_lba, nsec, entries)==0) {
                table->scheme = PART_SCHEME_GPT;
                table->first_usable = h.first_usable; table->last_usable = h.last_usable;
                if (gpt_parse_entries(entries, &h, table)==0) return PART_OK;
                return PART_ERR_BAD_CRC;
            }
        }
    }
    /* MBR (not a protective 0xEE entry). */
    if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
    for (i=0;i<4;i++) {
        const uint8_t *e = sec + 446 + i*16;
        uint8_t type = e[4];
        part_entry_t *p;
        if (type==0) continue;
        if (type==0xEE) return PART_ERR_NO_TABLE; /* protective MBR, GPT broken */
        p = &table->entries[table->count];
        p->index = i; p->mbr_type = type; p->type_guid[0]=type;
        p_memset(p->type_guid+1, 0, 15); p_memset(p->unique_guid, 0, 16);
        p->first_lba = p_rd32(e+8); p->last_lba = p->first_lba + p_rd32(e+12) - 1;
        p->attrs = 0; p->bootable = (e[0]==0x80) ? 1 : 0;
        p->name[0]=0;
        table->count++;
    }
    /* An MBR signature with no partitions is still an MBR table
     * (just an empty one) - part_add_mbr needs to probe it. Only
     * a missing signature means "no table". */
    table->scheme = PART_SCHEME_MBR;
    table->first_usable = 1; table->last_usable = dev->sectors-1;
    return PART_OK;
}

/* Write one GPT header sector. */
static void gpt_write_header(uint8_t *h, uint64_t my_lba, uint64_t alt_lba,
                             uint64_t first_usable, uint64_t last_usable,
                             uint64_t entries_lba, uint32_t entries_crc)
{
    p_memset(h, 0, 512);
    h[0]='E'; h[1]='F'; h[2]='I'; h[3]=' '; h[4]='P'; h[5]='A'; h[6]='R'; h[7]='T';
    p_wr32(h+8, 0x00010000u); p_wr32(h+12, 92);
    p_wr64(h+24, my_lba); p_wr64(h+32, alt_lba);
    p_wr64(h+40, first_usable); p_wr64(h+48, last_usable);
    p_wr64(h+56, 0x0000000000000002ULL); /* disk GUID, fixed v1 (see below) */
    p_wr64(h+64, 0x0000000000000001ULL);
    p_wr64(h+72, entries_lba); p_wr32(h+80, 128); p_wr32(h+84, 128);
    p_wr32(h+88, entries_crc);
    p_wr32(h+16, p_crc32(h, 92));
}
/* Write the full GPT (protective MBR + both headers + both
 * entry arrays) from a 128-entry array (zero = unused). */
static int gpt_write_all(const fisix_block_dev_t *dev, const uint8_t entries[128*128])
{
    uint8_t sec[512];
    uint32_t ecrc = p_crc32(entries, 128*128);
    uint64_t n = dev->sectors;
    int i;
    /* Protective MBR. */
    p_memset(sec, 0, 512);
    sec[446+4]=0xEE;
    p_wr32(sec+446+8, 1);
    p_wr32(sec+446+12, n>0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)n-1);
    sec[510]=0x55; sec[511]=0xAA;
    if (write_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
    /* Primary entries at LBA 2 (32 sectors). */
    if (write_secs(dev, 2, 32, entries)!=0) return PART_ERR_GENERIC;
    gpt_write_header(sec, 1, n-1, 34, n-34, 2, ecrc);
    /* Deterministic disk GUID: derived from the sector count so
     * two disks of different sizes differ; kept stable per disk. */
    p_make_guid(n ^ 0x46495349584F53ULL, sec+56);
    p_wr32(sec+16, 0); p_wr32(sec+16, p_crc32(sec, 92));
    if (write_sec(dev, 1, sec)!=0) return PART_ERR_GENERIC;
    /* Backup entries at LastBlock-32, backup header at LastBlock. */
    if (write_secs(dev, n-33, 32, entries)!=0) return PART_ERR_GENERIC;
    gpt_write_header(sec, n-1, 1, 34, n-34, n-33, ecrc);
    p_make_guid(n ^ 0x46495349584F53ULL, sec+56);
    p_wr32(sec+16, 0); p_wr32(sec+16, p_crc32(sec, 92));
    if (write_sec(dev, n-1, sec)!=0) return PART_ERR_GENERIC;
    (void)i;
    return PART_OK;
}
/* Load the current GPT entry array (primary copy) into buf. */
static int gpt_load_entries(const fisix_block_dev_t *dev, uint8_t entries[128*128])
{
    return read_secs(dev, 2, 32, entries)==0 ? PART_OK : PART_ERR_GENERIC;
}

int part_create_gpt(const fisix_block_dev_t *dev)
{
    uint8_t entries[128*128];
    uint8_t zero[512];
    uint64_t s;
    if (!dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    p_memset(entries, 0, sizeof(entries));
    p_memset(zero, 0, 512);
    /* Zero LBA 1..33 and the backup area, so no stale table survives. */
    for (s=1;s<34;s++) if (write_sec(dev, s, zero)!=0) return PART_ERR_GENERIC;
    for (s=dev->sectors-33;s<dev->sectors;s++) if (write_sec(dev, s, zero)!=0) return PART_ERR_GENERIC;
    return gpt_write_all(dev, entries);
}
int part_create_mbr(const fisix_block_dev_t *dev)
{
    uint8_t sec[512];
    if (!dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    p_memset(sec, 0, 512);
    sec[510]=0x55; sec[511]=0xAA;
    return write_sec(dev, 0, sec)==0 ? PART_OK : PART_ERR_GENERIC;
}

static int ranges_overlap(uint64_t a0, uint64_t a1, uint64_t b0, uint64_t b1)
{
    return a0<=b1 && b0<=a1;
}
static int gpt_check_free(const part_table_t *t, uint64_t first, uint64_t last, int skip_index)
{
    int i;
    if (first < t->first_usable || last > t->last_usable || first > last) return PART_ERR_OUT_OF_RANGE;
    for (i=0;i<t->count;i++) {
        if (t->entries[i].index==skip_index) continue;
        if (ranges_overlap(first, last, t->entries[i].first_lba, t->entries[i].last_lba)) return PART_ERR_OVERLAP;
    }
    return PART_OK;
}
/* Largest free gap in [first_usable, last_usable], 1 MiB aligned
 * start. Returns 0 with *first>*last when the disk is full. */
static int gpt_free_gap(const part_table_t *t, uint64_t *first_out, uint64_t *last_out)
{
    uint64_t cur = t->first_usable, best_first=0, best_last=0, best_len=0;
    int i, j;
    /* Walk sorted by start: simple selection over the small list. */
    for (;;) {
        uint64_t next_start = t->last_usable+1;
        int found=0;
        for (i=0;i<t->count;i++) {
            if (t->entries[i].first_lba>=cur && t->entries[i].first_lba<next_start) { next_start=t->entries[i].first_lba; found=1; }
        }
        if (!found) {
            if (t->last_usable>=cur) {
                uint64_t a = (cur+2047)/2048*2048, b = t->last_usable;
                if (b>=a && b-a+1>best_len) { best_len=b-a+1; best_first=a; best_last=b; }
            }
            break;
        }
        /* Gap [cur, next_start-1]. */
        {
            uint64_t a = (cur+2047)/2048*2048, b = next_start-1;
            if (b>=a && b-a+1>best_len) { best_len=b-a+1; best_first=a; best_last=b; }
        }
        /* Skip past every partition starting at next_start. */
        for (j=0;j<t->count;j++) if (t->entries[j].first_lba==next_start && t->entries[j].last_lba+1>cur) cur=t->entries[j].last_lba+1;
        if (cur>t->last_usable) break;
    }
    *first_out=best_first; *last_out=best_last;
    return PART_OK;
}
int part_largest_free_gap(const fisix_block_dev_t *dev, uint64_t *first_out, uint64_t *last_out)
{
    part_table_t t;
    int r = part_probe(dev, &t);
    if (r!=PART_OK || t.scheme!=PART_SCHEME_GPT) return PART_ERR_NO_TABLE;
    return gpt_free_gap(&t, first_out, last_out);
}

static void gpt_entry_write(uint8_t *e, const uint8_t type_guid[16], const uint8_t unique[16],
                            uint64_t first, uint64_t last, uint64_t attrs, const char *name)
{
    int i;
    p_memset(e, 0, 128);
    p_memcpy(e, type_guid, 16);
    p_memcpy(e+16, unique, 16);
    p_wr64(e+32, first); p_wr64(e+40, last); p_wr64(e+48, attrs);
    if (name) {
        for (i=0;i<PART_NAME_MAX && name[i];i++) { p_wr16(e+56+i*2, (uint16_t)(uint8_t)name[i]); }
    }
}

int part_add(const fisix_block_dev_t *dev, const uint8_t type_guid[16],
             uint64_t first_lba, uint64_t last_lba, const char *name, uint64_t attrs,
             int *index_out)
{
    part_table_t t;
    uint8_t entries[128*128];
    int r, slot=-1, i;
    uint8_t uniq[16];
    if (!dev_is_512(dev) || !type_guid) return PART_ERR_UNSUPPORTED;
    r = part_probe(dev, &t);
    if (r!=PART_OK || t.scheme!=PART_SCHEME_GPT) return PART_ERR_NO_TABLE;
    if (first_lba==0) return PART_ERR_OUT_OF_RANGE;
    r = gpt_check_free(&t, first_lba, last_lba, -1);
    if (r!=PART_OK) return r;
    if (gpt_load_entries(dev, entries)!=PART_OK) return PART_ERR_GENERIC;
    for (i=0;i<128;i++) if (p_guid_zero(entries+(size_t)i*128)) { slot=i; break; }
    if (slot<0) return PART_ERR_NO_SLOT;
    p_make_guid(dev->sectors ^ ((uint64_t)(slot+1)<<32) ^ first_lba, uniq);
    gpt_entry_write(entries+(size_t)slot*128, type_guid, uniq, first_lba, last_lba, attrs, name);
    r = gpt_write_all(dev, entries);
    if (r==PART_OK && index_out) *index_out=slot;
    return r;
}
int part_add_size(const fisix_block_dev_t *dev, const uint8_t type_guid[16],
                  uint64_t size_sectors, const char *name, uint64_t attrs,
                  int *index_out)
{
    uint64_t first, last;
    int r = part_largest_free_gap(dev, &first, &last);
    if (r!=PART_OK) return r;
    if (first==0 || last<first || last-first+1 < size_sectors) return PART_ERR_NO_SPACE;
    return part_add(dev, type_guid, first, first+size_sectors-1, name, attrs, index_out);
}
int part_delete(const fisix_block_dev_t *dev, int index)
{
    part_table_t t;
    int r;
    if (!dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    r = part_probe(dev, &t);
    if (r!=PART_OK) return r;
    if (t.scheme==PART_SCHEME_GPT) {
        uint8_t entries[128*128];
        if (index<0 || index>=128) return PART_ERR_NOT_FOUND;
        if (gpt_load_entries(dev, entries)!=PART_OK) return PART_ERR_GENERIC;
        if (p_guid_zero(entries+(size_t)index*128)) return PART_ERR_NOT_FOUND;
        p_memset(entries+(size_t)index*128, 0, 128);
        return gpt_write_all(dev, entries);
    } else {
        uint8_t sec[512];
        if (index<0 || index>=4) return PART_ERR_NOT_FOUND;
        if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
        if (sec[446+index*16+4]==0) return PART_ERR_NOT_FOUND;
        p_memset(sec+446+index*16, 0, 16);
        return write_sec(dev, 0, sec)==0 ? PART_OK : PART_ERR_GENERIC;
    }
}
int part_resize(const fisix_block_dev_t *dev, int index, uint64_t new_last_lba)
{
    part_table_t t;
    int r, i;
    if (!dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    r = part_probe(dev, &t);
    if (r!=PART_OK) return r;
    for (i=0;i<t.count;i++) if (t.entries[i].index==index) break;
    if (i>=t.count) return PART_ERR_NOT_FOUND;
    if (new_last_lba < t.entries[i].first_lba) return PART_ERR_OUT_OF_RANGE;
    if (t.scheme==PART_SCHEME_GPT) {
        uint8_t entries[128*128];
        if (new_last_lba > t.last_usable) return PART_ERR_OUT_OF_RANGE;
        r = gpt_check_free(&t, t.entries[i].first_lba, new_last_lba, index);
        if (r!=PART_OK) return r;
        if (gpt_load_entries(dev, entries)!=PART_OK) return PART_ERR_GENERIC;
        p_wr64(entries+(size_t)index*128+40, new_last_lba);
        return gpt_write_all(dev, entries);
    } else {
        uint8_t sec[512];
        uint64_t new_size;
        if (new_last_lba > dev->sectors-1) return PART_ERR_OUT_OF_RANGE;
        /* Overlap check against the other MBR primaries. */
        { int j; for (j=0;j<t.count;j++) { if (t.entries[j].index==index) continue; if (ranges_overlap(t.entries[i].first_lba, new_last_lba, t.entries[j].first_lba, t.entries[j].last_lba)) return PART_ERR_OVERLAP; } }
        new_size = new_last_lba - t.entries[i].first_lba + 1;
        if (new_size > 0xFFFFFFFFULL) return PART_ERR_OUT_OF_RANGE;
        if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
        p_wr32(sec+446+index*16+12, (uint32_t)new_size);
        return write_sec(dev, 0, sec)==0 ? PART_OK : PART_ERR_GENERIC;
    }
}
int part_set_boot(const fisix_block_dev_t *dev, int index)
{
    part_table_t t;
    int r, i, found=0;
    if (!dev_is_512(dev)) return PART_ERR_UNSUPPORTED;
    r = part_probe(dev, &t);
    if (r!=PART_OK) return r;
    for (i=0;i<t.count;i++) if (t.entries[i].index==index) found=1;
    if (!found) return PART_ERR_NOT_FOUND;
    if (t.scheme==PART_SCHEME_GPT) {
        uint8_t entries[128*128];
        if (gpt_load_entries(dev, entries)!=PART_OK) return PART_ERR_GENERIC;
        for (i=0;i<128;i++) {
            uint8_t *e = entries+(size_t)i*128;
            uint64_t a;
            if (p_guid_zero(e)) continue;
            a = p_rd64(e+48);
            if (i==index) a |= 0x4; else a &= ~0x4ULL;
            p_wr64(e+48, a);
        }
        return gpt_write_all(dev, entries);
    } else {
        uint8_t sec[512];
        if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
        for (i=0;i<4;i++) sec[446+i*16] = (i==index) ? 0x80 : 0x00;
        return write_sec(dev, 0, sec)==0 ? PART_OK : PART_ERR_GENERIC;
    }
}
int part_add_mbr(const fisix_block_dev_t *dev, uint8_t mbr_type, int bootable,
                 uint64_t first_lba, uint64_t size_sectors, int *index_out)
{
    part_table_t t;
    uint8_t sec[512];
    int r, i, slot=-1;
    uint64_t first, last;
    if (!dev_is_512(dev) || mbr_type==0 || size_sectors==0) return PART_ERR_UNSUPPORTED;
    r = part_probe(dev, &t);
    if (r!=PART_OK || t.scheme!=PART_SCHEME_MBR) return PART_ERR_NO_TABLE;
    if (first_lba==0) {
        /* First free gap, 1 MiB aligned, starting at LBA 2048. */
        uint64_t cur = 2048;
        int placed=0;
        for (;;) {
            int clash=0;
            for (i=0;i<t.count;i++) {
                if (ranges_overlap(cur, cur+size_sectors-1, t.entries[i].first_lba, t.entries[i].last_lba)) { cur = t.entries[i].last_lba+1; cur = (cur+2047)/2048*2048; clash=1; break; }
            }
            if (!clash) { placed=1; break; }
            if (cur+size_sectors-1 >= dev->sectors) break;
        }
        if (!placed) return PART_ERR_NO_SPACE;
        first = cur;
    } else {
        first = first_lba;
    }
    last = first + size_sectors - 1;
    if (last >= dev->sectors) return PART_ERR_OUT_OF_RANGE;
    for (i=0;i<t.count;i++) if (ranges_overlap(first, last, t.entries[i].first_lba, t.entries[i].last_lba)) return PART_ERR_OVERLAP;
    if (read_sec(dev, 0, sec)!=0) return PART_ERR_GENERIC;
    for (i=0;i<4;i++) if (sec[446+i*16+4]==0) { slot=i; break; }
    if (slot<0) return PART_ERR_NO_SLOT;
    {
        uint8_t *e = sec+446+slot*16;
        p_memset(e, 0, 16);
        e[0] = bootable ? 0x80 : 0x00;
        e[4] = mbr_type;
        p_wr32(e+8, (uint32_t)first);
        p_wr32(e+12, (uint32_t)size_sectors);
    }
    sec[510]=0x55; sec[511]=0xAA;
    r = write_sec(dev, 0, sec)==0 ? PART_OK : PART_ERR_GENERIC;
    if (r==PART_OK && index_out) *index_out=slot;
    return r;
}

/* ---- OS detection on a FAT volume ---- */
static int vol_has_file(const uint8_t *vol, size_t len, const char *path)
{
    uint8_t tmp[64];
    uint32_t out_len=0;
    return fat_read_file(vol, len, path, tmp, sizeof(tmp), &out_len)==0;
}
static int vol_has_dir(const uint8_t *vol, size_t len, const char *path)
{
    char list[512];
    return fat_list_dir(vol, len, path, list, sizeof(list))==0;
}
part_os_kind_t part_os_detect_on_volume(const uint8_t *vol, size_t len, char *label_out, size_t label_cap)
{
    int is32=0;
    const char *label = "Unknown";
    part_os_kind_t kind = PART_OS_UNKNOWN;
    if (!vol || !fat_probe(vol, len, &is32)) { if (label_out) p_strcpy(label_out, "Not a FAT volume", label_cap); return PART_OS_UNKNOWN; }
    if (vol_has_file(vol, len, "EFI/MICROSOFT/BOOT/BOOTMGFW.EFI") ||
        vol_has_file(vol, len, "EFI/MICROSOFT/BOOT/BOOTMGR.EFI")) { kind = PART_OS_WINDOWS; label = "Windows"; }
    else if (vol_has_file(vol, len, "EFI/UBUNTU/SHIMX64.EFI") || vol_has_file(vol, len, "EFI/UBUNTU/GRUBX64.EFI")) { kind = PART_OS_LINUX; label = "Linux (Ubuntu)"; }
    else if (vol_has_file(vol, len, "EFI/DEBIAN/GRUBX64.EFI")) { kind = PART_OS_LINUX; label = "Linux (Debian)"; }
    else if (vol_has_file(vol, len, "EFI/FEDORA/SHIMX64.EFI") || vol_has_file(vol, len, "EFI/FEDORA/GRUBX64.EFI")) { kind = PART_OS_LINUX; label = "Linux (Fedora)"; }
    else if (vol_has_file(vol, len, "EFI/BOOT/GRUBX64.EFI") || vol_has_file(vol, len, "EFI/BOOT/SHIMX64.EFI")) { kind = PART_OS_LINUX; label = "Linux (GRUB)"; }
    else if (vol_has_file(vol, len, "EFI/BOOT/BOOTX64.EFI") && vol_has_file(vol, len, "KERNEL.BIN")) { kind = PART_OS_FISIX; label = "FisixOS"; }
    else if (vol_has_file(vol, len, "EFI/BOOT/BOOTX64.EFI")) { kind = PART_OS_OTHER_UEFI; label = "Other UEFI"; }
    else if (vol_has_dir(vol, len, "EFI")) { kind = PART_OS_OTHER_UEFI; label = "Other UEFI"; }
    if (label_out) p_strcpy(label_out, label, label_cap);
    return kind;
}

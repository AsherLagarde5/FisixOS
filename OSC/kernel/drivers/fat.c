/* fat.c - FAT16/FAT32 reader (v1, kept) + FAT32 writer (new).
 *
 * Original FisixOS code, read-only FAT16/32 parser (NOT a copy
 * of FatFs). The writer below is new original code for the
 * read/write stage: cluster allocation, both FAT copies, FSInfo,
 * directory create/delete/mkdir, subdirectories, truncate.
 * No profanity anywhere, per the professional-vibe rule.
 *
 * Freestanding: no libc. Local helpers only.
 */
#include "fat.h"

static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
static void wr16(uint8_t *p, uint16_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void wr32(uint8_t *p, uint32_t v){ p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void f_memset(uint8_t *p, uint8_t v, size_t n){ size_t i; for (i=0;i<n;i++) p[i]=v; }
static void f_memcpy(uint8_t *d, const uint8_t *s, size_t n){ size_t i; for (i=0;i<n;i++) d[i]=s[i]; }

typedef struct { uint16_t bytes_per_sector; uint8_t sectors_per_cluster; uint16_t reserved; uint8_t nfats; uint16_t root_entries; uint32_t total_sectors; uint32_t fat_size; uint32_t root_cluster; int is_fat32; uint32_t fat_start; uint32_t data_start; uint32_t root_start; uint32_t cluster_count; uint32_t cluster_bytes; } bpb_t;

static int parse_bpb(const uint8_t *img, size_t len, bpb_t *b)
{
    uint32_t tot16, tot32, fat16, fat32, data_sectors;
    if (!img || len < 512 || img[510]!=0x55 || img[511]!=0xAA) return -1;
    b->bytes_per_sector = rd16(img+11); b->sectors_per_cluster = img[13]; b->reserved = rd16(img+14); b->nfats = img[16]; b->root_entries = rd16(img+17);
    tot16 = rd16(img+19); tot32 = rd32(img+32); b->total_sectors = tot16 ? tot16 : tot32;
    fat16 = rd16(img+22); fat32 = rd32(img+36); b->fat_size = fat16 ? fat16 : fat32; b->root_cluster = rd32(img+44);
    if (b->bytes_per_sector==0 || b->bytes_per_sector < 512 || (b->bytes_per_sector & (b->bytes_per_sector-1)) != 0) return -1;
    if (b->sectors_per_cluster==0 || b->nfats==0 || b->fat_size==0) return -1;
    if (b->total_sectors==0) return -1;
    b->is_fat32 = (b->root_entries==0);
    b->fat_start = (uint32_t)b->reserved * b->bytes_per_sector;
    b->root_start = b->fat_start + (uint32_t)b->nfats * b->fat_size * b->bytes_per_sector;
    if (b->is_fat32) b->data_start = b->root_start; else b->data_start = b->root_start + (uint32_t)b->root_entries * 32;
    if (b->data_start >= len) return -1;
    data_sectors = b->total_sectors - (b->reserved + (uint32_t)b->nfats * b->fat_size + (b->is_fat32 ? 0 : (uint32_t)((b->root_entries*32 + b->bytes_per_sector - 1)/b->bytes_per_sector)));
    if (b->sectors_per_cluster==0) return -1;
    b->cluster_count = data_sectors / b->sectors_per_cluster;
    b->cluster_bytes = (uint32_t)b->sectors_per_cluster * b->bytes_per_sector;
    return 0;
}

int fat_probe(const uint8_t *img, size_t len, int *is_fat32)
{
    bpb_t b;
    if (parse_bpb(img, len, &b)!=0) return 0;
    if (is_fat32) *is_fat32 = b.is_fat32;
    return 1;
}

static uint32_t cluster_offset(const bpb_t *b, uint32_t cluster)
{
    if (cluster < 2) return 0;
    return b->data_start + (cluster-2) * b->cluster_bytes;
}
static int cluster_is_eoc(const bpb_t *b, uint32_t c)
{
    return b->is_fat32 ? (c >= 0x0FFFFFF8u) : (c >= 0xFFF8u);
}
static uint32_t fat_entry_get(const uint8_t *img, const bpb_t *b, uint32_t cluster)
{
    uint32_t off;
    if (b->is_fat32) { off = b->fat_start + cluster*4; return rd32(img+off) & 0x0FFFFFFFu; }
    off = b->fat_start + cluster*2; return rd16(img+off);
}
/* Write one FAT entry into every FAT copy. img must be mutable. */
static void fat_entry_set(uint8_t *img, const bpb_t *b, uint32_t cluster, uint32_t val)
{
    uint8_t f;
    for (f=0; f<b->nfats; f++) {
        uint32_t base = b->fat_start + (uint32_t)f * b->fat_size * b->bytes_per_sector;
        if (b->is_fat32) { uint32_t off = base + cluster*4; uint32_t old = rd32(img+off) & 0xF0000000u; wr32(img+off, old | (val & 0x0FFFFFFFu)); }
        else { uint32_t off = base + cluster*2; wr16(img+off, (uint16_t)val); }
    }
}
static uint32_t fat_eoc(const bpb_t *b){ return b->is_fat32 ? 0x0FFFFFFFu : 0xFFFFu; }

/* ---- directory helpers ---- */
static int name_match(const uint8_t *entry, const char *want)
{
    char buf[16]; int i, j=0;
    for (i=0;i<8;i++) { if (entry[i]==' ') break; buf[j++]=entry[i]; }
    if (entry[8]!=' ') { buf[j++]='.'; for (i=8;i<11;i++) { if (entry[i]==' ') break; buf[j++]=entry[i]; } }
    buf[j]=0;
    { int k=0; while (buf[k] && want[k]) { char a=buf[k], c=want[k]; if (a>='a'&&a<='z') a-=32; if (c>='a'&&c<='z') c-=32; if (a!=c) return 0; k++; } return buf[k]==want[k]; }
}
/* Convert "BASE.EXT" / "BASE" to an 11-byte 8.3 name (space-padded,
 * upper case). Returns 0 or -1 on an empty/illegal name. A name
 * longer than 8.3 is truncated, not rejected. */
static int to_83(const char *name, uint8_t out11[11])
{
    int i, bi=0, ei=0, seen_dot=0;
    char base[9], ext[4];
    if (!name || !name[0]) return -1;
    for (i=0;i<11;i++) out11[i]=' ';
    for (i=0; name[i] && i<64; i++) {
        char c = name[i];
        if (c=='.') { if (seen_dot) return -1; seen_dot=1; continue; }
        if (c==' ' || c=='/' || c=='\\' || c==':' || c=='*' || c=='?' || c=='"' || c=='<' || c=='>' || c=='|') return -1;
        if (c>='a'&&c<='z') c=(char)(c-'a'+'A');
        if (!seen_dot) { if (bi<8) base[bi++]=c; }
        else { if (ei<3) ext[ei++]=c; }
    }
    if (bi==0) return -1;
    base[bi]=0; ext[ei]=0;
    for (i=0;i<bi;i++) out11[i]=(uint8_t)base[i];
    for (i=0;i<ei;i++) out11[8+i]=(uint8_t)ext[i];
    return 0;
}
/* Split a path into components. Returns component count, or -1.
 * Components are copied into comp[][16] (NUL-terminated, no
 * separator). Leading/trailing separators are ignored. */
static int split_path(const char *path, char comp[16][16])
{
    int n=0, i=0;
    if (!path) return -1;
    while (path[i]=='/' || path[i]=='\\') i++;
    while (path[i]) {
        int j=0;
        if (n>=16) return -1;
        while (path[i] && path[i]!='/' && path[i]!='\\') { if (j<15) comp[n][j++]=path[i]; i++; }
        comp[n][j]=0;
        if (j==0) return -1;
        n++;
        while (path[i]=='/' || path[i]=='\\') i++;
    }
    return n;
}
/* One directory entry location, for read and write. */
typedef struct { uint32_t offset; /* byte offset of the 32-byte entry in img */ uint32_t first_cluster; uint32_t size; uint8_t attr; } dirent_t;

/* Scan one directory for a name. dir_cluster: 0 = FAT16 fixed
 * root, 1 = FAT32 root (root_cluster), else a subdirectory
 * cluster. On success fills *found and returns 0; -2 = not
 * found; -1 = bad image. */
static int dir_find(const uint8_t *img, size_t len, const bpb_t *b, uint32_t dir_cluster, const char *want, dirent_t *found)
{
    uint8_t name11[11];
    if (to_83(want, name11)!=0) return FAT_ERR_BAD_NAME;
    if (!b->is_fat32 && dir_cluster==0) {
        size_t root_len = (size_t)b->root_entries*32, off;
        if (b->root_start+root_len>len) return -1;
        for (off=0; off+32<=root_len; off+=32) {
            const uint8_t *e = img+b->root_start+off;
            if (e[0]==0x00) break;
            if (e[0]==0xE5 || e[11]==0x0F) continue;
            { int k, ok=1; for (k=0;k<11;k++) if (e[k]!=name11[k]) { ok=0; break; } if (!ok) continue; }
            found->offset = (uint32_t)(b->root_start+off);
            found->first_cluster = rd16(e+26);
            found->size = rd32(e+28); found->attr = e[11];
            return 0;
        }
        return FAT_ERR_NOT_FOUND;
    } else {
        uint32_t c = (dir_cluster==1) ? b->root_cluster : dir_cluster;
        int guard=0;
        while (c>=2 && !cluster_is_eoc(b,c) && guard++<10000) {
            uint32_t off = cluster_offset(b,c), end = off + b->cluster_bytes, p;
            if (end>len) return -1;
            for (p=off; p+32<=end; p+=32) {
                const uint8_t *e = img+p;
                if (e[0]==0x00) return FAT_ERR_NOT_FOUND;
                if (e[0]==0xE5 || e[11]==0x0F) continue;
                { int k, ok=1; for (k=0;k<11;k++) if (e[k]!=name11[k]) { ok=0; break; } if (!ok) continue; }
                found->offset = p;
                found->first_cluster = rd16(e+26);
                if (b->is_fat32) found->first_cluster |= ((uint32_t)rd16(e+20)<<16);
                found->size = rd32(e+28); found->attr = e[11];
                return 0;
            }
            c = fat_entry_get(img,b,c);
        }
        return FAT_ERR_NOT_FOUND;
    }
}
/* Resolve a path to its parent directory cluster + final name.
 * parent_cluster: 0 = FAT16 root, 1 = FAT32 root, else subdir.
 * The final component is copied to final_name (no separator).
 * Returns 0, FAT_ERR_NOT_FOUND, FAT_ERR_NOT_DIR, FAT_ERR_BAD_NAME. */
static int resolve_parent(const uint8_t *img, size_t len, const bpb_t *b, const char *path, uint32_t *parent_cluster, char *final_name)
{
    char comp[16][16];
    int n, i;
    uint32_t cur;
    n = split_path(path, comp);
    if (n<=0) return FAT_ERR_BAD_NAME;
    cur = b->is_fat32 ? 1 : 0;
    for (i=0;i<n-1;i++) {
        dirent_t d;
        int r = dir_find(img,len,b,cur,comp[i],&d);
        if (r!=0) return r;
        if (!(d.attr & 0x10)) return FAT_ERR_NOT_DIR;
        cur = d.first_cluster;
        if (cur<2) return FAT_ERR_NOT_DIR;
    }
    { int j=0; while (comp[n-1][j] && j<15) { final_name[j]=comp[n-1][j]; j++; } final_name[j]=0; }
    *parent_cluster = cur;
    return 0;
}
/* Walk every entry of one directory, calling fn(offset, entry).
 * Stops when fn returns non-zero or the directory ends. */
typedef int (*dir_walk_fn)(uint8_t *img, uint32_t off, const uint8_t *e, void *ctx);
static int dir_walk(uint8_t *img, size_t len, const bpb_t *b, uint32_t dir_cluster, dir_walk_fn fn, void *ctx)
{
    if (!b->is_fat32 && dir_cluster==0) {
        size_t root_len = (size_t)b->root_entries*32, off;
        if (b->root_start+root_len>len) return -1;
        for (off=0; off+32<=root_len; off+=32) {
            const uint8_t *e = img+b->root_start+off;
            int r;
            if (e[0]==0x00) return 0;
            r = fn(img, (uint32_t)(b->root_start+off), e, ctx);
            if (r!=0) return r;
        }
        return 0;
    } else {
        uint32_t c = (dir_cluster==1) ? b->root_cluster : dir_cluster;
        int guard=0;
        while (c>=2 && !cluster_is_eoc(b,c) && guard++<10000) {
            uint32_t off = cluster_offset(b,c), end = off + b->cluster_bytes, p;
            int r;
            if (end>len) return -1;
            for (p=off; p+32<=end; p+=32) {
                const uint8_t *e = img+p;
                if (e[0]==0x00) return 0;
                r = fn(img, p, e, ctx);
                if (r!=0) return r;
            }
            c = fat_entry_get(img,b,c);
        }
        return 0;
    }
}

/* ---- allocation ---- */
static int alloc_one(uint8_t *img, const bpb_t *b, uint32_t *out)
{
    uint32_t c;
    for (c=2; c<b->cluster_count+2; c++) {
        if (fat_entry_get(img,b,c)==0) { fat_entry_set(img,b,c,fat_eoc(b)); *out=c; return 0; }
    }
    return FAT_ERR_NO_SPACE;
}
static void free_chain(uint8_t *img, const bpb_t *b, uint32_t first)
{
    uint32_t c = first;
    int guard=0;
    while (c>=2 && !cluster_is_eoc(b,c) && guard++<10000) {
        uint32_t nxt = fat_entry_get(img,b,c);
        fat_entry_set(img,b,c,0);
        c = nxt;
    }
}
static uint32_t count_free(const uint8_t *img, const bpb_t *b)
{
    uint32_t c, n=0;
    for (c=2; c<b->cluster_count+2; c++) if (fat_entry_get(img,b,c)==0) n++;
    return n;
}
/* Update the FSInfo free-cluster count + next-free hint (FAT32).
 * Best effort: silently skipped if the FSInfo signatures are absent. */
static void fsinfo_update(uint8_t *img, const bpb_t *b)
{
    uint32_t off, free_n, c;
    if (!b->is_fat32) return;
    /* FSInfo lives in sector 1 of the volume (byte offset
     * bytes_per_sector), inside the reserved area - NOT at
     * reserved+1. */
    off = (uint32_t)b->bytes_per_sector;
    if (off + 512 > (uint64_t)b->total_sectors * b->bytes_per_sector) return;
    if (rd32(img+off)!=0x41615252u || rd32(img+off+484)!=0x61417272u) return;
    free_n = count_free(img,b);
    wr32(img+off+488, free_n);
    for (c=2; c<b->cluster_count+2; c++) if (fat_entry_get(img,b,c)==0) { wr32(img+off+492, c); return; }
    wr32(img+off+492, 0xFFFFFFFFu);
}
static int write_chain_data(uint8_t *img, size_t len, const bpb_t *b, uint32_t first, const uint8_t *data, uint32_t data_len)
{
    uint32_t c = first, done=0;
    int guard=0;
    if (data_len==0) return 0;
    while (done<data_len && guard++<10000) {
        uint32_t off = cluster_offset(b,c), chunk = data_len-done;
        if (off+b->cluster_bytes>len) return -1;
        if (chunk>b->cluster_bytes) chunk=b->cluster_bytes;
        f_memcpy(img+off, data+done, chunk);
        if (chunk<b->cluster_bytes) f_memset(img+off+chunk, 0, b->cluster_bytes-chunk);
        done+=chunk;
        if (done>=data_len) break;
        c = fat_entry_get(img,b,c);
        if (c<2 || cluster_is_eoc(b,c)) return -1;
    }
    return done==data_len ? 0 : -1;
}
/* Allocate a chain of exactly n clusters (n may be 0 -> first=0).
 * Links them in order. On failure frees any partial chain. */
static int alloc_chain(uint8_t *img, const bpb_t *b, uint32_t n, uint32_t *first_out)
{
    uint32_t first=0, prev=0, i;
    *first_out=0;
    for (i=0;i<n;i++) {
        uint32_t c;
        if (alloc_one(img,b,&c)!=0) { if (first) free_chain(img,b,first); return FAT_ERR_NO_SPACE; }
        if (prev) fat_entry_set(img,b,prev,c);
        else first=c;
        prev=c;
    }
    *first_out=first;
    return 0;
}

/* ---- v1 reader, kept (now path-aware) ---- */
static int scan_dir(const uint8_t *img, size_t len, const bpb_t *b, const uint8_t *dir, size_t dir_len, const char *want, uint32_t *cluster, uint32_t *size, char *list_out, size_t list_cap, size_t *list_len)
{
    size_t off;
    for (off=0; off+32<=dir_len; off+=32) {
        const uint8_t *e = dir+off;
        if (e[0]==0x00) break;
        if (e[0]==0xE5) continue;
        if (e[11]==0x0F) continue;
        if (list_out) { char buf[16]; int i, j=0; for (i=0;i<8;i++) { if (e[i]==' ') break; buf[j++]=e[i]; } if (e[8]!=' ') { buf[j++]='.'; for (i=8;i<11;i++) { if (e[i]==' ') break; buf[j++]=e[i]; } } buf[j]=0; { int k=0; while (buf[k] && *list_len+1<list_cap) list_out[(*list_len)++]=buf[k++]; if (*list_len+1<list_cap) list_out[(*list_len)++]='\n'; list_out[*list_len]=0; } }
        if (want && name_match(e, want)) { *cluster = rd16(e+26); if (b->is_fat32) *cluster |= ((uint32_t)rd16(e+20)<<16); *size = rd32(e+28); return 0; }
    }
    (void)img; (void)len; return -2;
}
int fat_list_root(const uint8_t *img, size_t len, char *out, size_t out_cap)
{
    return fat_list_dir(img, len, "", out, out_cap);
}
int fat_list_dir(const uint8_t *img, size_t len, const char *path, char *out, size_t out_cap)
{
    bpb_t b; size_t list_len=0;
    char comp[16][16];
    int n, i;
    uint32_t cur;
    if (parse_bpb(img,len,&b)!=0 || !out || out_cap==0) return -1;
    out[0]=0;
    if (!path || !path[0] || (path[0]=='/' && !path[1]) || (path[0]=='\\' && !path[1])) {
        if (b.is_fat32) {
            uint32_t c = b.root_cluster; int guard=0;
            while (c>=2 && !cluster_is_eoc(&b,c) && guard++<1000) { uint32_t off=cluster_offset(&b,c); size_t clen=b.cluster_bytes; if (off+clen>len) return -1; scan_dir(img,len,&b,img+off,clen,0,0,0,out,out_cap,&list_len); c=fat_entry_get(img,&b,c); }
            return 0;
        } else {
            size_t root_len = (size_t)b.root_entries*32; if (b.root_start+root_len>len) return -1; scan_dir(img,len,&b,img+b.root_start,root_len,0,0,0,out,out_cap,&list_len); return 0;
        }
    }
    n = split_path(path, comp);
    if (n<=0) return -1;
    cur = b.is_fat32 ? 1 : 0;
    for (i=0;i<n;i++) {
        dirent_t d;
        int r = dir_find(img,len,&b,cur,comp[i],&d);
        if (r!=0) return -2;
        if (!(d.attr & 0x10)) return -1;
        cur = d.first_cluster;
    }
    {
        uint32_t c = cur; int guard=0;
        while (c>=2 && !cluster_is_eoc(&b,c) && guard++<1000) { uint32_t off=cluster_offset(&b,c); size_t clen=b.cluster_bytes; if (off+clen>len) return -1; scan_dir(img,len,&b,img+off,clen,0,0,0,out,out_cap,&list_len); c=fat_entry_get(img,&b,c); }
    }
    return 0;
}
int fat_read_file(const uint8_t *img, size_t len, const char *name83, uint8_t *out, size_t out_cap, uint32_t *out_len)
{
    bpb_t b; uint32_t cluster=0, size=0; int r; uint32_t copied=0; int guard=0;
    uint32_t parent; char final_name[16];
    if (parse_bpb(img,len,&b)!=0 || !out) return -1;
    r = resolve_parent(img,len,&b,name83,&parent,final_name);
    if (r!=0) return r==FAT_ERR_NOT_FOUND ? -2 : r;
    {
        dirent_t d;
        r = dir_find(img,len,&b,parent,final_name,&d);
        if (r!=0) return -2;
        if (d.attr & 0x10) return -1;
        cluster = d.first_cluster; size = d.size;
    }
    if (size==0) { if (out_len) *out_len=0; return 0; }
    if (cluster<2) return -1;
    while (cluster>=2 && !cluster_is_eoc(&b,cluster) && guard++<10000) {
        uint32_t off=cluster_offset(&b,cluster); size_t clen=b.cluster_bytes; size_t want = size-copied; if (want>clen) want=clen; if (off+want>len) return -1; if (copied+want>out_cap) return -1;
        { size_t i; for (i=0;i<want;i++) out[copied+i]=img[off+i]; } copied+=(uint32_t)want; if (copied>=size) break; cluster=fat_entry_get(img,&b,cluster);
    }
    if (out_len) *out_len=copied;
    return copied==size ? 0 : -1;
}

/* ---- writer ---- */
int fat_free_bytes(const uint8_t *img, size_t len, uint64_t *free_bytes, uint64_t *total_bytes)
{
    bpb_t b;
    if (parse_bpb(img,len,&b)!=0) return -1;
    if (free_bytes) *free_bytes = (uint64_t)count_free(img,&b) * b.cluster_bytes;
    if (total_bytes) *total_bytes = (uint64_t)b.cluster_count * b.cluster_bytes;
    return 0;
}

/* Find a free directory slot in dir_cluster; if the directory is
 * a cluster chain and every slot is used, allocate a new cluster
 * and return a slot in it. Sets *slot_off. FAT16 fixed root can
 * only return FAT_ERR_DIR_FULL when full. */
static int dir_alloc_slot(uint8_t *img, size_t len, const bpb_t *b, uint32_t dir_cluster, uint32_t *slot_off)
{
    if (!b->is_fat32 && dir_cluster==0) {
        size_t root_len = (size_t)b->root_entries*32, off;
        if (b->root_start+root_len>len) return -1;
        for (off=0; off+32<=root_len; off+=32) {
            const uint8_t *e = img+b->root_start+off;
            if (e[0]==0x00 || e[0]==0xE5) { *slot_off=(uint32_t)(b->root_start+off); return 0; }
        }
        return FAT_ERR_DIR_FULL;
    } else {
        uint32_t c = (dir_cluster==1) ? b->root_cluster : dir_cluster, prev=0;
        int guard=0;
        while (c>=2 && !cluster_is_eoc(b,c) && guard++<10000) {
            uint32_t off = cluster_offset(b,c), end = off + b->cluster_bytes, p;
            if (end>len) return -1;
            for (p=off; p+32<=end; p+=32) {
                const uint8_t *e = img+p;
                if (e[0]==0x00 || e[0]==0xE5) { *slot_off=p; return 0; }
            }
            prev=c; c=fat_entry_get(img,b,c);
        }
        /* Directory full: grow it by one cluster (root included). */
        {
            uint32_t nc;
            if (alloc_one(img,b,&nc)!=0) return FAT_ERR_NO_SPACE;
            if (prev) fat_entry_set(img,b,prev,nc);
            else return -1; /* no cluster to extend (should not happen) */
            { uint32_t off = cluster_offset(b,nc); if (off+b->cluster_bytes>len) return -1; f_memset(img+off, 0, b->cluster_bytes); }
            *slot_off = cluster_offset(b,nc);
            return 0;
        }
    }
}

int fat_write_file(uint8_t *img, size_t len, const char *path, const uint8_t *data, uint32_t data_len)
{
    bpb_t b;
    uint32_t parent, slot=0, first=0, need;
    char final_name[16];
    dirent_t old;
    int r, existed;
    uint8_t name11[11];
    if (!img || !path) return FAT_ERR_GENERIC;
    if (parse_bpb(img,len,&b)!=0) return FAT_ERR_GENERIC;
    if (!b.is_fat32) return FAT_ERR_NOT_FAT32;
    if (data_len>0 && !data) return FAT_ERR_GENERIC;
    r = resolve_parent(img,len,&b,path,&parent,final_name);
    if (r!=0) return r;
    if (to_83(final_name, name11)!=0) return FAT_ERR_BAD_NAME;
    need = (data_len + b.cluster_bytes - 1) / b.cluster_bytes;
    if (need > count_free(img,&b)) return FAT_ERR_NO_SPACE;
    r = dir_find(img,len,&b,parent,final_name,&old);
    existed = (r==0);
    if (existed && (old.attr & 0x10)) return FAT_ERR_NOT_DIR;
    /* Allocate the new chain first, so a failure leaves the old
     * file untouched. Then write data, then swap the entry. */
    if (alloc_chain(img,&b,need,&first)!=0) return FAT_ERR_NO_SPACE;
    if (write_chain_data(img,len,&b,first,data,data_len)!=0) { if (first) free_chain(img,&b,first); return FAT_ERR_GENERIC; }
    if (existed) {
        if (old.first_cluster>=2) free_chain(img,&b,old.first_cluster);
        /* Rewrite the existing entry in place. */
        { uint8_t *e = img+old.offset; int k; for (k=0;k<11;k++) e[k]=name11[k]; e[11]=0x20; wr16(e+20, (uint16_t)(first>>16)); wr16(e+26, (uint16_t)(first & 0xFFFFu)); wr32(e+28, data_len); }
    } else {
        r = dir_alloc_slot(img,len,&b,parent,&slot);
        if (r!=0) { if (first) free_chain(img,&b,first); return r; }
        { uint8_t *e = img+slot; int k; f_memset(e, 0, 32); for (k=0;k<11;k++) e[k]=name11[k]; e[11]=0x20; wr16(e+20, (uint16_t)(first>>16)); wr16(e+26, (uint16_t)(first & 0xFFFFu)); wr32(e+28, data_len); }
    }
    fsinfo_update(img,&b);
    return FAT_OK;
}

/* Context for the delete walk: find the entry, remember the
 * previous entry offset so LFN entries before it can be cleared. */
typedef struct { uint32_t target_off; uint32_t prev_off; int found_prev_lfn; } del_ctx_t;
static int del_walk_fn(uint8_t *img, uint32_t off, const uint8_t *e, void *ctx)
{
    del_ctx_t *c = (del_ctx_t *)ctx;
    (void)img;
    if (off == c->target_off) return 1; /* stop: found */
    if (e[11]==0x0F && e[0]!=0xE5) { c->prev_off = off; c->found_prev_lfn = 1; }
    else { c->found_prev_lfn = 0; }
    return 0;
}
int fat_delete_file(uint8_t *img, size_t len, const char *path)
{
    bpb_t b;
    uint32_t parent;
    char final_name[16];
    dirent_t d;
    int r;
    if (!img || !path) return FAT_ERR_GENERIC;
    if (parse_bpb(img,len,&b)!=0) return FAT_ERR_GENERIC;
    if (!b.is_fat32) return FAT_ERR_NOT_FAT32;
    r = resolve_parent(img,len,&b,path,&parent,final_name);
    if (r!=0) return r;
    r = dir_find(img,len,&b,parent,final_name,&d);
    if (r!=0) return FAT_ERR_NOT_FOUND;
    if (d.attr & 0x10) return FAT_ERR_NOT_DIR;
    /* Mark any LFN entries immediately before this entry. */
    {
        del_ctx_t ctx; ctx.target_off=d.offset; ctx.prev_off=0; ctx.found_prev_lfn=0;
        (void)dir_walk(img,len,&b,parent,del_walk_fn,&ctx);
        if (ctx.found_prev_lfn) {
            /* Walk backwards over contiguous LFN entries, staying
             * inside the same directory cluster as the entry. */
            uint32_t p = ctx.prev_off;
            uint32_t cl_start = b.data_start + ((d.offset - b.data_start) / b.cluster_bytes) * b.cluster_bytes;
            for (;;) {
                uint8_t *e = img+p;
                if (e[11]!=0x0F || e[0]==0xE5) break;
                e[0]=0xE5;
                if (p < cl_start+32) break;
                p-=32;
            }
        }
    }
    if (d.first_cluster>=2) free_chain(img,&b,d.first_cluster);
    img[d.offset]=0xE5;
    fsinfo_update(img,&b);
    return FAT_OK;
}

int fat_mkdir(uint8_t *img, size_t len, const char *path)
{
    bpb_t b;
    uint32_t parent, slot=0, first=0;
    char final_name[16];
    dirent_t d;
    int r;
    uint8_t name11[11];
    if (!img || !path) return FAT_ERR_GENERIC;
    if (parse_bpb(img,len,&b)!=0) return FAT_ERR_GENERIC;
    if (!b.is_fat32) return FAT_ERR_NOT_FAT32;
    r = resolve_parent(img,len,&b,path,&parent,final_name);
    if (r!=0) return r;
    if (to_83(final_name, name11)!=0) return FAT_ERR_BAD_NAME;
    r = dir_find(img,len,&b,parent,final_name,&d);
    if (r==0) return (d.attr & 0x10) ? FAT_OK : FAT_ERR_NOT_DIR;
    if (alloc_one(img,&b,&first)!=0) return FAT_ERR_NO_SPACE;
    r = dir_alloc_slot(img,len,&b,parent,&slot);
    if (r!=0) { free_chain(img,&b,first); return r; }
    /* New directory cluster: "." and ".." entries, rest zero. */
    {
        uint32_t off = cluster_offset(&b,first), parent_cl = (parent==1) ? b.root_cluster : parent;
        uint8_t *e;
        if (off+b.cluster_bytes>len) { free_chain(img,&b,first); return FAT_ERR_GENERIC; }
        f_memset(img+off, 0, b.cluster_bytes);
        e = img+off;
        e[0]='.'; { int k; for (k=1;k<11;k++) e[k]=' '; } e[11]=0x10;
        wr16(e+20, (uint16_t)(first>>16)); wr16(e+26, (uint16_t)(first & 0xFFFFu));
        e += 32;
        e[0]='.'; e[1]='.'; { int k; for (k=2;k<11;k++) e[k]=' '; } e[11]=0x10;
        wr16(e+20, (uint16_t)(parent_cl>>16)); wr16(e+26, (uint16_t)(parent_cl & 0xFFFFu));
    }
    { uint8_t *e = img+slot; int k; f_memset(e, 0, 32); for (k=0;k<11;k++) e[k]=name11[k]; e[11]=0x10; wr16(e+20, (uint16_t)(first>>16)); wr16(e+26, (uint16_t)(first & 0xFFFFu)); wr32(e+28, 0); }
    fsinfo_update(img,&b);
    return FAT_OK;
}

int fat_exists(const uint8_t *img, size_t len, const char *path)
{
    bpb_t b; uint32_t parent; char final_name[16]; dirent_t d;
    if (parse_bpb(img,len,&b)!=0) return 0;
    if (!path || !path[0] || (path[0]=='/' && !path[1])) return 1;
    if (resolve_parent(img,len,&b,path,&parent,final_name)!=0) return 0;
    return dir_find(img,len,&b,parent,final_name,&d)==0 ? 1 : 0;
}
int32_t fat_file_size(const uint8_t *img, size_t len, const char *path)
{
    bpb_t b; uint32_t parent; char final_name[16]; dirent_t d;
    if (parse_bpb(img,len,&b)!=0) return -1;
    if (resolve_parent(img,len,&b,path,&parent,final_name)!=0) return -1;
    if (dir_find(img,len,&b,parent,final_name,&d)!=0) return -1;
    if (d.attr & 0x10) return -1;
    return (int32_t)d.size;
}

/* ---- FAT32 format ---- */
int fat_format_fat32(uint8_t *img, size_t len, uint32_t spc, const char *label)
{
    uint32_t total_sectors, fat_sectors, data_sectors, clusters, i;
    uint32_t fat_start_sectors, data_start_sector;
    if (!img || len < 512 || (len % 512)!=0) return FAT_ERR_GENERIC;
    if (spc==0 || (spc & (spc-1))!=0 || spc>128) return FAT_ERR_GENERIC;
    total_sectors = (uint32_t)(len/512);
    fat_sectors = 0;
    for (i=0;i<4;i++) {
        data_sectors = total_sectors - 32 - 2*fat_sectors;
        clusters = data_sectors / spc;
        fat_sectors = (clusters + 2) * 4 / 512 + 1;
    }
    data_sectors = total_sectors - 32 - 2*fat_sectors;
    clusters = data_sectors / spc;
    if (clusters < 65525) return FAT_ERR_TOO_BIG; /* too small to be FAT32 */
    fat_start_sectors = 32;
    data_start_sector = fat_start_sectors + 2*fat_sectors;
    f_memset(img, 0, len);
    /* Boot sector. */
    img[0]=0xEB; img[1]=0x58; img[2]=0x90;
    { const char *oem="FISIXOS "; for (i=0;i<8;i++) img[3+i]=(uint8_t)oem[i]; }
    wr16(img+11, 512); img[13]=(uint8_t)spc; wr16(img+14, 32); img[16]=2;
    wr16(img+17, 0); img[21]=0xF8; wr16(img+22, 0);
    wr16(img+24, 63); wr16(img+26, 255);
    wr32(img+28, 0); wr32(img+32, total_sectors);
    wr32(img+36, fat_sectors); wr16(img+40, 0); wr16(img+42, 0);
    wr32(img+44, 2); wr16(img+48, 1); wr16(img+50, 6);
    img[64]=0x80; img[66]=0x29; wr32(img+67, 0x46495349u);
    { const char *lbl = label ? label : "NO NAME    "; int k; for (k=0;k<11;k++) img[71+k]=(uint8_t)(lbl[k] ? lbl[k] : ' '); }
    { const char *typ="FAT32   "; int k; for (k=0;k<8;k++) img[82+k]=(uint8_t)typ[k]; }
    img[510]=0x55; img[511]=0xAA;
    /* FSInfo, sector 1. */
    wr32(img+512, 0x41615252u); wr32(img+512+484, 0x61417272u);
    wr32(img+512+488, clusters - 1); /* cluster 2 (root) used */
    wr32(img+512+492, 3);
    wr32(img+512+508, 0xAA550000u);
    /* Backup boot sector, sector 6. */
    f_memcpy(img+6*512, img, 512);
    /* FATs: entries 0,1 reserved, 2 = root dir EOC. */
    for (i=0;i<2;i++) {
        uint8_t *fat = img + (fat_start_sectors + i*fat_sectors)*512;
        wr32(fat+0, 0x0FFFFFF8u); wr32(fat+4, 0x0FFFFFFFu); wr32(fat+8, 0x0FFFFFFFu);
    }
    (void)data_start_sector;
    return FAT_OK;
}

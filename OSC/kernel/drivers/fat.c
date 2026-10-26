#include "fat.h"
static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1]<<8)); }
static uint32_t rd32(const uint8_t *p){ return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24); }
typedef struct { uint16_t bytes_per_sector; uint8_t sectors_per_cluster; uint16_t reserved; uint8_t nfats; uint16_t root_entries; uint32_t total_sectors; uint32_t fat_size; uint32_t root_cluster; int is_fat32; uint32_t fat_start; uint32_t data_start; uint32_t root_start; } bpb_t;
static int parse_bpb(const uint8_t *img, size_t len, bpb_t *b)
{
    uint16_t sig; uint32_t tot16, tot32, fat16, fat32;
    if (!img || len < 512 || img[510]!=0x55 || img[511]!=0xAA) return -1;
    b->bytes_per_sector = rd16(img+11); b->sectors_per_cluster = img[13]; b->reserved = rd16(img+14); b->nfats = img[16]; b->root_entries = rd16(img+17);
    tot16 = rd16(img+19); tot32 = rd32(img+32); b->total_sectors = tot16 ? tot16 : tot32;
    fat16 = rd16(img+22); fat32 = rd32(img+36); b->fat_size = fat16 ? fat16 : fat32; b->root_cluster = rd32(img+44);
    if (b->bytes_per_sector==0 || b->sectors_per_cluster==0 || b->nfats==0 || b->fat_size==0) return -1;
    b->is_fat32 = (b->root_entries==0);
    b->fat_start = (uint32_t)b->reserved * b->bytes_per_sector;
    b->root_start = b->fat_start + (uint32_t)b->nfats * b->fat_size * b->bytes_per_sector;
    if (b->is_fat32) b->data_start = b->root_start; else b->data_start = b->root_start + (uint32_t)b->root_entries * 32;
    (void)sig; return 0;
}
static uint32_t cluster_offset(const bpb_t *b, uint32_t cluster)
{
    if (cluster < 2) return 0;
    return b->data_start + (cluster-2) * (uint32_t)b->sectors_per_cluster * b->bytes_per_sector;
}
static uint32_t next_cluster(const uint8_t *img, const bpb_t *b, uint32_t cluster)
{
    uint32_t off, v;
    if (b->is_fat32) { off = b->fat_start + cluster*4; if (off+4 > (uint32_t)0) { v = rd32(img+off) & 0x0FFFFFFF; return v; } return 0x0FFFFFFF; }
    off = b->fat_start + cluster*2; v = rd16(img+off); return v;
}
static int name_match(const uint8_t *entry, const char *want)
{
    char buf[16]; int i, j=0;
    for (i=0;i<8;i++) { if (entry[i]==' ') break; buf[j++]=entry[i]; }
    if (entry[8]!=' ') { buf[j++]='.'; for (i=8;i<11;i++) { if (entry[i]==' ') break; buf[j++]=entry[i]; } }
    buf[j]=0;
    { int k=0; while (buf[k] && want[k]) { char a=buf[k], c=want[k]; if (a>='a'&&a<='z') a-=32; if (c>='a'&&c<='z') c-=32; if (a!=c) return 0; k++; } return buf[k]==want[k]; }
}
static int scan_dir(const uint8_t *img, size_t len, const bpb_t *b, const uint8_t *dir, size_t dir_len, const char *want, uint32_t *cluster, uint32_t *size, char *list_out, size_t list_cap, size_t *list_len)
{
    size_t off;
    for (off=0; off+32<=dir_len; off+=32) {
        const uint8_t *e = dir+off;
        if (e[0]==0x00) break; if (e[0]==0xE5) continue; if (e[11]==0x0F) continue; /* LFN, skip in v1 */
        if (list_out) { char buf[16]; int i, j=0; for (i=0;i<8;i++) { if (e[i]==' ') break; buf[j++]=e[i]; } if (e[8]!=' ') { buf[j++]='.'; for (i=8;i<11;i++) { if (e[i]==' ') break; buf[j++]=e[i]; } } buf[j]=0; { int k=0; while (buf[k] && *list_len+1<list_cap) list_out[(*list_len)++]=buf[k++]; if (*list_len+1<list_cap) list_out[(*list_len)++]='\n'; list_out[*list_len]=0; } }
        if (want && name_match(e, want)) { *cluster = rd16(e+26); if (b->is_fat32) *cluster |= ((uint32_t)rd16(e+20)<<16); *size = rd32(e+28); return 0; }
    }
    (void)img; (void)len; return -2;
}
int fat_list_root(const uint8_t *img, size_t len, char *out, size_t out_cap)
{
    bpb_t b; size_t list_len=0; int r;
    if (parse_bpb(img,len,&b)!=0 || !out || out_cap==0) return -1; out[0]=0;
    if (b.is_fat32) {
        uint32_t c = b.root_cluster; int guard=0;
        while (c>=2 && c<0x0FFFFFF8 && guard++<1000) { uint32_t off=cluster_offset(&b,c); size_t clen=(size_t)b.sectors_per_cluster*b.bytes_per_sector; if (off+clen>len) return -1; r=scan_dir(img,len,&b,img+off,clen,0,0,0,out,out_cap,&list_len); if (r==0) return 0; c=next_cluster(img,&b,c); }
        return 0;
    } else {
        size_t root_len = (size_t)b.root_entries*32; if (b.root_start+root_len>len) return -1; r=scan_dir(img,len,&b,img+b.root_start,root_len,0,0,0,out,out_cap,&list_len); return r==0?0:0;
    }
}
int fat_read_file(const uint8_t *img, size_t len, const char *name83, uint8_t *out, size_t out_cap, uint32_t *out_len)
{
    bpb_t b; uint32_t cluster=0, size=0; int r; uint32_t copied=0; int guard=0;
    if (parse_bpb(img,len,&b)!=0 || !out) return -1;
    if (b.is_fat32) {
        uint32_t c = b.root_cluster;
        while (c>=2 && c<0x0FFFFFF8 && guard++<1000) { uint32_t off=cluster_offset(&b,c); size_t clen=(size_t)b.sectors_per_cluster*b.bytes_per_sector; if (off+clen>len) return -1; r=scan_dir(img,len,&b,img+off,clen,name83,&cluster,&size,0,0,0); if (r==0) break; c=next_cluster(img,&b,c); }
        if (r!=0) return -2;
    } else {
        size_t root_len = (size_t)b.root_entries*32; if (b.root_start+root_len>len) return -1; r=scan_dir(img,len,&b,img+b.root_start,root_len,name83,&cluster,&size,0,0,0); if (r!=0) return -2;
    }
    guard=0; while (cluster>=2 && cluster<0xFFFFFF8 && guard++<10000) {
        uint32_t off=cluster_offset(&b,cluster); size_t clen=(size_t)b.sectors_per_cluster*b.bytes_per_sector; size_t want = size-copied; if (want>clen) want=clen; if (off+want>len) return -1; if (copied+want>out_cap) return -1;
        { size_t i; for (i=0;i<want;i++) out[copied+i]=img[off+i]; } copied+=(uint32_t)want; if (copied>=size) break; cluster=next_cluster(img,&b,cluster); if (b.is_fat32 ? cluster>=0x0FFFFFF8 : cluster>=0xFFF8) break;
    }
    if (out_len) *out_len=copied; return copied==size ? 0 : -1;
}

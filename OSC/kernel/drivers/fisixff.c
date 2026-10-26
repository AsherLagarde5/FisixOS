#include "fisixff.h"
static int ff_strcmp(const char *a, const char *b){ while (*a && *a==*b) { a++; b++; } return (int)(unsigned char)*a - (int)(unsigned char)*b; }
static int ff_parse(const uint8_t *img, size_t len, const char *want, const uint8_t **data, uint32_t *size, char *list_out, size_t list_cap)
{
    uint32_t count, i, pos; size_t list_len=0;
    if (!img || len<12 || img[0]!='F' || img[1]!='F' || img[2]!='S' || img[3]!='1') return -1;
    count = img[4] | (img[5]<<8) | (img[6]<<16) | ((uint32_t)img[7]<<24); pos=12;
    for (i=0;i<count;i++) {
        uint16_t nl; uint32_t off, sz; char name[128]; int j;
        if (pos+2>len) return -1; nl = img[pos] | (img[pos+1]<<8); pos+=2;
        if (nl>=127 || pos+nl+8>len) return -1;
        for (j=0;j<nl;j++) name[j]=(char)img[pos+j]; name[nl]=0; pos+=nl;
        off = img[pos] | (img[pos+1]<<8) | (img[pos+2]<<16) | ((uint32_t)img[pos+3]<<24); sz = img[pos+4] | (img[pos+5]<<8) | (img[pos+6]<<16) | ((uint32_t)img[pos+7]<<24); pos+=8;
        if (list_out) { int k=0; while (name[k] && list_len+1<list_cap) list_out[list_len++]=name[k++]; if (list_len+1<list_cap) list_out[list_len++]='\n'; list_out[list_len]=0; }
        if (want && ff_strcmp(name, want)==0) { if (off+sz>len) return -1; if (data) *data=img+off; if (size) *size=sz; return 0; }
    }
    return want ? -2 : 0;
}
int ff_list(const uint8_t *img, size_t len, char *out, size_t out_size){ if (!out || out_size==0) return -1; out[0]=0; return ff_parse(img,len,0,0,0,out,out_size); }
int ff_read(const uint8_t *img, size_t len, const char *name, const uint8_t **data, uint32_t *size){ return ff_parse(img,len,name,data,size,0,0); }

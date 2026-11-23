/* bootcfg.c - BOOT.CFG parse/format (see bootcfg.h).
 * Original FisixOS code. Freestanding: no libc. */
#include "bootcfg.h"

void bootcfg_defaults(bootcfg_t *cfg)
{
    if (!cfg) return;
    cfg->default_choice = 0;
    cfg->timeout_secs = 0;
}

static int b_is_space(char c){ return c==' '||c=='\t'||c=='\r'; }
static int b_parse_int(const char *s, int *out)
{
    int v=0, digits=0;
    while (b_is_space(*s)) s++;
    while (*s>='0' && *s<='9') {
        /* Saturate: stop accumulating once v is past every valid
           range (max valid value is timeout 3600), so a long digit
           run can never overflow int via v*10 and wrap back into
           range. Remaining digits are still counted. */
        if (v < 100000) v=v*10+(*s-'0');
        digits++; s++;
    }
    if (!digits) return 0;
    *out=v; return 1;
}

int bootcfg_parse(const char *text, bootcfg_t *cfg)
{
    if (!text || !cfg) return -1;
    bootcfg_defaults(cfg);
    while (*text) {
        const char *line = text;
        const char *eol = text;
        char key[32]; int ki=0;
        const char *eq;
        while (*eol && *eol!='\n') eol++;
        /* Skip blank lines and comments. */
        { const char *p=line; while (p<eol && b_is_space(*p)) p++; if (p>=eol || *p=='#') { text = (*eol=='\n') ? eol+1 : eol; continue; } line=p; }
        eq = line;
        while (eq<eol && *eq!='=') eq++;
        if (eq>=eol) { text = (*eol=='\n') ? eol+1 : eol; continue; }
        while (line<eq && ki<31) key[ki++]=*line++;
        key[ki]=0;
        /* Trim trailing spaces from the key. */
        while (ki>0 && b_is_space(key[ki-1])) key[--ki]=0;
        {
            int v=0;
            const char *val = eq+1;
            if (b_parse_int(val, &v)) {
                if (key[0]=='d' && key[1]=='e' && key[2]=='f' && key[3]=='a' && key[4]=='u' && key[5]=='l' && key[6]=='t' && key[7]==0) {
                    if (v>=0 && v<=99) cfg->default_choice=v;
                } else if (key[0]=='t' && key[1]=='i' && key[2]=='m' && key[3]=='e' && key[4]=='o' && key[5]=='u' && key[6]=='t' && key[7]==0) {
                    if (v>=0 && v<=3600) cfg->timeout_secs=v;
                }
            }
        }
        text = (*eol=='\n') ? eol+1 : eol;
    }
    return 0;
}

int bootcfg_format(const bootcfg_t *cfg, char *buf, size_t cap)
{
    char tmp[64];
    int n=0, i=0, v;
    if (!cfg || !buf || cap<16) return -1;
    tmp[n++]='d'; tmp[n++]='e'; tmp[n++]='f'; tmp[n++]='a'; tmp[n++]='u'; tmp[n++]='l'; tmp[n++]='t'; tmp[n++]='=';
    v = cfg->default_choice;
    { char dig[8]; int d=0; if (v==0) dig[d++]='0'; while (v>0) { dig[d++]=(char)('0'+v%10); v/=10; } while (d>0) tmp[n++]=dig[--d]; }
    tmp[n++]='\n';
    tmp[n++]='t'; tmp[n++]='i'; tmp[n++]='m'; tmp[n++]='e'; tmp[n++]='o'; tmp[n++]='u'; tmp[n++]='t'; tmp[n++]='=';
    v = cfg->timeout_secs;
    { char dig[8]; int d=0; if (v==0) dig[d++]='0'; while (v>0) { dig[d++]=(char)('0'+v%10); v/=10; } while (d>0) tmp[n++]=dig[--d]; }
    tmp[n++]='\n';
    tmp[n]=0;
    if ((size_t)n+1>cap) return -1;
    for (i=0;i<=n;i++) buf[i]=tmp[i];
    return 0;
}

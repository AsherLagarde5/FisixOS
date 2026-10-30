#include "smbios.h"

static void *g_table = 0;
static uint32_t g_table_len = 0;
static int g_available = 0;
static char g_vendor[64];
static char g_version[64];
static char g_date[64];

static void copy_str(char *dst, const char *src, uint32_t max)
{
    uint32_t i = 0;
    if (!dst) return;
    dst[0] = 0;
    if (!src) return;
    while (i + 1 < max && src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

static const char *struct_string(const uint8_t *s, uint32_t len, uint8_t index)
{
    const uint8_t *p;
    uint8_t cur = 1;
    if (!s || index == 0) return 0;
    p = s + s[1];
    while (p < s + len) {
        if (*p == 0) {
            if (p + 1 < s + len && p[1] == 0) break;
            cur++;
            p++;
            continue;
        }
        if (cur == index) return (const char *)p;
        while (p < s + len && *p) p++;
    }
    return 0;
}

void smbios_set_table(void *table)
{
    const uint8_t *ep = (const uint8_t *)table;
    const uint8_t *t;
    uint32_t len = 0;
    uint32_t off = 0;
    g_available = 0; g_table = 0; g_table_len = 0;
    g_vendor[0] = 0; g_version[0] = 0; g_date[0] = 0;
    if (!ep) return;
    if (ep[0] == '_' && ep[1] == 'S' && ep[2] == 'M' && ep[3] == '3' && ep[4] == '_') {
        /* SMBIOS 3.x entry point: table address at offset 16 (64-bit). */
        t = (const uint8_t *)(uintptr_t)(*(const uint64_t *)(ep + 16));
        /* No total length in the 3.x entry point; walk until Type 127. */
        len = 0x10000;
    } else if (ep[0] == '_' && ep[1] == 'S' && ep[2] == 'M' && ep[3] == '_') {
        len = (uint32_t)ep[22] | ((uint32_t)ep[23] << 8);
        t = (const uint8_t *)(uintptr_t)(*(const uint32_t *)(ep + 24));
    } else {
        return;
    }
    if (!t || len == 0) return;
    g_table = (void *)t; g_table_len = len; g_available = 1;
    /* Walk structures; Type 0 = BIOS Information. */
    while (off + 4 <= len) {
        const uint8_t *s = t + off;
        uint8_t type = s[0], slen = s[1];
        uint32_t next;
        if (slen < 4) break;
        if (type == 0 && slen >= 20) {
            copy_str(g_vendor, struct_string(s, len - off, s[4]), 64);
            copy_str(g_version, struct_string(s, len - off, s[5]), 64);
            copy_str(g_date, struct_string(s, len - off, s[8]), 64);
            break;
        }
        if (type == 127) break;
        /* Skip formatted area, then strings until double NUL. */
        next = off + slen;
        while (next + 1 < len) {
            if (t[next] == 0 && t[next + 1] == 0) { next += 2; break; }
            next++;
        }
        if (next <= off) break;
        off = next;
    }
}

const char *smbios_bios_vendor(void) { return g_vendor; }
const char *smbios_bios_version(void) { return g_version; }
const char *smbios_bios_date(void) { return g_date; }
int smbios_is_available(void) { return g_available; }

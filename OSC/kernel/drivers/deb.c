/* deb.c - Debian .deb unpacker / installer for FisixOS.
 *
 * Language: C (the FisixOS kernel build is C; the ELF loader logic
 * here is a port/adaptation of Asher's Rust OS project code
 * kernel/src/elf.rs — header validation, PT_LOAD walk, .bss zeroing,
 * Linux initial-stack layout — rewritten in C for this build, with
 * all profanity stripped, per the professional-vibe rule. The
 * syscall number table is adapted from Asher's Rust OS project
 * kernel/src/syscall/mod.rs; see kernel/drivers/deb_linux_syscalls.h
 * for the Linux-number -> FisixOS-number mapping.
 *
 * Freestanding: no libc, no heap. All state is static. String and
 * memory helpers below are local (the kernel has no libc string.h).
 */
#include "deb.h"
#include "deb_inflate.h"
#include "deb_xz.h"
#include "deb_zstd.h"
#include "deb_embedded.h"
#include "proc.h"
#include "ring3.h"
#include "fse.h"
#include "fat.h"
#include "deb_gui.h"
#include "lvgl_port.h"

/* ---------- local helpers (no libc) ---------- */
static uint32_t d_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}
static int d_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static int d_strncmp(const char *a, const char *b, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        if (a[i] != b[i]) return (int)(unsigned char)a[i] - (int)(unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}
static void d_strcpy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    if (!cap) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static void d_memcpy(uint8_t *dst, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = src[i];
}
static void d_memset(uint8_t *dst, uint8_t v, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) dst[i] = v;
}
/* The freestanding kernel provides no libc memcpy/memset, but GCC
 * emits calls to them for struct assignment and large zeroing
 * (e.g. `g_pkgs[g_pkg_count++] = pkg;` in deb_install). Define
 * the standard symbols here so deb.c (and any later freestanding
 * code that assigns structs) links. No other kernel file defines
 * them, so there is no collision. */
void *memcpy(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void *memset(void *dst, int v, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    size_t i;
    for (i = 0; i < n; i++) d[i] = (uint8_t)v;
    return dst;
}
/* memmove: the vendored xz decoder (kernel/drivers/xz/) copies
 * overlapping ranges (LZMA2 dictionary refill). Same freestanding
 * shim as memcpy/memset above. */
void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i;
    if (d == s || n == 0) return dst;
    if (d < s) {
        for (i = 0; i < n; i++) d[i] = s[i];
    } else {
        i = n;
        while (i > 0) { i--; d[i] = s[i]; }
    }
    return dst;
}
/* strlen/memcmp/strchr: same freestanding shims as above.
 * Needed by the FFS driver (kernel/drivers/ffs_core.c), which
 * calls them directly; GCC also emits memcmp calls for struct
 * comparisons. */
size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n]) n++;
    return n;
}
int memcmp(const void *a, const void *b, size_t n)
{
    const uint8_t *x = (const uint8_t *)a;
    const uint8_t *y = (const uint8_t *)b;
    size_t i;
    for (i = 0; i < n; i++) {
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    }
    return 0;
}
char *strchr(const char *s, int c)
{
    char ch = (char)c;
    while (s && *s) {
        if (*s == ch) return (char *)s;
        s++;
    }
    return (s && ch == 0) ? (char *)s : 0;
}
static uint32_t d_rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint16_t d_rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint64_t d_rd64le(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* ---------- registry + staging pool ---------- */
static deb_pkg_t g_pkgs[DEB_MAX_PACKAGES];
static int g_pkg_count = 0;
static uint8_t g_stage[DEB_STAGE_POOL_SIZE];
static uint32_t g_stage_used = 0;
static int g_embedded_done = 0;

static int find_pkg(const char *name)
{
    int i, n = g_pkg_count;
    /* Defense in depth: the registry holds at most
     * DEB_MAX_PACKAGES entries, so never walk past it even if
     * the count were ever corrupt (an unzeroed .bss once did
     * exactly that and faulted). */
    if (n > DEB_MAX_PACKAGES) n = DEB_MAX_PACKAGES;
    for (i = 0; i < n; i++)
        if (d_strcmp(g_pkgs[i].name, name) == 0) return i;
    return -1;
}

/* Rebuild the staging pool from the registry (used after remove:
 * files are compacted, offsets rewritten). */
static void rebuild_stage(void)
{
    uint32_t dst = 0;
    int p, f, np = g_pkg_count;
    uint8_t tmp[4096];
    if (np > DEB_MAX_PACKAGES) np = DEB_MAX_PACKAGES;
    for (p = 0; p < np; p++) {
        for (f = 0; f < g_pkgs[p].file_count; f++) {
            deb_file_t *fl = &g_pkgs[p].files[f];
            uint32_t off = fl->offset, left = fl->size, chunk;
            /* Copy via a small bounce buffer so overlapping moves
             * inside the pool stay safe. */
            fl->offset = dst;
            while (left) {
                chunk = left > sizeof(tmp) ? sizeof(tmp) : left;
                d_memcpy(tmp, g_stage + off, chunk);
                d_memcpy(g_stage + dst, tmp, chunk);
                off += chunk; dst += chunk; left -= chunk;
            }
        }
    }
    g_stage_used = dst;
}

/* ---------- ar archive ---------- */
/* One ar member: name (up to 16 chars, space-padded, may end '/'),
 * data pointer and length. */
typedef struct {
    char name[24];
    const uint8_t *data;
    uint32_t len;
} ar_member_t;

static int ar_next(const uint8_t *ar, uint32_t ar_len, uint32_t *pos,
                   ar_member_t *m)
{
    uint32_t p, size, i;
    char szbuf[16];
    if (*pos + 60 > ar_len) return -1;
    p = *pos;
    for (i = 0; i < 16 && ar[p + i] != ' ' && ar[p + i] != '/'; i++)
        m->name[i] = (char)ar[p + i];
    m->name[i] = 0;
    for (i = 0; i < 10; i++) szbuf[i] = (char)ar[p + 48 + i];
    szbuf[10] = 0;
    size = 0;
    for (i = 0; szbuf[i] >= '0' && szbuf[i] <= '9'; i++)
        size = size * 10 + (uint32_t)(szbuf[i] - '0');
    if (p + 60 + size > ar_len) return -1;
    m->data = ar + p + 60;
    m->len = size;
    p += 60 + size;
    if (p & 1) p++; /* members are 2-byte aligned */
    *pos = p;
    return 0;
}

/* Compression of a tar member, by filename suffix. */
typedef enum { TAR_NONE = 0, TAR_GZ, TAR_XZ, TAR_ZST, TAR_BZ2 } tar_comp_t;
static tar_comp_t tar_compression(const char *name)
{
    uint32_t n = d_strlen(name);
    if (n >= 3 && d_strcmp(name + n - 3, ".gz") == 0) return TAR_GZ;
    if (n >= 3 && d_strcmp(name + n - 3, ".xz") == 0) return TAR_XZ;
    if (n >= 4 && d_strcmp(name + n - 4, ".zst") == 0) return TAR_ZST;
    if (n >= 4 && d_strcmp(name + n - 4, ".bz2") == 0) return TAR_BZ2;
    return TAR_NONE;
}

/* Decompressed tar scratch buffer (4 MiB, raised from 2 MiB in
 * stage 5 so the real x11-apps .deb installs: its data.tar
 * decompresses to 2,549,760 bytes, just over the old 2 MiB limit,
 * while its files (2,476 KiB installed) still fit the 4 MiB
 * staging pool. A tar bigger than this fails install with
 * DEB_ERR_TOO_BIG instead of overflowing. */
/* Scratch for one decompressed tar: 16 MiB holds libc6's data
 * tar (~12.8 MiB decompressed). Raised from 4 MiB for the
 * dynamic linker stage; BSS, so it costs RAM, not image size. */
static uint8_t g_tar_scratch[16u * 1024u * 1024u];

static int tar_decompress(const ar_member_t *m, const uint8_t **tar,
                          uint32_t *tar_len, char *err, uint32_t err_cap)
{
    tar_comp_t c = tar_compression(m->name);
    uint32_t out_len = 0;
    int r;
    switch (c) {
        case TAR_NONE:
            *tar = m->data;
            *tar_len = m->len;
            return DEB_OK;
        case TAR_GZ:
            r = deb_gunzip(m->data, m->len, g_tar_scratch,
                           sizeof(g_tar_scratch), &out_len);
            if (r == -2) {
                if (err) d_strcpy(err, "tar too big for 4 MiB scratch buffer", err_cap);
                return DEB_ERR_TOO_BIG;
            }
            if (r != 0) {
                if (err) d_strcpy(err, "gzip stream corrupt", err_cap);
                return DEB_ERR_BAD_TAR;
            }
            *tar = g_tar_scratch;
            *tar_len = out_len;
            return DEB_OK;
        case TAR_XZ:
            r = deb_xzunzip(m->data, m->len, g_tar_scratch,
                            sizeof(g_tar_scratch), &out_len);
            if (r == -2) {
                if (err) d_strcpy(err, "tar too big for 2 MiB scratch buffer", err_cap);
                return DEB_ERR_TOO_BIG;
            }
            if (r == -3) {
                if (err) d_strcpy(err, "xz stream uses an unsupported option (SHA-256 check, Delta filter, non-x86 BCJ)", err_cap);
                return DEB_ERR_UNSUPPORTED_COMPRESSION;
            }
            if (r != 0) {
                if (err) d_strcpy(err, "xz stream corrupt", err_cap);
                return DEB_ERR_BAD_TAR;
            }
            *tar = g_tar_scratch;
            *tar_len = out_len;
            return DEB_OK;
        case TAR_ZST:
            r = deb_zstdunzip(m->data, m->len, g_tar_scratch,
                              sizeof(g_tar_scratch), &out_len);
            if (r == -2) {
                if (err) d_strcpy(err, "tar too big for 2 MiB scratch buffer", err_cap);
                return DEB_ERR_TOO_BIG;
            }
            if (r == -3) {
                if (err) d_strcpy(err, "zstd decoder ran out of static pool memory", err_cap);
                return DEB_ERR_TOO_BIG;
            }
            if (r != 0) {
                if (err) d_strcpy(err, "zstd stream corrupt", err_cap);
                return DEB_ERR_BAD_TAR;
            }
            *tar = g_tar_scratch;
            *tar_len = out_len;
            return DEB_OK;
        case TAR_BZ2:
            if (err) d_strcpy(err, "control/data tar is bzip2-compressed: not supported in-kernel; repack with tools/deb_repack_gz.py", err_cap);
            return DEB_ERR_UNSUPPORTED_COMPRESSION;
    }
    return DEB_ERR_BAD_TAR;
}

/* ---------- tar ---------- */
static uint32_t tar_octal(const uint8_t *p, uint32_t max)
{
    uint32_t v = 0, i = 0;
    while (i < max && (p[i] == ' ' || p[i] == 0)) i++;
    while (i < max && p[i] >= '0' && p[i] <= '7') {
        v = v * 8 + (uint32_t)(p[i] - '0');
        i++;
    }
    return v;
}

/* Walk a tar, calling fn(path, data, len, is_dir) for each file.
 * GNU longname ('L') entries are honoured. Returns DEB_OK or
 * DEB_ERR_BAD_TAR. */
typedef int (*tar_cb_t)(const char *path, const uint8_t *data,
                        uint32_t len, void *ctx);

static int tar_walk(const uint8_t *tar, uint32_t tar_len, tar_cb_t fn,
                    void *ctx)
{
    uint32_t pos = 0;
    char longname[DEB_MAX_PATH];
    longname[0] = 0;
    while (pos + 512 <= tar_len) {
        const uint8_t *h = tar + pos;
        uint32_t size;
        char type;
        char path[DEB_MAX_PATH];
        uint32_t i, j;
        if (h[0] == 0) break; /* end-of-archive zero blocks */
        if (d_strncmp((const char *)h + 257, "ustar", 5) != 0 &&
            d_strncmp((const char *)h + 257, "ustar ", 6) != 0 &&
            h[257] != 0) {
            /* Not a ustar header and not an old tar header either;
             * old tar has no magic — accept it if the size field
             * parses, else fail. */
            if (tar_octal(h + 124, 12) == 0 && h[124] != '0') return DEB_ERR_BAD_TAR;
        }
        size = tar_octal(h + 124, 12);
        type = (char)h[156];
        if (pos + 512 + ((size + 511) & ~511u) > tar_len)
            return DEB_ERR_BAD_TAR;
        /* Build the path: ustar prefix + name, or the GNU longname. */
        if (longname[0]) {
            d_strcpy(path, longname, sizeof(path));
            longname[0] = 0;
        } else {
            j = 0;
            if (h[345]) { /* prefix */
                for (i = 0; i < 155 && h[345 + i]; i++) path[j++] = (char)h[345 + i];
                if (j + 1 < sizeof(path)) path[j++] = '/';
            }
            for (i = 0; i < 100 && h[i]; i++) {
                if (j + 1 >= sizeof(path)) break;
                path[j++] = (char)h[i];
            }
            path[j] = 0;
        }
        if (type == 'L') {
            /* GNU longname: the data block is the next file's name. */
            uint32_t n = size < sizeof(longname) - 1 ? size : sizeof(longname) - 1;
            for (i = 0; i < n; i++) longname[i] = (char)h[512 + i];
            longname[n] = 0;
        } else if (type == '5') {
            if (fn) {
                int r = fn(path, 0, 0, ctx);
                if (r != 0) return r;
            }
        } else if (type == '0' || type == 0 || type == '7') {
            if (fn) {
                int r = fn(path, h + 512, size, ctx);
                if (r != 0) return r;
            }
        }
        /* 'x'/'g' extended headers and anything else: skipped. */
        pos += 512 + ((size + 511) & ~511u);
    }
    return DEB_OK;
}

/* ---------- control file ---------- */
static void control_get(const uint8_t *data, uint32_t len,
                        const char *key, char *out, uint32_t cap)
{
    uint32_t i = 0, klen = d_strlen(key);
    out[0] = 0;
    while (i < len) {
        uint32_t line_start = i, line_len = 0;
        while (i < len && data[i] != '\n') { i++; line_len++; }
        if (i < len) i++; /* skip \n */
        if (line_len > klen && d_strncmp((const char *)data + line_start, key, klen) == 0 &&
            data[line_start + klen] == ':') {
            uint32_t v = line_start + klen + 1, e = line_start + line_len, o = 0;
            while (v < e && (data[v] == ' ' || data[v] == '\t')) v++;
            while (v < e && data[v] != '\r' && o + 1 < cap) out[o++] = (char)data[v++];
            out[o] = 0;
            return;
        }
    }
}

/* ---------- ELF ---------- */
static int elf_phdr(const uint8_t *data, uint32_t len, uint32_t idx,
                    uint32_t *p_type, uint64_t *p_offset, uint64_t *p_vaddr,
                    uint64_t *p_filesz, uint64_t *p_memsz)
{
    uint64_t phoff;
    uint16_t phentsize, phnum;
    const uint8_t *p;
    if (len < 64) return -1;
    phoff = d_rd64le(data + 32);
    phentsize = d_rd16le(data + 54);
    phnum = d_rd16le(data + 56);
    if (phentsize < 56 || idx >= phnum) return -1;
    if (phoff + (uint64_t)idx * phentsize + 56 > len) return -1;
    p = data + phoff + (uint64_t)idx * phentsize;
    *p_type = d_rd32le(p);
    *p_offset = d_rd64le(p + 8);
    *p_vaddr = d_rd64le(p + 16);
    *p_filesz = d_rd64le(p + 32);
    *p_memsz = d_rd64le(p + 40);
    return 0;
}

static uint16_t elf_phnum(const uint8_t *data, uint32_t len)
{
    if (len < 64) return 0;
    return d_rd16le(data + 56);
}

/* Map a virtual address to a file offset using PT_LOAD segments. */
static int elf_vaddr_to_off(const uint8_t *data, uint32_t len,
                            uint64_t vaddr, uint64_t *off)
{
    uint16_t n = elf_phnum(data, len), i;
    for (i = 0; i < n; i++) {
        uint32_t t;
        uint64_t o, v, f, m;
        if (elf_phdr(data, len, i, &t, &o, &v, &f, &m) != 0) continue;
        if (t == 1 && vaddr >= v && vaddr < v + f) {
            *off = o + (vaddr - v);
            return 0;
        }
    }
    return -1;
}

deb_elf_kind_t deb_elf_classify(const uint8_t *data, uint32_t len,
                                char *out, uint32_t out_cap)
{
    uint16_t phnum, i;
    int has_interp = 0, has_dynamic = 0;
    char interp[128];
    interp[0] = 0;
    if (out && out_cap) out[0] = 0;
    if (!data || len < 4) return DEB_ELF_NOT_ELF;
    /* FisixOS .fse first: its magic is 'F','S','E','1'. */
    if (len >= 64 && d_rd32le(data) == 0x31455346u) {
        if (out) d_strcpy(out, "FisixOS .fse program", out_cap);
        return DEB_ELF_FSE;
    }
    if (len < 64 || data[0] != 0x7F || data[1] != 'E' || data[2] != 'L' || data[3] != 'F')
        return DEB_ELF_NOT_ELF;
    if (data[4] != 2 || data[5] != 1 || d_rd16le(data + 18) != 0x3E) {
        if (out) d_strcpy(out, "ELF but not 64-bit little-endian x86-64", out_cap);
        return DEB_ELF_OTHER;
    }
    phnum = elf_phnum(data, len);
    for (i = 0; i < phnum; i++) {
        uint32_t t;
        uint64_t o, v, f, m;
        if (elf_phdr(data, len, i, &t, &o, &v, &f, &m) != 0) continue;
        if (t == 3) { /* PT_INTERP */
            uint32_t n = f < sizeof(interp) - 1 ? (uint32_t)f : sizeof(interp) - 1;
            uint32_t k;
            has_interp = 1;
            for (k = 0; k < n && o + k < len; k++) interp[k] = (char)data[o + k];
            interp[n] = 0;
        }
        if (t == 2) has_dynamic = 1; /* PT_DYNAMIC */
    }
    if (has_interp || has_dynamic) {
        if (out) {
            char tmp[256];
            uint32_t o = 0, k;
            const char *pre = "dynamic Linux ELF, needs interpreter ";
            while (pre[o]) { tmp[o] = pre[o]; o++; }
            for (k = 0; interp[k] && o + 1 < sizeof(tmp); k++) tmp[o++] = interp[k];
            /* List DT_NEEDED libraries from the dynamic section,
             * via DT_STRTAB (vaddr -> file offset through PT_LOAD). */
            {
                uint16_t j;
                uint64_t strtab_v = 0;
                for (j = 0; j < phnum && !strtab_v; j++) {
                    uint32_t t;
                    uint64_t o2, v, f, m;
                    if (elf_phdr(data, len, j, &t, &o2, &v, &f, &m) != 0) continue;
                    if (t == 2) {
                        uint64_t d;
                        for (d = o2; d + 16 <= o2 + f && d + 16 <= len; d += 16) {
                            int64_t tag = (int64_t)d_rd64le(data + d);
                            uint64_t val = d_rd64le(data + d + 8);
                            if (tag == 5) strtab_v = val;
                            if (tag == 0) break;
                        }
                    }
                }
                if (strtab_v) {
                    uint64_t str_off;
                    if (elf_vaddr_to_off(data, len, strtab_v, &str_off) == 0) {
                        int listed = 0;
                        for (j = 0; j < phnum; j++) {
                            uint32_t t;
                            uint64_t o2, v, f, m;
                            if (elf_phdr(data, len, j, &t, &o2, &v, &f, &m) != 0) continue;
                            if (t != 2) continue;
                            {
                                uint64_t d;
                                for (d = o2; d + 16 <= o2 + f && d + 16 <= len; d += 16) {
                                    int64_t tag = (int64_t)d_rd64le(data + d);
                                    uint64_t val = d_rd64le(data + d + 8);
                                    if (tag == 0) break;
                                    if (tag == 1 && listed < 3) {
                                        const char *suf = listed ? ", " : " (needs ";
                                        const uint8_t *s;
                                        uint32_t q;
                                        while (*suf && o + 1 < sizeof(tmp)) tmp[o++] = *suf++;
                                        s = data + str_off + val;
                                        for (q = 0; s[q] && o + 1 < sizeof(tmp) &&
                                                    str_off + val + q < len; q++)
                                            tmp[o++] = (char)s[q];
                                        listed++;
                                    }
                                }
                            }
                        }
                        if (listed) {
                            const char *suf = ")";
                            while (*suf && o + 1 < sizeof(tmp)) tmp[o++] = *suf++;
                        }
                    }
                }
            }
            tmp[o] = 0;
            d_strcpy(out, tmp, out_cap);
        }
        return DEB_ELF_DYNAMIC;
    }
    if (out) d_strcpy(out, "static Linux x86-64 ELF (no interpreter, no dynamic section)", out_cap);
    return DEB_ELF_STATIC;
}

int deb_elf_load(const uint8_t *data, uint32_t len,
                 uint8_t *load_buf, uint32_t load_cap,
                 deb_loaded_elf_t *loaded, char *err, uint32_t err_cap)
{
    /* Ported/adapted from Asher's Rust OS project kernel/src/elf.rs
     * (Rust, Asher): header checks, PT_LOAD walk, .bss zeroing,
     * user-stack layout. Differences from elf.rs, all deliberate:
     * - elf.rs maps each PT_LOAD into fresh user pages via a frame
     *   allocator + page tables; FisixOS has neither yet, so the
     *   segments are copied into a flat caller buffer at
     *   (p_vaddr - lowest PT_LOAD vaddr).
     * - elf.rs jumps to ring 3 with jump_to_user_mode (iretq,
     *   CS 0x23 / SS 0x1B); FisixOS has no user GDT segments, so
     *   this function returns the entry point instead of jumping.
     * - The Linux initial stack (argc=1, argv[0]=NULL, envp NULL,
     *   auxv AT_NULL) is built at the top of the load buffer, the
     *   same layout elf.rs writes at 0x7FFF0000. */
    uint16_t phnum, i;
    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFFull, max_vaddr = 0;
    uint64_t entry, stack_u64;
    uint64_t *sp;
    char reason[192];
    deb_elf_kind_t kind;
    if (!data || !load_buf || !loaded) {
        if (err) d_strcpy(err, "null argument", err_cap);
        return DEB_ERR_BAD_ELF;
    }
    kind = deb_elf_classify(data, len, reason, sizeof(reason));
    if (kind == DEB_ELF_FSE) {
        if (err) d_strcpy(err, "not an ELF: this is a FisixOS .fse, run it with proc_run_fse", err_cap);
        return DEB_ERR_BAD_ELF;
    }
    if (kind == DEB_ELF_DYNAMIC) {
        if (err) d_strcpy(err, reason, err_cap);
        return DEB_ERR_NOT_RUNNABLE;
    }
    if (kind != DEB_ELF_STATIC) {
        if (err) d_strcpy(err, "not a static x86-64 ELF", err_cap);
        return DEB_ERR_BAD_ELF;
    }
    phnum = elf_phnum(data, len);
    for (i = 0; i < phnum; i++) {
        uint32_t t;
        uint64_t o, v, f, m;
        if (elf_phdr(data, len, i, &t, &o, &v, &f, &m) != 0) continue;
        if (t != 1) continue;
        if (v < min_vaddr) min_vaddr = v;
        if (v + m > max_vaddr) max_vaddr = v + m;
    }
    if (min_vaddr == 0xFFFFFFFFFFFFFFFFull || max_vaddr <= min_vaddr) {
        if (err) d_strcpy(err, "ELF has no PT_LOAD segments", err_cap);
        return DEB_ERR_BAD_ELF;
    }
    if (max_vaddr - min_vaddr + 4096 > load_cap) {
        if (err) d_strcpy(err, "ELF segments too big for the load buffer", err_cap);
        return DEB_ERR_TOO_BIG;
    }
    d_memset(load_buf, 0, (uint32_t)(max_vaddr - min_vaddr));
    loaded->seg_count = 0;
    for (i = 0; i < phnum; i++) {
        uint32_t t;
        uint64_t o, v, f, m;
        uint32_t dst_off;
        if (elf_phdr(data, len, i, &t, &o, &v, &f, &m) != 0) continue;
        if (t != 1) continue;
        if (o + f > len) {
            if (err) d_strcpy(err, "PT_LOAD segment past end of file", err_cap);
            return DEB_ERR_BAD_ELF;
        }
        dst_off = (uint32_t)(v - min_vaddr);
        d_memcpy(load_buf + dst_off, data + o, (uint32_t)f);
        /* .bss (memsz > filesz) is already zero from the memset. */
        loaded->seg_count++;
    }
    entry = d_rd64le(data + 24);
    loaded->entry = entry;
    loaded->brk_base = (max_vaddr + 0xFFF) & ~0xFFFull;
    /* Linux initial stack at the top of the load buffer (elf.rs
     * puts it at 0x7FFF0000; here it is the buffer top, same
     * contents): argc=1, argv[0]=NULL, envp NULL, then the auxv.
     * The auxv layout is adapted from LetsGo-OS (MIT, Copyright
     * (c) 2022 Sansero), kernel/elfloader.go LoadAuxVector, ported
     * from 32-bit to 64-bit: AT_PAGESZ, AT_UID/GID/EUID/EGID,
     * AT_ENTRY, AT_PHDR, AT_PHENT, AT_PHNUM, AT_RANDOM, AT_NULL.
     * AT_PHDR is the load address of the program-header table
     * (first PT_LOAD segment covers the ELF header, so the phdrs
     * sit at min_vaddr + e_phoff in the loaded image).
     * AT_RANDOM points at the load buffer: FisixOS has no entropy
     * source wired to the loader yet. */
    stack_u64 = (uint64_t)(uintptr_t)(load_buf + load_cap - 256);
    sp = (uint64_t *)(uintptr_t)stack_u64;
    {
        uint64_t phdr_addr = min_vaddr + d_rd64le(data + 32);
        uint64_t phent = d_rd16le(data + 54);
        int w = 0;
        sp[w++] = 1; sp[w++] = 0; sp[w++] = 0; /* argc, argv[0], envp end */
        sp[w++] = 6; sp[w++] = 4096;           /* AT_PAGESZ */
        sp[w++] = 11; sp[w++] = 0;             /* AT_UID */
        sp[w++] = 12; sp[w++] = 0;             /* AT_EUID */
        sp[w++] = 13; sp[w++] = 0;             /* AT_GID */
        sp[w++] = 14; sp[w++] = 0;             /* AT_EGID */
        sp[w++] = 9; sp[w++] = entry;          /* AT_ENTRY */
        sp[w++] = 3; sp[w++] = phdr_addr;      /* AT_PHDR */
        sp[w++] = 4; sp[w++] = phent;          /* AT_PHENT */
        sp[w++] = 5; sp[w++] = (uint64_t)phnum;/* AT_PHNUM */
        sp[w++] = 25; sp[w++] = (uint64_t)(uintptr_t)load_buf; /* AT_RANDOM */
        sp[w++] = 0; sp[w++] = 0;              /* AT_NULL */
        (void)w;
    }
    loaded->stack_top = stack_u64;
    return DEB_OK;
}

/* ---------- install ---------- */
typedef struct {
    deb_pkg_t *pkg;
    int err;
    char err_msg[128];
} install_ctx_t;

static int install_file_cb(const char *path, const uint8_t *data,
                           uint32_t len, void *ctx)
{
    install_ctx_t *c = (install_ctx_t *)ctx;
    deb_pkg_t *pkg = c->pkg;
    deb_file_t *fl;
    char why[64];
    if (!data) return 0; /* directory: nothing to stage */
    if (pkg->file_count >= DEB_MAX_FILES_PER_PACKAGE) {
        d_strcpy(c->err_msg, "too many files in package", sizeof(c->err_msg));
        c->err = DEB_ERR_TOO_BIG;
        return -1;
    }
    if (len > DEB_STAGE_POOL_SIZE - g_stage_used) {
        d_strcpy(c->err_msg, "staging pool full (16 MiB)", sizeof(c->err_msg));
        c->err = DEB_ERR_TOO_BIG;
        return -1;
    }
    fl = &pkg->files[pkg->file_count];
    d_strcpy(fl->path, path, sizeof(fl->path));
    fl->offset = g_stage_used;
    fl->size = len;
    d_memcpy(g_stage + g_stage_used, data, len);
    g_stage_used += len;
    fl->is_elf = (deb_elf_classify(data, len, why, sizeof(why)) != DEB_ELF_NOT_ELF);
    fl->elf_kind = (uint8_t)deb_elf_classify(data, len, 0, 0);
    pkg->file_count++;
    pkg->total_size += len;
    return 0;
}

typedef struct {
    const uint8_t *data;
    uint32_t len;
} control_ctx_t;

/* The control file itself, copied out of the control tar into its
 * own buffer before the data tar is decompressed (both tars share
 * the scratch buffer, so the control tar would otherwise be
 * overwritten by the data tar). */
static uint8_t g_control_buf[8192];
static uint32_t g_control_len = 0;

static int control_file_cb(const char *path, const uint8_t *data,
                           uint32_t len, void *ctx)
{
    control_ctx_t *c = (control_ctx_t *)ctx;
    if (d_strcmp(path, "./control") == 0 || d_strcmp(path, "control") == 0) {
        if (len >= sizeof(g_control_buf)) return DEB_ERR_TOO_BIG;
        d_memcpy(g_control_buf, data, len);
        g_control_buf[len] = 0;
        g_control_len = len;
        c->data = g_control_buf;
        c->len = len;
        return 0;
    }
    return 0;
}

int deb_install(const uint8_t *data, uint32_t len, char *err, uint32_t err_cap)
{
    uint32_t pos;
    ar_member_t m;
    ar_member_t control_m, data_m;
    const uint8_t *control_tar = 0, *data_tar = 0;
    uint32_t control_tar_len = 0, data_tar_len = 0;
    int have_control = 0, have_data = 0, r;
    /* The package under construction. This MUST be static, not a
     * stack local: sizeof(deb_pkg_t) is ~72 KiB (512 files x 140
     * bytes) but the kernel stack is only 64 KiB (entry.S). A stack
     * local overflowed the stack by ~6.6 KiB into .rodata/.data
     * below it (the stack is the first thing in .bss), silently
     * corrupting globals; installing libc6 then faulted with a
     * wild jump into linux_syscall_dispatch (#GP, ASCII in a
     * register). The host test never saw it (8 MiB host stack).
     * deb_install is single-threaded and non-reentrant, so a
     * function-scope static is safe; it is re-zeroed below on
     * every call. */
    static deb_pkg_t pkg;
    control_ctx_t cc;
    install_ctx_t ic;
    if (err && err_cap) err[0] = 0;
    if (!data || len < 8 || d_strncmp((const char *)data, "!<arch>\n", 8) != 0) {
        if (err) d_strcpy(err, "not an ar archive (bad magic)", err_cap);
        return DEB_ERR_BAD_AR;
    }
    pos = 8;
    while (ar_next(data, len, &pos, &m) == 0) {
        if (d_strncmp(m.name, "debian-binary", 13) == 0) {
            if (m.len < 4 || d_strncmp((const char *)m.data, "2.0", 3) != 0) {
                if (err) d_strcpy(err, "unsupported debian-binary version (want 2.0)", err_cap);
                return DEB_ERR_BAD_AR;
            }
        } else if (d_strncmp(m.name, "control.tar", 11) == 0) {
            control_m = m;
            have_control = 1;
        } else if (d_strncmp(m.name, "data.tar", 8) == 0) {
            data_m = m;
            have_data = 1;
        }
        /* Unknown members are ignored, like dpkg does. */
    }
    if (!have_control) {
        if (err) d_strcpy(err, "no control.tar.* member", err_cap);
        return DEB_ERR_BAD_CONTROL;
    }
    if (!have_data) {
        if (err) d_strcpy(err, "no data.tar.* member", err_cap);
        return DEB_ERR_BAD_TAR;
    }
    /* Control first: it is decompressed into the shared scratch
     * buffer, and the control file is copied out of it before the
     * data tar is decompressed into the same buffer. */
    r = tar_decompress(&control_m, &control_tar, &control_tar_len, err, err_cap);
    if (r != DEB_OK) return r;
    cc.data = 0; cc.len = 0;
    r = tar_walk(control_tar, control_tar_len, control_file_cb, &cc);
    if (r != DEB_OK || !cc.data) {
        if (err) d_strcpy(err, "control file not found in control.tar", err_cap);
        return DEB_ERR_BAD_CONTROL;
    }
    r = tar_decompress(&data_m, &data_tar, &data_tar_len, err, err_cap);
    if (r != DEB_OK) return r;
    d_memset((uint8_t *)&pkg, 0, sizeof(pkg));
    control_get(cc.data, cc.len, "Package", pkg.name, sizeof(pkg.name));
    control_get(cc.data, cc.len, "Version", pkg.version, sizeof(pkg.version));
    control_get(cc.data, cc.len, "Architecture", pkg.arch, sizeof(pkg.arch));
    control_get(cc.data, cc.len, "Depends", pkg.depends, sizeof(pkg.depends));
    control_get(cc.data, cc.len, "Description", pkg.description, sizeof(pkg.description));
    if (!pkg.name[0]) {
        if (err) d_strcpy(err, "control file has no Package field", err_cap);
        return DEB_ERR_BAD_CONTROL;
    }
    if (find_pkg(pkg.name) >= 0) {
        if (err) d_strcpy(err, "package already installed (remove it first)", err_cap);
        return DEB_ERR_ALREADY_INSTALLED;
    }
    if (g_pkg_count >= DEB_MAX_PACKAGES) {
        if (err) d_strcpy(err, "package registry full (8 packages)", err_cap);
        return DEB_ERR_TOO_BIG;
    }
    ic.pkg = &pkg;
    ic.err = DEB_OK;
    ic.err_msg[0] = 0;
    r = tar_walk(data_tar, data_tar_len, install_file_cb, &ic);
    if (r != DEB_OK) {
        if (err) d_strcpy(err, ic.err_msg[0] ? ic.err_msg : "bad data tar", err_cap);
        /* Roll back any staged bytes from this failed install. */
        rebuild_stage();
        return ic.err != DEB_OK ? ic.err : DEB_ERR_BAD_TAR;
    }
    g_pkgs[g_pkg_count++] = pkg;
    return DEB_OK;
}

int deb_remove(const char *name)
{
    int idx, i;
    if (!name) return DEB_ERR_NOT_FOUND;
    idx = find_pkg(name);
    if (idx < 0) return DEB_ERR_NOT_FOUND;
    for (i = idx; i + 1 < g_pkg_count; i++) g_pkgs[i] = g_pkgs[i + 1];
    g_pkg_count--;
    rebuild_stage();
    return DEB_OK;
}

/* Sanitize one tar path component to an 8.3 FAT name. Directories
 * get the base part only (max 8 chars); files keep base.ext
 * (base max 8, ext max 3). Lower case becomes upper case, any
 * character FAT forbids becomes '_'. The '.' is always emitted
 * between base and extension, even when the base is exactly 8
 * chars long (the dot is a separator, not a base character). */
static void deb_sanitize_component(const char *src, int is_dir, char *dst, uint32_t cap)
{
    char base[9], ext[4];
    uint32_t bi = 0, ei = 0, i = 0, o = 0;
    int seen_dot = 0;
    if (!src || !dst || cap == 0) return;
    while (src[i] == '.' && src[i+1] == '/') i += 2;
    while (src[i] == '/') i++;
    for (; src[i] && src[i] != '/'; i++) {
        char c = src[i];
        if (c == '.') { if (is_dir) break; if (seen_dot) break; seen_dot = 1; continue; }
        if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-')) c = '_';
        if (!seen_dot) { if (bi < 8) base[bi++] = c; }
        else { if (ei < 3) ext[ei++] = c; }
    }
    base[bi] = 0; ext[ei] = 0;
    { uint32_t k; for (k = 0; base[k] && o + 1 < cap; k++) dst[o++] = base[k]; }
    if (!is_dir && seen_dot && ext[0]) {
        if (o + 1 < cap) dst[o++] = '.';
        { uint32_t k; for (k = 0; ext[k] && o + 1 < cap; k++) dst[o++] = ext[k]; }
    }
    if (o == 0) { dst[0] = '_'; dst[1] = 0; return; }
    dst[o] = 0;
}

int deb_persist(const char *name, uint8_t *img, size_t len, char *err, uint32_t err_cap)
{
    int idx, f;
    if (err && err_cap) err[0]=0;
    if (!name || !img || len==0) {
        if (err) d_strcpy(err, "bad arguments to deb_persist", err_cap);
        return DEB_ERR_DISK;
    }
    idx = find_pkg(name);
    if (idx < 0) {
        if (err) d_strcpy(err, "package not installed", err_cap);
        return DEB_ERR_NOT_FOUND;
    }
    if (!fat_probe(img, len, 0)) {
        if (err) d_strcpy(err, "volume is not a FAT volume", err_cap);
        return DEB_ERR_DISK;
    }
    for (f=0; f<g_pkgs[idx].file_count; f++) {
        deb_file_t *fl = &g_pkgs[idx].files[f];
        const char *p = fl->path;
        char fat_path[DEB_MAX_PATH];
        uint32_t fp=0;
        uint32_t plen = d_strlen(p);
        int r;
        if (plen>0 && p[plen-1]=='/') continue; /* directory entry */
        while (p[0]=='.' && p[1]=='/') p+=2;
        while (*p=='/') p++;
        if (!*p) continue;
        /* Build the FAT path component by component, creating
         * each directory on the way. */
        while (*p) {
            char comp[32];
            uint32_t ci=0;
            int is_last;
            const char *slash = p;
            while (*slash && *slash!='/') slash++;
            is_last = (*slash==0);
            while (p<slash && ci+1<sizeof(comp)) comp[ci++]=*p++;
            comp[ci]=0;
            if (*p=='/') p++;
            if (!is_last) {
                char dir83[32];
                deb_sanitize_component(comp, 1, dir83, sizeof(dir83));
                { uint32_t k=0; if (fp>0) fat_path[fp++]='/'; while (dir83[k] && fp+1<sizeof(fat_path)) fat_path[fp++]=dir83[k++]; fat_path[fp]=0; }
                r = fat_mkdir(img, len, fat_path);
                if (r!=0) {
                    if (err) d_strcpy(err, "could not create directory on volume", err_cap);
                    return DEB_ERR_DISK;
                }
            } else {
                char file83[32];
                deb_sanitize_component(comp, 0, file83, sizeof(file83));
                { uint32_t k=0; if (fp>0) fat_path[fp++]='/'; while (file83[k] && fp+1<sizeof(fat_path)) fat_path[fp++]=file83[k++]; fat_path[fp]=0; }
            }
        }
        r = fat_write_file(img, len, fat_path, g_stage + fl->offset, fl->size);
        if (r!=0) {
            if (err) d_strcpy(err, "could not write file to volume (no space?)", err_cap);
            return DEB_ERR_DISK;
        }
    }
    return DEB_OK;
}

int deb_count(void) { return g_pkg_count; }

int deb_get(int idx, deb_pkg_t *out)
{
    if (!out || idx < 0 || idx >= g_pkg_count) return DEB_ERR_NOT_FOUND;
    *out = g_pkgs[idx];
    return DEB_OK;
}

int deb_find(const char *name, deb_pkg_t *out)
{
    int idx = find_pkg(name);
    if (idx < 0) return DEB_ERR_NOT_FOUND;
    if (out) *out = g_pkgs[idx];
    return DEB_OK;
}

static int path_matches(const char *a, const char *b)
{
    if (d_strcmp(a, b) == 0) return 1;
    /* Tar paths usually start "./": accept both forms. */
    if (a[0] == '.' && a[1] == '/' && d_strcmp(a + 2, b) == 0) return 1;
    if (b[0] == '.' && b[1] == '/' && d_strcmp(a, b + 2) == 0) return 1;
    return 0;
}

int deb_file_read(const char *pkg, const char *path,
                  const uint8_t **data, uint32_t *len)
{
    int idx, f;
    if (!pkg || !path || !data || !len) return DEB_ERR_NOT_FOUND;
    idx = find_pkg(pkg);
    if (idx < 0) return DEB_ERR_NOT_FOUND;
    for (f = 0; f < g_pkgs[idx].file_count; f++) {
        if (path_matches(g_pkgs[idx].files[f].path, path)) {
            *data = g_stage + g_pkgs[idx].files[f].offset;
            *len = g_pkgs[idx].files[f].size;
            return DEB_OK;
        }
    }
    return DEB_ERR_NOT_FOUND;
}

/* path_matches() only compares two spellings of the same path;
 * this one also accepts a leading "/" on either side, so an
 * absolute Unix path matches a staged "./..." tar path. */
static int deb_path_matches_abs(const char *staged, const char *path)
{
    if (path_matches(staged, path)) return 1;
    if (staged[0] == '.' && staged[1] == '/' && path[0] == '/' &&
        d_strcmp(staged + 2, path + 1) == 0)
        return 1;
    if (path[0] == '.' && path[1] == '/' && staged[0] == '/' &&
        d_strcmp(staged + 1, path + 2) == 0)
        return 1;
    if (staged[0] == '/' && d_strcmp(staged, path) == 0) return 1;
    if (path[0] == '/' && d_strcmp(staged, path + 1) == 0) return 1;
    return 0;
}

int deb_file_find_by_path(const char *path,
                          const uint8_t **data, uint32_t *len)
{
    int p, f;
    if (!path || !data || !len) return DEB_ERR_NOT_FOUND;
    for (p = 0; p < g_pkg_count; p++) {
        for (f = 0; f < g_pkgs[p].file_count; f++) {
            if (deb_path_matches_abs(g_pkgs[p].files[f].path, path)) {
                *data = g_stage + g_pkgs[p].files[f].offset;
                *len = g_pkgs[p].files[f].size;
                return DEB_OK;
            }
        }
    }
    return DEB_ERR_NOT_FOUND;
}

int deb_resolve_lib_path(const char *path,
                         const uint8_t **data, uint32_t *len)
{
    int rc;
    /* Debian's /lib64/ld-linux-x86-64.so.2 is a symlink to
     * /lib/x86_64-linux-gnu/ld-linux-x86-64.so.2. The tar
     * walker skips symlinks (type '2'), so the /lib64 spelling
     * is never staged; rewrite it to the real file. The same
     * rewrite covers /usr/lib64/ld-linux-x86-64.so.2, which
     * some linkers spell that way. */
    if (path && d_strcmp(path, "/lib64/ld-linux-x86-64.so.2") == 0)
        path = "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2";
    else if (path && d_strcmp(path, "/usr/lib64/ld-linux-x86-64.so.2") == 0)
        path = "/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2";
    rc = deb_file_find_by_path(path, data, len);
    if (rc == DEB_OK) return rc;
    /* usrmerge fallback: Debian trixie (and any merged-/usr
     * system) stages libraries under /usr/lib/..., while the
     * classic spelling is /lib/.... A /lib/... path that is
     * not staged is the same file under /usr/lib/.... */
    if (path && d_strncmp(path, "/lib/", 5) == 0) {
        char usrpath[192];
        uint32_t n = d_strlen(path);
        if (n + 4 < sizeof(usrpath)) {
            usrpath[0] = '/'; usrpath[1] = 'u';
            usrpath[2] = 's'; usrpath[3] = 'r';
            d_strcpy(usrpath + 4, path, (uint32_t)(sizeof(usrpath) - 4));
            return deb_file_find_by_path(usrpath, data, len);
        }
    }
    return rc;
}

int deb_info(const char *name, char *out, uint32_t cap)
{
    /* Static, not a stack local: sizeof(deb_pkg_t) is ~72 KiB but
     * the kernel stack is only 64 KiB (see deb_install). */
    static deb_pkg_t pkg;
    uint32_t o = 0, i;
    if (!out || !cap) return DEB_ERR_NOT_FOUND;
    out[0] = 0;
    if (deb_find(name, &pkg) != DEB_OK) return DEB_ERR_NOT_FOUND;
    {
        /* "name version (arch)\nDepends: ...\nDescription: ...\nN files, N bytes\n" */
        const char *parts[8];
        char numbuf[16];
        uint32_t n = pkg.total_size, d = 0;
        char *p = numbuf + 15;
        *p = 0;
        if (n == 0) *--p = '0';
        while (n) { *--p = (char)('0' + n % 10); n /= 10; }
        parts[0] = pkg.name; parts[1] = " "; parts[2] = pkg.version;
        parts[3] = " ("; parts[4] = pkg.arch; parts[5] = ")\n";
        parts[6] = 0; parts[7] = 0;
        for (i = 0; i < 6; i++) {
            const char *s = parts[i];
            while (*s && o + 1 < cap) out[o++] = *s++;
        }
        {
            const char *pre = "Depends: ";
            while (*pre && o + 1 < cap) out[o++] = *pre++;
            {
                const char *s = pkg.depends[0] ? pkg.depends : "(none)";
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            if (o + 1 < cap) out[o++] = '\n';
        }
        {
            const char *pre = "Description: ";
            while (*pre && o + 1 < cap) out[o++] = *pre++;
            {
                const char *s = pkg.description[0] ? pkg.description : "(none)";
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            if (o + 1 < cap) out[o++] = '\n';
        }
        {
            char fbuf[16];
            uint32_t fn = (uint32_t)pkg.file_count;
            int fi = 0;
            char *q = fbuf + 15;
            *q = 0;
            if (fn == 0) *--q = '0';
            while (fn) { *--q = (char)('0' + fn % 10); fn /= 10; }
            while (q[fi]) fi++;
            {
                const char *s = q;
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            {
                const char *s = " files, ";
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            {
                const char *s = p;
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            {
                const char *s = " bytes staged\n";
                while (*s && o + 1 < cap) out[o++] = *s++;
            }
            (void)d; (void)fbuf;
        }
        out[o] = 0;
    }
    return DEB_OK;
}

int deb_run(const char *pkg, const char *path, char *err, uint32_t err_cap)
{
    const uint8_t *data;
    uint32_t len;
    char reason[192];
    deb_elf_kind_t kind;
    int r;
    if (err && err_cap) err[0] = 0;
    r = deb_file_read(pkg, path, &data, &len);
    if (r != DEB_OK) {
        if (err) d_strcpy(err, "file not found in package", err_cap);
        return DEB_ERR_NOT_FOUND;
    }
    kind = deb_elf_classify(data, len, reason, sizeof(reason));
    if (kind == DEB_ELF_FSE) {
        return proc_run_fse(data, len);
    }
    /* Debian GUI app bridge (docs/DEB_GUI.md): a registered
     * app's binary can never run as an ELF (dynamic, needs
     * ld-linux + X11), so before refusing it, hand it to the
     * native LVGL bridge, which re-implements its UI + logic.
     * The bridge resumes LVGL (the terminal runs with LVGL
     * suspended) and shows its screen; success is DEB_OK. */
    if (deb_gui_is_bridged(pkg, path)) {
        lvgl_port_resume();
        if (deb_gui_show(pkg, path)) return DEB_OK;
    }
    if (kind == DEB_ELF_STATIC || kind == DEB_ELF_DYNAMIC) {
        /* Ring-3 execution lives in ring3.c now: load the ELF
         * into user pages, jump with iretq, serve Linux
         * syscalls, return the exit code. A dynamic ELF goes
         * through its interpreter (ld-linux from the installed
         * libc6 .deb); a static ELF runs directly. */
        int rc = ring3_run_elf(data, len, path, err, err_cap);
        if (rc >= 0) return rc;
        return DEB_ERR_NOT_RUNNABLE;
    }
    if (err) d_strcpy(err, reason, err_cap);
    return DEB_ERR_NOT_RUNNABLE;
}

void deb_embedded_mark_done(void)
{
    g_embedded_done = 1;
}

void deb_install_embedded(void)
{
    const uint8_t *data;
    uint32_t len;
    char err[128];
    int i = 0;
    if (g_embedded_done) return;
    g_embedded_done = 1;
    while (deb_embedded_find(i, &data, &len) != 0) {
        /* Best effort at first use: a bad embedded .deb is
         * reported by `deb list` being empty, not by crashing boot. */
        (void)deb_install(data, len, err, sizeof(err));
        i++;
    }
}

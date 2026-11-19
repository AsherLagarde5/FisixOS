/* linux_syscall.c - Linux x86_64 syscall dispatcher for FisixOS.
 *
 * Implements the Linux-number syscalls a static hello-world /
 * coreutils-style binary needs, and — since the dynamic linker
 * stage (docs/DYNLINK.md) — the ones the real glibc dynamic
 * linker (ld-linux) and a dynamic Debian binary need: file-backed
 * mmap, pread64, openat/newfstatat of .deb-installed files under
 * /lib/..., access, futex / rt_sigaction stubs. Served on top of
 * the ring-3 process state (ring3.c) and the kernel serial port.
 * See docs/RING3.md for the implemented / stub / unimplemented
 * table.
 *
 * The numbers come from kernel/drivers/deb_linux_syscalls.h
 * (adapted from Asher's Rust OS project syscall/mod.rs, with
 * all profanity stripped). The implementations here are new
 * FisixOS code.
 *
 * Freestanding: no libc, no heap. All state is static.
 */
#include "linux_syscall.h"
#include "deb_linux_syscalls.h"
#include "deb.h"
#include "ring3.h"
#include "memory.h"

/* ---------- serial output (COM1) ---------- */
static void ls_outb(uint16_t port, uint8_t value)
{
#ifdef FISIX_HOST_TEST
    /* Host test: no COM1 here; raw inb/outb would fault in
     * userspace. */
    (void)port; (void)value;
    return;
#else
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
#endif
}
static uint8_t ls_inb(uint16_t port)
{
#ifdef FISIX_HOST_TEST
    (void)port;
    return 0x20; /* pretend the transmitter is always ready */
#else
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
#endif
}
static void ls_serial_putc(char c)
{
    uint32_t to = 100000;
    uint8_t lsr;
    do {
        lsr = ls_inb(0x3F8 + 5);
        if (to-- == 0) return;
    } while ((lsr & 0x20) == 0);
    ls_outb(0x3F8, (uint8_t)c);
}
static void (*g_out_hook)(char c) = 0;

void linux_syscall_set_output_hook(void (*hook)(char c))
{
    g_out_hook = hook;
}

static void ls_putc(char c)
{
    if (g_out_hook) g_out_hook(c);
    else ls_serial_putc(c);
}
static void ls_puts(const char *s)
{
    while (s && *s) ls_putc(*s++);
}
static void ls_put_u64(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (v == 0) { ls_putc('0'); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    ls_puts(&buf[i]);
}

/* ---------- helpers ---------- */
static int ls_strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}
static uint64_t ls_strlen(const char *s)
{
    uint64_t n = 0;
    while (s && s[n]) n++;
    return n;
}
static void ls_strcpy(char *dst, const char *src, uint64_t cap)
{
    uint64_t i = 0;
    if (!cap) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}
static uint64_t ls_err(int e) { return (uint64_t)(int64_t)(-e); }

/* Read a NUL-terminated user string into a kernel buffer.
 * Returns the length, or -1 if the pointer is not readable
 * user memory or the string does not end within cap bytes. */
static int ls_copy_user_str(uint64_t uaddr, char *dst, uint64_t cap)
{
    uint64_t i;
    if (cap == 0) return -1;
    for (i = 0; i < cap; i++) {
        uint64_t phys;
        uint8_t *p;
        if (ring3_translate(ring3_kernel_pml4(), uaddr + i, &phys, 0) != 0)
            return -1;
        p = (uint8_t *)ring3_phys_to_ptr(phys);
        dst[i] = (char)*p;
        if (dst[i] == 0) return (int)i;
    }
    dst[cap - 1] = 0;
    return -1;
}

/* Read one little-endian uint64 from user memory (byte-wise, so
 * it works even if the 8 bytes straddle a page). Returns 0, or
 * -1 if the address is not readable user memory. Used by
 * writev to fetch each iovec's base/length. */
static int ls_read_user_u64(uint64_t uaddr, uint64_t *out)
{
    uint64_t i, v = 0;
    for (i = 0; i < 8; i++) {
        uint64_t phys;
        uint8_t *p;
        if (ring3_translate(ring3_kernel_pml4(), uaddr + i, &phys, 0) != 0)
            return -1;
        p = (uint8_t *)ring3_phys_to_ptr(phys);
        v |= ((uint64_t)*p) << (i * 8);
    }
    *out = v;
    return 0;
}

/* ---------- process state ---------- */
int g_linux_exited = 0;
int g_linux_exit_code = 0;
static uint64_t g_last_unimplemented = 0;
static uint64_t g_brk = 0;       /* current brk */
static uint64_t g_brk_base = 0;  /* initial brk (end of segments) */
static uint64_t g_brk_cap = 0;   /* brk_base + RING3_HEAP_MAX */
static char g_prog_path[128] = "/usr/bin/static-hello";
static uint64_t g_tid_address = 0; /* set_tid_address() pointer */

/* Tiny read-only in-memory files, mirroring the ones in
 * syscall.c (the FisixOS int 0x80 side) so a Linux binary can
 * open the same two built-in files. */
typedef struct { const char *name; const char *data; uint64_t len; } ls_memfile_t;
static const ls_memfile_t g_memfiles[] = {
    { "/hello.txt", "Hello from FisixOS syscalls\n", 28 },
    { "/sysinfo.txt", "FisixOS syscall ABI v1\n", 23 },
};
#define LS_MEMFILE_COUNT 2

typedef enum { LS_FD_NONE = 0, LS_FD_MEMFILE, LS_FD_DEBFILE, LS_FD_NULL, LS_FD_ZERO } ls_fd_kind_t;
typedef struct {
    int used;
    ls_fd_kind_t kind;
    int file_idx;   /* memfile index, for LS_FD_MEMFILE */
    /* Byte source for LS_FD_MEMFILE / LS_FD_DEBFILE: a pointer
     * into the memfile table or the deb staging pool, plus its
     * length. The pointer is only valid while no deb install /
     * remove runs (single-threaded: nothing installs or removes
     * while a user process is running), and fds are cleared by
     * linux_syscall_reset() at the start of every run. */
    const uint8_t *data;
    uint64_t len;
    uint64_t off;
} ls_fd_t;
#define LS_MAX_FDS 16
static ls_fd_t g_fds[LS_MAX_FDS]; /* index 0 = fd 3 */

typedef struct {
    int used;
    uint64_t addr;
    uint64_t len;
    uint64_t prot; /* Linux PROT_* bits, for the record */
} ls_mmap_t;
#define LS_MAX_MMAPS 32
static ls_mmap_t g_mmaps[LS_MAX_MMAPS];
static uint64_t g_mmap_next = RING3_MMAP_ARENA_BASE;

void linux_syscall_reset(uint64_t brk_base, const char *prog_path)
{
    int i;
    g_linux_exited = 0;
    g_linux_exit_code = 0;
    g_last_unimplemented = 0;
    g_brk_base = brk_base;
    g_brk = brk_base;
    g_brk_cap = brk_base + RING3_HEAP_MAX;
    g_tid_address = 0;
    if (prog_path && prog_path[0])
        ls_strcpy(g_prog_path, prog_path, sizeof(g_prog_path));
    for (i = 0; i < LS_MAX_FDS; i++) {
        g_fds[i].used = 0;
        g_fds[i].kind = LS_FD_NONE;
        g_fds[i].file_idx = -1;
        g_fds[i].data = 0;
        g_fds[i].len = 0;
        g_fds[i].off = 0;
    }
    for (i = 0; i < LS_MAX_MMAPS; i++) {
        g_mmaps[i].used = 0;
        g_mmaps[i].addr = 0;
        g_mmaps[i].len = 0;
        g_mmaps[i].prot = 0;
    }
    g_mmap_next = RING3_MMAP_ARENA_BASE;
}

int linux_syscall_exit_code(void) { return g_linux_exit_code; }

void linux_syscall_note_exit(int code)
{
    g_linux_exited = 1;
    g_linux_exit_code = code;
}

uint64_t linux_syscall_last_unimplemented(void)
{
    return g_last_unimplemented;
}

/* ---------- fd helpers ---------- */
/* Allocate an fd for a file-backed source (memfile or
 * .deb-staged file). data/len point at the file bytes. */
static int ls_fd_alloc_file(ls_fd_kind_t kind, int file_idx,
                            const uint8_t *data, uint64_t len)
{
    int i;
    for (i = 0; i < LS_MAX_FDS; i++) {
        if (!g_fds[i].used) {
            g_fds[i].used = 1;
            g_fds[i].kind = kind;
            g_fds[i].file_idx = file_idx;
            g_fds[i].data = data;
            g_fds[i].len = len;
            g_fds[i].off = 0;
            return 3 + i;
        }
    }
    return -1;
}
static int ls_fd_alloc(ls_fd_kind_t kind, int file_idx)
{
    return ls_fd_alloc_file(kind, file_idx, 0, 0);
}
static ls_fd_t *ls_fd_get(uint64_t fd)
{
    if (fd < 3 || fd >= 3 + LS_MAX_FDS) return 0;
    if (!g_fds[fd - 3].used) return 0;
    return &g_fds[fd - 3];
}

/* What an openable path resolves to. */
typedef struct {
    int kind; /* 0 = not found, 1 = built-in memfile, 2 = .deb-staged
               * file, 3 = /dev/null, 4 = /dev/zero */
    int file_idx;          /* memfile index, for kind 1 */
    const uint8_t *data;   /* file bytes, for kinds 1 and 2 */
    uint64_t len;          /* file length, for kinds 1 and 2 */
} ls_resolved_t;

/* Resolve an openat/open path to a built-in memfile, a
 * .deb-installed file, or a device. Leading "./" is skipped; a
 * missing leading "/" is tolerated. The .deb lookup goes through
 * deb_file_find_by_path() (absolute Unix paths such as
 * /lib/x86_64-linux-gnu/libc.so.6) with the /lib64 symlink
 * alias applied by deb_resolve_lib_path(). Returns 0 and fills
 * *out, or -1 when the path does not exist. */
static int ls_resolve_path(const char *path, ls_resolved_t *out)
{
    int i;
    const uint8_t *data;
    uint32_t len;
    if (!path || !out) return -1;
    out->kind = 0;
    out->file_idx = -1;
    out->data = 0;
    out->len = 0;
    if (path[0] == '.' && path[1] == '/') path += 2;
    if (ls_strcmp(path, "/dev/null") == 0 ||
        ls_strcmp(path, "dev/null") == 0) {
        out->kind = 3;
        return 0;
    }
    if (ls_strcmp(path, "/dev/zero") == 0 ||
        ls_strcmp(path, "dev/zero") == 0) {
        out->kind = 4;
        return 0;
    }
    for (i = 0; i < LS_MEMFILE_COUNT; i++) {
        if (ls_strcmp(path, g_memfiles[i].name) == 0) {
            out->kind = 1;
            out->file_idx = i;
            out->data = (const uint8_t *)g_memfiles[i].data;
            out->len = g_memfiles[i].len;
            return 0;
        }
        if (g_memfiles[i].name[0] == '/' &&
            ls_strcmp(path, g_memfiles[i].name + 1) == 0) {
            out->kind = 1;
            out->file_idx = i;
            out->data = (const uint8_t *)g_memfiles[i].data;
            out->len = g_memfiles[i].len;
            return 0;
        }
    }
    /* .deb-installed files: the dynamic linker's library search
     * path lives here. deb_resolve_lib_path() applies the
     * /lib64/ld-linux symlink alias; plain paths (e.g. a
     * library under /usr/lib/...) go through the same lookup. */
    if (deb_resolve_lib_path(path, &data, &len) == DEB_OK) {
        out->kind = 2;
        out->data = data;
        out->len = len;
        return 0;
    }
    return -1;
}

/* Open the resolved path: allocate an fd for it. Returns the fd,
 * or a negative errno (as a uint64_t). */
static uint64_t ls_open_resolved(const ls_resolved_t *r)
{
    int fd;
    if (r->kind == 3) {
        fd = ls_fd_alloc(LS_FD_NULL, -1);
        return fd < 0 ? ls_err(LINUX_ENOMEM) : (uint64_t)fd;
    }
    if (r->kind == 4) {
        fd = ls_fd_alloc(LS_FD_ZERO, -1);
        return fd < 0 ? ls_err(LINUX_ENOMEM) : (uint64_t)fd;
    }
    if (r->kind == 1) {
        fd = ls_fd_alloc_file(LS_FD_MEMFILE, r->file_idx, r->data, r->len);
        return fd < 0 ? ls_err(LINUX_ENOMEM) : (uint64_t)fd;
    }
    if (r->kind == 2) {
        fd = ls_fd_alloc_file(LS_FD_DEBFILE, -1, r->data, r->len);
        return fd < 0 ? ls_err(LINUX_ENOMEM) : (uint64_t)fd;
    }
    return ls_err(LINUX_ENOENT);
}

/* Copy up to count bytes from a file-backed fd's bytes at
 * file_off into the user buffer at uaddr. Returns the number of
 * bytes copied, or a negative errno (as a uint64_t). Used by
 * read() (fd offset) and pread64() (explicit offset). */
static uint64_t ls_copy_from_fd(ls_fd_t *fd, uint64_t uaddr,
                                uint64_t count, uint64_t file_off)
{
    uint64_t pml4 = ring3_kernel_pml4();
    uint64_t i, n;
    if (fd->kind != LS_FD_MEMFILE && fd->kind != LS_FD_DEBFILE)
        return ls_err(LINUX_EINVAL);
    if (!ring3_user_range_ok(uaddr, count)) return ls_err(LINUX_EFAULT);
    if (file_off >= fd->len) return 0;
    n = fd->len - file_off;
    if (n > count) n = count;
    /* Copy byte-wise through the page tables: the user buffer
     * is a user vaddr, not a kernel pointer (this also keeps
     * the host test honest, where user vaddrs are not host
     * pointers). */
    for (i = 0; i < n; i++) {
        uint64_t phys;
        uint8_t *p;
        if (ring3_translate(pml4, uaddr + i, &phys, 0) != 0)
            return ls_err(LINUX_EFAULT);
        p = (uint8_t *)ring3_phys_to_ptr(phys);
        *p = fd->data[file_off + i];
    }
    return n;
}

/* Map fresh zeroed pages at [addr, addr+len) with the Linux
 * PROT bits in prot (1=R, 2=W, 4=X). When file_data is given,
 * the file bytes at file_off.. are copied in (file-backed
 * mmap); bytes past the end of the file stay zero, like the
 * tail of ld-linux's first mapping of libc. Returns 0 or -1. */
static int ls_mmap_map_range(uint64_t pml4, uint64_t addr, uint64_t len,
                             uint64_t prot, const uint8_t *file_data,
                             uint64_t file_len, uint64_t file_off)
{
    uint64_t p;
    uint64_t fl = RING3_PTE_US;
    if (prot & 2) fl |= RING3_PTE_RW;
    if (!(prot & 4)) fl |= RING3_PTE_NX;
    for (p = addr; p < addr + len; p += 4096) {
        uint64_t frame = ring3_alloc_frame();
        uint8_t *fp;
        uint64_t k;
        if (!frame) return -1;
        fp = (uint8_t *)ring3_phys_to_ptr(frame);
        for (k = 0; k < 4096; k++) fp[k] = 0;
        if (file_data) {
            uint64_t fo = file_off + (p - addr);
            uint64_t n = 0;
            if (fo < file_len) {
                n = file_len - fo;
                if (n > 4096) n = 4096;
                for (k = 0; k < n; k++) fp[k] = file_data[fo + k];
            }
        }
        if (ring3_map_page(pml4, p, frame, fl) != 0) {
            ring3_free_frame(frame);
            return -1;
        }
    }
    return 0;
}

/* Unmap the pages in [addr, addr+len) and cut the range out of
 * the tracked mmap regions covering it (trimming a head or
 * tail, or splitting a region the range lands inside of).
 * Returns 0, or -1 if a split needed a table slot and none was
 * free (the pages are still unmapped; only the bookkeeping is
 * short). Used by MAP_FIXED (replace only the overlapping
 * pages, not the whole reservation) and by munmap (partial
 * unmaps are legal). */
static int ls_mmap_unmap_range(uint64_t pml4, uint64_t addr, uint64_t len)
{
    uint64_t end = addr + len;
    uint64_t i, p;
    for (i = 0; i < LS_MAX_MMAPS; i++) {
        uint64_t s, e, j;
        if (!g_mmaps[i].used) continue;
        s = g_mmaps[i].addr;
        e = s + g_mmaps[i].len;
        if (addr >= e || s >= end) continue; /* no overlap */
        if (addr <= s && end >= e) {
            g_mmaps[i].used = 0; /* whole region covered */
        } else if (addr <= s) {
            g_mmaps[i].addr = end; /* overlap at the head */
            g_mmaps[i].len = e - end;
        } else if (end >= e) {
            g_mmaps[i].len = addr - s; /* overlap at the tail */
        } else {
            /* Interior cut: split into [s, addr) and [end, e).
             * The tail needs a free slot. */
            int tail = -1;
            for (j = 0; j < LS_MAX_MMAPS; j++)
                if (!g_mmaps[j].used) { tail = (int)j; break; }
            if (tail < 0) return -1;
            g_mmaps[tail].used = 1;
            g_mmaps[tail].addr = end;
            g_mmaps[tail].len = e - end;
            g_mmaps[tail].prot = g_mmaps[i].prot;
            g_mmaps[i].len = addr - s;
        }
    }
    for (p = addr; p < end; p += 4096)
        ring3_unmap_page(pml4, p);
    return 0;
}

/* Linux struct stat (x86-64 layout, 144 bytes). Only the
 * fields a caller normally reads are filled. */
typedef struct {
    uint64_t st_dev;
    uint64_t st_ino;
    uint64_t st_nlink;
    uint32_t st_mode;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t __pad0;
    uint64_t st_rdev;
    int64_t st_size;
    int64_t st_blksize;
    int64_t st_blocks;
    int64_t st_atime;
    uint64_t st_atime_nsec;
    int64_t st_mtime;
    uint64_t st_mtime_nsec;
    int64_t st_ctime;
    uint64_t st_ctime_nsec;
    int64_t __unused[3];
} ls_stat_t;

static void ls_fill_stat(ls_stat_t *st, uint32_t mode, int64_t size)
{
    uint64_t i;
    uint8_t *p = (uint8_t *)st;
    for (i = 0; i < sizeof(*st); i++) p[i] = 0;
    st->st_dev = 1;
    st->st_ino = 1;
    st->st_nlink = 1;
    st->st_mode = mode;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_rdev = 0;
    st->st_size = size;
    st->st_blksize = 4096;
    st->st_blocks = (size + 511) / 512;
}

/* Linux struct utsname (6 x 65 bytes). */
typedef struct { char f[6][65]; } ls_utsname_t;

static void ls_fill_utsname(ls_utsname_t *u)
{
    static const char *names[6] = {
        "Linux", "fisixos", "6.0.0-fisix", "#1 FisixOS", "x86_64", ""
    };
    int i, j;
    for (i = 0; i < 6; i++) {
        for (j = 0; j < 65; j++) u->f[i][j] = 0;
        ls_strcpy(u->f[i], names[i], 65);
    }
}

/* ---------- PRNG for getrandom (xorshift64, rdtsc-seeded).
 * NOT cryptographic: there is no entropy source wired up yet.
 * Documented in docs/RING3.md. */
static uint64_t g_rng_state = 0;
static uint64_t ls_rand64(void)
{
    uint64_t x;
    if (!g_rng_state) {
        uint64_t tsc;
        uint32_t tsc_lo, tsc_hi;
        __asm__ volatile ("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
        tsc = ((uint64_t)tsc_hi << 32) | tsc_lo;
        g_rng_state = tsc ? tsc : 0x9E3779B97F4A7C15ull;
    }
    x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    g_rng_state = x;
    return x;
}

/* ---------- the dispatcher ---------- */
uint64_t linux_syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2,
                                uint64_t a3, uint64_t a4, uint64_t a5,
                                uint64_t a6)
{
    uint64_t pml4 = ring3_kernel_pml4();
    switch (num) {
        case LINUX_SYS_READ: { /* 0 */
            ls_fd_t *fd = ls_fd_get(a1);
            uint64_t i;
            if (a1 == 0) return 0; /* stdin: EOF, not wired */
            if (!fd) return ls_err(LINUX_EBADF);
            if (!ring3_user_range_ok(a2, a3)) return ls_err(LINUX_EFAULT);
            if (fd->kind == LS_FD_NULL) return 0;
            if (fd->kind == LS_FD_ZERO) {
                for (i = 0; i < a3; i++) {
                    uint64_t phys;
                    uint8_t *p;
                    if (ring3_translate(pml4, a2 + i, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    *p = 0;
                }
                return a3;
            }
            {
                uint64_t n = ls_copy_from_fd(fd, a2, a3, fd->off);
                if ((int64_t)n < 0) return n;
                fd->off += n;
                return n;
            }
        }
        case LINUX_SYS_PREAD64: { /* 17: read at an offset */
            ls_fd_t *fd = ls_fd_get(a1);
            int64_t off = (int64_t)a4; /* a3 is the count, a4 the offset */
            if (!fd) return ls_err(LINUX_EBADF);
            if (off < 0) return ls_err(LINUX_EINVAL);
            if (fd->kind == LS_FD_NULL) return 0;
            if (fd->kind == LS_FD_ZERO) {
                uint64_t i;
                if (!ring3_user_range_ok(a2, a3)) return ls_err(LINUX_EFAULT);
                for (i = 0; i < a3; i++) {
                    uint64_t phys;
                    uint8_t *p;
                    if (ring3_translate(pml4, a2 + i, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    *p = 0;
                }
                return a3;
            }
            /* The file position is untouched: ld-linux reads ELF
             * headers with pread64 while the fd offset stays 0. */
            return ls_copy_from_fd(fd, a2, a3, (uint64_t)off);
        }
        case LINUX_SYS_WRITE: { /* 1 */
            uint64_t i;
            if (a1 != 1 && a1 != 2) return ls_err(LINUX_EBADF);
            if (!ring3_user_range_ok(a2, a3)) return ls_err(LINUX_EFAULT);
            for (i = 0; i < a3; i++) {
                uint64_t phys;
                uint8_t *p;
                if (ring3_translate(pml4, a2 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                ls_putc((char)*p);
            }
            return a3;
        }
        case LINUX_SYS_WRITEV: { /* 20: write from an iovec array.
            * ld.so prints its errors (and glibc writes stdio
            * output) with writev, so without this the dynamic
            * loader's failure reason is invisible: it exits 127
            * with no message. Same fd 1/2-only serial output as
            * write; each iovec is {base, len} read from user
            * memory. iovcnt is capped at Linux's UIO_MAXIOV. */
            uint64_t i, total = 0;
            if (a1 != 1 && a1 != 2) return ls_err(LINUX_EBADF);
            if (a3 > 1024) return ls_err(LINUX_EINVAL);
            for (i = 0; i < a3; i++) {
                uint64_t base, len, j;
                if (ls_read_user_u64(a2 + i * 16, &base) != 0)
                    return ls_err(LINUX_EFAULT);
                if (ls_read_user_u64(a2 + i * 16 + 8, &len) != 0)
                    return ls_err(LINUX_EFAULT);
                if (len > 0 && !ring3_user_range_ok(base, len))
                    return ls_err(LINUX_EFAULT);
                for (j = 0; j < len; j++) {
                    uint64_t phys;
                    uint8_t *p;
                    if (ring3_translate(pml4, base + j, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    ls_putc((char)*p);
                }
                total += len;
            }
            return total;
        }
        case LINUX_SYS_OPEN: { /* 2 */
            char path[128];
            ls_resolved_t r;
            if (ls_copy_user_str(a1, path, sizeof(path)) < 0)
                return ls_err(LINUX_EFAULT);
            if (ls_resolve_path(path, &r) != 0)
                return ls_err(LINUX_ENOENT);
            return ls_open_resolved(&r);
        }
        case LINUX_SYS_CLOSE: { /* 3 */
            ls_fd_t *fd;
            /* stdin/stdout/stderr are not in the fd table (the
             * serial port), but glibc's close_stdout closes
             * them at exit and fails the program (exit code 1)
             * if the close errors, so answer 0 like Linux. */
            if (a1 <= 2) return 0;
            fd = ls_fd_get(a1);
            if (!fd) return ls_err(LINUX_EBADF);
            fd->used = 0;
            fd->kind = LS_FD_NONE;
            return 0;
        }
        case LINUX_SYS_FSTAT: { /* 5 */
            ls_fd_t *fd = ls_fd_get(a1);
            ls_stat_t st;
            uint64_t phys;
            if (a1 <= 2) {
                /* stdin/stdout/stderr are not in the fd table:
                 * they are the serial port. glibc's hello calls
                 * fstat(1) (gnulib close_stdout path), so answer
                 * with a character device instead of -EBADF. */
                if (!ring3_user_range_ok(a2, sizeof(st))) return ls_err(LINUX_EFAULT);
                ls_fill_stat(&st, 0020600, 0);
            } else {
                if (!fd) return ls_err(LINUX_EBADF);
                if (!ring3_user_range_ok(a2, sizeof(st))) return ls_err(LINUX_EFAULT);
                if (fd->kind == LS_FD_MEMFILE || fd->kind == LS_FD_DEBFILE)
                    ls_fill_stat(&st, 0100644, (int64_t)fd->len);
                else
                    ls_fill_stat(&st, 0020666, 0);
            }
            {
                uint64_t i;
                /* struct stat may straddle a page; copy byte-wise. */
                for (i = 0; i < sizeof(st); i++) {
                    uint8_t *p;
                    if (ring3_translate(pml4, a2 + i, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    *p = ((uint8_t *)&st)[i];
                }
            }
            return 0;
        }
        case LINUX_SYS_LSEEK: { /* 8 */
            ls_fd_t *fd = ls_fd_get(a1);
            int64_t off = (int64_t)a2;
            uint64_t len;
            if (!fd) return ls_err(LINUX_EBADF);
            if (fd->kind != LS_FD_MEMFILE && fd->kind != LS_FD_DEBFILE) return 0;
            len = fd->len;
            if (a3 == 0) { /* SEEK_SET */
                if (off < 0) return ls_err(LINUX_EINVAL);
                fd->off = (uint64_t)off;
            } else if (a3 == 1) { /* SEEK_CUR */
                int64_t n = (int64_t)fd->off + off;
                if (n < 0) return ls_err(LINUX_EINVAL);
                fd->off = (uint64_t)n;
            } else if (a3 == 2) { /* SEEK_END */
                int64_t n = (int64_t)len + off;
                if (n < 0) return ls_err(LINUX_EINVAL);
                fd->off = (uint64_t)n;
            } else {
                return ls_err(LINUX_EINVAL);
            }
            return fd->off;
        }
        case LINUX_SYS_MMAP: { /* 9 */
            uint64_t len = (a2 + 0xFFF) & ~0xFFFull;
            uint64_t prot = a3, flags = a4;
            uint64_t addr, i;
            int slot = -1;
            int fixed;
            /* File-backed mmap (the dynamic linker's whole job):
             * the file bytes are copied into fresh pages at map
             * time. There is no page cache and no write-back: a
             * MAP_SHARED file mapping behaves like MAP_PRIVATE
             * here (writes stay in the process's pages). The
             * .deb staging pool is read-only to the user process
             * anyway, so nothing is lost. MAP_DENYWRITE (0x800,
             * which ld-linux passes) is ignored. */
            const uint8_t *file_data = 0;
            uint64_t file_len = 0, file_off = 0;
            if (len == 0) return ls_err(LINUX_EINVAL);
            if (len > RING3_MMAP_ARENA_SIZE) return ls_err(LINUX_ENOMEM);
            if (!(flags & 0x20)) { /* not MAP_ANONYMOUS */
                ls_fd_t *fd = ls_fd_get(a5);
                if (!fd) return ls_err(LINUX_EBADF);
                if (fd->kind != LS_FD_MEMFILE &&
                    fd->kind != LS_FD_DEBFILE)
                    return ls_err(LINUX_ENODEV);
                /* ld-linux maps segments whose vaddr/offset are
                 * not page-aligned (libc's data segment starts
                 * at 0x1cf8d0): it rounds both down to the page,
                 * and the ELF invariant (vaddr ≡ offset mod
                 * 4096) keeps the bytes aligned, so the byte at
                 * vaddr V is the file byte at offset O. Round
                 * the offset down the same way; a MAP_FIXED pair
                 * whose page offsets differ is -EINVAL, like
                 * Linux (whose mmap requires a page-aligned
                 * offset). */
                if ((flags & 0x10) && a1 != 0 &&
                    (a1 & 0xFFF) != (a6 & 0xFFF))
                    return ls_err(LINUX_EINVAL);
                file_data = fd->data;
                file_len = fd->len;
                file_off = a6 & ~0xFFFull;
            }
            fixed = (flags & 0x10) && a1 != 0; /* MAP_FIXED */
            if (fixed) {
                addr = a1 & ~0xFFFull;
                if (addr < RING3_MMAP_ARENA_BASE ||
                    addr + len > RING3_MMAP_ARENA_BASE + RING3_MMAP_ARENA_SIZE)
                    return ls_err(LINUX_ENOMEM);
                /* Replace only the overlapping pages: ld-linux
                 * maps each libc segment MAP_FIXED inside the
                 * reservation it made with the first mmap, so
                 * dropping the whole reservation would destroy
                 * the segments mapped before this one. */
                if (ls_mmap_unmap_range(pml4, addr, len) != 0)
                    return ls_err(LINUX_ENOMEM);
            } else {
                addr = g_mmap_next;
                /* The bump pointer can land inside a MAP_FIXED
                 * region: ld-linux's anonymous bss mapping runs
                 * past the end of its reservation, so the next
                 * anonymous mmap would overlap it. Skip past
                 * every tracked region the range overlaps, like
                 * the Linux allocator picking a free address. */
                for (;;) {
                    uint64_t adv = addr;
                    for (i = 0; i < LS_MAX_MMAPS; i++) {
                        uint64_t s, e;
                        if (!g_mmaps[i].used) continue;
                        s = g_mmaps[i].addr;
                        e = s + g_mmaps[i].len;
                        if (addr < e && s < addr + len && e > adv)
                            adv = e;
                    }
                    if (adv == addr) break;
                    addr = adv;
                }
                if (addr + len > RING3_MMAP_ARENA_BASE + RING3_MMAP_ARENA_SIZE)
                    return ls_err(LINUX_ENOMEM);
            }
            for (i = 0; i < LS_MAX_MMAPS; i++)
                if (!g_mmaps[i].used) { slot = (int)i; break; }
            if (slot < 0) return ls_err(LINUX_ENOMEM);
            if (ls_mmap_map_range(pml4, addr, len, prot, file_data,
                                  file_len, file_off) != 0)
                return ls_err(LINUX_ENOMEM);
            if (!fixed) g_mmap_next = addr + len;
            g_mmaps[slot].used = 1;
            g_mmaps[slot].addr = addr;
            g_mmaps[slot].len = len;
            g_mmaps[slot].prot = prot;
            return addr;
        }
        case LINUX_SYS_MPROTECT: { /* 10 */
            uint64_t len = (a2 + 0xFFF) & ~0xFFFull;
            uint64_t fl = RING3_PTE_US;
            if (a2 == 0) return ls_err(LINUX_ENOMEM);
            if (a3 & 2) fl |= RING3_PTE_RW;
            if (!(a3 & 4)) fl |= RING3_PTE_NX;
            if (ring3_protect(pml4, a1, len, fl) != 0)
                return ls_err(LINUX_ENOMEM);
            return 0;
        }
        case LINUX_SYS_MUNMAP: { /* 11 */
            uint64_t len = (a2 + 0xFFF) & ~0xFFFull;
            uint64_t addr = a1 & ~0xFFFull;
            if (len == 0) return ls_err(LINUX_EINVAL);
            if (addr < RING3_MMAP_ARENA_BASE ||
                addr + len > RING3_MMAP_ARENA_BASE + RING3_MMAP_ARENA_SIZE)
                return ls_err(LINUX_EINVAL);
            /* Partial unmaps are legal: the range is cut out of
             * whatever tracked regions it overlaps. ld-linux
             * unmaps its ld.so.cache mapping with the unrounded
             * length, and unmaps reservation tails the same way.
             * Unmapping pages that were never mapped is a
             * no-op, like Linux. */
            if (ls_mmap_unmap_range(pml4, addr, len) != 0)
                return ls_err(LINUX_ENOMEM);
            return 0;
        }
        case LINUX_SYS_BRK: { /* 12 */
            uint64_t want;
            if (a1 == 0) return g_brk;
            want = (a1 + 0xFFF) & ~0xFFFull;
            if (want < g_brk_base) return g_brk; /* shrinking below base: no-op */
            if (want > g_brk_cap) return g_brk;  /* past the cap: no-op */
            if (want > ((g_brk + 0xFFF) & ~0xFFFull)) {
                /* Grow: map the new pages (zeroed). Like Linux,
                 * the returned break is the requested address,
                 * not the page-rounded one. */
                uint64_t p;
                for (p = (g_brk + 0xFFF) & ~0xFFFull; p < want; p += 4096) {
                    uint64_t frame = ring3_alloc_frame();
                    uint8_t *fp;
                    uint64_t k;
                    if (!frame) return g_brk;
                    fp = (uint8_t *)ring3_phys_to_ptr(frame);
                    for (k = 0; k < 4096; k++) fp[k] = 0;
                    if (ring3_map_page(pml4, p, frame,
                                       RING3_PTE_US | RING3_PTE_RW |
                                       RING3_PTE_NX) != 0) {
                        ring3_free_frame(frame);
                        return g_brk;
                    }
                }
            }
            /* Shrinking: keep the pages mapped (cheap) and just
             * move the break down. Unmapping on shrink is a
             * later optimisation. */
            g_brk = a1;
            return g_brk;
        }
        case LINUX_SYS_RT_SIGACTION: /* 13: stub, no signals */
        case LINUX_SYS_RT_SIGPROCMASK: /* 14: stub, no signals */
            /* glibc's NPTL init installs handlers for SIGCANCEL
             * etc. via rt_sigaction; there are no signals in
             * FisixOS yet, so claiming success is the honest
             * stub: nothing is actually installed, and no signal
             * can ever be delivered. */
            return 0;
        case LINUX_SYS_IOCTL: /* 16: stub */
            return ls_err(LINUX_ENOTTY);
        case LINUX_SYS_ACCESS: /* 21: nothing exists outside the
                                * memfiles / .deb files, and
                                * openat() is the real check */
            return ls_err(LINUX_ENOENT);
        case LINUX_SYS_SCHED_YIELD: /* 24 */
            return 0;
        case LINUX_SYS_MADVISE: /* 28: stub, advice ignored */
            return 0;
        case LINUX_SYS_NANOSLEEP: /* 35: stub, no sleep */
            return 0;
        case LINUX_SYS_GETPID: /* 39 */
            return 1;
        case LINUX_SYS_EXIT: /* 60 */
            linux_syscall_note_exit((int)(a1 & 0xFF));
            return a1;
        case LINUX_SYS_UNAME: { /* 63 */
            ls_utsname_t u;
            uint64_t phys, i;
            if (!ring3_user_range_ok(a1, sizeof(u))) return ls_err(LINUX_EFAULT);
            ls_fill_utsname(&u);
            for (i = 0; i < sizeof(u); i++) {
                uint8_t *p;
                if (ring3_translate(pml4, a1 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                *p = ((uint8_t *)&u)[i];
            }
            return 0;
        }
        case LINUX_SYS_GETCWD: { /* 79 */
            if (a2 < 2) return ls_err(LINUX_EINVAL);
            if (!ring3_user_range_ok(a1, 2)) return ls_err(LINUX_EFAULT);
            {
                uint64_t phys;
                uint8_t *p;
                if (ring3_translate(pml4, a1, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                p[0] = '/';
                if (ring3_translate(pml4, a1 + 1, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                p[0] = 0;
            }
            return 2;
        }
        case LINUX_SYS_CHDIR: /* 80: stub, no filesystem yet */
            return 0;
        case LINUX_SYS_GETTIMEOFDAY: { /* 96: stub, zero time */
            uint64_t i;
            if (a1 && !ring3_user_range_ok(a1, 16)) return ls_err(LINUX_EFAULT);
            if (a2 && !ring3_user_range_ok(a2, 8)) return ls_err(LINUX_EFAULT);
            if (a1) {
                for (i = 0; i < 16; i++) {
                    uint64_t phys;
                    uint8_t *p;
                    if (ring3_translate(pml4, a1 + i, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    *p = 0;
                }
            }
            return 0;
        }
        case LINUX_SYS_SYSINFO: { /* 99: stub, plausible totals */
            uint64_t total = memory_total_bytes();
            uint64_t i;
            if (!ring3_user_range_ok(a1, 112)) return ls_err(LINUX_EFAULT);
            for (i = 0; i < 112; i++) {
                uint64_t phys;
                uint8_t *p;
                if (ring3_translate(pml4, a1 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                *p = 0;
            }
            /* sysinfo layout: uptime, loads[3], totalram,
             * freeram, ... Write totalram/freeram (bytes 32
             * and 40) as 8-byte values. */
            {
                uint64_t phys;
                uint8_t *p;
                uint64_t v;
                int k;
                if (ring3_translate(pml4, a1 + 32, &phys, 0) == 0) {
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    v = total;
                    for (k = 0; k < 8; k++) p[k] = (uint8_t)(v >> (8 * k));
                }
                if (ring3_translate(pml4, a1 + 40, &phys, 0) == 0) {
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    v = total / 2;
                    for (k = 0; k < 8; k++) p[k] = (uint8_t)(v >> (8 * k));
                }
            }
            return 0;
        }
        case LINUX_SYS_GETUID: /* 102 */
        case LINUX_SYS_GETGID: /* 104 */
        case LINUX_SYS_GETEUID: /* 107 */
        case LINUX_SYS_GETEGID: /* 108 */
            return 0;
        case LINUX_SYS_GETPPID: /* 110 */
            return 0;
        case LINUX_SYS_ARCH_PRCTL: { /* 158 */
            if (a1 == 0x1001) return ring3_get_fs_base(); /* GET_FS */
            if (a1 == 0x1002) { /* SET_FS */
                ring3_set_fs_base(a2);
                return 0;
            }
            return ls_err(LINUX_EINVAL);
        }
        case LINUX_SYS_REBOOT: /* 169: never */
            return ls_err(LINUX_EPERM);
        case LINUX_SYS_GETTID: /* 186: one thread, pid 1 */
            return 1;
        case LINUX_SYS_TIME: /* 201: stub, zero */
            if (a1) {
                uint64_t phys;
                uint8_t *p;
                int k;
                if (!ring3_user_range_ok(a1, 8)) return ls_err(LINUX_EFAULT);
                if (ring3_translate(pml4, a1, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                for (k = 0; k < 8; k++) p[k] = 0;
            }
            return 0;
        case LINUX_SYS_FUTEX: /* 202: stub, no threads */
            /* glibc only calls futex on lock contention, and
             * there is only one thread here, so a futex call
             * that does arrive is uncontended: return 0. A
             * FUTEX_WAIT that ever really waited would spin
             * instead of sleeping; that path is not reachable
             * single-threaded. */
            return 0;
        case LINUX_SYS_SET_TID_ADDRESS: { /* 218 */
            g_tid_address = a1;
            return 1; /* the "tid": pid 1 */
        }
        case LINUX_SYS_CLOCK_GETTIME: { /* 228: stub, zero time */
            uint64_t i;
            if (a1 != 0 && a1 != 1 && a1 != 2) return ls_err(LINUX_EINVAL);
            if (!ring3_user_range_ok(a2, 16)) return ls_err(LINUX_EFAULT);
            for (i = 0; i < 16; i++) {
                uint64_t phys;
                uint8_t *p;
                if (ring3_translate(pml4, a2 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                *p = 0;
            }
            return 0;
        }
        case LINUX_SYS_EXIT_GROUP: /* 231 */
            linux_syscall_note_exit((int)(a1 & 0xFF));
            return a1;
        case LINUX_SYS_OPENAT: { /* 257 */
            char path[128];
            ls_resolved_t r;
            (void)a5; (void)a6;
            /* dirfd: AT_FDCWD (-100) only. A real fd as dirfd
             * is not supported: memfiles have no directories.
             * The dirfd is a 32-bit int: ld.so passes AT_FDCWD
             * zero-extended (0xFFFFFF9C), so compare the
             * truncated 32-bit value, as Linux does, or every
             * openat fails with -EBADF. */
            if ((int32_t)a1 != -100 && !ls_fd_get(a1))
                return ls_err(LINUX_EBADF);
            if (ls_copy_user_str(a2, path, sizeof(path)) < 0)
                return ls_err(LINUX_EFAULT);
            if (ls_resolve_path(path, &r) != 0)
                return ls_err(LINUX_ENOENT);
            return ls_open_resolved(&r);
        }
        case LINUX_SYS_NEWFSTATAT: { /* 262 */
            char path[128];
            ls_resolved_t r;
            ls_stat_t st;
            uint64_t phys, i;
            /* AT_EMPTY_PATH (0x1000) with an empty path stats
             * the dirfd itself: glibc's fstatat() uses it. */
            if ((int32_t)a1 != -100 && !ls_fd_get(a1))
                return ls_err(LINUX_EBADF);
            if (ls_copy_user_str(a2, path, sizeof(path)) < 0)
                return ls_err(LINUX_EFAULT);
            if (!ring3_user_range_ok(a3, sizeof(st))) return ls_err(LINUX_EFAULT);
            if (path[0] == 0 && (a4 & 0x1000)) {
                ls_fd_t *fd = ls_fd_get(a1);
                if (!fd) return ls_err(LINUX_EBADF);
                if (fd->kind == LS_FD_MEMFILE || fd->kind == LS_FD_DEBFILE)
                    ls_fill_stat(&st, 0100644, (int64_t)fd->len);
                else
                    ls_fill_stat(&st, 0020666, 0);
            } else if (ls_resolve_path(path, &r) == 0) {
                if (r.kind == 3 || r.kind == 4)
                    ls_fill_stat(&st, 0020666, 0);
                else
                    ls_fill_stat(&st, 0100644, (int64_t)r.len);
            } else {
                return ls_err(LINUX_ENOENT);
            }
            for (i = 0; i < sizeof(st); i++) {
                uint8_t *p;
                if (ring3_translate(pml4, a3 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                *p = ((uint8_t *)&st)[i];
            }
            return 0;
        }
        case LINUX_SYS_READLINKAT: { /* 267 */
            char path[128];
            uint64_t n;
            if ((int32_t)a1 != -100 && !ls_fd_get(a1))
                return ls_err(LINUX_EBADF);
            if (ls_copy_user_str(a2, path, sizeof(path)) < 0)
                return ls_err(LINUX_EFAULT);
            if (ls_strcmp(path, "/proc/self/exe") != 0)
                return ls_err(LINUX_ENOENT);
            n = ls_strlen(g_prog_path);
            if (n > a4) n = a4;
            if (!ring3_user_range_ok(a3, n)) return ls_err(LINUX_EFAULT);
            {
                uint64_t i;
                for (i = 0; i < n; i++) {
                    uint64_t phys;
                    uint8_t *p;
                    if (ring3_translate(pml4, a3 + i, &phys, 0) != 0)
                        return ls_err(LINUX_EFAULT);
                    p = (uint8_t *)ring3_phys_to_ptr(phys);
                    *p = (uint8_t)g_prog_path[i];
                }
            }
            return n;
        }
        case LINUX_SYS_SET_ROBUST_LIST: /* 273: stub */
            return 0;
        case LINUX_SYS_STATX: /* 332: not implemented; glibc
                               * falls back to newfstatat */
            g_last_unimplemented = num;
            return ls_err(LINUX_ENOSYS);
        case LINUX_SYS_RSEQ: /* 334: stub */
            return 0;
        case LINUX_SYS_PRLIMIT64: /* 302: stub */
            return 0;
        case LINUX_SYS_GETRANDOM: { /* 318 */
            uint64_t i, r = 0;
            if (!ring3_user_range_ok(a1, a2)) return ls_err(LINUX_EFAULT);
            for (i = 0; i < a2; i++) {
                uint64_t phys;
                uint8_t *p;
                if (i % 8 == 0) r = ls_rand64();
                if (ring3_translate(pml4, a1 + i, &phys, 0) != 0)
                    return ls_err(LINUX_EFAULT);
                p = (uint8_t *)ring3_phys_to_ptr(phys);
                *p = (uint8_t)(r >> (8 * (i % 8)));
            }
            return a2;
        }
        default: {
            g_last_unimplemented = num;
            /* Log straight to COM1, bypassing the output hook,
             * so this never pollutes a captured process output
             * (the boot self-test compares its capture exactly). */
            {
                char nbuf[21];
                int ni = 20;
                uint64_t nv = num;
                const char *pfx = "[FisixOS] LINUX SYSCALL UNIMPLEMENTED ";
                nbuf[ni] = 0;
                if (nv == 0) nbuf[--ni] = '0';
                while (nv > 0 && ni > 0) { nbuf[--ni] = (char)('0' + (nv % 10)); nv /= 10; }
                while (*pfx) ls_serial_putc(*pfx++);
                while (nbuf[ni]) ls_serial_putc(nbuf[ni++]);
                ls_serial_putc('\n');
            }
            return ls_err(LINUX_ENOSYS);
        }
    }
}

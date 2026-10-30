#include "syscall.h"
#include "system/idt.h"

extern void syscall_entry(void);

static void serial_putc(char c)
{
    uint32_t to = 100000; uint8_t lsr;
    do { __asm__ volatile ("inb %1, %0" : "=a"(lsr) : "Nd"((uint16_t)(0x3F8 + 5))); if (to-- == 0) return; } while ((lsr & 0x20) == 0);
    __asm__ volatile ("outb %0, %1" : : "a"((uint8_t)c), "Nd"((uint16_t)0x3F8));
}
static void serial_write(const char *s, uint64_t len)
{
    uint64_t i; for (i = 0; i < len; i++) serial_putc(s[i]);
}

/* Tiny read-only in-memory filesystem for syscall open/read. This is
   not a disk filesystem: it lets a syscall-using program open and
   read two built-in files today, while the disk FS (FAT/FisixFF
   on NVMe) is wired into the kernel in a later stage. */
typedef struct { const char *name; const char *data; uint64_t len; } memfile_t;
static const memfile_t g_memfiles[] = {
    { "/hello.txt", "Hello from FisixOS syscalls\n", 28 },
    { "/sysinfo.txt", "FisixOS syscall ABI v1\n", 23 },
};
typedef struct { int file_idx; uint64_t off; int used; } memfd_t;
static memfd_t g_fds[8];
static uint8_t g_heap[65536];
static uint64_t g_heap_used = 0;
static int g_exited = 0;
static int g_exit_code = 0;

static int str_eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
uint64_t syscall_dispatch(uint64_t num, uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5)
{
    (void)a4; (void)a5;
    switch (num) {
        case SYS_EXIT:
            g_exited = 1; g_exit_code = (int)a1; return a1;
        case SYS_WRITE: {
            const char *buf = (const char *)(uintptr_t)a2;
            if ((a1 != 1 && a1 != 2) || !buf) return (uint64_t)-1;
            serial_write(buf, a3); return a3;
        }
        case SYS_READ: {
            uint8_t *buf = (uint8_t *)(uintptr_t)a2;
            uint64_t f, i, n;
            if (a1 == 0) return 0; /* keyboard not wired to syscalls yet */
            if (a1 < 3 || !buf) return (uint64_t)-1;
            f = a1 - 3;
            if (f >= 8 || !g_fds[f].used) return (uint64_t)-1;
            {
                const memfile_t *mf = &g_memfiles[g_fds[f].file_idx];
                if (g_fds[f].off >= mf->len) return 0;
                n = mf->len - g_fds[f].off;
                if (n > a3) n = a3;
                for (i = 0; i < n; i++) buf[i] = (uint8_t)mf->data[g_fds[f].off + i];
                g_fds[f].off += n;
                return n;
            }
        }
        case SYS_OPEN: {
            const char *path = (const char *)(uintptr_t)a1;
            uint64_t i, f;
            if (!path) return (uint64_t)-1;
            for (i = 0; i < 2; i++) if (str_eq(path, g_memfiles[i].name)) {
                for (f = 0; f < 8; f++) if (!g_fds[f].used) {
                    g_fds[f].used = 1; g_fds[f].file_idx = (int)i; g_fds[f].off = 0;
                    return 3 + f;
                }
                return (uint64_t)-1;
            }
            return (uint64_t)-1;
        }
        case SYS_CLOSE: {
            uint64_t f = a1 - 3;
            if (a1 < 3 || f >= 8 || !g_fds[f].used) return (uint64_t)-1;
            g_fds[f].used = 0; return 0;
        }
        case SYS_SBRK: {
            uint64_t old = g_heap_used;
            if (a1 > sizeof(g_heap) - g_heap_used) return (uint64_t)-1;
            g_heap_used += a1;
            return (uint64_t)(uintptr_t)(g_heap + old);
        }
        case SYS_GETPID: return 1;
        case SYS_YIELD: return 0;
        default: return (uint64_t)-1;
    }
}
void syscall_init(void)
{
    /* Gate 0x80, present interrupt gate, DPL 0 for now: the kernel
       runs in ring 0 and there is no ring-3 user mode, GDT, or
       user stack yet, so a DPL-3 gate (0xEE) would be a lie until
       that exists. Ring-3 programs cannot use int 0x80 until then. */
    idt_set_gate(0x80, (uint64_t)syscall_entry, idt_current_cs(), 0x8E);
}
int syscall_selftest(void)
{
    uint64_t ret = 0;
    const char *msg = "SYSCALL OK\n";
    __asm__ volatile (
        "movq $1, %%rax\n"
        "movq $1, %%rdi\n"
        "movq %1, %%rsi\n"
        "movq $11, %%rdx\n"
        "int $0x80\n"
        "movq %%rax, %0\n"
        : "=r"(ret) : "r"(msg) : "rax", "rdi", "rsi", "rdx", "rcx", "r8", "r9", "r10", "r11", "memory");
    return ret == 11;
}
int syscall_was_exit(void) { return g_exited; }
int syscall_exit_code(void) { return g_exit_code; }

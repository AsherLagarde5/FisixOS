/* ring3.c - ring-3 user-mode execution for FisixOS.
 *
 * See ring3.h / docs/RING3.md. Design references: LetsGo-OS
 * InitUserMode (GDT user segments + TSS) and Asher's Rust OS
 * project jump_to_user_mode (iretq, CS 0x23 / SS 0x1B), both
 * ported to C here; no code copied verbatim, no profanity.
 *
 * Freestanding: no libc, no heap. All state is static.
 */
#include "ring3.h"
#include "linux_syscall.h"
#include "deb.h"
#include "deb_embedded.h"
#include "system/idt.h"

extern uint8_t kernel_end[];

/* ---------- serial debug (COM1, same pattern as kernel.c) ---------- */
/* The port helpers are only used by the real serial putc below;
 * the host test build has no COM1, so they are compiled out. */
#ifndef FISIX_HOST_TEST
static void r3_outb(uint16_t port, uint8_t value)
{
    __asm__ volatile ("outb %0, %1" : : "a"(value), "Nd"(port));
}
static uint8_t r3_inb(uint16_t port)
{
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
#endif
static void r3_serial_putc(char c)
{
#ifdef FISIX_HOST_TEST
    /* Host test: no COM1 here; raw inb/outb would fault in
     * userspace. The host test prints its own CHECK lines. */
    (void)c;
    return;
#else
    uint32_t to = 100000;
    uint8_t lsr;
    do {
        lsr = r3_inb(0x3F8 + 5);
        if (to-- == 0) return;
    } while ((lsr & 0x20) == 0);
    r3_outb(0x3F8, (uint8_t)c);
#endif
}
static void r3_print(const char *s)
{
    while (s && *s) r3_serial_putc(*s++);
}
static void r3_print_u64(uint64_t v)
{
    char buf[21];
    int i = 20;
    buf[i] = 0;
    if (v == 0) { r3_print("0"); return; }
    while (v > 0 && i > 0) { buf[--i] = (char)('0' + (v % 10)); v /= 10; }
    r3_print(&buf[i]);
}
static void r3_print_hex(uint64_t v)
{
    const char *d = "0123456789abcdef";
    char buf[16];
    int i = 0, j;
    if (v == 0) { r3_print("0x0"); return; }
    while (v > 0 && i < 16) { buf[i++] = d[v & 15]; v >>= 4; }
    r3_print("0x");
    for (j = i - 1; j >= 0; j--) {
        char c[2];
        c[0] = buf[j];
        c[1] = 0;
        r3_print(c);
    }
}

/* ---------- local memory helpers (no libc) ---------- */
static void r3_memset(void *dst, uint8_t v, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    uint64_t i;
    for (i = 0; i < n; i++) d[i] = v;
}
static void r3_memcpy(void *dst, const void *src, uint64_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    uint64_t i;
    for (i = 0; i < n; i++) d[i] = s[i];
}
static uint32_t r3_strlen(const char *s)
{
    uint32_t n = 0;
    while (s && s[n]) n++;
    return n;
}

/* ---------- MSRs ---------- */
#define MSR_EFER 0xC0000080u
#define MSR_STAR 0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_FMASK 0xC0000084u
#define MSR_FS_BASE 0xC0000100u

static uint64_t r3_rdmsr(uint32_t msr)
{
    uint32_t lo, hi;
    __asm__ volatile ("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((uint64_t)hi << 32) | lo;
}
static void r3_wrmsr(uint32_t msr, uint64_t v)
{
    __asm__ volatile ("wrmsr" : : "a"((uint32_t)v), "d"((uint32_t)(v >> 32)),
                      "c"(msr));
}

/* ---------- GDT + TSS ---------- */
/* GDT layout (selector = index * 8 | RPL):
 *   0x00 null
 *   0x08 kernel code (64-bit, DPL 0)
 *   0x10 kernel data (DPL 0)
 *   0x18 user data   (DPL 3; SS 0x1B = 0x18 | 3)
 *   0x20 user code   (DPL 3; CS 0x23 = 0x20 | 3)
 *   0x28 TSS (16-byte descriptor, occupies slots 5 and 6)
 * STAR is programmed so SYSCALL loads CS 0x08 / SS 0x10 and
 * SYSRET loads CS 0x23 / SS 0x1B: STAR[47:32] = 0x08,
 * STAR[63:48] = 0x13 (SYSRET CS = 0x13 + 16 = 0x23,
 * SS = 0x13 + 8 = 0x1B). */
static uint64_t g_gdt[8];
static struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) g_gdtr;

typedef struct {
    uint32_t reserved0;
    uint64_t rsp0, rsp1, rsp2;
    uint64_t reserved1;
    uint64_t ist1, ist2, ist3, ist4, ist5, ist6, ist7;
    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iomap_base;
} __attribute__((packed)) ring3_tss_t;

static ring3_tss_t g_tss;
static uint8_t g_irq_stack[16384] __attribute__((aligned(16)));
static uint16_t g_tr_selector = 0x28;

static uint64_t gdt_entry(uint32_t base, uint32_t limit, uint8_t access,
                          uint8_t flags)
{
    uint64_t e = 0;
    e |= (uint64_t)(limit & 0xFFFF);
    e |= (uint64_t)(base & 0xFFFFFF) << 16;
    e |= (uint64_t)access << 40;
    e |= (uint64_t)((limit >> 16) & 0xF) << 48;
    e |= (uint64_t)(flags & 0xF) << 52;
    e |= (uint64_t)((base >> 24) & 0xFF) << 56;
    return e;
}

static void ring3_gdt_init(void)
{
    uint64_t tss_base = (uint64_t)(uintptr_t)&g_tss;
    uint32_t tss_limit = (uint32_t)sizeof(g_tss) - 1;
    g_gdt[0] = 0;
    /* Kernel code: access 0x9A, flags 0xA (G=1, L=1, D/B=0). */
    g_gdt[1] = gdt_entry(0, 0xFFFFF, 0x9A, 0xA);
    /* Kernel data: access 0x92, flags 0xC (G=1, D/B=1). */
    g_gdt[2] = gdt_entry(0, 0xFFFFF, 0x92, 0xC);
    /* User data: access 0xF2 (DPL 3), flags 0xC. */
    g_gdt[3] = gdt_entry(0, 0xFFFFF, 0xF2, 0xC);
    /* User code: access 0xFA (DPL 3), flags 0xA. */
    g_gdt[4] = gdt_entry(0, 0xFFFFF, 0xFA, 0xA);
    /* TSS: type 0x9 (available 64-bit TSS), present, DPL 0. */
    g_gdt[5] = gdt_entry((uint32_t)tss_base, tss_limit, 0x89, 0x0);
    g_gdt[6] = tss_base >> 32;
    g_gdt[7] = 0;
    g_gdtr.limit = (uint16_t)(sizeof(g_gdt) - 1);
    g_gdtr.base = (uint64_t)(uintptr_t)g_gdt;
    /* TSS: rsp0 is the stack interrupts switch to when they
     * fire in ring 3. iomap_base = size of the TSS means "no
     * I/O permission bitmap": user code cannot use in/out at
     * all (any attempt faults with #GP). */
    r3_memset(&g_tss, 0, sizeof(g_tss));
    g_tss.rsp0 = (uint64_t)(uintptr_t)(g_irq_stack + sizeof(g_irq_stack));
    g_tss.iomap_base = (uint16_t)sizeof(g_tss);
}

/* ---------- frame allocator ---------- */
#define RING3_MAX_SEGMENTS 32
typedef struct { uint64_t start, end; } r3_segment_t;
static r3_segment_t g_segments[RING3_MAX_SEGMENTS];
static int g_seg_count = 0;
static int g_seg_cur = 0;
static uint64_t g_bump = 0;
static uint64_t g_free_list = 0; /* phys of head; next ptr in frame[0] */
static uint64_t g_frames_total = 0;
static uint64_t g_frames_free = 0;
static int g_frame_ready = 0;

static int ring3_frame_init(const kernel_boot_info_t *info)
{
    uint32_t i;
    uint64_t kend = (uint64_t)(uintptr_t)kernel_end;
    g_seg_count = 0;
    g_seg_cur = 0;
    g_bump = 0;
    g_free_list = 0;
    g_frames_total = 0;
    g_frames_free = 0;
    if (!info || info->mem_range_count == 0) return -1;
    for (i = 0; i < info->mem_range_count; i++) {
        uint64_t start, end;
        if (info->mem_ranges[i].type != KERNEL_MEM_CONVENTIONAL)
            continue;
        start = info->mem_ranges[i].base;
        end = start + info->mem_ranges[i].pages * 4096ull;
        if (start < 0x100000) start = 0x100000; /* skip legacy low RAM */
        /* Never hand out the loader image or the kernel image:
         * everything below kernel_end is off limits. */
        if (start < kend) start = kend;
        start = (start + 0xFFF) & ~0xFFFull;
        end &= ~0xFFFull;
        if (end <= start) continue;
        if (g_seg_count >= RING3_MAX_SEGMENTS) break;
        g_segments[g_seg_count].start = start;
        g_segments[g_seg_count].end = end;
        g_seg_count++;
        g_frames_total += (end - start) / 4096;
    }
    if (g_seg_count == 0) return -1;
    g_bump = g_segments[0].start;
    g_frames_free = g_frames_total;
    g_frame_ready = 1;
    return 0;
}

/* Host-test entry point: same init, no hardware touched. The
 * host test fakes kernel_end by linking its own symbol. */
int ring3_frame_init_for_test(const kernel_boot_info_t *info)
{
    return ring3_frame_init(info);
}

uint64_t ring3_alloc_frame(void)
{
    uint64_t f;
    if (!g_frame_ready) return 0;
    if (g_free_list) {
        f = g_free_list;
        g_free_list = *(uint64_t *)ring3_phys_to_ptr(f);
        g_frames_free--;
        return f;
    }
    while (g_seg_cur < g_seg_count) {
        if (g_bump + 4096 <= g_segments[g_seg_cur].end) {
            f = g_bump;
            g_bump += 4096;
            g_frames_free--;
            return f;
        }
        g_seg_cur++;
        if (g_seg_cur < g_seg_count)
            g_bump = g_segments[g_seg_cur].start;
    }
    return 0;
}

void ring3_free_frame(uint64_t phys)
{
    if (!phys || !g_frame_ready) return;
    *(uint64_t *)ring3_phys_to_ptr(phys) = g_free_list;
    g_free_list = phys;
    g_frames_free++;
}

uint64_t ring3_frames_total(void) { return g_frames_total; }
uint64_t ring3_frames_free(void) { return g_frames_free; }

/* ---------- page tables ---------- */
static uint64_t g_pml4_phys = 0;
static int g_hw_ready = 0;
static uint64_t g_fs_base_shadow = 0;

/* User pages mapped for the current process, so the next run
 * can unmap them. (vaddr only; the frame is freed on unmap.) */
#define RING3_MAX_USER_PAGES 8192
static uint64_t g_user_pages[RING3_MAX_USER_PAGES];
static int g_user_page_count = 0;

static void r3_invlpg(uint64_t vaddr)
{
    /* Privileged: a no-op on the host test, where the hardware
     * is not up (g_hw_ready == 0) and there is no TLB to flush. */
    if (!g_hw_ready) return;
    __asm__ volatile ("invlpg (%0)" :: "r"(vaddr) : "memory");
}

static uint64_t r3_alloc_zeroed_frame(void)
{
    uint64_t f = ring3_alloc_frame();
    if (f) r3_memset(ring3_phys_to_ptr(f), 0, 4096);
    return f;
}

/* Split a 2 MiB page-table entry into a 4 KiB page table whose
 * entries identity-map the same 2 MiB (supervisor, RW). Used
 * when a user mapping lands inside the 2 MiB-mapped region
 * (user stack, mmap arena). pd_entry points at the PD entry
 * (in identity-mapped memory). Returns 0 or -1. */
static int ring3_split_2mb(uint64_t *pd_entry)
{
    uint64_t old = *pd_entry;
    uint64_t base, pt_phys, *pt;
    uint64_t i;
    if (!(old & RING3_PTE_P) || !(old & (1ull << 7))) return 0; /* not a 2M page */
    base = old & RING3_PTE_ADDR_MASK_2MB;
    pt_phys = r3_alloc_zeroed_frame();
    if (!pt_phys) return -1;
    pt = (uint64_t *)ring3_phys_to_ptr(pt_phys);
    for (i = 0; i < 512; i++)
        pt[i] = (base + i * 4096) | RING3_PTE_P | RING3_PTE_RW;
    /* The PT pointer is an intermediate entry: it needs U/S so
     * a user-mode walk can reach the PT. The leaf entries stay
     * supervisor identity mappings (only explicitly mapped user
     * pages get U/S). */
    *pd_entry = pt_phys | RING3_PTE_P | RING3_PTE_RW | RING3_PTE_US;
    return 0;
}

/* Walk to the page-table entry for vaddr. If create != 0,
 * allocate missing intermediate tables (and split 2 MiB pages
 * on the way down). Returns a pointer to the PT entry, or 0. */
static uint64_t *ring3_walk(uint64_t pml4, uint64_t vaddr, int create)
{
    uint64_t *table, entry;
    int level;
    uint64_t idx;
    table = (uint64_t *)ring3_phys_to_ptr(pml4);
    for (level = 4; level > 1; level--) {
        idx = (vaddr >> (12 + 9 * (level - 1))) & 0x1FF;
        entry = table[idx];
        if (!(entry & RING3_PTE_P)) {
            uint64_t f;
            if (!create) return 0;
            f = r3_alloc_zeroed_frame();
            if (!f) return 0;
            /* New intermediate table: U/S so a user-mode walk
             * can traverse it (see pagetables_build). */
            table[idx] = f | RING3_PTE_P | RING3_PTE_RW | RING3_PTE_US;
            entry = table[idx];
        }
        if (level == 2 && (entry & (1ull << 7))) {
            /* 2 MiB page at the PD level. */
            if (!create) return 0;
            if (ring3_split_2mb(&table[idx]) != 0) return 0;
            entry = table[idx];
        }
        table = (uint64_t *)ring3_phys_to_ptr(entry & RING3_PTE_ADDR_MASK);
    }
    idx = (vaddr >> 12) & 0x1FF;
    return &table[idx];
}

int ring3_map_page(uint64_t pml4, uint64_t vaddr, uint64_t phys,
                   uint64_t flags)
{
    uint64_t *e = ring3_walk(pml4, vaddr, 1);
    if (!e) return -1;
    *e = (phys & RING3_PTE_ADDR_MASK) | (flags & (RING3_PTE_RW | RING3_PTE_US |
                                        RING3_PTE_NX)) | RING3_PTE_P;
    r3_invlpg(vaddr);
    if (flags & RING3_PTE_US) {
        if (g_user_page_count < RING3_MAX_USER_PAGES)
            g_user_pages[g_user_page_count++] = vaddr & ~0xFFFull;
    }
    return 0;
}

int ring3_unmap_page(uint64_t pml4, uint64_t vaddr)
{
    uint64_t *e = ring3_walk(pml4, vaddr, 0);
    uint64_t old;
    if (!e || !(*e & RING3_PTE_P)) return -1;
    /* Only a USER page may be unmapped here: an identity-mapped
     * supervisor page (which shares these vaddrs after a 2 MiB
     * split) must never have its PTE cleared or its frame
     * freed — the frame belongs to the identity map, not the
     * frame allocator, and freeing it would corrupt the
     * allocator and punch a hole in the kernel's own mapping.
     * The syscall layer's MAP_FIXED / munmap paths call this
     * for ranges that may cover never-mapped arena pages, so
     * a supervisor page here is "nothing to unmap", not an
     * error worth acting on. */
    if (!(*e & RING3_PTE_US)) return -1;
    old = *e & RING3_PTE_ADDR_MASK;
    *e = 0;
    r3_invlpg(vaddr);
    if (old) ring3_free_frame(old);
    return 0;
}

int ring3_translate(uint64_t pml4, uint64_t vaddr, uint64_t *phys_out,
                    uint64_t *flags_out)
{
    uint64_t *table, entry;
    int level;
    uint64_t idx;
    table = (uint64_t *)ring3_phys_to_ptr(pml4);
    for (level = 4; level > 1; level--) {
        idx = (vaddr >> (12 + 9 * (level - 1))) & 0x1FF;
        entry = table[idx];
        if (!(entry & RING3_PTE_P)) return -1;
        if (level == 2 && (entry & (1ull << 7))) {
            /* 2 MiB page. */
            if (phys_out)
                *phys_out = (entry & RING3_PTE_ADDR_MASK_2MB) + (vaddr & 0x1FFFFF);
            if (flags_out) *flags_out = entry;
            return 0;
        }
        table = (uint64_t *)ring3_phys_to_ptr(entry & RING3_PTE_ADDR_MASK);
    }
    idx = (vaddr >> 12) & 0x1FF;
    entry = table[idx];
    if (!(entry & RING3_PTE_P)) return -1;
    if (phys_out) *phys_out = (entry & RING3_PTE_ADDR_MASK) + (vaddr & 0xFFF);
    if (flags_out) *flags_out = entry;
    return 0;
}

int ring3_protect(uint64_t pml4, uint64_t vaddr, uint64_t len,
                  uint64_t flags)
{
    uint64_t p, end = (vaddr + len + 0xFFF) & ~0xFFFull;
    uint64_t keep = RING3_PTE_RW | RING3_PTE_US | RING3_PTE_NX;
    for (p = vaddr & ~0xFFFull; p < end; p += 4096) {
        uint64_t *e = ring3_walk(pml4, p, 1);
        if (!e || !(*e & RING3_PTE_P)) return -1;
        *e = (*e & ~keep) | (flags & keep);
        r3_invlpg(p);
    }
    return 0;
}

/* Returns 1 if vaddr is mapped through a walk whose EVERY level
 * (PML4E, PDPTE, PDE, PTE) carries U/S, i.e. a user-mode access
 * can actually reach it. The leaf flags go to flags_out. The
 * plain translate() only reports the leaf, which once hid a bug
 * where the intermediate entries were supervisor-only and user
 * code faulted on a leaf that looked fine. */
int ring3_user_walk_ok(uint64_t pml4, uint64_t vaddr, uint64_t *flags_out)
{
    uint64_t *table, entry;
    int level;
    uint64_t idx;
    table = (uint64_t *)ring3_phys_to_ptr(pml4);
    for (level = 4; level > 1; level--) {
        idx = (vaddr >> (12 + 9 * (level - 1))) & 0x1FF;
        entry = table[idx];
        if (!(entry & RING3_PTE_P)) return 0;
        if (!(entry & RING3_PTE_US)) return 0;
        if (level == 2 && (entry & (1ull << 7))) {
            if (flags_out) *flags_out = entry;
            return 1;
        }
        table = (uint64_t *)ring3_phys_to_ptr(entry & RING3_PTE_ADDR_MASK);
    }
    idx = (vaddr >> 12) & 0x1FF;
    entry = table[idx];
    if (!(entry & RING3_PTE_P)) return 0;
    if (!(entry & RING3_PTE_US)) return 0;
    if (flags_out) *flags_out = entry;
    return 1;
}

int ring3_user_range_ok(uint64_t vaddr, uint64_t len)
{
    uint64_t p, end;
    if (len == 0) return 1;
    if (vaddr + len < vaddr) return 0; /* wrap */
    end = (vaddr + len + 0xFFF) & ~0xFFFull;
    for (p = vaddr & ~0xFFFull; p < end; p += 4096) {
        uint64_t phys, flags;
        /* A user access needs U/S at every level, not just the
         * leaf: check the whole walk, not just translate(). */
        (void)phys;
        if (!ring3_user_walk_ok(g_pml4_phys, p, &flags))
            return 0;
    }
    return 1;
}

uint64_t ring3_kernel_pml4(void) { return g_pml4_phys; }

/* Build the kernel/user page tables and switch CR3 to them:
 *   0..128 MiB: 4 KiB identity pages (kernel image, user ELF
 *     area 0x400000, brk heap live here)
 *   128 MiB..4 GiB: 2 MiB identity pages (framebuffer, MMIO)
 * Everything is supervisor-only until a user mapping is added
 * on top with ring3_map_page(). The build half is also used by
 * the host test (ring3_pagetables_build, no CR3 switch there).
 * Returns 0 or -1. */
int ring3_pagetables_build(void)
{
    uint64_t pml4, pdpt, pd[4], pt[64];
    uint64_t *t;
    uint64_t i, j;
    pml4 = r3_alloc_zeroed_frame();
    pdpt = r3_alloc_zeroed_frame();
    if (!pml4 || !pdpt) return -1;
    for (i = 0; i < 4; i++) {
        pd[i] = r3_alloc_zeroed_frame();
        if (!pd[i]) return -1;
    }
    for (i = 0; i < 64; i++) {
        pt[i] = r3_alloc_zeroed_frame();
        if (!pt[i]) return -1;
    }
    /* PML4[0] -> PDPT. The intermediate entries (PML4E, PDPTE,
     * PDE pointing at a PT) must all carry U/S: a user-mode
     * access faults unless EVERY level of the walk is marked
     * user, even when the leaf PTE itself is user. The leaf
     * 2 MiB identity pages below stay supervisor-only, so user
     * code still cannot read kernel memory through them. */
    t = (uint64_t *)ring3_phys_to_ptr(pml4);
    t[0] = pdpt | RING3_PTE_P | RING3_PTE_RW | RING3_PTE_US;
    /* PDPT[0..3] -> PDs. */
    t = (uint64_t *)ring3_phys_to_ptr(pdpt);
    for (i = 0; i < 4; i++)
        t[i] = pd[i] | RING3_PTE_P | RING3_PTE_RW | RING3_PTE_US;
    /* PD[0]: entries 0..63 -> 4 KiB page tables (0..128 MiB),
     * entries 64..511 -> 2 MiB pages. */
    t = (uint64_t *)ring3_phys_to_ptr(pd[0]);
    for (i = 0; i < 64; i++)
        t[i] = pt[i] | RING3_PTE_P | RING3_PTE_RW | RING3_PTE_US;
    for (i = 64; i < 512; i++)
        t[i] = (i * 0x200000ull) | RING3_PTE_P | RING3_PTE_RW |
               (1ull << 7);
    /* PD[1..3]: all 2 MiB pages. */
    for (j = 1; j < 4; j++) {
        t = (uint64_t *)ring3_phys_to_ptr(pd[j]);
        for (i = 0; i < 512; i++)
            t[i] = (j * 0x40000000ull + i * 0x200000ull) |
                   RING3_PTE_P | RING3_PTE_RW | (1ull << 7);
    }
    /* The 4 KiB tables: identity-map 0..128 MiB. */
    for (i = 0; i < 64; i++) {
        t = (uint64_t *)ring3_phys_to_ptr(pt[i]);
        for (j = 0; j < 512; j++)
            t[j] = (i * 0x200000ull + j * 4096) |
                   RING3_PTE_P | RING3_PTE_RW;
    }
    g_pml4_phys = pml4;
    return 0;
}

static int ring3_pagetables_init(void)
{
    if (ring3_pagetables_build() != 0) return -1;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(g_pml4_phys) : "memory");
    return 0;
}

/* ---------- SYSCALL MSRs ---------- */
static void ring3_syscall_msrs_init(void)
{
    uint64_t efer = r3_rdmsr(MSR_EFER);
    r3_wrmsr(MSR_EFER, efer | 1); /* EFER.SCE */
    /* STAR: SYSCALL CS 0x08 / SS 0x10, SYSRET CS 0x23 / SS 0x1B. */
    r3_wrmsr(MSR_STAR, (0x13ull << 48) | (0x08ull << 32));
    r3_wrmsr(MSR_LSTAR, (uint64_t)(uintptr_t)linux_syscall_entry);
    /* FMASK: clear IF and DF on SYSCALL entry. */
    r3_wrmsr(MSR_FMASK, 0x600);
}

int ring3_init(const kernel_boot_info_t *info)
{
    if (ring3_frame_init(info) != 0) {
        r3_print("[FisixOS] RING3: no usable conventional memory, "
                 "ring-3 unavailable\n");
        return -1;
    }
    ring3_gdt_init();
    ring3_load_gdt(&g_gdtr);
    __asm__ volatile ("ltr %0" :: "m"(g_tr_selector));
    if (ring3_pagetables_init() != 0) {
        r3_print("[FisixOS] RING3: page table setup failed\n");
        return -1;
    }
    ring3_syscall_msrs_init();
    g_hw_ready = 1;
    r3_print("[FisixOS] RING3: GDT+TSS, page tables, SYSCALL MSRs up; ");
    r3_print_u64(g_frames_total);
    r3_print(" frames total\n");
    return 0;
}

int ring3_ready(void) { return g_hw_ready; }

void ring3_install_fault_gates(void)
{
    uint16_t cs = idt_current_cs();
    idt_set_gate(14, (uint64_t)ring3_fault_pf, cs, 0x8E);
    idt_set_gate(13, (uint64_t)ring3_fault_gp, cs, 0x8E);
}

void ring3_set_fs_base(uint64_t base)
{
    g_fs_base_shadow = base;
    if (g_hw_ready) r3_wrmsr(MSR_FS_BASE, base);
}
uint64_t ring3_get_fs_base(void) { return g_fs_base_shadow; }

/* ---------- fault handling ---------- */
static int g_ring3_active = 0;
static int g_ring3_faulted = 0;

int ring3_fault_handler(uint64_t vec, uint64_t err, uint64_t cr2,
                        uint64_t cs, uint64_t rip,
                        const uint64_t *regs)
{
    r3_print("[FisixOS] RING3 FAULT vec=");
    r3_print_u64(vec);
    r3_print(" err=");
    r3_print_hex(err);
    r3_print(" cr2=");
    r3_print_hex(cr2);
    r3_print(" cs=");
    r3_print_hex(cs);
    r3_print(" rip=");
    r3_print_hex(rip);
    if (regs) {
        /* Saved registers, in push order (r15 at [0] .. rax
         * at [14]): rax=[14] rbx=[13] rcx=[12] rdx=[11]
         * rsi=[10] rdi=[9] rbp=[8] r8=[7] r9=[6] r10=[5]
         * r11=[4] r12=[3] r13=[2] r14=[1] r15=[0]. */
        r3_print(" regs: rax=");
        r3_print_hex(regs[14]);
        r3_print(" rbx=");
        r3_print_hex(regs[13]);
        r3_print(" rcx=");
        r3_print_hex(regs[12]);
        r3_print(" rdx=");
        r3_print_hex(regs[11]);
        r3_print(" rsi=");
        r3_print_hex(regs[10]);
        r3_print(" rdi=");
        r3_print_hex(regs[9]);
        r3_print(" rbp=");
        r3_print_hex(regs[8]);
        r3_print(" r8=");
        r3_print_hex(regs[7]);
        r3_print(" r9=");
        r3_print_hex(regs[6]);
        r3_print(" r10=");
        r3_print_hex(regs[5]);
        r3_print(" r11=");
        r3_print_hex(regs[4]);
        r3_print(" r12=");
        r3_print_hex(regs[3]);
        r3_print(" r13=");
        r3_print_hex(regs[2]);
        r3_print(" r14=");
        r3_print_hex(regs[1]);
        r3_print(" r15=");
        r3_print_hex(regs[0]);
        /* The faulting link_map is in RBP (ld.so keeps the
         * current link_map there while resolving). Dump its
         * l_addr (+0), l_ld (+0x10, dynamic section) and
         * l_info[DT_STRTAB] (+0x68) through the process page
         * tables: l_addr names the object (0x40000000 = main
         * exe, 0x50000000 = ld.so itself, 0x10000000+ = libc). */
        {
            uint64_t lm = regs[8];
            uint64_t offs[3] = { 0x0, 0x10, 0x68 };
            int oi;
            r3_print(" linkmap=");
            r3_print_hex(lm);
            for (oi = 0; oi < 3; oi++) {
                uint64_t phys = 0, fl = 0, v = 0;
                int i;
                if (ring3_translate(g_pml4_phys, lm + offs[oi], &phys, &fl) == 0) {
                    uint8_t *p = (uint8_t *)ring3_phys_to_ptr(phys);
                    for (i = 0; i < 8; i++) v |= (uint64_t)p[i] << (8 * i);
                } else {
                    v = 0xFFFFFFFFFFFFFFFFull;
                }
                r3_print(" [");
                r3_print_hex(offs[oi]);
                r3_print("]=");
                r3_print_hex(v);
            }
            /* Dump the first 4 dynamic entries at l_ld (+0x10):
             * if they are zeros, the mapped dynamic section has
             * no content (mmap file copy failed) and the parse
             * found no DT_STRTAB; if they match the file's
             * entries, the parse itself is the problem. */
            {
                uint64_t dyn = 0;
                uint64_t phys = 0, fl = 0;
                int i;
                if (ring3_translate(g_pml4_phys, lm + 0x10, &phys, &fl) == 0) {
                    uint8_t *p = (uint8_t *)ring3_phys_to_ptr(phys);
                    for (i = 0; i < 8; i++) dyn |= (uint64_t)p[i] << (8 * i);
                }
                r3_print(" dyn@");
                r3_print_hex(dyn);
                r3_print(":");
                for (i = 0; i < 4; i++) {
                    uint64_t tag = 0, val = 0;
                    int k;
                    if (ring3_translate(g_pml4_phys, dyn + (uint64_t)i * 16, &phys, &fl) == 0) {
                        uint8_t *p = (uint8_t *)ring3_phys_to_ptr(phys);
                        for (k = 0; k < 8; k++) tag |= (uint64_t)p[k] << (8 * k);
                    } else { tag = 0xFFFFFFFFFFFFFFFFull; }
                    if (ring3_translate(g_pml4_phys, dyn + (uint64_t)i * 16 + 8, &phys, &fl) == 0) {
                        uint8_t *p = (uint8_t *)ring3_phys_to_ptr(phys);
                        for (k = 0; k < 8; k++) val |= (uint64_t)p[k] << (8 * k);
                    } else { val = 0xFFFFFFFFFFFFFFFFull; }
                    r3_print(" (");
                    r3_print_hex(tag);
                    r3_print(",");
                    r3_print_hex(val);
                    r3_print(")");
                }
            }
        }
    }
    if (vec == 14) {
        /* Dump what the page tables actually say for the faulting
         * address: the entry the CPU saw, at fault time. */
        uint64_t fphys = 0, fflags = 0;
        int ftr = ring3_translate(g_pml4_phys, cr2, &fphys, &fflags);
        uint64_t cr3v = 0;
        __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3v));
        r3_print(" pte_tr=");
        r3_print_u64((uint64_t)ftr);
        r3_print(" pte_phys=");
        r3_print_hex(fphys);
        r3_print(" pte_flags=");
        r3_print_hex(fflags);
        r3_print(" cr3=");
        r3_print_hex(cr3v);
        r3_print(" pml4=");
        r3_print_hex(g_pml4_phys);
    }
    r3_print("\n");
    if ((cs & 3) != 3) {
        /* A fault in ring 0 is a kernel bug: never resume. */
        r3_print("[FisixOS] RING3 FAULT in kernel mode, halting\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }
    if (!g_ring3_active) {
        /* A ring-3 fault with no process running cannot happen
         * through the normal entry path; halt rather than
         * jumping to a stale kernel stack. */
        r3_print("[FisixOS] RING3 FAULT with no active process, halting\n");
        for (;;) { __asm__ volatile ("hlt"); }
    }
    g_ring3_faulted = 1;
    linux_syscall_note_exit(139);
    return 1; /* kill the user process */
}

int ring3_last_faulted(void) { return g_ring3_faulted; }
int ring3_last_exit_code(void) { return linux_syscall_exit_code(); }

/* ---------- ELF loading into user pages ---------- */
static uint16_t r3_rd16(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t r3_rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r3_rd64(const uint8_t *p)
{
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; i--) v = (v << 8) | p[i];
    return v;
}

/* One program header. */
typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset, vaddr, filesz, memsz;
} r3_phdr_t;

static int r3_phdr(const uint8_t *data, uint32_t len, uint32_t idx,
                   r3_phdr_t *ph)
{
    uint64_t phoff;
    uint16_t phentsize, phnum;
    const uint8_t *p;
    if (len < 64) return -1;
    phoff = r3_rd64(data + 32);
    phentsize = r3_rd16(data + 54);
    phnum = r3_rd16(data + 56);
    if (phentsize < 56 || idx >= phnum) return -1;
    if (phoff + (uint64_t)idx * phentsize + 56 > len) return -1;
    p = data + phoff + (uint64_t)idx * phentsize;
    ph->type = r3_rd32(p);
    ph->flags = r3_rd32(p + 4);
    ph->offset = r3_rd64(p + 8);
    ph->vaddr = r3_rd64(p + 16);
    ph->filesz = r3_rd64(p + 32);
    ph->memsz = r3_rd64(p + 40);
    return 0;
}

static void r3_strcpy(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = 0;
    if (!cap) return;
    while (src[i] && i + 1 < cap) { dst[i] = src[i]; i++; }
    dst[i] = 0;
}

/* Unmap + free every user page from the previous run, so the
 * next process starts with a clean address space. */
static void ring3_clear_user(void)
{
    int i;
    for (i = 0; i < g_user_page_count; i++)
        ring3_unmap_page(g_pml4_phys, g_user_pages[i]);
    g_user_page_count = 0;
}

/* Write 8 bytes at a user vaddr (kernel writes through the
 * identity map after translating). Returns 0 or -1. */
static int r3_write_u64(uint64_t vaddr, uint64_t val)
{
    uint64_t phys;
    uint8_t *p;
    if (ring3_translate(g_pml4_phys, vaddr, &phys, 0) != 0) return -1;
    p = (uint8_t *)ring3_phys_to_ptr(phys);
    {
        int i;
        for (i = 0; i < 8; i++) p[i] = (uint8_t)(val >> (8 * i));
    }
    return 0;
}

static int r3_write_bytes(uint64_t vaddr, const void *src, uint64_t n)
{
    uint64_t i;
    const uint8_t *s = (const uint8_t *)src;
    for (i = 0; i < n; i++) {
        uint64_t phys;
        uint8_t *p;
        if (ring3_translate(g_pml4_phys, vaddr + i, &phys, 0) != 0)
            return -1;
        p = (uint8_t *)ring3_phys_to_ptr(phys);
        *p = s[i];
    }
    return 0;
}

static void r3_strcat(char *dst, const char *src, uint32_t cap)
{
    uint32_t i = r3_strlen(dst), j = 0;
    if (!cap) return;
    while (src[j] && i + 1 < cap) { dst[i++] = src[j++]; }
    dst[i] = 0;
}

/* Map every PT_LOAD segment of the ELF at (base + p_vaddr):
 * per-page frames, file bytes copied, .bss zeroed. base is 0
 * for a static ET_EXEC, RING3_DYN_MAIN_BASE /
 * RING3_DYN_INTERP_BASE for a PIE executable / its
 * interpreter. On success fills *min_out / *max_out (the
 * covered vaddr range, base included) and *seg_count. Returns
 * 0 or -1 with a reason in err; on failure the caller must
 * ring3_clear_user(). This is the segment loop ring3_load_elf
 * used to inline, extracted so the dynamic path can load two
 * ELFs (main + interpreter) with the same code. */
static int r3_load_segments(const uint8_t *data, uint32_t len,
                            uint64_t base, uint64_t *min_out,
                            uint64_t *max_out, int *seg_count,
                            char *err, uint32_t err_cap)
{
    uint16_t phnum = r3_rd16(data + 56);
    uint16_t i;
    uint64_t min_vaddr = 0xFFFFFFFFFFFFFFFFull, max_vaddr = 0;
    r3_phdr_t ph;
    for (i = 0; i < phnum; i++) {
        if (r3_phdr(data, len, i, &ph) != 0) continue;
        if (ph.type != 1) continue; /* PT_LOAD */
        if (base + ph.vaddr < min_vaddr) min_vaddr = base + ph.vaddr;
        if (base + ph.vaddr + ph.memsz > max_vaddr)
            max_vaddr = base + ph.vaddr + ph.memsz;
    }
    if (min_vaddr == 0xFFFFFFFFFFFFFFFFull || max_vaddr <= min_vaddr) {
        if (err) r3_strcpy(err, "ELF has no PT_LOAD segments", err_cap);
        return -1;
    }
    *seg_count = 0;
    for (i = 0; i < phnum; i++) {
        uint64_t p, page_start, page_end;
        uint64_t flags;
        uint64_t vaddr, filesz, memsz, offset;
        if (r3_phdr(data, len, i, &ph) != 0) continue;
        if (ph.type != 1) continue;
        vaddr = base + ph.vaddr;
        filesz = ph.filesz;
        memsz = ph.memsz;
        offset = ph.offset;
        /* PF_X=1, PF_W=2, PF_R=4. */
        flags = RING3_PTE_US;
        if (ph.flags & 2) flags |= RING3_PTE_RW;
        if (!(ph.flags & 1)) flags |= RING3_PTE_NX;
        page_start = vaddr & ~0xFFFull;
        page_end = (vaddr + memsz + 0xFFF) & ~0xFFFull;
        for (p = page_start; p < page_end; p += 4096) {
            uint64_t phys, old_flags, frame;
            /* "Present" only counts if the page is already a
             * USER page from an earlier segment (overlapping
             * PT_LOADs share it). The identity map has these
             * vaddrs present as SUPERVISOR pages: reusing one
             * of those would write segment data into the
             * identity frame instead of a fresh user frame. */
            int present = (ring3_translate(g_pml4_phys, p, &phys, &old_flags) == 0) &&
                          (old_flags & RING3_PTE_US);
            uint64_t lo, hi, zlo, zhi;
            if (present) {
                /* Page already mapped by an earlier segment
                 * (overlapping PT_LOADs): reuse the frame and
                 * widen the permissions to the union. */
                uint64_t combo = old_flags;
                if (flags & RING3_PTE_RW) combo |= RING3_PTE_RW;
                if (!(flags & RING3_PTE_NX)) combo &= ~RING3_PTE_NX;
                ring3_protect(g_pml4_phys, p, 4096, combo);
                frame = phys & RING3_PTE_ADDR_MASK;
            } else {
                frame = ring3_alloc_frame();
                if (!frame) {
                    if (err) r3_strcpy(err, "out of frames loading ELF", err_cap);
                    return -1;
                }
                r3_memset(ring3_phys_to_ptr(frame), 0, 4096);
                if (ring3_map_page(g_pml4_phys, p, frame, flags) != 0) {
                    ring3_free_frame(frame);
                    if (err) r3_strcpy(err, "page map failed loading ELF", err_cap);
                    return -1;
                }
            }
            /* File bytes overlapping this page. */
            lo = p > vaddr ? p : vaddr;
            hi = p + 4096 < vaddr + filesz ? p + 4096
                                           : vaddr + filesz;
            if (hi > lo) {
                uint64_t file_off = offset + (lo - vaddr);
                uint64_t dst_off = lo - p;
                if (file_off + (hi - lo) > len) {
                    if (err) r3_strcpy(err, "PT_LOAD past end of file", err_cap);
                    return -1;
                }
                r3_memcpy((uint8_t *)ring3_phys_to_ptr(frame) + dst_off,
                          data + file_off, hi - lo);
            }
            /* .bss bytes in this page (only needed for a reused
             * page; a fresh frame is already zero). */
            if (present) {
                zlo = p > vaddr + filesz ? p : vaddr + filesz;
                zhi = p + 4096 < vaddr + memsz ? p + 4096
                                               : vaddr + memsz;
                if (zhi > zlo)
                    r3_memset((uint8_t *)ring3_phys_to_ptr(frame) + (zlo - p),
                              0, zhi - zlo);
            }
        }
        (*seg_count)++;
    }
    *min_out = min_vaddr;
    *max_out = max_vaddr;
    return 0;
}

int ring3_load_elf(const uint8_t *data, uint32_t len,
                   ring3_proc_t *proc, char *err, uint32_t err_cap)
{
    char reason[192];
    deb_elf_kind_t kind, interp_kind;
    uint16_t phnum, i;
    uint64_t min_vaddr = 0, max_vaddr = 0;
    uint64_t entry, phoff, phent;
    uint64_t stack_str, stack_rnd, sp_base, w;
    uint64_t main_base = 0, interp_base = 0, interp_entry = 0;
    const uint8_t *interp_data = 0;
    uint32_t interp_len = 0;
    int seg_count = 0;
    r3_phdr_t ph;
    if (!data || !proc) {
        if (err) r3_strcpy(err, "null argument", err_cap);
        return -1;
    }
    if (!g_hw_ready && !g_pml4_phys) {
        /* The host test builds the page tables without the
         * hardware bring-up; the kernel always has both. */
        if (err) r3_strcpy(err, "ring-3 page tables not set up", err_cap);
        return -1;
    }
    kind = deb_elf_classify(data, len, reason, sizeof(reason));
    if (kind != DEB_ELF_STATIC && kind != DEB_ELF_DYNAMIC) {
        if (err) r3_strcpy(err, reason[0] ? reason : "not a static or dynamic x86-64 ELF", err_cap);
        return -1;
    }
    phnum = r3_rd16(data + 56);
    phoff = r3_rd64(data + 32);
    phent = r3_rd16(data + 54);
    entry = r3_rd64(data + 24);
    if (kind == DEB_ELF_DYNAMIC) {
        /* Dynamic: read the PT_INTERP path out of the file,
         * then resolve the interpreter from .deb-installed
         * files (deb_resolve_lib_path() applies the /lib64
         * symlink alias). The interpreter is the real
         * ld-linux from the installed libc6 .deb; the kernel
         * does not implement a linker of its own, it just
         * loads ld-linux and lets it do the linking. */
        char interp_path[128];
        int found = 0;
        for (i = 0; i < phnum; i++) {
            if (r3_phdr(data, len, i, &ph) != 0) continue;
            if (ph.type == 3) { /* PT_INTERP */
                uint32_t n = ph.filesz < sizeof(interp_path) - 1
                    ? (uint32_t)ph.filesz : sizeof(interp_path) - 1;
                uint32_t k;
                for (k = 0; k < n && ph.offset + k < len; k++)
                    interp_path[k] = (char)data[ph.offset + k];
                interp_path[n] = 0;
                found = 1;
            }
        }
        if (!found) {
            if (err) r3_strcpy(err, "dynamic ELF has no PT_INTERP", err_cap);
            return -1;
        }
        if (deb_resolve_lib_path(interp_path, &interp_data,
                                 &interp_len) != DEB_OK) {
            if (err) {
                r3_strcpy(err, "interpreter not installed: ", err_cap);
                r3_strcat(err, interp_path, err_cap);
            }
            return -1;
        }
        interp_kind = deb_elf_classify(interp_data, interp_len,
                                       reason, sizeof(reason));
        if (interp_kind == DEB_ELF_NOT_ELF ||
            interp_kind == DEB_ELF_OTHER ||
            interp_kind == DEB_ELF_FSE) {
            if (err) r3_strcpy(err, "interpreter is not a loadable x86-64 ELF", err_cap);
            return -1;
        }
        main_base = RING3_DYN_MAIN_BASE;
        interp_base = RING3_DYN_INTERP_BASE;
        interp_entry = interp_base + r3_rd64(interp_data + 24);
    }
    ring3_clear_user();
    if (r3_load_segments(data, len, main_base, &min_vaddr, &max_vaddr,
                         &seg_count, err, err_cap) != 0) {
        ring3_clear_user();
        return -1;
    }
    proc->seg_count = seg_count;
    if (kind == DEB_ELF_DYNAMIC) {
        int iseg = 0;
        uint64_t imin = 0, imax = 0;
        if (r3_load_segments(interp_data, interp_len, interp_base,
                             &imin, &imax, &iseg, err, err_cap) != 0) {
            ring3_clear_user();
            return -1;
        }
        proc->seg_count += iseg;
        proc->interp_base = interp_base;
        proc->main_entry = main_base + entry;
        /* The kernel jumps to the interpreter, not the program:
         * ld-linux relocates the program, runs its init, then
         * jumps to the main entry itself. */
        proc->entry = interp_entry;
    } else {
        proc->interp_base = 0;
        proc->main_entry = entry;
        proc->entry = entry;
    }
    proc->brk_base = (max_vaddr + 0xFFF) & ~0xFFFull;
    proc->phdr_addr = min_vaddr + phoff;
    proc->phnum = phnum;
    proc->phent = phent;
    proc->pml4 = g_pml4_phys;
    /* User stack: 64 KiB at the top of the user address space,
     * mirroring the 0x7FFF0000 stack in Asher's Rust OS project
     * elf.rs. Initial stack contents are the Linux ABI layout:
     * argc, argv, envp NULL, then the auxv (same entries as
     * deb_elf_load(): AT_PAGESZ, AT_UID/GID/EUID/EGID, AT_ENTRY,
     * AT_PHDR, AT_PHENT, AT_PHNUM, AT_RANDOM, AT_NULL), plus
     * the dynamic-linker entries: AT_BASE (interpreter load
     * base, dynamic only), AT_HWCAP / AT_HWCAP2 (0: generic
     * IFUNC implementations), AT_CLKTCK (100), AT_SECURE (0),
     * AT_EXECFN (argv[0]). AT_ENTRY / AT_PHDR always describe
     * the MAIN executable, never the interpreter. */
    {
        uint64_t p;
        for (p = RING3_USER_STACK_TOP - RING3_USER_STACK_SIZE;
             p < RING3_USER_STACK_TOP; p += 4096) {
            uint64_t frame = ring3_alloc_frame();
            if (!frame) {
                if (err) r3_strcpy(err, "out of frames for user stack", err_cap);
                ring3_clear_user();
                return -1;
            }
            r3_memset(ring3_phys_to_ptr(frame), 0, 4096);
            if (ring3_map_page(g_pml4_phys, p, frame,
                               RING3_PTE_US | RING3_PTE_RW |
                               RING3_PTE_NX) != 0) {
                ring3_free_frame(frame);
                if (err) r3_strcpy(err, "page map failed for user stack", err_cap);
                ring3_clear_user();
                return -1;
            }
        }
    }
    stack_str = RING3_USER_STACK_TOP - 0x100; /* argv[0] string */
    stack_rnd = RING3_USER_STACK_TOP - 0x80;  /* AT_RANDOM bytes */
    /* argc goes here. 0x300 below the top so the auxv (up to
     * 19 pairs = 304 bytes) fits between the block base and
     * the argv[0] string without overwriting it. */
    sp_base = RING3_USER_STACK_TOP - 0x300;   /* argc goes here */
    /* AT_RANDOM: 16 bytes of not-cryptographic randomness
     * (rdtsc-seeded; getrandom() in the dispatcher is the same).
     * The stack page is already zeroed, so writing the bytes is
     * enough; the exact values do not matter to glibc. */
    {
        uint64_t tsc;
        uint32_t tsc_lo, tsc_hi;
        uint8_t rnd[16];
        int k;
        __asm__ volatile ("rdtsc" : "=a"(tsc_lo), "=d"(tsc_hi));
        tsc = ((uint64_t)tsc_hi << 32) | tsc_lo;
        for (k = 0; k < 16; k++) {
            tsc ^= tsc << 13;
            tsc ^= tsc >> 7;
            tsc ^= tsc << 17;
            rnd[k] = (uint8_t)tsc;
        }
        if (r3_write_bytes(stack_rnd, rnd, sizeof(rnd)) != 0) {
            if (err) r3_strcpy(err, "stack write failed", err_cap);
            ring3_clear_user();
            return -1;
        }
    }
    if (r3_write_bytes(stack_str, proc->prog_path,
                       r3_strlen(proc->prog_path) + 1) != 0) {
        if (err) r3_strcpy(err, "stack write failed", err_cap);
        ring3_clear_user();
        return -1;
    }
    w = 0;
    r3_write_u64(sp_base + w++ * 8, 1);              /* argc */
    r3_write_u64(sp_base + w++ * 8, stack_str);      /* argv[0] */
    r3_write_u64(sp_base + w++ * 8, 0);              /* argv NULL */
    r3_write_u64(sp_base + w++ * 8, 0);              /* envp NULL */
    r3_write_u64(sp_base + w++ * 8, RING3_AT_PAGESZ);
    r3_write_u64(sp_base + w++ * 8, 4096);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_UID);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_EUID);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_GID);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_EGID);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_ENTRY);
    r3_write_u64(sp_base + w++ * 8, proc->main_entry);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_PHDR);
    r3_write_u64(sp_base + w++ * 8, proc->phdr_addr);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_PHENT);
    r3_write_u64(sp_base + w++ * 8, phent);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_PHNUM);
    r3_write_u64(sp_base + w++ * 8, phnum);
    /* AT_HWCAP is 0 on purpose: glibc then uses its generic
     * (non-AVX/SSE) IFUNC implementations, which are correct on
     * any x86-64. Claiming features the CPU lacks would let
     * ld-linux pick an optimized memcpy that faults with #UD. */
    r3_write_u64(sp_base + w++ * 8, RING3_AT_HWCAP);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_HWCAP2);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_CLKTCK);
    r3_write_u64(sp_base + w++ * 8, 100);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_SECURE);
    r3_write_u64(sp_base + w++ * 8, 0);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_RANDOM);
    r3_write_u64(sp_base + w++ * 8, stack_rnd);
    r3_write_u64(sp_base + w++ * 8, RING3_AT_EXECFN);
    r3_write_u64(sp_base + w++ * 8, stack_str);
    if (proc->interp_base) {
        /* Dynamic only: AT_BASE is the interpreter load base.
         * A static executable has no AT_BASE on Linux. */
        r3_write_u64(sp_base + w++ * 8, RING3_AT_BASE);
        r3_write_u64(sp_base + w++ * 8, proc->interp_base);
    }
    r3_write_u64(sp_base + w++ * 8, RING3_AT_NULL);
    r3_write_u64(sp_base + w++ * 8, 0);
    proc->stack_top = sp_base;
    /* Verify the stack landed: read argc back through the
     * page tables. A failed write here would mean the user
     * process starts with a corrupt stack. */
    {
        uint64_t phys, argc = 0;
        uint8_t *p;
        if (ring3_translate(g_pml4_phys, sp_base, &phys, 0) != 0) {
            if (err) r3_strcpy(err, "stack verify failed", err_cap);
            ring3_clear_user();
            return -1;
        }
        p = (uint8_t *)ring3_phys_to_ptr(phys);
        {
            int i;
            for (i = 0; i < 8; i++) argc |= (uint64_t)p[i] << (8 * i);
        }
        if (argc != 1) {
            if (err) r3_strcpy(err, "stack verify failed (argc != 1)", err_cap);
            ring3_clear_user();
            return -1;
        }
    }
    return 0;
}

int ring3_run_elf(const uint8_t *data, uint32_t len,
                  const char *prog_path, char *err, uint32_t err_cap)
{
    ring3_proc_t proc;
    int rc;
    if (err && err_cap) err[0] = 0;
    if (!g_hw_ready) {
        if (err) r3_strcpy(err, "ring-3 not initialized", err_cap);
        return -1;
    }
    r3_memset(&proc, 0, sizeof(proc));
    r3_strcpy(proc.prog_path,
              prog_path && prog_path[0] ? prog_path : "/usr/bin/static-hello",
              sizeof(proc.prog_path));
    r3_print("[FisixOS] RING3 SELFTEST: loading ELF...\n");
    rc = ring3_load_elf(data, len, &proc, err, err_cap);
    if (rc != 0) return -1;
    r3_print("[FisixOS] RING3 SELFTEST: ELF loaded, entry=");
    r3_print_hex(proc.entry);
    r3_print(" brk_base=");
    r3_print_hex(proc.brk_base);
    {
        uint64_t ephys, eflags;
        int etr = ring3_translate(g_pml4_phys, proc.entry, &ephys, &eflags);
        r3_print(" entry_tr=");
        r3_print_u64((uint64_t)etr);
        r3_print(" entry_phys=");
        r3_print_hex(ephys);
        r3_print(" entry_flags=");
        r3_print_hex(eflags);
        r3_print(" efer=");
        r3_print_hex(r3_rdmsr(MSR_EFER));
    }
    r3_print("\n");
    linux_syscall_reset(proc.brk_base, proc.prog_path);
    g_ring3_active = 1;
    g_ring3_faulted = 0;
    r3_print("[FisixOS] RING3: entering user mode, entry=0x");
    r3_print_hex(proc.entry);
    r3_print(" rsp=0x");
    r3_print_hex(proc.stack_top);
    r3_print("\n");
    ring3_enter_user(proc.entry, proc.stack_top);
    g_ring3_active = 0;
    if (g_ring3_faulted) {
        if (err) r3_strcpy(err, "user process faulted (see serial log)", err_cap);
        return -1;
    }
    rc = linux_syscall_exit_code();
    r3_print("[FisixOS] RING3: user process exited, code=");
    r3_print_u64((uint64_t)rc);
    r3_print("\n");
    if (rc != 0 && err)
        r3_strcpy(err, "user process exited with a non-zero code", err_cap);
    return rc;
}

/* ---------- boot self-test ---------- */
static char g_selftest_out[256];
static uint32_t g_selftest_out_len = 0;

static void ring3_selftest_capture(char c)
{
    if (g_selftest_out_len + 1 < sizeof(g_selftest_out))
        g_selftest_out[g_selftest_out_len++] = c;
    g_selftest_out[g_selftest_out_len] = 0;
    r3_serial_putc(c);
}

/* Dynamic half of the boot self-test: run the real Debian
 * hello (dynamic, needs ld-linux + libc.so.6 from the
 * installed libc6 .deb) and check its exact output. Returns 0
 * on pass, -1 on fail. A missing hello package is a SKIP
 * (reported, not a failure): the static half is the one that
 * must pass. Called only after the static half passes. */
static int ring3_selftest_dynamic(void)
{
    const uint8_t *data;
    uint32_t len = 0;
    char err[256];
    int rc;
    int frc = deb_file_read("hello", "./usr/bin/hello", &data, &len);
    r3_print("[FisixOS] RING3 SELFTEST DYN: deb_file_read rc=");
    r3_print_u64((uint64_t)frc);
    r3_print(" len=");
    r3_print_u64(len);
    r3_print("\n");
    if (frc != 0) {
        r3_print("[FisixOS] RING3 SELFTEST DYN: SKIP (no hello .deb installed)\n");
        return 0;
    }
    r3_print("[FisixOS] RING3 SELFTEST DYN: dynamic hello staged, len=");
    r3_print_u64(len);
    r3_print("\n");
    g_selftest_out_len = 0;
    g_selftest_out[0] = 0;
    linux_syscall_set_output_hook(ring3_selftest_capture);
    rc = ring3_run_elf(data, len, "/usr/bin/hello", err, sizeof(err));
    linux_syscall_set_output_hook(0);
    if (rc < 0) {
        r3_print("[FisixOS] RING3 SELFTEST DYN: run failed, err='");
        r3_print(err);
        r3_print("'\n");
        return -1;
    }
    r3_print("[FisixOS] RING3 SELFTEST DYN: run returned ");
    r3_print_u64((uint64_t)rc);
    r3_print(", output='");
    r3_print(g_selftest_out);
    r3_print("'\n");
    if (rc == 0 && g_selftest_out_len == 14) {
        /* Exact check: "Hello, world!\n" is 14 bytes. */
        const char *want = "Hello, world!\n";
        uint32_t i;
        int ok = 1;
        for (i = 0; i < 14; i++)
            if (g_selftest_out[i] != want[i]) ok = 0;
        if (ok) {
            r3_print("[FisixOS] RING3 SELFTEST DYN: PASS (dynamic Debian ELF ran in ring 3)\n");
            return 0;
        }
    }
    r3_print("[FisixOS] RING3 SELFTEST DYN: FAIL (rc or output mismatch)\n");
    return -1;
}

int ring3_selftest(void)
{
    const uint8_t *data;
    uint32_t len = 0;
    char err[256];
    int rc;
    if (!g_hw_ready) {
        r3_print("[FisixOS] RING3 SELFTEST: SKIP (ring-3 not initialized)\n");
        return -1;
    }
    /* Install the embedded .debs one by one and report each
     * result: deb_install_embedded() swallows the per-package
     * error, which hides why a big .deb (libc6) fails to
     * install in the kernel while it installs fine on the host. */
    {
        int i;
        for (i = 0; i < 8; i++) {
            const uint8_t *edata = 0;
            uint32_t elen = 0;
            char eerr[256];
            int erc;
            if (!deb_embedded_find(i, &edata, &elen)) break;
            eerr[0] = 0;
            erc = deb_install(edata, elen, eerr, sizeof(eerr));
            r3_print("[FisixOS] RING3 SELFTEST: embedded deb ");
            r3_print_u64((uint64_t)i);
            r3_print(" len=");
            r3_print_u64(elen);
            r3_print(" rc=");
            if (erc < 0) { r3_print("-"); r3_print_u64((uint64_t)(-erc)); }
            else r3_print_u64((uint64_t)erc);
            r3_print(" err='");
            r3_print(eerr);
            r3_print("'\n");
        }
        /* The embedded .debs are installed now; mark them done
         * so the terminal's later deb_install_embedded() does
         * not decompress all three again. */
        deb_embedded_mark_done();
    }
    r3_print("[FisixOS] RING3 SELFTEST: embedded .deb installed, pkg_count=");
    r3_print_u64((uint64_t)deb_count());
    r3_print("\n");
    {
        int frc = deb_file_read("fisixapp", "./usr/bin/static-hello", &data, &len);
        r3_print("[FisixOS] RING3 SELFTEST: deb_file_read rc=");
        r3_print_u64((uint64_t)frc);
        r3_print(" len=");
        r3_print_u64(len);
        r3_print("\n");
        if (frc != 0) {
            r3_print("[FisixOS] RING3 SELFTEST: SKIP (no embedded static hello)\n");
            return -1;
        }
    }
    r3_print("[FisixOS] RING3 SELFTEST: static hello staged, len=");
    r3_print_u64(len);
    r3_print(" data=");
    r3_print_hex((uint64_t)(uintptr_t)data);
    r3_print("\n");
    g_selftest_out_len = 0;
    g_selftest_out[0] = 0;
    linux_syscall_set_output_hook(ring3_selftest_capture);
    rc = ring3_run_elf(data, len, "/usr/bin/static-hello", err, sizeof(err));
    linux_syscall_set_output_hook(0);
    if (rc < 0) {
        r3_print("[FisixOS] RING3 SELFTEST: run failed, err='");
        r3_print(err);
        r3_print("'\n");
    } else {
        r3_print("[FisixOS] RING3 SELFTEST: run returned ");
        r3_print_u64((uint64_t)rc);
        r3_print(", output='");
        r3_print(g_selftest_out);
        r3_print("'\n");
    }
    if (rc == 0 && g_selftest_out_len == 13 &&
        g_selftest_out[0] == 'S') {
        /* Exact check: "STATIC HELLO\n" is 13 bytes. */
        const char *want = "STATIC HELLO\n";
        uint32_t i;
        int ok = 1;
        for (i = 0; i < 13; i++)
            if (g_selftest_out[i] != want[i]) ok = 0;
        if (ok) {
            r3_print("[FisixOS] RING3 SELFTEST: PASS (static Linux ELF ran in ring 3)\n");
            /* Static passed: now the dynamic half (real Debian
             * hello through ld-linux + libc.so.6). */
            return ring3_selftest_dynamic();
        }
    }
    r3_print("[FisixOS] RING3 SELFTEST: FAIL (rc or output mismatch)\n");
    return -1;
}

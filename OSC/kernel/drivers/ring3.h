/* ring3.h - ring-3 user-mode execution for FisixOS.
 *
 * Stage 1+2 of the .deb / Linux ELF work (see docs/RING3.md):
 *   - own GDT with user code/data segments (CS 0x23 / SS 0x1B),
 *     kernel code/data (0x08 / 0x10) and a 64-bit TSS (0x28)
 *     whose rsp0 is a dedicated interrupt stack;
 *   - a frame allocator over the UEFI conventional-memory ranges
 *     the loader hands over in kernel_boot_info_t;
 *   - the kernel's own 4-level page tables (CR3 switch away from
 *     the UEFI tables): 0..128 MiB identity-mapped with 4 KiB
 *     pages, 128 MiB..4 GiB with 2 MiB pages, user pages mapped
 *     on top with the U/S bit;
 *   - iretq entry to a Linux ELF at ring 3, SYSCALL/SYSRET via
 *     the EFER/STAR/LSTAR/FMASK MSRs (handler in ring3_entry.S),
 *     and #PF/#GP fault handlers that kill the user process
 *     instead of triple-faulting.
 *
 * Design references (see LICENSES/THIRD-PARTY.md):
 *   - LetsGo-OS (MIT, Copyright (c) 2022 Sansero),
 *     kernel/userspace.go InitUserMode: GDT user segments + TSS.
 *   - Asher's Rust OS project, kernel/src/elf.rs
 *     jump_to_user_mode: iretq with CS 0x23 / SS 0x1B.
 * Both were design references, ported to C here; no code was
 * copied verbatim and there is no profanity anywhere.
 *
 * Limits, stated plainly: one user process at a time (a single
 * shared PML4); user address space is 0..4 GiB identity-mapped
 * for the kernel half; the heap is capped at 8 MiB and the mmap
 * arena at 256 MiB. See docs/RING3.md.
 */
#ifndef FISIX_RING3_H
#define FISIX_RING3_H
#include <stdint.h>
#include "boot_info.h"

/* Page-table entry flags. */
#define RING3_PTE_P   (1ull << 0)
#define RING3_PTE_RW  (1ull << 1)
#define RING3_PTE_US  (1ull << 2)
#define RING3_PTE_NX  (1ull << 63)
/* Physical-address bits of an entry: bits 51:12 for a 4 KiB
 * entry, bits 51:21 for a 2 MiB entry. Never use ~0xFFF on an
 * entry to get its address: bit 63 (NX) would survive the mask
 * and corrupt the address (that bug once made translate()
 * return NX-tagged addresses and unmap() free a wild frame). */
#define RING3_PTE_ADDR_MASK 0x000FFFFFFFFFF000ull
#define RING3_PTE_ADDR_MASK_2MB 0x000FFFFFFFE00000ull

/* User address-space layout. The ELF loads at its own p_vaddr
 * (normally 0x400000); the stack and mmap arena are fixed here. */
#define RING3_USER_STACK_TOP 0x80000000ull /* one-past-top, 64 KiB mapped below it */
#define RING3_USER_STACK_SIZE (64u * 1024u)
#define RING3_MMAP_ARENA_BASE 0x10000000ull /* 256 MiB, grows up */
#define RING3_MMAP_ARENA_SIZE (256u * 1024u * 1024u)
#define RING3_HEAP_MAX (8u * 1024u * 1024u)
/* Dynamic (PIE) executables and their interpreter are loaded at
 * fixed bases instead of their p_vaddr (which starts at 0):
 * the main executable at 1 GiB, ld-linux at 1.25 GiB. Both are
 * inside the 0..4 GiB identity map (user pages are mapped on
 * top of it), clear of the mmap arena (0x10000000), the brk
 * heap (which grows from the end of the main executable), and
 * the stack (0x7FFF0000). ld-linux is ~240 KiB and a Debian
 * hello is ~16 KiB, so neither base comes close to the next. */
#define RING3_DYN_MAIN_BASE 0x40000000ull
#define RING3_DYN_INTERP_BASE 0x50000000ull

/* Linux initial-stack values, shared with the host test. */
#define RING3_AT_NULL 0
#define RING3_AT_PHDR 3
#define RING3_AT_PHENT 4
#define RING3_AT_PHNUM 5
#define RING3_AT_PAGESZ 6
#define RING3_AT_BASE 7
#define RING3_AT_ENTRY 9
#define RING3_AT_UID 11
#define RING3_AT_EUID 12
#define RING3_AT_GID 13
#define RING3_AT_EGID 14
#define RING3_AT_HWCAP 16
#define RING3_AT_CLKTCK 17
#define RING3_AT_SECURE 23
#define RING3_AT_RANDOM 25
#define RING3_AT_HWCAP2 26
#define RING3_AT_EXECFN 31

/* One loaded user process: what ring3_load_elf() fills in. */
typedef struct {
    uint64_t entry;      /* ELF e_entry (for a dynamic ELF: the
                          * interpreter entry, which is what the
                          * kernel jumps to; the main entry is in
                          * the auxv AT_ENTRY) */
    uint64_t stack_top;  /* initial user RSP (points at argc) */
    uint64_t brk_base;   /* first address after the loaded segments */
    uint64_t phdr_addr;  /* AT_PHDR: load address of the phdr table */
    uint64_t phnum;
    uint64_t phent;
    uint64_t pml4;       /* physical address of the process PML4 */
    uint64_t interp_base; /* 0 for a static ELF, else the ld-linux
                           * load base (auxv AT_BASE) */
    uint64_t main_entry;  /* main executable entry (auxv AT_ENTRY) */
    int seg_count;
    char prog_path[128]; /* argv[0] + /proc/self/exe answer */
} ring3_proc_t;

/* --- bring-up (called from kernel_startup) --- */
/* GDT + TSS + frame allocator + page tables + SYSCALL MSRs.
 * Must run before idt_init() (the IDT gates take the new CS).
 * Returns 0 on success, -1 if there is no usable memory map
 * (ring-3 stays unavailable; deb_run() refuses honestly). */
int ring3_init(const kernel_boot_info_t *info);
/* Install the #PF (14) and #GP (13) gates. Call after idt_init(). */
void ring3_install_fault_gates(void);
int ring3_ready(void); /* 1 after a successful ring3_init() */

/* --- frame allocator (physical 4 KiB pages) --- */
uint64_t ring3_alloc_frame(void); /* 0 = out of memory */
void ring3_free_frame(uint64_t phys);
uint64_t ring3_frames_total(void);
uint64_t ring3_frames_free(void);
/* Host-test only: init the frame allocator from a fake map,
 * and build the page tables without touching CR3. */
int ring3_frame_init_for_test(const kernel_boot_info_t *info);
int ring3_pagetables_build(void);

/* --- page tables (pml4 = physical address of a PML4) --- */
uint64_t ring3_kernel_pml4(void); /* the shared kernel/user PML4 */
/* Map one 4 KiB page. Flags are RING3_PTE_* (P is added).
 * Returns 0 or -1. Splits a 2 MiB identity page if the vaddr
 * lands in the 2 MiB-mapped region. */
int ring3_map_page(uint64_t pml4, uint64_t vaddr, uint64_t phys,
                   uint64_t flags);
/* Unmap one 4 KiB page (present bit cleared, TLB flushed for
 * that address). Returns 0 or -1 if it was not mapped. */
int ring3_unmap_page(uint64_t pml4, uint64_t vaddr);
/* Translate a user vaddr to a physical address. Returns 0 and
 * fills *phys_out and *flags_out, or -1 if not present. */
int ring3_translate(uint64_t pml4, uint64_t vaddr, uint64_t *phys_out,
                    uint64_t *flags_out);
/* Change the flags (RW/NX/US) of already-mapped pages in
 * [vaddr, vaddr+len). Returns 0 or -1. */
int ring3_protect(uint64_t pml4, uint64_t vaddr, uint64_t len,
                  uint64_t flags);
/* 1 if every page in [vaddr, vaddr+len) is present with U/S set,
 * i.e. the kernel may safely read/write that user buffer. */
int ring3_user_range_ok(uint64_t vaddr, uint64_t len);
/* 1 if every level of the walk to vaddr carries U/S (see ring3.c). */
int ring3_user_walk_ok(uint64_t pml4, uint64_t vaddr, uint64_t *flags_out);

/* --- user process --- */
/* Load a Linux ELF into fresh user pages of the shared PML4
 * (old user pages from a previous run are unmapped first),
 * build the Linux initial stack, fill *proc. Does NOT jump.
 * - Static ELF (ET_EXEC, no PT_INTERP): loaded at its own
 *   p_vaddr, entry is its e_entry.
 * - Dynamic ELF (ET_DYN with PT_INTERP): the executable is
 *   loaded at RING3_DYN_MAIN_BASE and its interpreter
 *   (ld-linux, resolved from .deb-installed files via
 *   deb_resolve_lib_path()) at RING3_DYN_INTERP_BASE; entry
 *   is the interpreter entry, and the auxv carries AT_BASE,
 *   AT_PHDR/AT_ENTRY for the main executable, so the real
 *   ld-linux can relocate it and resolve its DT_NEEDED
 *   libraries. See docs/DYNLINK.md.
 * Returns 0, or -1 with a reason in err. */
int ring3_load_elf(const uint8_t *data, uint32_t len,
                   ring3_proc_t *proc, char *err, uint32_t err_cap);
/* Load + enter ring 3 + wait for exit. Returns the process exit
 * code (0..255) on a clean exit, or -1 with a reason in err
 * (load failure, fault, or ring-3 not initialized). */
int ring3_run_elf(const uint8_t *data, uint32_t len,
                  const char *prog_path, char *err, uint32_t err_cap);
int ring3_last_exit_code(void); /* exit code of the last run */
int ring3_last_faulted(void);   /* 1 if the last run died on a fault */

/* FS base for arch_prctl(SET_FS). Written to the IA32_FS_BASE
 * MSR when the hardware is up; recorded in a shadow variable
 * otherwise (host test, or before ring3_init). */
void ring3_set_fs_base(uint64_t base);
uint64_t ring3_get_fs_base(void);

/* Boot self-test: installs the embedded .debs, runs the static
 * hello ELF in ring 3 and checks its exact output, then runs
 * the real dynamic Debian hello through ld-linux + libc.so.6
 * and checks its exact output. Prints RING3 SELFTEST lines to
 * serial. Returns 0 when both halves pass (or when the static
 * half passes and the hello .deb is not installed: the dynamic
 * half is then SKIP, not FAIL). */
int ring3_selftest(void);

/* --- assembly entry points (ring3_entry.S) --- */
void ring3_load_gdt(void *gdtr);
void ring3_enter_user(uint64_t entry, uint64_t user_rsp);
extern void linux_syscall_entry(void);
extern void ring3_fault_pf(void);
extern void ring3_fault_gp(void);
/* Called by the fault stubs: vector 13/14, error code, CR2,
 * faulting CS. Returns 1 to kill the user process, 0 to resume
 * (only for faults the kernel can resume from; ring-0 faults
 * never return). */
int ring3_fault_handler(uint64_t vec, uint64_t err, uint64_t cr2,
                        uint64_t cs, uint64_t rip,
                        const uint64_t *regs);

/* Physical address -> pointer. On the kernel this is the
 * identity map; the host test overrides it (FISIX_HOST_TEST)
 * to point at its simulated RAM. */
#ifdef FISIX_HOST_TEST
extern uint8_t *g_ring3_host_mem;
extern uint64_t g_ring3_host_mem_phys;
static inline void *ring3_phys_to_ptr(uint64_t phys)
{
    return (void *)(g_ring3_host_mem + (phys - g_ring3_host_mem_phys));
}
#else
static inline void *ring3_phys_to_ptr(uint64_t phys)
{
    return (void *)(uintptr_t)phys;
}
#endif

#endif

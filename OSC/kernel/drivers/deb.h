/* deb.h - Debian .deb package support for FisixOS.
 *
 * What a .deb is: an `ar` archive with three members, in order:
 *   debian-binary  - text "2.0\n"
 *   control.tar.*  - tar of the control files (control, preinst, ...)
 *   data.tar.*     - tar of the files to install
 * The tar members are usually compressed: .gz (supported here),
 * .xz or .zst (detected, reported, see docs/DEB.md).
 *
 * Honest scope (read docs/DEB.md for the full story):
 * - This is a real .deb unpacker + installer: ar/tar/gzip parsing,
 *   control-file parsing, dependency listing, an installed-package
 *   registry, and uninstall (removes the staged files from the
 *   registry). Files are staged in a static RAM pool, AND - since
 *   the read/write stage - can be written to a real FAT32 volume
 *   with deb_persist(): the staged files land on disk (via the
 *   FAT32 writer in fat.c) and survive a reboot. The RAM pool is
 *   still the staging area; deb_persist is the step that makes an
 *   install permanent. In the kernel GUI nothing mounts a volume
 *   yet (see vfs.h), so deb_persist is host-proven for now and
 *   becomes live in the kernel the moment a volume is mounted.
 *   The build-time path (tools/embed_deb.py) that bakes a .deb
 *   into the kernel image the same way .fse apps are embedded
 *   still exists alongside it.
 * - Debian binaries are Linux ELFs. Dynamic glibc binaries
 *   (the normal case, e.g. hello from Debian) RUN on FisixOS
 *   now: ring3_load_elf() loads /lib64/ld-linux-x86-64.so.2
 *   from the installed libc6 .deb, and the real ld-linux
 *   resolves libc.so.6 and the other DT_NEEDED libraries from
 *   .deb-installed files (see docs/DYNLINK.md). deb_run()
 *   refuses a dynamic ELF only when its interpreter or a
 *   needed library is not installed, with the exact reason.
 * - Static Linux ELFs: they RUN, in ring 3 (kernel/drivers/
 *   ring3.c + linux_syscall.c, see docs/RING3.md). The ELF is
 *   loaded into fresh user pages (PT_LOAD walk, .bss zeroing,
 *   Linux initial stack with the full auxv, adapted from
 *   Asher's Rust OS project kernel/src/elf.rs and LetsGo-OS
 *   kernel/elfloader.go LoadAuxVector), then entered with an
 *   iretq jump (CS 0x23 / SS 0x1B, GDT user segments + TSS
 *   after LetsGo-OS InitUserMode) and served Linux syscalls
 *   through the SYSCALL/SYSRET MSRs. One user process at a
 *   time; dynamic ELFs are still refused (see below).
 * - .fse files inside a .deb DO run, via the existing proc_run_fse().
 *
 * - Dynamic Linux ELFs: they RUN now, in ring 3 (see
 *   docs/DYNLINK.md): ring3_load_elf() loads the PT_INTERP
 *   interpreter (the real ld-linux from the installed libc6
 *   .deb) plus the executable, the interpreter resolves the
 *   DT_NEEDED libraries from .deb-installed files, and the
 *   Linux syscalls in linux_syscall.c serve it (file-backed
 *   mmap, pread64, openat of /lib/... paths). One user process
 *   at a time; no fork/execve/clone yet.
 *
 * No profanity anywhere in this file or the .deb code: FisixOS is
 * the professional-vibe project.
 */
#ifndef FISIX_DEB_H
#define FISIX_DEB_H
#include <stdint.h>
#include <stddef.h>

#define DEB_MAX_PACKAGES 8
/* Raised 64 -> 256 in stage 5, then 256 -> 512 for the dynamic
 * linker stage: real Debian packages commonly have more than
 * 256 files (libc6 2.36 has 286). Each deb_file_t is ~140
 * bytes, so this is ~72 KiB per package slot. */
#define DEB_MAX_FILES_PER_PACKAGE 512
#define DEB_MAX_PATH 128
/* Static staging pool for all extracted files. Raised 4 MiB ->
 * 16 MiB for the dynamic linker stage: libc6 alone extracts
 * to ~12.8 MiB. The pool is BSS, so raising it costs RAM at
 * runtime, not kernel image size. A big package still fails
 * install with a clear "staging pool full" error instead of
 * corrupting memory. */
#define DEB_STAGE_POOL_SIZE (16u * 1024u * 1024u)

#define DEB_OK 0
#define DEB_ERR_BAD_AR (-1)
#define DEB_ERR_BAD_CONTROL (-2)
#define DEB_ERR_BAD_TAR (-3)
#define DEB_ERR_UNSUPPORTED_COMPRESSION (-4)
#define DEB_ERR_TOO_BIG (-5)
#define DEB_ERR_NO_MEMORY (-6)
#define DEB_ERR_ALREADY_INSTALLED (-7)
#define DEB_ERR_NOT_FOUND (-8)
#define DEB_ERR_BAD_ELF (-9)
#define DEB_ERR_NOT_RUNNABLE (-10)
#define DEB_ERR_DISK (-11) /* a write to the FAT volume failed */

typedef struct {
    char path[DEB_MAX_PATH]; /* install path as in the tar, e.g. ./usr/bin/hello */
    uint32_t offset;         /* offset into the staging pool */
    uint32_t size;
    uint8_t is_elf;
    uint8_t elf_kind; /* see deb_elf_kind_t */
} deb_file_t;

typedef struct {
    char name[64];
    char version[32];
    char arch[16];
    char depends[256];
    char description[128];
    int file_count;
    deb_file_t files[DEB_MAX_FILES_PER_PACKAGE];
    uint32_t total_size;
} deb_pkg_t;

/* ELF classification, from deb_elf_classify(). */
typedef enum {
    DEB_ELF_NOT_ELF = 0,
    DEB_ELF_FSE = 1,        /* FisixOS .fse: runnable via proc_run_fse() */
    DEB_ELF_STATIC = 2,    /* static Linux x86-64 ELF: runs in ring 3 */
    DEB_ELF_DYNAMIC = 3,   /* dynamic Linux ELF: needs ld-linux + glibc, refused */
    DEB_ELF_OTHER = 4      /* ELF, but wrong class/machine (e.g. 32-bit, ARM) */
} deb_elf_kind_t;

/* Install a .deb from memory. On failure *err (if given) gets a
 * short human-readable reason. Returns DEB_OK or a DEB_ERR_* code.
 * Reinstalling the same package name+version is an error; remove
 * it first with deb_remove(). */
int deb_install(const uint8_t *data, uint32_t len, char *err, uint32_t err_cap);

/* Remove a package by name: drops its files from the staging pool
 * (compacts the pool) and unregisters it. Returns DEB_OK or
 * DEB_ERR_NOT_FOUND. */
int deb_remove(const char *name);

/* Installed-package registry. */
int deb_count(void);
int deb_get(int idx, deb_pkg_t *out);
int deb_find(const char *name, deb_pkg_t *out);

/* Human-readable one-package summary into out (NUL-terminated).
 * Returns DEB_OK or DEB_ERR_NOT_FOUND. */
int deb_info(const char *name, char *out, uint32_t cap);

/* Read a staged file from an installed package. Returns DEB_OK and
 * sets the data pointer and length for the staged copy, or DEB_ERR_NOT_FOUND. */
int deb_file_read(const char *pkg, const char *path,
                  const uint8_t **data, uint32_t *len);

/* Find a staged file by its install path, across ALL installed
 * packages (first match wins). The path may be written with or
 * without a leading "./" and with or without a leading "/":
 * "./usr/bin/hello", "usr/bin/hello" and "/usr/bin/hello" all
 * match the same staged file. This is the lookup the Linux
 * syscall layer (openat/newfstatat) and the dynamic loader
 * (PT_INTERP) use to resolve absolute Unix paths such as
 * /lib/x86_64-linux-gnu/libc.so.6 to .deb-installed files.
 * Returns DEB_OK or DEB_ERR_NOT_FOUND. */
int deb_file_find_by_path(const char *path,
                          const uint8_t **data, uint32_t *len);

/* Resolve a library/interpreter path the way the dynamic linker
 * needs it: first apply the standard Debian symlink
 * /lib64/ld-linux-x86-64.so.2 -> /lib/x86_64-linux-gnu/
 * ld-linux-x86-64.so.2 (tar_walk() skips symlinks, so the
 * /lib64 path is never staged as a file), then look the result
 * up with deb_file_find_by_path(). Returns DEB_OK or
 * DEB_ERR_NOT_FOUND. */
int deb_resolve_lib_path(const char *path,
                         const uint8_t **data, uint32_t *len);

/* Classify a blob: .fse, static ELF, dynamic ELF, other ELF, or not
 * an ELF at all. Fills out (if given) with a one-line reason, e.g.
 * the PT_INTERP path or the DT_NEEDED list for dynamic binaries. */
deb_elf_kind_t deb_elf_classify(const uint8_t *data, uint32_t len,
                                char *out, uint32_t out_cap);

/* ELF loading, adapted from Asher's Rust OS project kernel/src/elf.rs
 * (Rust, by Asher): validates the ELF64 header (magic, class 64,
 * little-endian, x86-64), walks the program headers, and copies
 * every PT_LOAD segment into a flat load buffer at its p_vaddr
 * offset, zero-filling .bss (p_memsz > p_filesz), exactly like
 * elf.rs does with page mapping, and builds the Linux-ABI initial
 * stack layout (argc/argv/auxv, as in elf.rs). This flat-buffer
 * form is kept for the host test; the real ring-3 run uses
 * ring3_load_elf() (kernel/drivers/ring3.c), which maps the same
 * segments into user pages and jumps to them. Returns DEB_OK, or
 * DEB_ERR_BAD_ELF, or DEB_ERR_NOT_RUNNABLE (dynamic ELF:
 * PT_INTERP present) with the reason in err. */
typedef struct {
    uint64_t entry;
    uint64_t stack_top; /* top of the prepared Linux-ABI stack */
    uint64_t brk_base;  /* first address after the loaded segments */
    int seg_count;
} deb_loaded_elf_t;
int deb_elf_load(const uint8_t *data, uint32_t len,
                 uint8_t *load_buf, uint32_t load_cap,
                 deb_loaded_elf_t *loaded, char *err, uint32_t err_cap);

/* Run a file from an installed package.
 * - .fse: runs it via proc_run_fse() and returns its return code.
 * - static ELF: runs it in ring 3 (ring3_run_elf()) and returns
 *   its exit code (0 = clean exit). On failure returns
 *   DEB_ERR_NOT_RUNNABLE with the reason in err.
 * - dynamic ELF: runs it in ring 3 through its interpreter
 *   (ring3_run_elf() loads ld-linux + the executable; the real
 *   ld-linux resolves DT_NEEDED from .deb-installed files).
 *   Returns the exit code, or DEB_ERR_NOT_RUNNABLE with the
 *   reason (interpreter or library not installed, load
 *   failure) in err.
 * - anything else: DEB_ERR_NOT_RUNNABLE. */
int deb_run(const char *pkg, const char *path, char *err, uint32_t err_cap);

/* Install every .deb baked into the kernel image by
 * tools/embed_deb.py (deb_embedded.c). No-op if none are baked in.
 * Called by the terminal the first time a `deb` command runs. */
void deb_install_embedded(void);

/* Mark the embedded .debs as already installed, so a later
 * deb_install_embedded() is a no-op. For callers that install
 * the embedded .debs one by one themselves (the ring-3 boot
 * self-test, which reports each package's install result)
 * and therefore must not have them re-decompressed later. */
void deb_embedded_mark_done(void);

/* Write an installed package's staged files onto a FAT32 volume
 * image (img/len, starting at the volume BPB - see fat.h), so
 * the install survives a reboot. Tar paths such as
 * ./usr/bin/hello are mapped to 8.3 FAT paths (USR/BIN/HELLO,
 * directories created as needed); directory entries in the tar
 * are skipped. Returns DEB_OK, DEB_ERR_NOT_FOUND, or
 * DEB_ERR_DISK (with the reason in err when given). The RAM
 * staging pool is left untouched: deb_remove() still works
 * afterwards, and re-running deb_persist overwrites the same
 * on-disk files. */
int deb_persist(const char *name, uint8_t *img, size_t len, char *err, uint32_t err_cap);

#endif

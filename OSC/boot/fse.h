/* .fse - FiSix Executable, v1.
 *
 * A .fse file is: a 64-byte header, then x86-64 code+data.
 * It is loaded at FISIX_FSE_LOAD_ADDRESS by the loader terminal,
 * and its entry point is called as:
 *   int fse_main(fisix_api_t *api);
 * using the normal System V x86-64 calling convention (first arg
 * in RDI), because the loader itself is built that way for its
 * internal calls. The API table is the program's only way to talk
 * to FisixOS in v1: print text, print a number, and nothing else
 * that needs a syscall, because FisixOS has no userspace syscall
 * ABI yet. A .fse program must return; it must not ExitBootServices,
 * touch UEFI boot services directly, or assume any address other
 * than what it was linked at (see tools/make_fse.py).
 *
 * Header layout, little-endian, packed, 64 bytes total:
 *   u32 magic         = 'F','S','E','1' = 0x31455346
 *   u32 version       = 1
 *   u32 entry_offset  = byte offset from the start of the file
 *                       to the entry point (usually 64 + offset
 *                       of fse_main inside the linked binary)
 *   u32 code_size     = bytes after the header (code + data)
 *   u32 api_version   = 1
 *   u32 reserved[11]  = 0
 *
 * Building one with GCC (cross-target, on Linux):
 *   x86_64-linux-gnu-gcc -ffreestanding -nostdlib -fno-stack-protector \
 *     -Wl,-T,tools/fse.ld -o hello.elf hello.c
 *   python3 tools/make_fse.py hello.elf hello.fse
 * then in FisixOS, after login, in the loader terminal:
 *   run hello.fse
 * or just: hello.fse
 *
 * Building one with Rust (cross-target, on Linux):
 *   see tools/rust-fse/ - no_std, target x86_64-unknown-fisix,
 *   entry fse_main, same API table, same make_fse.py step.
 * Native GCC / Cargo running ON FisixOS is NOT v1: that needs a
 * FisixOS syscall ABI, a libc and a process loader in the kernel,
 * which do not exist yet. See tools/PORTING-GCC-RUST.md.
 */
#ifndef FISIX_FSE_H
#define FISIX_FSE_H

#include <stdint.h>
#include <stddef.h>

#define FISIX_FSE_MAGIC 0x31455346u
#define FISIX_FSE_VERSION 1
#define FISIX_FSE_API_VERSION 1
#define FISIX_FSE_LOAD_ADDRESS 0x300000ULL
#define FISIX_FSE_HEADER_SIZE 64
#define FISIX_FSE_MAX_CODE_SIZE (256 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_offset;
    uint32_t code_size;
    uint32_t api_version;
    uint32_t reserved[11];
} __attribute__((packed)) fse_header_t;

typedef struct fisix_api {
    void (*print)(const char *ascii_text);
    void (*print_u64)(uint64_t value);
    void (*print_newline)(void);
    /* Added for editors like Nano: single-key input and file I/O.
       The first three fields never move, so v1 programs still work. */
    int (*read_key)(void);
    int (*file_read)(const char *path, char *buf, unsigned int buf_size);
    int (*file_write)(const char *path, const char *buf, unsigned int len);
} fisix_api_t;

typedef int (*fse_entry_t)(fisix_api_t *api);

#endif

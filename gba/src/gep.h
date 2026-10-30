/* GEP - Game Boy Executable Program, v1.
 *
 * NOT a PC .fse file. A PC .fse is x86-64 code and cannot run
 * on ARM7TDMI. A .gep file is ARM code for the GBA, with a
 * 64-byte header in the same shape as .fse, so the idea carries
 * over but the binary does not.
 *
 * A .gep file is: a 64-byte header, then ARM code+data.
 * It is loaded at GEP_LOAD_ADDRESS by the GEP loader, and its
 * entry point is called as:
 *   int gep_main(gep_api_t *api);
 * using the normal ARM AAPCS calling convention (first arg in r0).
 * The API table is the program's only supported way to talk to
 * FisixGBA in v1: print text, read a button, file I/O on the
 * cart filesystem. A .gep program must return; it must not
 * touch hardware registers directly, assume any address other
 * than what it was linked at, or overwrite the loader.
 *
 * Header layout, little-endian, packed, 64 bytes total:
 *   u32 magic         = 'G','E','P','1' = 0x31504547
 *   u32 version       = 1
 *   u32 entry_offset  = byte offset from the start of the file
 *                       to the entry point (usually 64 + offset
 *                       of gep_main inside the linked binary)
 *   u32 code_size     = bytes after the header (code + data,
 *                       NOT including .bss, which is zeroed)
 *   u32 api_version   = 1
 *   u32 reserved[11]  = 0, except reserved[0] = bss_size in bytes
 *                       (set by tools/make_gep.py, zeroed by loader)
 *
 * Building one (see tools/make_gep.py, tools/gep.ld):
 *   arm-none-eabi-gcc -ffreestanding -nostdlib -mcpu=arm7tdmi \
 *     -Wl,-T,tools/gep.ld -o hello.elf examples/hello.c
 *   python3 tools/make_gep.py hello.elf hello.gep
 * then in FisixGBA Terminal:
 *   RUN HELLO.GEP
 *
 * GBA API semantics, v1 (honest limits):
 * - print: draws to the LCD console, 8x8 font, 30 chars/line,
 *   wraps, clears and restarts at the top when full.
 * - print_u64: decimal, via print.
 * - read_key: waits for a NEW button press and returns its
 *   GBA key mask (KEY_A etc. from gba.h, as an int). It does
 *   NOT return a character — GBA has no keyboard of its own,
 *   the on-screen keyboard is a separate OS feature.
 * - file_read/file_write: cart filesystem, 512 bytes max per
 *   file (see cart.c). file_write returns -1 if len > 512.
 * - dir_list: newline-separated cart file names, count returned.
 * - file_delete/file_rename/file_mkdir: NOT supported in the
 *   flat cart filesystem, always return -1. Do not claim them.
 * - file_exists: 1/0. file_size: size or -1.
 * - get_time: GBA has no RTC in this build, fills all six
 *   fields with 0. Do not claim a date.
 * - clear_screen: clears the LCD console and resets the cursor.
 * - sleep_ms: waits approximately that many milliseconds,
 *   using the 16384 Hz timer.
 */
#ifndef FISIXGBA_GEP_H
#define FISIXGBA_GEP_H

#include <stdint.h>

#define GEP_MAGIC 0x31504547u /* 'G','E','P','1' little-endian */
#define GEP_VERSION 1
#define GEP_API_VERSION 2
#define GEP_LOAD_ADDRESS 0x02008000u
#define GEP_HEADER_SIZE 64
#define GEP_MAX_CODE_SIZE (64 * 1024)
#define GEP_MAX_BSS_SIZE (16 * 1024)

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t entry_offset;
    uint32_t code_size;
    uint32_t api_version;
    uint32_t reserved[11];
} __attribute__((packed)) gep_header_t;

typedef struct gep_api {
    void (*print)(const char *text);
    void (*print_u64)(uint64_t value);
    void (*print_newline)(void);
    int (*read_key)(void);
    int (*file_read)(const char *path, char *buf, unsigned int buf_size);
    int (*file_write)(const char *path, const char *buf, unsigned int len);
    int (*dir_list)(const char *path, char *buf, unsigned int buf_size);
    int (*file_delete)(const char *path);
    int (*file_rename)(const char *old_path, const char *new_path);
    int (*file_mkdir)(const char *path);
    int (*file_exists)(const char *path);
    int (*file_size)(const char *path);
    void (*get_time)(unsigned int *year, unsigned int *month, unsigned int *day,
                     unsigned int *hour, unsigned int *minute, unsigned int *second);
    void (*clear_screen)(void);
    void (*sleep_ms)(unsigned int ms);
    /* API v2, appended only, so v1 programs still work: the
       fields above never move. Patch path pipes, see pipe.c:
       pipe_write returns 0/-1, pipe_read returns bytes read
       or -1 and consumes the message, pipe_clear returns 0/-1. */
    int (*pipe_write)(const char *channel, const char *data, unsigned int len);
    int (*pipe_read)(const char *channel, char *buf, unsigned int buf_size);
    int (*pipe_clear)(const char *channel);
} gep_api_t;

typedef int (*gep_entry_t)(gep_api_t *api);

/* Loader, in src/gep.c. Returns the app's return code, or a
   negative error: -1 bad header/magic/version, -2 code too big
   or out of range, -3 not found, -4 bss too big. */
int gep_run_data(const unsigned char *data, unsigned int len);
int gep_run_file(const char *name);
int gep_run_embedded(const char *name);
int gep_list_embedded(char names[][32], int max);

#endif

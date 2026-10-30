/* User Update Files — NOT patch files.
 *
 * An update file is one binary that updates the system. It
 * lists exactly which files it touches, and nothing else
 * changes. A file is replaced only if the update lists it
 * with REPLACE, added only if it lists it with ADD, deleted
 * only if it lists it with DELETE. No blanket replace-all.
 *
 * Header, 64 bytes, little-endian, packed:
 *   u32 magic         = 'U','P','D','1' = 0x31445055
 *   u32 version       = 1
 *   u32 platform      = target platform, see below
 *   u32 entry_count   = number of entries after the header
 *   u32 payload_crc16 = CRC16 (poly 0x1021, same as FisixLink)
 *                       over all entry bytes after the header
 *   u32 reserved[11]  = 0
 * Then, for each entry, in order:
 *   char name[32]     = system file name, e.g. HELLO.GEP
 *   u32 action        = 0 ADD, 1 REPLACE, 2 DELETE
 *   u32 data_len      = bytes of data after this entry header
 *                       (0 for DELETE)
 *   u8 data[data_len] = file content, if any
 *
 * Platforms — the built-in checker:
 *   1 = GBA (this build)
 *   2 = UEFI x86_64 (PC FisixOS)
 *   3 = C64 (Commodore 64 port, queued)
 *   4 = ARM64 (future, not a current build)
 * update_check() rejects a file whose platform does not
 * match the system it is running on, BEFORE anything is
 * written. So a UEFI update on a GBA, a C64 update on a
 * GBA, a GBA update on UEFI, etc., all fail with
 * UPDATE_WRONG_PLATFORM and change nothing. A .patch file
 * run as an update fails the magic check, and an .update
 * file run as a patch fails the same way in the other
 * direction — they are different formats, on purpose.
 *
 * GBA limits, honestly:
 * - Cart files are 512 bytes max, so an update entry on
 *   GBA must be <= 512 bytes. Larger modules belong in
 *   ROM at build time, not in a cart update.
 * - "System files" on GBA are cart files: GEP apps, patch
 *   scripts, text files. The ROM itself cannot be rewritten
 *   by an update on a real cart — that is hardware, not a
 *   software choice. On PC UEFI, the same format can target
 *   files on disk, where replacement is possible.
 */
#ifndef FISIXGBA_UPDATE_H
#define FISIXGBA_UPDATE_H

#include <stdint.h>

#define UPDATE_MAGIC 0x31445055u /* 'U','P','D','1' */
#define UPDATE_VERSION 1
#define UPDATE_HEADER_SIZE 64

#define UPDATE_PLATFORM_GBA 1
#define UPDATE_PLATFORM_UEFI 2
#define UPDATE_PLATFORM_C64 3
#define UPDATE_PLATFORM_ARM64 4
#define UPDATE_PLATFORM_CURRENT UPDATE_PLATFORM_GBA

#define UPDATE_ACTION_ADD 0
#define UPDATE_ACTION_REPLACE 1
#define UPDATE_ACTION_DELETE 2

/* Check only, writes nothing. Returns 0 if OK, else:
   -1 bad magic, -2 bad version, -3 wrong platform,
   -4 truncated, -5 bad CRC, -6 entry too big / bad action. */
int update_check(const unsigned char *data, unsigned int len);
/* Check, then apply. Returns applied count, or the same
   negative error as update_check (nothing written then).
   Summary counts are filled if pointers are non-NULL:
   applied, skipped (ADD for an existing file), deleted. */
int update_apply(const unsigned char *data, unsigned int len,
                 int *applied, int *skipped, int *deleted);
int update_run_file(const char *name);
int update_run_file_counts(const char *name, int *applied, int *skipped, int *deleted, unsigned int *platform_out);
int update_list_embedded(char names[][32], int max);
const char *update_platform_name(unsigned int platform);

#endif

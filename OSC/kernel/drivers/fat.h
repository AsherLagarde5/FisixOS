#ifndef FISIX_FAT_H
#define FISIX_FAT_H
#include <stdint.h>
#include <stddef.h>
/* FAT16 and FAT32 reader, read-only, v1.
 * Input is a whole FAT volume image (or a partition image starting
 * at the BPB), not a GPT disk. The FisixOS loader already finds
 * a FAT partition offset; this reader starts at that offset.
 * Supports: BPB parsing, root directory scan (FAT16 fixed root,
 * FAT32 root cluster chain), LFN? No - 8.3 names only in v1,
 * cluster chain following, reading one file by 8.3 name.
 * Returns 0 on success, negative on error, -2 if not found.
 * This does NOT write, format, or mount - read-only, safe.
 */
int fat_read_file(const uint8_t *img, size_t len, const char *name83, uint8_t *out, size_t out_cap, uint32_t *out_len);
int fat_list_root(const uint8_t *img, size_t len, char *out, size_t out_cap);
#endif

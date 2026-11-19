/* fat.h - FAT16/FAT32 reader + FAT32 writer for FisixOS.
 *
 * Choice for the read/write stage: FAT write, not EOSFS write.
 * EOSFS in this tree is a listing stub (hardcoded entries, no
 * on-disk format, no allocator), so "writing EOSFS" would mean
 * inventing a filesystem from scratch. FAT32 is the real
 * on-disk format of every FisixOS EFI System Partition (the
 * installer formats FAT32, UEFI firmware, Windows and Linux
 * all read it), so a FAT32 writer gives real persistence and
 * real interoperability. A brand-new FisixOS filesystem (FFS)
 * is a separate, later stage: it needs its own on-disk format
 * plus Linux/Windows/BSD and GRUB/rEFInd drivers, which is
 * far more than one RW stage. EOSFS is left exactly as it is
 * here - it is NOT renamed to FFS.
 *
 * All write functions operate in place on a mutable volume
 * image buffer (uint8_t *img, size_t len): the whole FAT
 * volume, starting at its BPB (for a partition, pass the
 * buffer at the partition's first LBA). The caller owns
 * persistence: on the host the test writes the buffer back
 * to a file; in FisixOS the block layer (block.h) moves the
 * same bytes to/from a disk. Nothing here touches a RAM-only
 * pool: bytes land in the image, so they survive a reboot
 * once the image is flushed to disk.
 *
 * Names are 8.3 only (v1 read behaviour, kept). Paths may use
 * '/' or '\' separators and may name subdirectories, e.g.
 * "EFI/BOOT/BOOTX64.EFI" or "DATA/INNER.TXT". Long names are
 * not created; a longer component is truncated to 8.3.
 * FAT16 volumes stay read-only: fat_write_file and friends
 * return FAT_ERR_NOT_FAT32 on them (the ESPs FisixOS makes
 * are FAT32, and FAT16 needs a different root-directory
 * layout for writes).
 *
 * Error codes: 0 = ok, negative = error. -2 is "not found",
 * matching the v1 reader.
 */
#ifndef FISIX_FAT_H
#define FISIX_FAT_H
#include <stdint.h>
#include <stddef.h>

#define FAT_OK 0
#define FAT_ERR_GENERIC (-1)
#define FAT_ERR_NOT_FOUND (-2)
#define FAT_ERR_NO_SPACE (-3)
#define FAT_ERR_NOT_FAT32 (-4)
#define FAT_ERR_BAD_NAME (-5)
#define FAT_ERR_DIR_FULL (-6)
#define FAT_ERR_TOO_BIG (-7)
#define FAT_ERR_NOT_DIR (-8)

/* --- v1 read API (unchanged) --- */
int fat_read_file(const uint8_t *img, size_t len, const char *name83, uint8_t *out, size_t out_cap, uint32_t *out_len);
int fat_list_root(const uint8_t *img, size_t len, char *out, size_t out_cap);

/* 1 = the buffer parses as a FAT volume, 0 = not FAT.
 * Sets *is_fat32 (1 = FAT32, 0 = FAT16) when non-NULL. */
int fat_probe(const uint8_t *img, size_t len, int *is_fat32);

/* Free / total data bytes in the volume (data clusters only). */
int fat_free_bytes(const uint8_t *img, size_t len, uint64_t *free_bytes, uint64_t *total_bytes);

/* List one directory: path "" or "/" = root, otherwise a path
 * such as "EFI" or "EFI/BOOT". Output is one "NAME.EXT" or
 * "NAME<dir>" line per entry, NUL-terminated. Directories are
 * listed with a trailing '/'. "." and ".." are skipped. */
int fat_list_dir(const uint8_t *img, size_t len, const char *path, char *out, size_t out_cap);

/* Create or overwrite a file, truncating any old contents.
 * Allocates clusters as needed, links them in both FAT copies,
 * writes the data, and updates the directory entry (first
 * cluster + size). An empty write (data_len 0) creates a
 * zero-length file with no clusters. Returns FAT_OK or a
 * FAT_ERR_* code. Existing files the write does not touch are
 * left byte-identical: only the file's own old chain is freed
 * and only its own directory entry is changed. */
int fat_write_file(uint8_t *img, size_t len, const char *path, const uint8_t *data, uint32_t data_len);

/* Delete a file: frees its cluster chain (both FAT copies),
 * marks its directory entry 0xE5, and also marks any LFN
 * entries immediately before it 0xE5. */
int fat_delete_file(uint8_t *img, size_t len, const char *path);

/* Create a directory (all parent components must already
 * exist). Returns FAT_OK if it already exists as a directory. */
int fat_mkdir(uint8_t *img, size_t len, const char *path);

/* Does path exist (file or directory)? 1 = yes, 0 = no/error. */
int fat_exists(const uint8_t *img, size_t len, const char *path);

/* Size of a file, or -1 if not found / not a file. */
int32_t fat_file_size(const uint8_t *img, size_t len, const char *path);

/* Format a blank buffer as FAT32. len must be a whole number of
 * 512-byte sectors and large enough for >= 65525 clusters
 * (about 32 MiB at 1 sector/cluster, 64 MiB at 2). spc =
 * sectors per cluster (1, 2, 4, 8...). label is the 11-char
 * volume label (padded with spaces, may be NULL for "NO NAME").
 * Writes BPB, FSInfo, backup boot sector, both FATs and an
 * empty root directory cluster. Returns FAT_OK or FAT_ERR_*. */
int fat_format_fat32(uint8_t *img, size_t len, uint32_t spc, const char *label);

#endif

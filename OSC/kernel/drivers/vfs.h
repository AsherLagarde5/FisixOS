/* vfs.h - tiny mounted-volume layer for the kernel.
 *
 * The kernel has no block driver wired to a mounted volume
 * yet (NVMe read exists, NVMe write is new and unproven on
 * hardware; there is no AHCI/SATA write driver at all), so
 * nothing in the kernel mounts a volume today and proc
 * file_write still returns -1 at runtime - see docs. What
 * this file DOES provide, and what the host test proves, is
 * the real wiring: mount a FAT32 volume image, and every
 * proc file operation (read/write/delete/mkdir/exists/size/
 * dir_list) works on the bytes of that image, through the
 * FAT32 writer in fat.c. When a kernel block driver can hand
 * the kernel a volume image, mounting it is one call and
 * .fse programs, accounts and .deb persistence all start
 * writing to disk with no further changes to proc.c.
 *
 * The mounted image is a caller-owned buffer; vfs does not
 * copy it and does not flush it to disk - flushing is the
 * block driver's job (block_flush / nvme write-back).
 */
#ifndef FISIX_VFS_H
#define FISIX_VFS_H
#include <stdint.h>
#include <stddef.h>

/* Mount a FAT32 volume image (buffer at the volume's BPB).
 * Returns 0 on success, -1 if the buffer is not a FAT32 volume. */
int vfs_mount(uint8_t *img, size_t len);
void vfs_unmount(void);
int vfs_mounted(void);
/* Byte length of the mounted volume, 0 when unmounted. */
size_t vfs_volume_len(void);

int vfs_read(const char *path, uint8_t *buf, size_t cap, uint32_t *out_len);
int vfs_write(const char *path, const uint8_t *data, uint32_t len);
int vfs_delete(const char *path);
int vfs_mkdir(const char *path);
int vfs_exists(const char *path);
int32_t vfs_size(const char *path);
int vfs_list(const char *path, char *out, size_t cap);

#endif

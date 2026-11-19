#ifndef NTFS_KERNEL_H
#define NTFS_KERNEL_H
/* Kernel-side NTFS volume scan (read-only).
 *
 * Scans the NVMe namespace (namespace 1) for NTFS partitions at
 * kernel startup, after nvme_init() has run, and keeps a text
 * listing of the first NTFS volume's root directory for the GUI.
 *
 * READ-ONLY by design: the block ops handed to ntfs.c have no
 * write callback, so ntfs_mount(writable=1) fails. The NVMe
 * driver does have nvme_write_lba, but it is UNPROVEN on
 * hardware/QEMU (see kernel/drivers/nvme.h) - kernel NTFS
 * writes stay off until that write path is proven on a scratch
 * disk. Full read/write NTFS lives in the loader `ntfs` command
 * (boot/efi_main.c), which uses UEFI Block I/O WriteBlocks. */
void ntfs_kernel_scan(void);
/* Text for the GUI: root listing, or a short "no NTFS volume"
   message. Never NULL. */
const char *ntfs_kernel_text(void);
int ntfs_kernel_found(void);
#endif

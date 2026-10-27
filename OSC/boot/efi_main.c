#include "efi.h"
#include "eosfs.h"
#include "memory.h"
#include "accounts.h"
#include "fse.h"
#include "../kernel/boot_info.h"

EFI_HANDLE gImageHandle;
EFI_SYSTEM_TABLE *gST;
EFI_BOOT_SERVICES *gBS;

/* Installed-only build: the same source, compiled with
   -DFISIX_INSTALLED_BUILD=1, produces the copy the installer writes
   to a target disk as EFI/BOOT/BOOTX64.EFI. That copy has no
   Installer menu entry and no working `install` command, so an
   installed disk boots as the OS instead of offering to install
   itself again. The normal (installer-media) build has this as 0.
   See terminal_install() and boot_run_menu(). */
#ifndef FISIX_INSTALLED_BUILD
#define FISIX_INSTALLED_BUILD 0
#endif

/* Minimal COM1 serial debug, for QEMU: OVMF also logs to COM1, so these
   markers appear in the serial log before any ConOut output. */
static void serial_outb(uint16_t port, uint8_t v) {
    __asm__ volatile ("outb %0, %1" : : "a"(v), "Nd"(port));
}
static uint8_t serial_inb(uint16_t port) {
    uint8_t v;
    __asm__ volatile ("inb %1, %0" : "=a"(v) : "Nd"(port));
    return v;
}
static void serial_mark(const char *s) {
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x80);
    serial_outb(0x3F8 + 0, 0x03);
    serial_outb(0x3F8 + 1, 0x00);
    serial_outb(0x3F8 + 3, 0x03);
    serial_outb(0x3F8 + 2, 0xC7);
    serial_outb(0x3F8 + 4, 0x0B);
    while (s && *s) {
        uint32_t t = 100000;
        while ((serial_inb(0x3F8 + 5) & 0x20) == 0 && t-- > 0) { }
        serial_outb(0x3F8, (uint8_t)*s);
        s++;
    }
    serial_outb(0x3F8, (uint8_t)'\n');
}

static const CHAR16 boot_message[] = {
    'F','i','s','i','x',' ','O','S',' ','E','F','I',' ','b','o','o','t','\r','\n',0
};

typedef enum {
    TERMINAL_MODE_BATCH = 0,
    TERMINAL_MODE_BASH = 1
} TerminalMode;

static void terminal_run_nano(void);
static void terminal_first_time_setup(void);
static int terminal_login(void);
static int terminal_read_line(CHAR16 *line, size_t max_length);
static void terminal_write_ascii(const char *text);
static void terminal_write_ascii_line(const char *text);
static int terminal_open_file(const CHAR16 *path, UINT64 mode, UINT64 attributes, EFI_FILE_PROTOCOL **file);

/* ================= Bootable installer (v1, whole spare disk only) =================
   This is the installer Derek asked for: boot the FisixOS ISO/USB, type
   "install" in the loader terminal, pick a target disk, and FisixOS itself
   writes a GPT + FAT32 EFI System Partition to that disk, with
   EFI/BOOT/BOOTX64.EFI, kernel.bin and startup.nsh copied from the boot
   drive. It does NOT run on Windows, and the PS1 files are only Windows
   helpers for writing an ISO/IMG, not the installer.
   Self-copy fix: the target's BOOTX64.EFI is NOT a copy of the running
   installer loader. The installer media carries a separate, modified
   copy at EFI/BOOT/INSTAL.BIN (same source, built with
   -DFISIX_INSTALLED_BUILD=1, Installer menu/command removed), and
   terminal_install() reads that file and writes it as the target's
   BOOTX64.EFI. The .BIN extension keeps it out of the bootloader's
   *.EFI scan and out of firmware's boot search, so it is only ever
   an install payload, never a boot entry. The name must stay 8.3
   (INSTAL.BIN, not INSTALLED.BIN): a longer name becomes
   INSTAL~1.BIN on the FAT ESP and the UEFI file open for the
   long name fails, aborting the install. If INSTAL.BIN is missing
   the installer aborts with nothing written, instead of silently
   copying the running installer onto the target again.
   v1 scope, stated plainly:
   - Whole spare disk only. It erases the picked disk. It does NOT
     preserve Windows partitions, shrink Windows, or dual-boot. That
     multi-partition version is a separate, later step.
   - Only 512-byte sectors. Other sector sizes abort.
   - Partition size is user-chosen in MB, min 64, max 2048 in v1
     (keeps the FAT buffer bounded), default 512 if blank.
   - The boot device is excluded via Loaded Image DeviceHandle, so
     you cannot pick the CD/USB you booted from.
   - Double confirmation: type ERASE, then re-type the disk number.
   No writes happen before both confirmations pass. */
typedef EFI_STATUS (EFIAPI *EFI_HANDLE_PROTOCOL_FN)(EFI_HANDLE Handle, const EFI_GUID *Protocol, void **Interface);
typedef EFI_STATUS (EFIAPI *EFI_LOCATE_HANDLE_BUFFER_FN)(UINT32 SearchType, const EFI_GUID *Protocol, void *SearchKey, UINTN *NoHandles, EFI_HANDLE **Buffer);

static void install_u64_to_ascii(uint64_t v, char *out, size_t max)
{
    char tmp[24]; int i = 0, j = 0;
    if (max == 0) return;
    if (v == 0) { out[0] = '0'; out[1] = 0; return; }
    while (v > 0 && i < 20) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0 && j + 1 < (int)max) out[j++] = tmp[--i];
    out[j] = 0;
}
static void install_write_num(const char *prefix, uint64_t v, const char *suffix)
{
    char num[24]; char line[128]; size_t i = 0;
    install_u64_to_ascii(v, num, sizeof(num));
    while (prefix[i] && i + 1 < sizeof(line)) { line[i] = prefix[i]; i++; }
    { size_t k = 0; while (num[k] && i + 1 < sizeof(line)) line[i++] = num[k++]; }
    { size_t k = 0; while (suffix[k] && i + 1 < sizeof(line)) line[i++] = suffix[k++]; }
    line[i] = 0;
    terminal_write_ascii_line(line);
}
static int install_parse_u64(const CHAR16 *s, uint64_t *out)
{
    uint64_t v = 0; int digits = 0;
    if (!s || !out) return 0;
    while (*s == ' ' || *s == '\t') s++;
    while (*s >= '0' && *s <= '9') { v = v * 10 + (uint64_t)(*s - '0'); s++; digits++; }
    while (*s == ' ' || *s == '\t') s++;
    if (*s != 0 || digits == 0) return 0;
    *out = v; return 1;
}
static int install_equal_ascii(const CHAR16 *s, const char *a)
{
    /* Case-insensitive: QEMU sendkey and real keyboards differ on shift,
       and a confirmation should not fail on case alone. */
    while (*a) {
        CHAR16 c = *s;
        if (c >= 'a' && c <= 'z') c = (CHAR16)(c - 'a' + 'A');
        if (c != (CHAR16)(uint8_t)*a) return 0;
        s++; a++;
    }
    return *s == 0;
}
static uint32_t install_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu; size_t i; int b;
    for (i = 0; i < len; i++) {
        crc ^= data[i];
        for (b = 0; b < 8; b++) crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : (crc >> 1);
    }
    return crc ^ 0xFFFFFFFFu;
}
static void install_put32(uint8_t *p, uint32_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24); }
static void install_put64(uint8_t *p, uint64_t v) { int i; for (i=0;i<8;i++) p[i]=(uint8_t)(v>>(8*i)); }
static void install_put16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void install_put_utf16(uint8_t *p, const char *s, size_t max_chars)
{
    size_t i;
    for (i = 0; i < max_chars; i++) {
        if (s[i]) { p[2*i]=(uint8_t)s[i]; p[2*i+1]=0; } else { p[2*i]=0; p[2*i+1]=0; }
    }
}
static int install_read_boot_file(const CHAR16 *path, uint8_t **out_buf, uint64_t *out_len)
{
    EFI_FILE_PROTOCOL *file = 0; UINTN count; EFI_STATUS st;
    if (!gBS || !gBS->AllocatePool) return 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) return 0;
    st = gBS->AllocatePool(EfiLoaderData, 512 * 1024, (void **)out_buf);
    if (EFI_ERROR(st) || !*out_buf) { file->Close(file); return 0; }
    count = 512 * 1024;
    st = file->Read(file, &count, *out_buf);
    file->Close(file);
    if (EFI_ERROR(st) || count == 0 || count >= 512 * 1024) { gBS->FreePool(*out_buf); return 0; }
    *out_len = count; return 1;
}
static int install_write_blocks(EFI_BLOCK_IO_PROTOCOL *bio, uint64_t lba, const void *buf, uint64_t bytes)
{
    if (!bio || !bio->Media || bio->Media->BlockSize != 512) return 0;
    if (bytes % 512 != 0) return 0;
    return !EFI_ERROR(bio->WriteBlocks(bio, bio->Media->MediaId, lba, (UINTN)bytes, buf));
}


/* Boot-disk exclusion for HDD boot: LoadedImage DeviceHandle is the
   partition handle, not the whole-disk handle, so comparing handles
   alone does NOT exclude the boot disk. Parse the LoadedImage
   FilePath device path for an HD node (type 4, subtype 1) and
   return its PartitionStart LBA, or (uint64_t)-1 if there is none
   (e.g. CD boot). A whole-disk candidate whose GPT has a partition
   starting at that LBA is the boot disk and must be excluded. */
static uint64_t install_boot_partition_start(void)
{
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_HANDLE_PROTOCOL_FN handle_proto;
    EFI_GUID dp_guid = { 0x09576e91, 0x6d3f, 0x11d2, { 0x8e, 0x39, 0x00, 0xa0, 0xc9, 0x69, 0x72, 0x3b } };
    void *dpv = 0;
    uint8_t *dp;
    if (!gBS) return (uint64_t)-1;
    handle_proto = (EFI_HANDLE_PROTOCOL_FN)gBS->HandleProtocol;
    if (!handle_proto) return (uint64_t)-1;
    if (EFI_ERROR(handle_proto(gImageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded)) || !loaded || !loaded->DeviceHandle) return (uint64_t)-1;
    if (EFI_ERROR(handle_proto(loaded->DeviceHandle, &dp_guid, &dpv)) || !dpv) return (uint64_t)-1;
    dp = (uint8_t *)dpv;
    for (;;) {
        uint8_t type = dp[0], subtype = dp[1];
        uint16_t len = (uint16_t)(dp[2] | (dp[3] << 8));
        if (type == 0x7F && subtype == 0xFF) break;
        if (len < 4) break;
        if (type == 4 && subtype == 1 && len >= 42) {
            uint64_t start = 0;
            int k;
            for (k = 0; k < 8; k++) start |= (uint64_t)dp[8 + k] << (8 * k);
            return start;
        }
        dp += len;
    }
    return (uint64_t)-1;
}
static int install_disk_is_boot_disk(EFI_BLOCK_IO_PROTOCOL *bio, uint64_t boot_part_start)
{
    uint8_t *hdr = 0, *entries = 0;
    int i, found = 0;
    if (boot_part_start == (uint64_t)-1 || !bio || !bio->Media || bio->Media->BlockSize != 512) return 0;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, 512, (void **)&hdr))) return 0;
    if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, 1, 512, hdr))) { gBS->FreePool(hdr); return 0; }
    if (!(hdr[0]=='E' && hdr[1]=='F' && hdr[2]=='I' && hdr[3]==' ' && hdr[4]=='P' && hdr[5]=='A' && hdr[6]=='R' && hdr[7]=='T')) { gBS->FreePool(hdr); return 0; }
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, 16384, (void **)&entries))) { gBS->FreePool(hdr); return 0; }
    if (EFI_ERROR(bio->ReadBlocks(bio, bio->Media->MediaId, 2, 16384, entries))) { gBS->FreePool(hdr); gBS->FreePool(entries); return 0; }
    for (i = 0; i < 128; i++) {
        uint8_t *e = entries + i * 128;
        uint64_t first = 0;
        int k, nonzero = 0;
        for (k = 0; k < 16; k++) if (e[k]) nonzero = 1;
        if (!nonzero) continue;
        for (k = 0; k < 8; k++) first |= (uint64_t)e[32 + k] << (8 * k);
        if (first == boot_part_start) { found = 1; break; }
    }
    gBS->FreePool(hdr); gBS->FreePool(entries);
    return found;
}

static void terminal_install(void)
{
    EFI_HANDLE *handles = 0; UINTN handle_count = 0;
    EFI_HANDLE boot_device = 0;
    EFI_BLOCK_IO_PROTOCOL *disks[16]; uint64_t disk_mb[16]; int disk_count = 0;
    EFI_LOCATE_HANDLE_BUFFER_FN locate_buf;
    EFI_HANDLE_PROTOCOL_FN handle_proto;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    CHAR16 line[128];
    uint64_t choice = 0, size_mb = 512, confirm_num = 0;
    EFI_BLOCK_IO_PROTOCOL *bio; uint64_t disk_sectors, part_sectors, part_start, part_end;
    uint8_t *boot_file = 0, *kernel_file = 0, *startup_file = 0;
    uint64_t boot_len = 0, kernel_len = 0, startup_len = 0;
    uint8_t *sector = 0, *entries = 0, *fat = 0;
    uint32_t fat_sectors, spc, data_sectors, clusters;
    uint64_t data_start, fat_start, root_cluster = 2, efi_cluster = 3, bootdir_cluster = 4;
    uint64_t boot_start, kernel_start, startup_start, next_cluster;
    int i;
#if FISIX_INSTALLED_BUILD
    /* An installed copy must never install itself again: there is no
       INSTAL.BIN payload on an installed disk, and copying the
       running loader would recreate the self-copy bug. */
    terminal_write_ascii_line("This copy is already installed. The Installer is not available on an installed disk.");
    return;
#endif
    terminal_write_ascii_line("FisixOS installer v1 - whole spare disk only.");
    terminal_write_ascii_line("Installs the installed-only copy (no Installer option on the target), not this running installer.");
    terminal_write_ascii_line("WARNING: the picked disk will be ERASED. Windows partitions are NOT preserved in v1.");
    terminal_write_ascii_line("Do not pick the disk you booted from - it is excluded below, but check the list.");
    if (!gBS) { terminal_write_ascii_line("No boot services."); return; }
    locate_buf = (EFI_LOCATE_HANDLE_BUFFER_FN)gBS->LocateHandleBuffer;
    handle_proto = (EFI_HANDLE_PROTOCOL_FN)gBS->HandleProtocol;
    if (!locate_buf || !handle_proto) { terminal_write_ascii_line("Block I/O enumeration not available."); return; }
    if (!EFI_ERROR(handle_proto(gImageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded)) && loaded) boot_device = loaded->DeviceHandle;
    if (EFI_ERROR(locate_buf(2, &EFI_BLOCK_IO_PROTOCOL_GUID, 0, &handle_count, &handles)) || !handles) {
        terminal_write_ascii_line("No block devices found."); return;
    }
    for (i = 0; (UINTN)i < handle_count && disk_count < 16; i++) {
        EFI_BLOCK_IO_PROTOCOL *b = 0;
        if (EFI_ERROR(handle_proto(handles[i], &EFI_BLOCK_IO_PROTOCOL_GUID, (void **)&b)) || !b || !b->Media) continue;
        if (b->Media->LogicalPartition || !b->Media->MediaPresent || b->Media->ReadOnly) continue;
        if (b->Media->BlockSize != 512) continue;
        if (boot_device && handles[i] == boot_device) continue;
        if (install_disk_is_boot_disk(b, install_boot_partition_start())) continue;
        disks[disk_count] = b;
        disk_mb[disk_count] = (b->Media->LastBlock + 1) * 512 / (1024 * 1024);
        disk_count++;
    }
    install_write_num("Boot partition start LBA = ", install_boot_partition_start(), "");
    if (disk_count == 0) {
        terminal_write_ascii_line("No writable whole disks found (boot disk excluded). Attach a spare disk and try again.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    for (i = 0; i < disk_count; i++) {
        install_write_num("Disk ", (uint64_t)i, "");
        install_write_num("  size MB = ", disk_mb[i], "");
        terminal_write_ascii_line(disk_mb[i] == 0 ? "  (smaller than 1 MB - too small)" : "  removable/whole disk, 512-byte sectors");
    }
    terminal_write_ascii("Disk number: ");
    terminal_read_line(line, 128);
    if (!install_parse_u64(line, &choice) || choice >= (uint64_t)disk_count) {
        terminal_write_ascii_line("Invalid disk number. Aborted, nothing written."); 
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    bio = disks[choice];
    disk_sectors = bio->Media->LastBlock + 1;
    terminal_write_ascii("Partition size in MB (min 64, max 2048, blank = 512): ");
    terminal_read_line(line, 128);
    if (line[0] == 0) size_mb = 512;
    else if (!install_parse_u64(line, &size_mb)) {
        terminal_write_ascii_line("Invalid size. Aborted, nothing written.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    if (size_mb < 64 || size_mb > 2048) {
        terminal_write_ascii_line("Size out of range for v1 (64-2048 MB). Aborted, nothing written.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    part_sectors = size_mb * 2048;
    part_start = 2048;
    part_end = part_start + part_sectors - 1;
    if (part_end + 34 >= disk_sectors) {
        terminal_write_ascii_line("Partition does not fit on this disk with GPT backup space. Aborted, nothing written.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    install_write_num("You picked disk ", choice, " - this disk WILL BE ERASED.");
    terminal_write_ascii("Type ERASE to confirm: ");
    terminal_read_line(line, 128);
    if (!install_equal_ascii(line, "ERASE")) {
        terminal_write_ascii_line("Not confirmed. Aborted, nothing written.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    terminal_write_ascii("Type the disk number again: ");
    terminal_read_line(line, 128);
    if (!install_parse_u64(line, &confirm_num) || confirm_num != choice) {
        terminal_write_ascii_line("Disk number mismatch. Aborted, nothing written.");
        if (gBS->FreePool) gBS->FreePool(handles);
        return;
    }
    /* Read source files BEFORE writing anything, so a missing file aborts cleanly.
       NOTE: boot_file here is the installed-only payload INSTAL.BIN,
       NOT the running BOOTX64.EFI. It is written to the target as
       EFI/BOOT/BOOTX64.EFI below, so the target boots as the OS with
       no Installer option. Copying the running loader would be the
       old self-copy bug. */
    if (!install_read_boot_file((const CHAR16[]){'\\','E','F','I','\\','B','O','O','T','\\','I','N','S','T','A','L','.','B','I','N',0}, &boot_file, &boot_len)) {
        terminal_write_ascii_line("Could not read INSTAL.BIN from boot drive. Aborted, nothing written.");
        goto cleanup;
    }
    if (!install_read_boot_file((const CHAR16[]){'\\','k','e','r','n','e','l','.','b','i','n',0}, &kernel_file, &kernel_len)) {
        terminal_write_ascii_line("Could not read kernel.bin from boot drive. Aborted, nothing written.");
        goto cleanup;
    }
    if (!install_read_boot_file((const CHAR16[]){'\\','s','t','a','r','t','u','p','.','n','s','h',0}, &startup_file, &startup_len)) {
        terminal_write_ascii_line("Could not read startup.nsh from boot drive. Aborted, nothing written.");
        goto cleanup;
    }
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, 512, (void **)&sector)) ||
        EFI_ERROR(gBS->AllocatePool(EfiLoaderData, 16384, (void **)&entries))) {
        terminal_write_ascii_line("Out of memory. Aborted."); goto cleanup;
    }
    /* FAT32 geometry. */
    /* FAT32 needs >=65525 clusters, or firmware treats it as FAT16 and
       will not find EFI/BOOT. Small partitions therefore need small
       clusters: 2 sectors/cluster for <=256 MB, 8 for larger. */
    spc = (part_sectors <= 524288) ? 2 : 8;
    fat_sectors = 0;
    { int it; for (it = 0; it < 4; it++) {
        data_sectors = (uint32_t)(part_sectors - 32 - 2 * fat_sectors);
        clusters = data_sectors / spc;
        fat_sectors = (clusters + 2) * 4 / 512 + 1;
    } }
    data_sectors = (uint32_t)(part_sectors - 32 - 2 * fat_sectors);
    clusters = data_sectors / spc;
    fat_start = part_start + 32;
    data_start = fat_start + 2 * fat_sectors;
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, fat_sectors * 512, (void **)&fat))) {
        terminal_write_ascii_line("Out of memory for FAT. Aborted."); goto cleanup;
    }
    for (i = 0; (uint32_t)i < fat_sectors * 512 / 4; i++) ((uint32_t *)fat)[i] = 0;
    ((uint32_t *)fat)[0] = 0x0FFFFFF8u; ((uint32_t *)fat)[1] = 0x0FFFFFFFu; ((uint32_t *)fat)[2] = 0x0FFFFFFFu;
    ((uint32_t *)fat)[3] = 0x0FFFFFFFu; ((uint32_t *)fat)[4] = 0x0FFFFFFFu;
    /* File cluster chains, in order: BOOTX64, kernel, startup. */
    next_cluster = 5;
    boot_start = next_cluster; next_cluster += (boot_len + spc * 512 - 1) / (spc * 512);
    kernel_start = next_cluster; next_cluster += (kernel_len + spc * 512 - 1) / (spc * 512);
    startup_start = next_cluster; next_cluster += (startup_len + spc * 512 - 1) / (spc * 512);
    if (next_cluster > clusters + 2) { terminal_write_ascii_line("Files do not fit in partition. Aborted, nothing written yet."); goto cleanup; }
    { uint64_t c, s, e;
      c = boot_start; e = boot_start + (boot_len + spc*512 - 1)/(spc*512);
      for (s = c; s < e; s++) ((uint32_t *)fat)[s] = (s + 1 < e) ? (uint32_t)(s + 1) : 0x0FFFFFFFu;
      c = kernel_start; e = kernel_start + (kernel_len + spc*512 - 1)/(spc*512);
      for (s = c; s < e; s++) ((uint32_t *)fat)[s] = (s + 1 < e) ? (uint32_t)(s + 1) : 0x0FFFFFFFu;
      c = startup_start; e = startup_start + (startup_len + spc*512 - 1)/(spc*512);
      for (s = c; s < e; s++) ((uint32_t *)fat)[s] = (s + 1 < e) ? (uint32_t)(s + 1) : 0x0FFFFFFFu;
    }
    /* GPT: protective MBR, primary entries + header, backup entries + header. */
    for (i = 0; i < 512; i++) sector[i] = 0;
    sector[446 + 4] = 0xEE;
    install_put32(&sector[446 + 8], 1);
    install_put32(&sector[446 + 12], disk_sectors > 0xFFFFFFFFULL ? 0xFFFFFFFFu : (uint32_t)disk_sectors - 1);
    sector[510] = 0x55; sector[511] = 0xAA;
    if (!install_write_blocks(bio, 0, sector, 512)) { terminal_write_ascii_line("Write failed at MBR."); goto cleanup; }
    for (i = 0; i < 16384; i++) entries[i] = 0;
    { uint8_t *e = entries;
      static const uint8_t esp_type[16] = {0x28,0x73,0x2A,0xC1,0x1F,0xF8,0xD2,0x11,0xBA,0x4B,0x00,0xA0,0xC9,0x3E,0xC9,0x3B};
      for (i = 0; i < 16; i++) e[i] = esp_type[i];
      e[16]=0x01; e[17]=0x00; e[18]=0x00; e[19]=0x00; /* unique GUID, fixed v1 */
      install_put64(&e[32], part_start); install_put64(&e[40], part_end);
      install_put_utf16(&e[56], "FisixOS", 36);
    }
    if (!install_write_blocks(bio, 2, entries, 16384)) { terminal_write_ascii_line("Write failed at GPT entries."); goto cleanup; }
    { uint32_t entry_crc = install_crc32(entries, 16384);
      for (i = 0; i < 512; i++) sector[i] = 0;
      sector[0]='E'; sector[1]='F'; sector[2]='I'; sector[3]=' '; sector[4]='P'; sector[5]='A'; sector[6]='R'; sector[7]='T';
      install_put32(&sector[8], 0x00010000u); install_put32(&sector[12], 92);
      install_put64(&sector[24], 1); install_put64(&sector[32], disk_sectors - 1);
      install_put64(&sector[40], 34); install_put64(&sector[48], disk_sectors - 34);
      sector[56]=0x02; install_put64(&sector[72], 2); install_put32(&sector[80], 128); install_put32(&sector[84], 128);
      install_put32(&sector[88], entry_crc);
      install_put32(&sector[16], install_crc32(sector, 92));
      if (!install_write_blocks(bio, 1, sector, 512)) { terminal_write_ascii_line("Write failed at GPT header."); goto cleanup; }
      serial_mark("INSTALL_GPT_OK");
      /* Backup entries at LastBlock-32, backup header at LastBlock. */
      if (!install_write_blocks(bio, disk_sectors - 33, entries, 16384)) { terminal_write_ascii_line("Write failed at backup GPT entries."); goto cleanup; }
      for (i = 0; i < 512; i++) sector[i] = 0;
      sector[0]='E'; sector[1]='F'; sector[2]='I'; sector[3]=' '; sector[4]='P'; sector[5]='A'; sector[6]='R'; sector[7]='T';
      install_put32(&sector[8], 0x00010000u); install_put32(&sector[12], 92);
      install_put64(&sector[24], disk_sectors - 1); install_put64(&sector[32], 1);
      install_put64(&sector[40], 34); install_put64(&sector[48], disk_sectors - 34);
      sector[56]=0x02; install_put64(&sector[72], disk_sectors - 33); install_put32(&sector[80], 128); install_put32(&sector[84], 128);
      install_put32(&sector[88], entry_crc);
      install_put32(&sector[16], install_crc32(sector, 92));
      if (!install_write_blocks(bio, disk_sectors - 1, sector, 512)) { terminal_write_ascii_line("Write failed at backup GPT header."); goto cleanup; }
    }
    /* FAT32 boot sector, FSInfo, backup boot, FATs. */
    for (i = 0; i < 512; i++) sector[i] = 0;
    sector[0]=0xEB; sector[1]=0x58; sector[2]=0x90;
    sector[3]='F'; sector[4]='I'; sector[5]='S'; sector[6]='I'; sector[7]='X'; sector[8]='O'; sector[9]='S'; sector[10]=' ';
    install_put16(&sector[11], 512); sector[13]=(uint8_t)spc; install_put16(&sector[14], 32); sector[16]=2;
    install_put16(&sector[17], 0); sector[21]=0xF8; install_put16(&sector[22], 0);
    install_put16(&sector[24], 63); install_put16(&sector[26], 255);
    install_put32(&sector[28], (uint32_t)part_start); install_put32(&sector[32], (uint32_t)part_sectors);
    install_put32(&sector[36], fat_sectors); install_put16(&sector[40], 0); install_put16(&sector[42], 0);
    install_put32(&sector[44], 2); install_put16(&sector[48], 1); install_put16(&sector[50], 6);
    sector[64]=0x80; sector[66]=0x29; install_put32(&sector[67], 0x46495349u);
    { const char *lbl = "FISIXOS    "; for (i=0;i<11;i++) sector[71+i]=(uint8_t)lbl[i]; }
    { const char *typ = "FAT32   "; for (i=0;i<8;i++) sector[82+i]=(uint8_t)typ[i]; }
    sector[510]=0x55; sector[511]=0xAA;
    if (!install_write_blocks(bio, part_start, sector, 512)) { terminal_write_ascii_line("Write failed at FAT boot sector."); goto cleanup; }
    for (i = 0; i < 512; i++) sector[i] = 0;
    install_put32(&sector[0], 0x41615252u); install_put32(&sector[484], 0x61417272u);
    install_put32(&sector[488], clusters - (uint32_t)(next_cluster - 2)); install_put32(&sector[492], (uint32_t)next_cluster); install_put32(&sector[508], 0xAA550000u);
    if (!install_write_blocks(bio, part_start + 1, sector, 512)) { terminal_write_ascii_line("Write failed at FSInfo."); goto cleanup; }
    /* Rebuild boot sector for backup copy. */
    for (i = 0; i < 512; i++) sector[i] = 0;
    sector[0]=0xEB; sector[1]=0x58; sector[2]=0x90;
    sector[3]='F'; sector[4]='I'; sector[5]='S'; sector[6]='I'; sector[7]='X'; sector[8]='O'; sector[9]='S'; sector[10]=' ';
    install_put16(&sector[11], 512); sector[13]=(uint8_t)spc; install_put16(&sector[14], 32); sector[16]=2;
    sector[21]=0xF8; install_put16(&sector[24], 63); install_put16(&sector[26], 255);
    install_put32(&sector[28], (uint32_t)part_start); install_put32(&sector[32], (uint32_t)part_sectors);
    install_put32(&sector[36], fat_sectors); install_put32(&sector[44], 2); install_put16(&sector[48], 1); install_put16(&sector[50], 6);
    sector[64]=0x80; sector[66]=0x29; install_put32(&sector[67], 0x46495349u);
    { const char *lbl = "FISIXOS    "; for (i=0;i<11;i++) sector[71+i]=(uint8_t)lbl[i]; }
    { const char *typ = "FAT32   "; for (i=0;i<8;i++) sector[82+i]=(uint8_t)typ[i]; }
    sector[510]=0x55; sector[511]=0xAA;
    if (!install_write_blocks(bio, part_start + 6, sector, 512)) { terminal_write_ascii_line("Write failed at backup boot sector."); goto cleanup; }
    if (!install_write_blocks(bio, fat_start, fat, fat_sectors * 512)) { terminal_write_ascii_line("Write failed at FAT1."); goto cleanup; }
    if (!install_write_blocks(bio, fat_start + fat_sectors, fat, fat_sectors * 512)) { terminal_write_ascii_line("Write failed at FAT2."); goto cleanup; }
    serial_mark("INSTALL_FAT_OK");
    /* Directories and files. */
    { uint8_t *dir = 0;
      if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, spc * 512, (void **)&dir))) { terminal_write_ascii_line("Out of memory for directories."); goto cleanup; }
      for (i = 0; i < spc * 512; i++) dir[i] = 0;
      /* Root: EFI dir, KERNEL.BIN, STARTUP.NSH */
      { uint8_t *e = dir;
        const char *n = "EFI         "; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)efi_cluster); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21);
        e += 32; n = "KERNEL  BIN"; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x20; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)kernel_start); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21); install_put32(&e[28], (uint32_t)kernel_len);
        e += 32; n = "STARTUP NSH"; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x20; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)startup_start); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21); install_put32(&e[28], (uint32_t)startup_len);
        e += 32; n = "FISIXOS    "; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x08; install_put16(&e[20], 0); install_put16(&e[26], 0);
      }
      if (!install_write_blocks(bio, data_start, dir, spc * 512)) { terminal_write_ascii_line("Write failed at root directory."); gBS->FreePool(dir); goto cleanup; }
      for (i = 0; i < spc * 512; i++) dir[i] = 0;
      { uint8_t *e = dir;
        e[0]='.'; for (i=1;i<11;i++) e[i]=' '; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)efi_cluster); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21);
        e += 32; e[0]='.'; e[1]='.'; for (i=2;i<11;i++) e[i]=' '; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)root_cluster); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21);
        e += 32; const char *n = "BOOT        "; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)bootdir_cluster);
      }
      if (!install_write_blocks(bio, data_start + (efi_cluster - 2) * spc, dir, spc * 512)) { terminal_write_ascii_line("Write failed at EFI directory."); gBS->FreePool(dir); goto cleanup; }
      for (i = 0; i < spc * 512; i++) dir[i] = 0;
      { uint8_t *e = dir;
        e[0]='.'; for (i=1;i<11;i++) e[i]=' '; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)bootdir_cluster); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21);
        e += 32; e[0]='.'; e[1]='.'; for (i=2;i<11;i++) e[i]=' '; e[11]=0x10; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)efi_cluster); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21);
        e += 32; const char *n = "BOOTX64 EFI"; for (i=0;i<11;i++) e[i]=(uint8_t)n[i]; e[11]=0x20; install_put16(&e[20], 0); install_put16(&e[26], (uint16_t)boot_start); install_put16(&e[22], 0x5C21); install_put16(&e[24], 0x5C21); install_put32(&e[28], (uint32_t)boot_len);
      }
      if (!install_write_blocks(bio, data_start + (bootdir_cluster - 2) * spc, dir, spc * 512)) { terminal_write_ascii_line("Write failed at BOOT directory."); gBS->FreePool(dir); goto cleanup; }
      gBS->FreePool(dir);
      /* File data, cluster by cluster. */
      { struct { uint8_t *buf; uint64_t len; uint64_t start; } files[3];
        files[0].buf = boot_file; files[0].len = boot_len; files[0].start = boot_start;
        files[1].buf = kernel_file; files[1].len = kernel_len; files[1].start = kernel_start;
        files[2].buf = startup_file; files[2].len = startup_len; files[2].start = startup_start;
        for (i = 0; i < 3; i++) {
            uint64_t off = 0, cl = files[i].start;
            while (off < files[i].len) {
                uint64_t chunk = files[i].len - off; uint64_t cap = spc * 512;
                uint8_t *clbuf = 0;
                if (chunk > cap) chunk = cap;
                if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, spc * 512, (void **)&clbuf))) { terminal_write_ascii_line("Out of memory for file data."); goto cleanup; }
                for (int j = 0; j < spc * 512; j++) clbuf[j] = 0;
                for (uint64_t j = 0; j < chunk; j++) clbuf[j] = files[i].buf[off + j];
                if (!install_write_blocks(bio, data_start + (cl - 2) * spc, clbuf, spc * 512)) { terminal_write_ascii_line("Write failed at file data."); gBS->FreePool(clbuf); goto cleanup; }
                gBS->FreePool(clbuf);
                off += chunk; cl++;
            }
        }
      }
    }
    if (bio->FlushBlocks) bio->FlushBlocks(bio);
    serial_mark("INSTALL_FILES_OK");
    serial_mark("INSTALL_DONE");
    terminal_write_ascii_line("Install complete. Reboot, remove the install media, and boot from the target disk.");
    terminal_write_ascii_line("First boot there will show First-Time Setup (no user account was installed).");
cleanup:
    if (handles && gBS->FreePool) gBS->FreePool(handles);
    if (boot_file) gBS->FreePool(boot_file);
    if (kernel_file) gBS->FreePool(kernel_file);
    if (startup_file) gBS->FreePool(startup_file);
    if (sector) gBS->FreePool(sector);
    if (entries) gBS->FreePool(entries);
    if (fat) gBS->FreePool(fat);
}

static int terminal_execute_command(const CHAR16 *command);

#define TERMINAL_MAX_ARGS 8
#define TERMINAL_MAX_LINE 128
#define TERMINAL_MAX_EDITOR_LINES 8
#define TERMINAL_DEFAULT_DRIVE 'B'

static TerminalMode terminal_mode = TERMINAL_MODE_BATCH;
static int terminal_echo_enabled = 1;
static CHAR16 terminal_current_drive = TERMINAL_DEFAULT_DRIVE;
static CHAR16 terminal_current_path[TERMINAL_MAX_LINE] = {
    TERMINAL_DEFAULT_DRIVE, ':', '/', 0
};

static const CHAR16 autoexec_batch[] = {
    '@','e','c','h','o',' ','o','f','f','\r','\n',
    'h','e','l','p','\r','\n',
    's','t','a','t','u','s','\r','\n',
    'v','e','r','\r','\n',
    'l','s','\r','\n',
    'm','e','m','\r','\n',
    'c','l','e','a','r','\r','\n',
    0
};

static int terminal_match_command(const CHAR16 *command, const CHAR16 *name)
{
    while (*command && *name && *command == *name) {
        command++;
        name++;
    }

    return (*name == 0) && (*command == 0 || *command == ' ');
}

static const CHAR16 *terminal_command_args(const CHAR16 *command)
{
    while (*command && *command != ' ') {
        command++;
    }

    while (*command == ' ') {
        command++;
    }

    return command;
}

static void terminal_write(const CHAR16 *text)
{
    if (gST && gST->ConOut && gST->ConOut->OutputString) {
        gST->ConOut->OutputString(gST->ConOut, text);
    }
}

static void terminal_write_line(const CHAR16 *text)
{
    terminal_write(text);
    terminal_write((const CHAR16[]){ '\r', '\n', 0 });
}

/* First-time setup writes plain ASCII strings, so it converts them to
   CHAR16 here instead of spelling every message out as a CHAR16 array
   literal like the older terminal code does. */
static void terminal_write_ascii(const char *text)
{
    CHAR16 buffer[256];
    size_t i = 0;
    while (text && text[i] && i + 1 < sizeof(buffer) / sizeof(buffer[0])) {
        buffer[i] = (CHAR16)(uint8_t)text[i];
        i++;
    }
    buffer[i] = 0;
    terminal_write(buffer);
}

static void terminal_write_ascii_line(const char *text)
{
    terminal_write_ascii(text);
    terminal_write((const CHAR16[]){ '\r', '\n', 0 });
}

/* Clear the screen on every boot, so the firmware vendor logo does
   not stay behind FisixOS text. ConOut ClearScreen clears the text
   screen; filling the GOP framebuffer black clears a graphics logo
   that ConOut alone would leave. Best effort, never fatal. */
static void boot_clear_screen(void)
{
    if (gST && gST->ConOut && gST->ConOut->ClearScreen) {
        if (gST->ConOut->SetAttribute) gST->ConOut->SetAttribute(gST->ConOut, 0x07);
        gST->ConOut->ClearScreen(gST->ConOut);
    }
    if (gBS && gBS->LocateProtocol) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
        EFI_STATUS st = gBS->LocateProtocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, 0, (void **)&gop);
        if (!EFI_ERROR(st) && gop && gop->Mode && gop->Mode->FrameBufferBase && gop->Mode->Info) {
            uint64_t w = gop->Mode->Info->HorizontalResolution;
            uint64_t h = gop->Mode->Info->VerticalResolution;
            uint64_t pixels = w * h;
            if (pixels > 0 && pixels < 16 * 1024 * 1024) {
                uint32_t *fb = (uint32_t *)(uintptr_t)gop->Mode->FrameBufferBase;
                uint64_t i;
                for (i = 0; i < pixels; i++) fb[i] = 0;
            }
        }
    }
}

static int terminal_equal_str(const CHAR16 *a, const CHAR16 *b)
{
    if (!a || !b) return 0;
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == 0 && *b == 0;
}

static int terminal_has_space(const CHAR16 *s)
{
    if (!s) return 0;
    while (*s) { if (*s == ' ') return 1; s++; }
    return 0;
}

static void terminal_set_drive(CHAR16 drive)
{
    if (drive == 'B' || drive == 'C') {
        terminal_current_drive = drive;
        terminal_current_path[0] = drive;
        terminal_current_path[1] = ':';
        terminal_current_path[2] = '/';
        terminal_current_path[3] = 0;
    }
}

static int terminal_drive_prefix(const CHAR16 *path, CHAR16 *drive)
{
    if (path && path[0] >= 'A' && path[0] <= 'Z' && path[1] == ':') {
        *drive = path[0];
        return 1;
    }

    return 0;
}

static void terminal_show_drive_root(CHAR16 drive)
{
    eosfs_entry_t entries[EOSFS_MAX_ENTRIES];
    int count = 0;
    int i;

    if (!eosfs_list_path(terminal_current_path, entries, EOSFS_MAX_ENTRIES, &count)) {
        terminal_write_line((const CHAR16[]){ 'f','i','l','e',' ','s','y','s','t','e','m',' ','u','n','a','v','a','i','l','a','b','l','e',0 });
        return;
    }

    if (drive == 'C') {
        terminal_write_line((const CHAR16[]){ 'C',':','/',' ','-',' ','G','P','T',' ','d','i','s','k',' ','p','a','r','t','i','t','i','o','n',0 });
    } else {
        terminal_write_line((const CHAR16[]){ 'B',':','/',' ','-',' ','G','P','T',' ','d','i','s','k',' ','p','a','r','t','i','t','i','o','n',0 });
    }

    for (i = 0; i < count; ++i) {
        terminal_write(entries[i].name);
        terminal_write((const CHAR16[]){ ' ', 0 });
        terminal_write(entries[i].type == EOSFS_ENTRY_DIR ? (const CHAR16[]){ '<','d','i','r','>',0 } : (const CHAR16[]){ '<','f','i','l','e','>',0 });
        terminal_write_line((const CHAR16[]){ 0 });
    }
}

static const CHAR16 *terminal_current_prompt(void)
{
    static CHAR16 prompt[64];
    int index = 0;

    prompt[index++] = terminal_current_drive;
    prompt[index++] = ':';
    prompt[index++] = '/';
    prompt[index++] = ' ';

    if (terminal_mode == TERMINAL_MODE_BASH) {
        prompt[index++] = 'b';
        prompt[index++] = 'a';
        prompt[index++] = 's';
        prompt[index++] = 'h';
    } else {
        prompt[index++] = 'b';
        prompt[index++] = 'a';
        prompt[index++] = 't';
        prompt[index++] = 'c';
        prompt[index++] = 'h';
    }

    prompt[index++] = '>';
    prompt[index++] = ' ';
    prompt[index] = 0;

    return prompt;
}

static void terminal_set_mode(TerminalMode mode)
{
    terminal_mode = mode;
    terminal_echo_enabled = 1;
}

static void terminal_write_arg_list(CHAR16 *argv[], int argc, int start_index)
{
    int i;
    for (i = start_index; i < argc; ++i) {
        if (i > start_index) {
            terminal_write((const CHAR16[]){ ' ', 0 });
        }
        terminal_write(argv[i]);
    }
}

static int terminal_parse_command(const CHAR16 *command, CHAR16 *argv[], int max_args)
{
    static CHAR16 line[TERMINAL_MAX_LINE];
    size_t i = 0;
    int argc = 0;
    CHAR16 *cursor;

    while (*command && i < (TERMINAL_MAX_LINE - 1)) {
        line[i++] = *command++;
    }
    line[i] = 0;

    cursor = line;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }

    while (*cursor && argc < max_args) {
        argv[argc++] = cursor;
        while (*cursor && *cursor != ' ' && *cursor != '\t') {
            cursor++;
        }

        if (*cursor == 0) {
            break;
        }

        *cursor++ = 0;
        while (*cursor == ' ' || *cursor == '\t') {
            cursor++;
        }
    }

    return argc;
}

static int terminal_contains(const CHAR16 *text, const CHAR16 *needle)
{
    if (*needle == 0) return 1;
    while (*text) {
        const CHAR16 *a = text;
        const CHAR16 *b = needle;
        while (*a && *b && *a == *b) { ++a; ++b; }
        if (*b == 0) return 1;
        ++text;
    }
    return 0;
}

static void terminal_copy_segment(CHAR16 *destination, const CHAR16 *start, const CHAR16 *end)
{
    size_t length = 0;
    while (start < end && (*start == ' ' || *start == '\t')) ++start;
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) --end;
    while (start < end && length + 1 < TERMINAL_MAX_LINE) destination[length++] = *start++;
    destination[length] = 0;
}

static const CHAR16 *terminal_end(const CHAR16 *text)
{
    while (*text) ++text;
    return text;
}

static int terminal_try_pipeline(const CHAR16 *command)
{
    const CHAR16 *pipe = command;
    CHAR16 left[TERMINAL_MAX_LINE];
    CHAR16 right[TERMINAL_MAX_LINE];
    CHAR16 *argv[TERMINAL_MAX_ARGS];
    int argc;

    while (*pipe && *pipe != '|') ++pipe;
    if (*pipe != '|') return -1;
    terminal_copy_segment(left, command, pipe);
    terminal_copy_segment(right, pipe + 1, terminal_end(pipe + 1));
    argc = terminal_parse_command(left, argv, TERMINAL_MAX_ARGS);
    if (argc < 2 || !terminal_match_command(argv[0], (const CHAR16[]){'e','c','h','o',0})) {
        terminal_write_line((const CHAR16[]){'p','i','p','e','s',' ','c','u','r','r','e','n','t','l','y',' ','s','u','p','p','o','r','t',' ','e','c','h','o',' ','|',' ','g','r','e','p',0});
        return 0;
    }
    argc = terminal_parse_command(right, argv, TERMINAL_MAX_ARGS);
    if (argc != 2 || !terminal_match_command(argv[0], (const CHAR16[]){'g','r','e','p',0})) {
        terminal_write_line((const CHAR16[]){'u','s','a','g','e',':',' ','e','c','h','o',' ','<','t','e','x','t','>',' ','|',' ','g','r','e','p',' ','<','p','a','t','t','e','r','n','>',0});
        return 0;
    }
    terminal_copy_segment(left, command + 5, pipe);
    if (terminal_contains(left, argv[1])) terminal_write_line(left);
    return 0;
}

static int terminal_open_file(const CHAR16 *path, UINT64 mode, UINT64 attributes, EFI_FILE_PROTOCOL **file)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_STATUS status;
    if (!gBS || !gBS->LocateProtocol) return 0;
    if (fisix_selected_fs) {
        fs = fisix_selected_fs;
    } else {
        status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
        if (EFI_ERROR(status) || !fs) return 0;
    }
    if (EFI_ERROR(fs->OpenVolume(fs, &root))) return 0;
    status = root->Open(root, file, path, mode, attributes);
    root->Close(root);
    return !EFI_ERROR(status) && *file;
}

static int terminal_cat_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[120];
    CHAR16 text[121];
    UINTN count;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) return 0;
    do {
        count = sizeof(bytes);
        if (EFI_ERROR(file->Read(file, &count, bytes))) { file->Close(file); return 0; }
        for (UINTN i = 0; i < count; ++i) text[i] = bytes[i];
        text[count] = 0;
        if (count) terminal_write(text);
    } while (count == sizeof(bytes));
    file->Close(file);
    terminal_write_line((const CHAR16[]){0});
    return 1;
}

static int terminal_touch_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file)) return 0;
    file->Close(file);
    return 1;
}

static int terminal_remove_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    EFI_STATUS status;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &file)) return 0;
    status = file->Delete(file);
    return !EFI_ERROR(status);
}

static int terminal_write_file(const CHAR16 *path, CHAR16 *argv[], int argc, int start, int append)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[TERMINAL_MAX_LINE];
    UINTN count = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file)) return 0;
    if (append) file->SetPosition(file, (UINT64)-1);
    else if (EFI_ERROR(file->SetPosition(file, 0))) { file->Close(file); return 0; }
    for (int i = start; i < argc && count + 2 < sizeof(bytes); ++i) {
        if (i > start) bytes[count++] = ' ';
        for (int j = 0; argv[i][j] && count + 2 < sizeof(bytes); ++j) bytes[count++] = (uint8_t)argv[i][j];
    }
    bytes[count++] = '\n';
    if (EFI_ERROR(file->Write(file, &count, bytes))) { file->Close(file); return 0; }
    if (file->Flush) file->Flush(file);
    file->Close(file);
    return 1;
}

static int terminal_source_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t bytes[4096];
    UINTN count = sizeof(bytes) - 1;
    CHAR16 line[TERMINAL_MAX_LINE];
    size_t line_length = 0;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) return 0;
    if (EFI_ERROR(file->Read(file, &count, bytes))) { file->Close(file); return 0; }
    file->Close(file);
    for (UINTN i = 0; i < count; ++i) {
        if (bytes[i] == '\r') continue;
        if (bytes[i] == '\n') {
            line[line_length] = 0;
            if (line_length) terminal_execute_command(line);
            line_length = 0;
        } else if (line_length + 1 < TERMINAL_MAX_LINE) line[line_length++] = bytes[i];
    }
    if (line_length) { line[line_length] = 0; terminal_execute_command(line); }
    return 1;
}


/* ================= .fse runner ================= */
static void fse_api_print(const char *s)
{
    CHAR16 buf[256];
    size_t i = 0;
    if (!s) return;
    while (s[i] && i + 1 < 256) { buf[i] = (CHAR16)(uint8_t)s[i]; i++; }
    buf[i] = 0;
    terminal_write(buf);
}
static void fse_api_print_u64(uint64_t v)
{
    char tmp[24], out[24];
    int i = 0, j = 0;
    if (v == 0) { fse_api_print("0"); return; }
    while (v > 0 && i < 20) { tmp[i++] = (char)('0' + (v % 10)); v /= 10; }
    while (i > 0) out[j++] = tmp[--i];
    out[j] = 0;
    fse_api_print(out);
}
static void fse_api_print_newline(void)
{
    terminal_write((const CHAR16[]){ '\r', '\n', 0 });
}
static int fse_api_read_key(void)
{
    EFI_INPUT_KEY key;
    if (!gST || !gST->ConIn || !gST->ConIn->ReadKeyStroke) return -1;
    for (;;) {
        gST->ConIn->ReadKeyStroke(gST->ConIn, &key);
        if (key.UnicodeChar) return (int)key.UnicodeChar;
        if (key.ScanCode) return (int)(0x10000 + key.ScanCode);
    }
}
static int fse_ascii_to_char16(const char *s, CHAR16 *out, size_t max)
{
    size_t i = 0;
    if (!s || !out || max == 0) return 0;
    while (s[i] && i + 1 < max) { out[i] = (CHAR16)(uint8_t)s[i]; i++; }
    out[i] = 0;
    return 1;
}
static int fse_api_file_read(const char *path, char *buf, unsigned int buf_size)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *file = 0;
    UINTN total = 0;
    if (!path || !buf || buf_size == 0) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ, 0, &file)) return -1;
    while (total + 1 < buf_size) {
        UINTN chunk = buf_size - total - 1;
        EFI_STATUS st = file->Read(file, &chunk, buf + total);
        if (EFI_ERROR(st) || chunk == 0) break;
        total += chunk;
    }
    buf[total] = 0;
    file->Close(file);
    return (int)total;
}
static int fse_api_file_write(const char *path, const char *buf, unsigned int len)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *file = 0;
    UINTN count;
    if (!path || !buf) return -1;
    if (fisix_live_boot) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file)) return -1;
    if (file->SetPosition && EFI_ERROR(file->SetPosition(file, 0))) { file->Close(file); return -1; }
    count = len;
    if (EFI_ERROR(file->Write(file, &count, buf)) || count != len) { file->Close(file); return -1; }
    if (file->Flush) file->Flush(file);
    file->Close(file);
    return 0;
}
/* ---- .fse API v2 ---- */
static int fse_api_dir_list(const char *path, char *buf, unsigned int buf_size)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *dir = 0, *root = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    int count = 0;
    unsigned int used = 0;
    uint8_t info_buf[512];
    if (!buf || buf_size == 0) return -1;
    buf[0] = 0;
    if (!path || !path[0] || (path[0] == '\\' && !path[1]) || (path[0] == '/' && !path[1])) {
        if (!gBS || !gBS->LocateProtocol) return -1;
        if (fisix_selected_fs) fs = fisix_selected_fs;
        else if (EFI_ERROR(gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs)) || !fs) return -1;
        if (EFI_ERROR(fs->OpenVolume(fs, &root)) || !root) return -1;
        dir = root;
    } else {
        if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
        if (!terminal_open_file(wpath, EFI_FILE_MODE_READ, 0, &dir)) return -1;
    }
    for (;;) {
        UINTN sz = sizeof(info_buf);
        EFI_FILE_INFO *info;
        EFI_STATUS st = dir->Read(dir, &sz, info_buf);
        if (EFI_ERROR(st) || sz == 0) break;
        info = (EFI_FILE_INFO *)info_buf;
        if (!info->FileName[0] || info->FileName[0] == '.') continue;
        {
            char name[128];
            unsigned int ni = 0, k = 0;
            int is_dir = (info->Attribute & EFI_FILE_DIRECTORY) ? 1 : 0;
            while (info->FileName[ni] && ni + 1 < sizeof(name)) { name[ni] = (char)(uint8_t)info->FileName[ni]; ni++; }
            name[ni] = 0;
            /* Skip a name that would not fit, rather than cutting it. */
            if (used + ni + (is_dir ? 1 : 0) + 1 + 1 > buf_size) continue;
            for (k = 0; k < ni; k++) buf[used++] = name[k];
            if (is_dir) buf[used++] = '/';
            buf[used++] = '\n';
            buf[used] = 0;
            count++;
        }
    }
    dir->Close(dir);
    return count;
}
static int fse_api_file_delete(const char *path)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *file = 0;
    if (!path) return -1;
    if (fisix_live_boot) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE, 0, &file)) return -1;
    if (EFI_ERROR(file->Delete(file))) return -1;
    return 0;
}
static int fse_api_file_rename(const char *old_path, const char *new_path)
{
    CHAR16 wold[128], wnew[128];
    EFI_FILE_PROTOCOL *src = 0, *dst = 0;
    uint8_t chunk[1024];
    uint8_t info_buf[512];
    UINTN isz;
    if (!old_path || !new_path) return -1;
    if (fisix_live_boot) return -1;
    if (!fse_ascii_to_char16(old_path, wold, 128) || !fse_ascii_to_char16(new_path, wnew, 128)) return -1;
    if (!terminal_open_file(wold, EFI_FILE_MODE_READ, 0, &src)) return -1;
    isz = sizeof(info_buf);
    if (!EFI_ERROR(src->GetInfo(src, &EFI_FILE_INFO_GUID, &isz, info_buf))) {
        if (((EFI_FILE_INFO *)info_buf)->Attribute & EFI_FILE_DIRECTORY) { src->Close(src); return -1; }
    }
    if (!terminal_open_file(wnew, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &dst)) { src->Close(src); return -1; }
    for (;;) {
        UINTN n = sizeof(chunk);
        EFI_STATUS st = src->Read(src, &n, chunk);
        if (EFI_ERROR(st)) { src->Close(src); dst->Close(dst); fse_api_file_delete(new_path); return -1; }
        if (n == 0) break;
        {
            UINTN w = n;
            if (EFI_ERROR(dst->Write(dst, &w, chunk)) || w != n) { src->Close(src); dst->Close(dst); fse_api_file_delete(new_path); return -1; }
        }
    }
    src->Close(src);
    if (dst->Flush) dst->Flush(dst);
    dst->Close(dst);
    return fse_api_file_delete(old_path);
}
static int fse_api_file_mkdir(const char *path)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *dir = 0;
    if (!path) return -1;
    if (fisix_live_boot) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_DIRECTORY, &dir)) return -1;
    dir->Close(dir);
    return 0;
}
static int fse_api_file_exists(const char *path)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *file = 0;
    if (!path) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ, 0, &file)) return 0;
    file->Close(file);
    return 1;
}
static int fse_api_file_size(const char *path)
{
    CHAR16 wpath[128];
    EFI_FILE_PROTOCOL *file = 0;
    uint8_t info_buf[512];
    UINTN sz = sizeof(info_buf);
    uint64_t size;
    if (!path) return -1;
    if (!fse_ascii_to_char16(path, wpath, 128)) return -1;
    if (!terminal_open_file(wpath, EFI_FILE_MODE_READ, 0, &file)) return -1;
    if (EFI_ERROR(file->GetInfo(file, &EFI_FILE_INFO_GUID, &sz, info_buf))) { file->Close(file); return -1; }
    if (((EFI_FILE_INFO *)info_buf)->Attribute & EFI_FILE_DIRECTORY) { file->Close(file); return -1; }
    size = ((EFI_FILE_INFO *)info_buf)->FileSize;
    file->Close(file);
    return size > 0x7fffffffULL ? -1 : (int)size;
}
static void fse_api_get_time(unsigned int *year, unsigned int *month, unsigned int *day,
                             unsigned int *hour, unsigned int *minute, unsigned int *second)
{
    /* UEFI RuntimeServices: 24-byte table header, then GetTime. */
    typedef struct { EFI_TABLE_HEADER Hdr; EFI_STATUS (EFIAPI *GetTime)(EFI_TIME *Time, void *Capabilities); } fisix_runtime_t;
    EFI_TIME tm;
    if (year) *year = 0;
    if (month) *month = 0;
    if (day) *day = 0;
    if (hour) *hour = 0;
    if (minute) *minute = 0;
    if (second) *second = 0;
    if (!gST || !gST->RuntimeServices) return;
    if (EFI_ERROR(((fisix_runtime_t *)gST->RuntimeServices)->GetTime(&tm, 0))) return;
    if (year) *year = tm.Year;
    if (month) *month = tm.Month;
    if (day) *day = tm.Day;
    if (hour) *hour = tm.Hour;
    if (minute) *minute = tm.Minute;
    if (second) *second = tm.Second;
}
static void fse_api_clear_screen(void)
{
    boot_clear_screen();
}
static void fse_api_sleep_ms(unsigned int ms)
{
    typedef EFI_STATUS (EFIAPI *fisix_stall_t)(UINTN Microseconds);
    if (gBS && gBS->Stall) ((fisix_stall_t)gBS->Stall)((UINTN)ms * 1000);
}
static fisix_api_t fse_api = { fse_api_print, fse_api_print_u64, fse_api_print_newline, fse_api_read_key, fse_api_file_read, fse_api_file_write,
    fse_api_dir_list, fse_api_file_delete, fse_api_file_rename, fse_api_file_mkdir, fse_api_file_exists, fse_api_file_size,
    fse_api_get_time, fse_api_clear_screen, fse_api_sleep_ms };

static int fse_run_file(const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *file = 0;
    fse_header_t hdr;
    UINTN count;
    uint64_t load_addr = FISIX_FSE_LOAD_ADDRESS;
    EFI_ALLOCATE_PAGES alloc_pages;
    EFI_STATUS st;
    uint8_t *code = 0;
    fse_entry_t entry;
    int rc;
    if (!terminal_open_file(path, EFI_FILE_MODE_READ, 0, &file)) {
        terminal_write_ascii_line("fse: could not open file.");
        return 0;
    }
    count = sizeof(hdr);
    st = file->Read(file, &count, &hdr);
    if (EFI_ERROR(st) || count != sizeof(hdr)) { terminal_write_ascii_line("fse: could not read header."); file->Close(file); return 0; }
    if (hdr.magic != FISIX_FSE_MAGIC || hdr.version != FISIX_FSE_VERSION || hdr.api_version > 2 ||
        hdr.entry_offset < FISIX_FSE_HEADER_SIZE || hdr.entry_offset >= FISIX_FSE_HEADER_SIZE + hdr.code_size ||
        hdr.code_size == 0 || hdr.code_size > FISIX_FSE_MAX_CODE_SIZE) {
        terminal_write_ascii_line("fse: bad header (not a Fisix .fse v1 file?).");
        file->Close(file);
        return 0;
    }
    alloc_pages = (EFI_ALLOCATE_PAGES)gBS->AllocatePages;
    st = alloc_pages(AllocateAddress, EfiLoaderData, (hdr.code_size + 4095) / 4096, &load_addr);
    if (EFI_ERROR(st)) {
        terminal_write_ascii_line("fse: could not allocate 0x300000 for the program.");
        file->Close(file);
        return 0;
    }
    code = (uint8_t *)(uintptr_t)load_addr;
    {
        UINTN total = 0;
        while (total < hdr.code_size) {
            UINTN chunk = hdr.code_size - total;
            st = file->Read(file, &chunk, code + total);
            if (EFI_ERROR(st) || chunk == 0) break;
            total += chunk;
        }
        file->Close(file);
        if (total != hdr.code_size) {
            terminal_write_ascii_line("fse: truncated code.");
            if (gBS && gBS->FreePages) ((EFI_STATUS (EFIAPI *)(uint64_t, UINTN))gBS->FreePages)(load_addr, (hdr.code_size + 4095) / 4096);
            return 0;
        }
    }
    entry = (fse_entry_t)(uintptr_t)(load_addr + (hdr.entry_offset - FISIX_FSE_HEADER_SIZE));
    serial_mark("FSE_RUN_START");
    rc = entry(&fse_api);
    serial_mark("FSE_RUN_DONE");
    /* Free the .fse pages, otherwise a second run in the same
     * boot fails AllocateAddress at 0x300000 (found while
     * testing browsy.fse twice in one boot). */
    if (gBS && gBS->FreePages) ((EFI_STATUS (EFIAPI *)(uint64_t, UINTN))gBS->FreePages)(load_addr, (hdr.code_size + 4095) / 4096);
    terminal_write_ascii("fse exited with code ");
    fse_api_print_u64((uint64_t)(rc < 0 ? 0 : rc));
    terminal_write_ascii_line("");
    return 0;
}
static int fse_path_ends_fse(const CHAR16 *s)
{
    size_t n = 0;
    while (s[n]) n++;
    if (n < 4) return 0;
    return (s[n-4] == '.' || s[n-4] == '.') &&
        (s[n-3] == 'f' || s[n-3] == 'F') &&
        (s[n-2] == 's' || s[n-2] == 'S') &&
        (s[n-1] == 'e' || s[n-1] == 'E');
}

static int terminal_execute_command(const CHAR16 *command)
{
    const CHAR16 *separator = command;
    CHAR16 *argv[TERMINAL_MAX_ARGS];
    int argc = terminal_parse_command(command, argv, TERMINAL_MAX_ARGS);
    const CHAR16 *args = terminal_command_args(command);

    while (*separator && *separator != '&') ++separator;
    if (*separator == '&') {
        CHAR16 left[TERMINAL_MAX_LINE];
        CHAR16 right[TERMINAL_MAX_LINE];
        terminal_copy_segment(left, command, separator);
        terminal_copy_segment(right, separator + 1, terminal_end(separator + 1));
        terminal_execute_command(left);
        return terminal_execute_command(right);
    }

    if (terminal_try_pipeline(command) >= 0) return 0;

    if (argc == 0) {
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ '@','e','c','h','o',0 })) {
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'o','f','f',0 })) {
            terminal_echo_enabled = 0;
        } else if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'o','n',0 })) {
            terminal_echo_enabled = 1;
        }
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','m',0 })) {
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','w','i','t','c','h','l','a','n','g',0 })) {
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'b','a','s','h',0 })) {
            terminal_set_mode(TERMINAL_MODE_BASH);
            terminal_write_line((const CHAR16[]){ 's','w','i','t','c','h','e','d',' ','t','o',' ','b','a','s','h',0 });
            return 0;
        }
        if (argc > 1 && terminal_match_command(argv[1], (const CHAR16[]){ 'b','a','t','c','h',0 })) {
            terminal_set_mode(TERMINAL_MODE_BATCH);
            terminal_write_line((const CHAR16[]){ 's','w','i','t','c','h','e','d',' ','t','o',' ','b','a','t','c','h',0 });
            return 0;
        }
        terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','s','w','i','t','c','h','l','a','n','g',' ','b','a','s','h',' ','|',' ','b','a','t','c','h',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'i','n','s','t','a','l','l',0 })) {
#if FISIX_INSTALLED_BUILD
        terminal_write_ascii_line("This copy is already installed. The Installer is not available on an installed disk.");
#else
        terminal_install();
#endif
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','u','n',0 })) {
        if (argc != 2) { terminal_write_ascii_line("usage: run <file.fse>"); return 0; }
        fse_run_file(argv[1]);
        return 0;
    }

    if (argc >= 1 && fse_path_ends_fse(argv[0])) {
        fse_run_file(argv[0]);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'h','e','l','p',0 })) {
        terminal_write_line((const CHAR16[]){
            'a','v','a','i','l','a','b','l','e',' ','c','o','m','m','a','n','d','s',':',
            ' ','h','e','l','p',',',' ','e','c','h','o',',',' ','s','t','a','t','u','s',',',
            ' ','s','y','s','t','e','m',',',' ','v','e','r',',',' ','d','i','r',',',' ','l','s',',',
            ' ','m','e','m',',',' ','c','l','e','a','r',',',' ','c','l','s',',',' ','p','a','u','s','e',',',
            ' ','e','x','i','t',',',' ','s','h','u','t','d','o','w','n',',',' ','r','e','b','o','o','t',',',
            ' ','t','y','p','e',',',' ','s','e','t',',',' ','t','i','m','e',',',' ','d','a','t','e',',',
            ' ','c','d',',',' ','c','o','p','y',',',' ','d','e','l',',',' ','r','e','n',',',' ','m','d',',',' ','r','d',',',
            ' ','p','a','t','h',',',' ','w','h','o','a','m','i',',',' ','p','w','d',',',' ','s','w','i','t','c','h','l','a','n','g',',',' ','i','n','s','t','a','l','l',',',' ','g','u','i',',',' ','r','u','n',' ','<','f','i','l','e','.','f','s','e','>',0
        });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'e','c','h','o',0 })) {
        terminal_write_arg_list(argv, argc, 1);
        terminal_write_line((const CHAR16[]){ 0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'n','a','n','o',0 })) {
        terminal_run_nano();
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','s',0 })) {
        terminal_write_line((const CHAR16[]){ 'a','p','p','s',':',' ','n','a','n','o',',',' ','c','a','l','c',',',' ','c','a','t',',',' ','m','e','m','o',',',' ','t','o','d','o',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','a','l','c',0 })) {
        terminal_write_line((const CHAR16[]){ 'c','a','l','c','u','l','a','t','o','r',' ','s','t','u','b',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','a','t',0 })) {
        if (argc != 2 || !terminal_cat_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','c','a','t',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','o','u','c','h',0 })) {
        if (argc != 2 || !terminal_touch_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','t','o','u','c','h',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','m',0 })) {
        if (argc != 2 || !terminal_remove_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','r','m',' ','<','f','i','l','e','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'w','r','i','t','e',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','e','n','d',0 })) {
        int append = terminal_match_command(argv[0], (const CHAR16[]){ 'a','p','p','e','n','d',0 });
        if (argc < 3 || !terminal_write_file(argv[1], argv, argc, 2, append)) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','w','r','i','t','e','|','a','p','p','e','n','d',' ','<','f','i','l','e','>',' ','<','t','e','x','t',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','o','u','r','c','e',0 })) {
        if (argc != 2 || !terminal_source_file(argv[1])) terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','s','o','u','r','c','e',' ','<','s','c','r','i','p','t','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','e','m','o',0 })) {
        terminal_write_line((const CHAR16[]){ 'm','e','m','o',' ','a','p','p',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','o','d','o',0 })) {
        terminal_write_line((const CHAR16[]){ 't','o','d','o',':',' ','s','o','m','e',' ','m','o','r','e',' ','j','u','n','k',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','t','a','t','u','s',0 })) {
        terminal_write_line((const CHAR16[]){ 'o','k',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','y','s','t','e','m',0 })) {
        terminal_write_line(args);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'v','e','r',0 })) {
        terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','b','a','t','c','h',' ','t','e','r','m','i','n','a','l',' ','0','.','1',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','i','r',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'l','s',0 })) {
        if (argc > 1 && terminal_drive_prefix(argv[1], &terminal_current_drive)) {
            terminal_set_drive(terminal_current_drive);
        }

        terminal_show_drive_root(terminal_current_drive);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','e','m',0 })) {
        terminal_write_line((const CHAR16[]){ 'm','e','m','o','r','y',' ','m','a','n','a','g','e','r',' ','r','e','a','d','y',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','l','e','a','r',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 'c','l','s',0 })) {
        boot_clear_screen();
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','u','s','e',0 })) {
        terminal_write_line((const CHAR16[]){ 'p','r','e','s','s',' ','a','n','y',' ','k','e','y',' ','t','o',' ','c','o','n','t','i','n','u','e',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'g','u','i',0 })) {
        terminal_write_line((const CHAR16[]){ 'S','t','a','r','t','i','n','g',' ','t','h','e',' ','F','i','s','i','x','O','S',' ','G','U','I','.','.','.',0 });
        serial_mark("GUI_CMD");
        return 1;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'e','x','i','t',0 }) || terminal_match_command(argv[0], (const CHAR16[]){ 's','h','u','t','d','o','w','n',0 })) {
        terminal_write_line((const CHAR16[]){ 's','h','u','t','d','o','w','n',' ','c','o','m','m','a','n','d',' ','r','e','c','e','i','v','e','d',0 });
        return 1;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','b','o','o','t',0 })) {
        terminal_write_line((const CHAR16[]){ 'r','e','b','o','o','t',' ','c','o','m','m','a','n','d',' ','r','e','c','e','i','v','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','y','p','e',0 })) {
        terminal_write_line((const CHAR16[]){ 'b','a','t',' ','s','c','r','i','p','t',' ','e','x','e','c','u','t','i','o','n',' ','s','u','p','p','o','r','t',' ','a','c','t','i','v','e',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 's','e','t',0 })) {
        terminal_write_line((const CHAR16[]){ 'e','n','v','i','r','o','n','m','e','n','t',' ','v','a','r','i','a','b','l','e','s',' ','s','e','t',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 't','i','m','e',0 })) {
        terminal_write_line((const CHAR16[]){ '0','0',':','0','0',':','0','0',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','a','t','e',0 })) {
        terminal_write_line((const CHAR16[]){ '0','6','/','0','8','/','2','0','2','6',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','d',0 })) {
        if (argc > 1 && terminal_drive_prefix(argv[1], &terminal_current_drive)) {
            terminal_set_drive(terminal_current_drive);
            terminal_write_line((const CHAR16[]){ 'd','r','i','v','e',' ','s','e','t',' ','t','o',0 });
            terminal_write_line(terminal_current_path);
            return 0;
        }

        terminal_write_line(terminal_current_path);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'c','o','p','y',0 })) {
        terminal_write_line((const CHAR16[]){ 'c','o','p','y',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'd','e','l',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','e','l','e','t','e',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','e','n',0 })) {
        terminal_write_line((const CHAR16[]){ 'r','e','n','a','m','e',' ','o','p','e','r','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'm','d',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','i','r','e','c','t','o','r','y',' ','c','r','e','a','t','i','o','n',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'r','d',0 })) {
        terminal_write_line((const CHAR16[]){ 'd','i','r','e','c','t','o','r','y',' ','r','e','m','o','v','a','l',' ','s','u','p','p','o','r','t','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','t','h',0 })) {
        terminal_write_line((const CHAR16[]){ 'p','a','t','h',' ','s','e','t',' ','t','o',' ','F','i','s','i','x',' ','O','S',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'w','h','o','a','m','i',0 })) {
        terminal_write_line(accounts_current_user());
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'g','r','e','p',0 })) {
        if (argc < 3) {
            terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','g','r','e','p',' ','<','p','a','t','t','e','r','n','>',' ','<','t','e','x','t','>',0 });
            return 0;
        }
        for (int i = 2; i < argc; ++i) if (terminal_contains(argv[i], argv[1])) terminal_write_line(argv[i]);
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'u','s','e','r','s',0 })) {
        for (int i = 0; i < accounts_count(); ++i) terminal_write_line(accounts_user_at(i));
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'u','s','e','r','a','d','d',0 })) {
        if (argc == 3 && accounts_add(argv[1], argv[2])) terminal_write_line((const CHAR16[]){ 'u','s','e','r',' ','c','r','e','a','t','e','d',0 });
        else terminal_write_line((const CHAR16[]){ 'u','s','a','g','e',':',' ','u','s','e','r','a','d','d',' ','<','n','a','m','e','>',' ','<','p','a','s','s','w','o','r','d','>',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','a','s','s','w','d',0 })) {
        CHAR16 old_password[32];
        CHAR16 new_password[32];
        terminal_write((const CHAR16[]){ 'c','u','r','r','e','n','t',' ','p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(old_password, sizeof(old_password) / sizeof(old_password[0]));
        terminal_echo_enabled = 1;
        terminal_write((const CHAR16[]){ 'n','e','w',' ','p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(new_password, sizeof(new_password) / sizeof(new_password[0]));
        terminal_echo_enabled = 1;
        terminal_write_line(accounts_change_password(old_password, new_password)
            ? (const CHAR16[]){ 'p','a','s','s','w','o','r','d',' ','u','p','d','a','t','e','d',0 }
            : (const CHAR16[]){ 'p','a','s','s','w','o','r','d',' ','n','o','t',' ','c','h','a','n','g','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'l','o','g','i','n',0 })) {
        terminal_login();
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'p','w','d',0 })) {
        terminal_write_line((const CHAR16[]){ '/',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'b','a','s','h',0 })) {
        terminal_set_mode(TERMINAL_MODE_BASH);
        terminal_write_line((const CHAR16[]){ 'b','a','s','h',' ','m','o','d','e',' ','e','n','a','b','l','e','d',0 });
        return 0;
    }

    if (terminal_match_command(argv[0], (const CHAR16[]){ 'b','a','t','c','h',0 })) {
        terminal_set_mode(TERMINAL_MODE_BATCH);
        terminal_write_line((const CHAR16[]){ 'b','a','t','c','h',' ','m','o','d','e',' ','e','n','a','b','l','e','d',0 });
        return 0;
    }

    terminal_write_line((const CHAR16[]){ 'u','n','k','n','o','w','n',' ','c','o','m','m','a','n','d',0 });
    return 0;
}

static void terminal_run_batch_file(const CHAR16 *batch_data)
{
    const CHAR16 *line_start = batch_data;

    while (*line_start) {
        const CHAR16 *line_end = line_start;
        CHAR16 line[128];
        size_t offset = 0;

        while (*line_end && *line_end != '\r' && *line_end != '\n' && offset < (sizeof(line) - 1)) {
            line[offset++] = *line_end++;
        }

        line[offset] = 0;

        if (offset > 0) {
            if (terminal_echo_enabled) {
                terminal_write(terminal_current_prompt());
            }
            terminal_execute_command(line);
        }

        while (*line_end == '\r' || *line_end == '\n') {
            line_end++;
        }

        line_start = line_end;
    }
}

static int terminal_read_line(CHAR16 *line, size_t max_length)
{
    size_t length = 0;
    EFI_INPUT_KEY key;

    while (gST && gST->ConIn && gST->ConIn->ReadKeyStroke) {
        gST->ConIn->ReadKeyStroke(gST->ConIn, &key);

        if (key.UnicodeChar == 0) {
            continue;
        }

        if (key.UnicodeChar == '\r') {
            terminal_write((const CHAR16[]){ '\r', '\n', 0 });
            line[length] = 0;
            return (int)length;
        }

        if (key.UnicodeChar == '\b') {
            if (length > 0) {
                length--;
                line[length] = 0;
                terminal_write((const CHAR16[]){ '\b', ' ', '\b', 0 });
            }
            continue;
        }

        if (length + 1 >= max_length) {
            continue;
        }

        if (terminal_echo_enabled) {
            terminal_write((const CHAR16[]){ key.UnicodeChar, 0 });
        }

        line[length++] = key.UnicodeChar;
    }

    return 0;
}

static void terminal_run_nano(void)
{
    int line_count = 0;

    terminal_write_line((const CHAR16[]){ 'n','a','n','o','-','l','i','k','e',' ','e','d','i','t','o','r',' ','e','n','t','e','r','e','d',0 });
    terminal_write_line((const CHAR16[]){ 't','y','p','e',' ','l','i','n','e','s',',',' ','s','a','v','e',',',' ','o','r',' ','e','x','i','t',0 });

    for (;;) {
        CHAR16 line[TERMINAL_MAX_LINE];
        size_t length;
        terminal_write((const CHAR16[]){ 'n','a','n','o','>',' ',0 });
        length = (size_t)terminal_read_line(line, sizeof(line) / sizeof(line[0]));

        if (length == 0) {
            continue;
        }

        if (terminal_match_command(line, (const CHAR16[]){ 'e','x','i','t',0 }) || terminal_match_command(line, (const CHAR16[]){ 'q','u','i','t',0 })) {
            terminal_write_line((const CHAR16[]){ 'e','x','i','t','i','n','g',' ','n','a','n','o',0 });
            return;
        }

        if (terminal_match_command(line, (const CHAR16[]){ 's','a','v','e',0 })) {
            terminal_write_line((const CHAR16[]){ 's','a','v','e','d',' ','n','a','n','o',' ','b','u','f','f','e','r',0 });
            return;
        }

        if (line_count >= TERMINAL_MAX_EDITOR_LINES) {
            terminal_write_line((const CHAR16[]){ 'e','d','i','t','o','r',' ','b','u','f','f','e','r',' ','f','u','l','l',0 });
            continue;
        }

        line_count++;
        terminal_write_line((const CHAR16[]){ 'l','i','n','e',' ','a','d','d','e','d',0 });
    }
}

static void terminal_run_shell(void)
{
    CHAR16 line[TERMINAL_MAX_LINE];

    terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','s','h','e','l','l',' ','r','e','a','d','y',0 });

    for (;;) {
        size_t length;

        terminal_write(terminal_current_prompt());
        length = (size_t)terminal_read_line(line, sizeof(line) / sizeof(line[0]));

        if (length == 0) {
            continue;
        }

        if (terminal_execute_command(line)) {
            break;
        }
    }
}

static void terminal_run_batch(void)
{
    terminal_write_line((const CHAR16[]){ 'F','i','s','i','x',' ','O','S',' ','b','a','t','c','h',' ','t','e','r','m','i','n','a','l',' ','r','e','a','d','y',0 });
    terminal_run_batch_file(autoexec_batch);
}

/* Save the setup choice (not any Wi-Fi password) to \fisix_setup.txt,
   so a later boot can tell setup already ran even if the user store
   is checked separately. Best effort: a failed record write does not
   block login, because the saved user account is the real gate. */
static void terminal_save_setup_record(const char *network, const CHAR16 *ssid)
{
    if (fisix_live_boot) return;
    EFI_FILE_PROTOCOL *file = 0;
    char record[192];
    size_t i = 0, j = 0;
    UINTN count;
    const char *p;

    for (p = "fisix_setup=done\nnetwork="; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
    for (p = network; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
    if (ssid && ssid[0]) {
        for (p = "\nssid="; *p && i + 1 < sizeof(record); p++) record[i++] = *p;
        while (ssid[j] && i + 1 < sizeof(record)) record[i++] = (char)(uint8_t)ssid[j++];
    }
    if (i + 1 < sizeof(record)) record[i++] = '\n';
    record[i] = 0;

    if (!terminal_open_file((const CHAR16[]){ '\\', 'f', 'i', 's', 'i', 'x', '_', 's', 'e', 't', 'u', 'p', '.', 't', 'x', 't', 0 },
                            EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE, EFI_FILE_ARCHIVE, &file))
        return;
    if (file->SetPosition && EFI_ERROR(file->SetPosition(file, 0))) { file->Close(file); return; }
    count = i;
    if (!EFI_ERROR(file->Write(file, &count, record)) && file->Flush) file->Flush(file);
    file->Close(file);
}

/* First-time setup. Runs only on a first boot (no saved user store),
   before the normal login screen. Steps: create the first admin
   account, then pick an internet option. There is no network driver
   in FisixOS yet, so the internet step records the choice for later
   and continues offline instead of pretending to connect. The Wi-Fi
   password, if typed, is never written to disk. */
static void terminal_first_time_setup(void)
{
    CHAR16 username[32];
    CHAR16 password[32];
    CHAR16 confirm[32];
    CHAR16 ssid[32];
    CHAR16 wifi_password[32];
    CHAR16 choice[8];
    const char *network = "skip";
    int i;

    serial_mark("FIRST_SETUP_START");
    terminal_write_ascii_line("========================================");
    terminal_write_ascii_line("Welcome to FisixOS - First-Time Setup");
    terminal_write_ascii_line("========================================");
    terminal_write_ascii_line("This only runs once, on the first boot.");
    terminal_write_ascii_line("After setup, log in with the account you create here.");
    terminal_write_ascii_line("");

    for (;;) {
        terminal_write_ascii("Choose a username: ");
        if (terminal_read_line(username, sizeof(username) / sizeof(username[0])) == 0) continue;
        if (terminal_has_space(username)) {
            terminal_write_ascii_line("Usernames cannot contain spaces. Try again.");
            continue;
        }
        for (;;) {
            terminal_write_ascii("Choose a password: ");
            terminal_echo_enabled = 0;
            terminal_read_line(password, sizeof(password) / sizeof(password[0]));
            terminal_echo_enabled = 1;
            if (password[0] == 0) continue;
            terminal_write_ascii("Type the password again: ");
            terminal_echo_enabled = 0;
            terminal_read_line(confirm, sizeof(confirm) / sizeof(confirm[0]));
            terminal_echo_enabled = 1;
            if (!terminal_equal_str(password, confirm)) {
                terminal_write_ascii_line("Passwords did not match. Try again.");
                continue;
            }
            break;
        }
        if (accounts_setup_first_user(username, password)) break;
        /* Live-media case: on a read-only ISO/DVD the save fails, but
           accounts_setup_first_user already replaced the in-memory
           account, so the user can still continue for this session.
           It just will not persist after reboot. Ask, don't loop forever. */
        terminal_write_ascii_line("Could not save that account (read-only media?).");
        terminal_write_ascii("Continue with this account for this session only? (y/n): ");
        {
            CHAR16 live_choice[8];
            terminal_read_line(live_choice, sizeof(live_choice) / sizeof(live_choice[0]));
            if (live_choice[0] == 'y' || live_choice[0] == 'Y') {
                serial_mark("FIRST_SETUP_ACCOUNT_LIVE");
                break;
            }
        }
        terminal_write_ascii_line("Try again.");
    }
    serial_mark("FIRST_SETUP_ACCOUNT_OK");

    terminal_write_ascii_line("");
    terminal_write_ascii_line("Internet setup:");
    terminal_write_ascii_line("  1 = Wired (Ethernet)");
    terminal_write_ascii_line("  2 = Wi-Fi");
    terminal_write_ascii_line("  3 = Skip for now");
    terminal_write_ascii("Choose 1, 2 or 3: ");
    terminal_read_line(choice, sizeof(choice) / sizeof(choice[0]));
    ssid[0] = 0;
    if (choice[0] == '1') {
        network = "wired";
        serial_mark("FIRST_SETUP_NET_WIRED");
        terminal_write_ascii_line("Wired selected.");
    } else if (choice[0] == '2') {
        network = "wifi";
        serial_mark("FIRST_SETUP_NET_WIFI");
        terminal_write_ascii("Wi-Fi network name (SSID): ");
        terminal_read_line(ssid, sizeof(ssid) / sizeof(ssid[0]));
        terminal_write_ascii("Wi-Fi password: ");
        terminal_echo_enabled = 0;
        terminal_read_line(wifi_password, sizeof(wifi_password) / sizeof(wifi_password[0]));
        terminal_echo_enabled = 1;
        /* Deliberately never saved: clear it as soon as it is read. */
        for (i = 0; i < 32; i++) wifi_password[i] = 0;
        terminal_write_ascii_line("Wi-Fi selected.");
    } else {
        network = "skip";
        serial_mark("FIRST_SETUP_NET_SKIP");
        terminal_write_ascii_line("Skipping internet setup. You can set it up later.");
    }

    terminal_write_ascii_line("Note: FisixOS does not have a network driver yet,");
    terminal_write_ascii_line("so this choice is saved for later and the system");
    terminal_write_ascii_line("will continue offline for now. No explosion today.");
    terminal_save_setup_record(network, ssid);

    terminal_write_ascii_line("");
    terminal_write_ascii_line("Setup complete!");
    terminal_write_ascii("Welcome to FisixOS, ");
    terminal_write(username);
    terminal_write_ascii_line("");
    terminal_write_ascii_line("Please log in with your new account.");
    serial_mark("FIRST_SETUP_DONE");
}

static int terminal_login(void)
{
    CHAR16 username[32];
    CHAR16 password[32];

    for (;;) {
        terminal_write((const CHAR16[]){ 'l','o','g','i','n',':',' ',0 });
        if (terminal_read_line(username, sizeof(username) / sizeof(username[0])) == 0) continue;
        terminal_write((const CHAR16[]){ 'p','a','s','s','w','o','r','d',':',' ',0 });
        terminal_echo_enabled = 0;
        terminal_read_line(password, sizeof(password) / sizeof(password[0]));
        terminal_echo_enabled = 1;
        if (accounts_login(username, password)) return 1;
        terminal_write_line((const CHAR16[]){ 'i','n','v','a','l','i','d',' ','c','r','e','d','e','n','t','i','a','l','s',0 });
    }
}

typedef void (*kernel_entry_t)(const kernel_boot_info_t *boot_info);

#define KERNEL_LOAD_ADDRESS 0x200000ULL
#define KERNEL_MAX_BYTES    (1024ULL * 1024ULL)

static EFI_STATUS efi_load_kernel(void)
{
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
    EFI_FILE_PROTOCOL *root = 0;
    EFI_FILE_PROTOCOL *kernel = 0;
    EFI_ALLOCATE_PAGES allocate_pages;
    uint64_t address = KERNEL_LOAD_ADDRESS;
    UINTN bytes = KERNEL_MAX_BYTES;
    EFI_STATUS status;

    if (!gBS || !gBS->LocateProtocol || !gBS->AllocatePages) return EFI_LOAD_ERROR;
    if (fisix_selected_fs) {
        fs = fisix_selected_fs;
    } else {
        status = gBS->LocateProtocol(&EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, (void **)&fs);
        if (EFI_ERROR(status) || !fs) return status;
    }
    status = fs->OpenVolume(fs, &root);
    if (EFI_ERROR(status) || !root) return status;
    status = root->Open(root, &kernel, (const CHAR16[]){'\\','k','e','r','n','e','l','.','b','i','n',0}, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(status) || !kernel) { root->Close(root); return status; }

    allocate_pages = (EFI_ALLOCATE_PAGES)gBS->AllocatePages;
    status = allocate_pages(AllocateAddress, EfiLoaderData, KERNEL_MAX_BYTES / 4096, &address);
    if (EFI_ERROR(status)) {
        address = 0;
        status = allocate_pages(AllocateAnyPages, EfiLoaderData, KERNEL_MAX_BYTES / 4096, &address);
    }
    if (!EFI_ERROR(status)) {
        uint8_t *memory = (uint8_t *)(uintptr_t)KERNEL_LOAD_ADDRESS;
        for (UINTN i = 0; i < KERNEL_MAX_BYTES; ++i) memory[i] = 0;
        status = kernel->Read(kernel, &bytes, memory);
        if (bytes == 0) status = EFI_LOAD_ERROR;
    }
    kernel->Close(kernel);
    root->Close(root);
    return status;
}


/* Driver-pack v1 helpers: run while Boot Services are still alive.
   GOP gives the kernel a real framebuffer address/mode (firmware framebuffer,
   not a full Iris Xe driver). SMBIOS gives the kernel the BIOS table
   (Type 0 = BIOS vendor/version/date) without touching the firmware. */
static void efi_fill_driver_info(kernel_boot_info_t *boot_info)
{
    EFI_GRAPHICS_OUTPUT_PROTOCOL *gop = 0;
    EFI_STATUS status;
    if (!gBS || !boot_info) return;
    status = gBS->LocateProtocol(&EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID, 0, (void **)&gop);
    if (!EFI_ERROR(status) && gop && gop->Mode && gop->Mode->Info) {
        boot_info->framebuffer_base = gop->Mode->FrameBufferBase;
        boot_info->framebuffer_width = gop->Mode->Info->HorizontalResolution;
        boot_info->framebuffer_height = gop->Mode->Info->VerticalResolution;
        boot_info->framebuffer_pitch = gop->Mode->Info->PixelsPerScanLine;
        serial_mark("DRIVER_GOP_OK");
    }
    if (gST) {
        size_t i;
        for (i = 0; i < gST->NumberOfTableEntries; ++i) {
            EFI_GUID *g = &gST->ConfigurationTable[i].VendorGuid;
            if (g->Data1 == 0xeb9d2d31 && g->Data2 == 0x2d88 && g->Data3 == 0x11d3) {
                boot_info->smbios = gST->ConfigurationTable[i].VendorTable;
                serial_mark("DRIVER_SMBIOS_OK");
            }
            /* ACPI RSDP: prefer 2.0, fall back to 1.0. This was missing
               before, so the kernel always reported acpi_available = 0. */
            if (g->Data1 == 0x8868e871 && g->Data2 == 0xe4f1 && g->Data3 == 0x11d3) {
                boot_info->rsdp = gST->ConfigurationTable[i].VendorTable;
                serial_mark("DRIVER_RSDP20_OK");
            } else if (g->Data1 == 0xeb9d2d30 && g->Data2 == 0x2d88 && g->Data3 == 0x11d3) {
                if (!boot_info->rsdp) boot_info->rsdp = gST->ConfigurationTable[i].VendorTable;
            }
        }
    }
}

/* This is the only code allowed to use Boot Services during the handoff.
   The map key is valid only for the exact map returned immediately before
   ExitBootServices(), so do not print or allocate between those calls. */
static EFI_STATUS efi_leave_boot_services(kernel_boot_info_t *boot_info)
{
    static uint8_t memory_map_buffer[128 * 1024];
    EFI_GET_MEMORY_MAP get_memory_map;
    EFI_EXIT_BOOT_SERVICES exit_boot_services;
    size_t memory_map_size;
    size_t map_key;
    size_t descriptor_size;
    uint32_t descriptor_version;
    EFI_STATUS status;
    uint64_t total_memory = 0;
    uint8_t *cursor;

    if (!gBS || !gBS->GetMemoryMap || !gBS->ExitBootServices) {
        return EFI_LOAD_ERROR;
    }

    get_memory_map = (EFI_GET_MEMORY_MAP)gBS->GetMemoryMap;
    exit_boot_services = (EFI_EXIT_BOOT_SERVICES)gBS->ExitBootServices;

    for (;;) {
        memory_map_size = sizeof(memory_map_buffer);
        map_key = 0;
        descriptor_size = 0;
        descriptor_version = 0;
        status = get_memory_map(&memory_map_size,
                                (EFI_MEMORY_DESCRIPTOR *)memory_map_buffer,
                                &map_key, &descriptor_size,
                                &descriptor_version);
        if (EFI_ERROR(status) || descriptor_size == 0) {
            return status;
        }

        total_memory = 0;
        cursor = memory_map_buffer;
        for (size_t i = 0; i < memory_map_size / descriptor_size; ++i) {
            EFI_MEMORY_DESCRIPTOR *descriptor = (EFI_MEMORY_DESCRIPTOR *)cursor;
            if (descriptor->Type == EfiConventionalMemory ||
                descriptor->Type == EfiLoaderCode ||
                descriptor->Type == EfiLoaderData ||
                descriptor->Type == EfiBootServicesCode ||
                descriptor->Type == EfiBootServicesData) {
                total_memory += descriptor->NumberOfPages * 4096ULL;
            }
            cursor += descriptor_size;
        }

        status = exit_boot_services(gImageHandle, map_key);
        if (status == EFI_SUCCESS) {
            boot_info->memory_bytes = total_memory;
            gBS = NULL;
            return EFI_SUCCESS;
        }
        if (status != EFI_INVALID_PARAMETER) {
            return status;
        }
    }
}


/* ================= FisixOS bootloader menu =================
   On every boot: clear the screen (vendor logo gone), scan every
   SimpleFileSystem on every disk for *.EFI files, and show:
     FisixOS - Installed  (a disk with EFI/BOOT/BOOTX64.EFI + kernel.bin)
     FisixOS - Liveboot   (everything in RAM, nothing is saved)
     FisixOS - Installer  (copy this FisixOS to a disk, literally)
     Boot <other .EFI>    (any other EFI file found, chainloaded)
     0 = FisixOS terminal (old normal boot)
   Blank Enter = first Installed disk if one exists, else normal boot.
   Installer reuses terminal_install(), which copies the installed-only
   payload INSTAL.BIN (as BOOTX64.EFI) + kernel.bin + startup.nsh
   from the boot drive to a fresh GPT+FAT32 ESP on the picked disk.
   The menu just makes that a first-class boot choice instead of a
   hidden terminal command. On an installed-only build
   (FISIX_INSTALLED_BUILD=1) the Installer entry is compiled out
   entirely, so an installed disk never offers to install itself.
   Liveboot sets fisix_live_boot, so accounts_save() and the setup
   record become RAM-only (see accounts.c / terminal_save_setup_record).
   Installed sets fisix_selected_fs, so accounts, setup, the shell
   and the kernel all load from that disk, not from the boot media. */
typedef EFI_STATUS (EFIAPI *EFI_LOAD_IMAGE_FN)(BOOLEAN BootPolicy, EFI_HANDLE ParentImageHandle, void *DevicePath, void *SourceBuffer, UINTN SourceSize, EFI_HANDLE *ImageHandle);
typedef EFI_STATUS (EFIAPI *EFI_START_IMAGE_FN)(EFI_HANDLE ImageHandle, UINTN *ExitDataSize, CHAR16 **ExitData);

typedef struct {
    EFI_HANDLE handle;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs;
    int is_boot;
    int has_kernel;
    int has_bootx64;
    int has_users;
} boot_fs_t;
typedef struct {
    int fs_index;
    CHAR16 path[128];
    char ascii[128];
} boot_efi_t;

static boot_fs_t boot_fss[16];
static int boot_fs_count = 0;
static int boot_boot_fs = -1;
static boot_efi_t boot_efis[32];
static int boot_efi_count = 0;

static void boot_to_ascii(const CHAR16 *s, char *out, size_t max)
{
    size_t i = 0;
    if (!s || !out || max == 0) return;
    while (s[i] && i + 1 < max) { out[i] = (char)(uint8_t)s[i]; i++; }
    out[i] = 0;
}
static int boot_name_is_efi(const CHAR16 *name)
{
    size_t n = 0;
    while (name[n]) n++;
    if (n < 4) return 0;
    {
        CHAR16 a = name[n-4], b = name[n-3], c = name[n-2], d = name[n-1];
        if (a >= 'a' && a <= 'z') a = (CHAR16)(a - 'a' + 'A');
        if (b >= 'a' && b <= 'z') b = (CHAR16)(b - 'a' + 'A');
        if (c >= 'a' && c <= 'z') c = (CHAR16)(c - 'a' + 'A');
        if (d >= 'a' && d <= 'z') d = (CHAR16)(d - 'a' + 'A');
        return a == '.' && b == 'E' && c == 'F' && d == 'I';
    }
}
static int boot_name_eq_ascii(const CHAR16 *name, const char *a)
{
    size_t i = 0;
    while (name[i] && a[i]) {
        CHAR16 c = name[i];
        char d = a[i];
        if (c >= 'a' && c <= 'z') c = (CHAR16)(c - 'a' + 'A');
        if (d >= 'a' && d <= 'z') d = (char)(d - 'a' + 'A');
        if (c != (CHAR16)(uint8_t)d) return 0;
        i++;
    }
    return name[i] == 0 && a[i] == 0;
}
static int boot_file_exists(EFI_FILE_PROTOCOL *root, const CHAR16 *path)
{
    EFI_FILE_PROTOCOL *f = 0;
    EFI_STATUS st;
    if (!root) return 0;
    st = root->Open(root, &f, path, EFI_FILE_MODE_READ, 0);
    if (EFI_ERROR(st) || !f) return 0;
    f->Close(f);
    return 1;
}
static void boot_scan_dir(EFI_FILE_PROTOCOL *root, EFI_FILE_PROTOCOL *dir, const CHAR16 *dir_path, int fs_index, int depth)
{
    uint8_t buf[512];
    for (;;) {
        UINTN sz = sizeof(buf);
        EFI_STATUS st;
        EFI_FILE_INFO *info;
        CHAR16 name[64];
        size_t ni = 0, pi = 0;
        if (!dir) return;
        st = dir->Read(dir, &sz, buf);
        if (EFI_ERROR(st) || sz == 0) return;
        info = (EFI_FILE_INFO *)buf;
        if (!info->FileName[0]) return;
        if (info->FileName[0] == '.') continue;
        while (info->FileName[ni] && ni + 1 < sizeof(name)/sizeof(name[0])) { name[ni] = info->FileName[ni]; ni++; }
        name[ni] = 0;
        if (info->Attribute & EFI_FILE_DIRECTORY) {
            CHAR16 new_path[128];
            EFI_FILE_PROTOCOL *sub = 0;
            if (depth >= 3) continue;
            if (depth == 0 && !boot_name_eq_ascii(name, "EFI")) continue;
            while (dir_path[pi] && pi + 1 < 120) { new_path[pi] = dir_path[pi]; pi++; }
            new_path[pi++] = '\\';
            { size_t k = 0; while (name[k] && pi + 1 < 120) new_path[pi++] = name[k++]; }
            new_path[pi] = 0;
            if (EFI_ERROR(root->Open(root, &sub, new_path, EFI_FILE_MODE_READ, 0)) || !sub) continue;
            boot_scan_dir(root, sub, new_path, fs_index, depth + 1);
            sub->Close(sub);
        } else if (boot_name_is_efi(name)) {
            boot_efi_t *e;
            CHAR16 full[128];
            size_t k = 0;
            if (boot_efi_count >= 32) continue;
            while (dir_path[k] && k + 1 < 120) { full[k] = dir_path[k]; k++; }
            full[k++] = '\\';
            { size_t j = 0; while (name[j] && k + 1 < 120) full[k++] = name[j++]; }
            full[k] = 0;
            e = &boot_efis[boot_efi_count++];
            e->fs_index = fs_index;
            { size_t j = 0; while (full[j] && j + 1 < 128) { e->path[j] = full[j]; j++; } e->path[j] = 0; }
            boot_to_ascii(full, e->ascii, sizeof(e->ascii));
        }
    }
}
static void boot_scan_all(void)
{
    EFI_LOCATE_HANDLE_BUFFER_FN locate_buf;
    EFI_HANDLE_PROTOCOL_FN handle_proto;
    EFI_HANDLE *handles = 0;
    UINTN handle_count = 0;
    EFI_LOADED_IMAGE_PROTOCOL *loaded = 0;
    EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *boot_fs_proto = 0;
    int i;
    boot_fs_count = 0;
    boot_efi_count = 0;
    boot_boot_fs = -1;
    if (!gBS) return;
    locate_buf = (EFI_LOCATE_HANDLE_BUFFER_FN)gBS->LocateHandleBuffer;
    handle_proto = (EFI_HANDLE_PROTOCOL_FN)gBS->HandleProtocol;
    if (!locate_buf || !handle_proto) return;
    if (!EFI_ERROR(handle_proto(gImageHandle, &EFI_LOADED_IMAGE_PROTOCOL_GUID, (void **)&loaded)) && loaded && loaded->DeviceHandle) {
        (void)handle_proto(loaded->DeviceHandle, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void **)&boot_fs_proto);
    }
    if (EFI_ERROR(locate_buf(2, &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, 0, &handle_count, &handles)) || !handles) return;
    for (i = 0; (UINTN)i < handle_count && boot_fs_count < 16; i++) {
        EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *fs = 0;
        EFI_FILE_PROTOCOL *root = 0;
        boot_fs_t *bf;
        if (EFI_ERROR(handle_proto(handles[i], &EFI_SIMPLE_FILE_SYSTEM_PROTOCOL_GUID, (void **)&fs)) || !fs) continue;
        if (EFI_ERROR(fs->OpenVolume(fs, &root)) || !root) continue;
        bf = &boot_fss[boot_fs_count];
        bf->handle = handles[i];
        bf->fs = fs;
        bf->is_boot = (boot_fs_proto && fs == boot_fs_proto) ? 1 : 0;
        bf->has_kernel = boot_file_exists(root, (const CHAR16[]){'\\','k','e','r','n','e','l','.','b','i','n',0});
        bf->has_bootx64 = boot_file_exists(root, (const CHAR16[]){'\\','E','F','I','\\','B','O','O','T','\\','B','O','O','T','X','6','4','.','E','F','I',0});
        bf->has_users = boot_file_exists(root, (const CHAR16[]){'\\','f','i','s','i','x','_','u','s','e','r','s','.','d','b',0});
        if (bf->is_boot) boot_boot_fs = boot_fs_count;
        boot_scan_dir(root, root, (const CHAR16[]){0}, boot_fs_count, 0);
        root->Close(root);
        boot_fs_count++;
    }
    if (boot_boot_fs < 0 && boot_fs_count > 0) {
        /* Firmware did not give us a boot FS match (e.g. El Torito
           virtual disk). Fall back to the first Fisix volume, so
           Liveboot/Installer still read from the right media. */
        for (i = 0; i < boot_fs_count; i++) {
            if (boot_fss[i].has_bootx64 && boot_fss[i].has_kernel) { boot_boot_fs = i; boot_fss[i].is_boot = 1; break; }
        }
        if (boot_boot_fs < 0) { boot_boot_fs = 0; boot_fss[0].is_boot = 1; }
    }
    if (handles && gBS->FreePool) gBS->FreePool(handles);
}
static void boot_chainload(int efi_index)
{
    boot_efi_t *e;
    EFI_FILE_PROTOCOL *root = 0, *file = 0;
    uint8_t *buf = 0;
    UINTN total = 0, cap = 2 * 1024 * 1024;
    EFI_LOAD_IMAGE_FN load_image;
    EFI_START_IMAGE_FN start_image;
    EFI_HANDLE image = 0;
    EFI_STATUS st;
    if (efi_index < 0 || efi_index >= boot_efi_count) return;
    e = &boot_efis[efi_index];
    if (e->fs_index < 0 || e->fs_index >= boot_fs_count) return;
    if (EFI_ERROR(boot_fss[e->fs_index].fs->OpenVolume(boot_fss[e->fs_index].fs, &root)) || !root) {
        terminal_write_ascii_line("Could not open that disk.");
        return;
    }
    if (EFI_ERROR(root->Open(root, &file, e->path, EFI_FILE_MODE_READ, 0)) || !file) {
        terminal_write_ascii_line("Could not open that EFI file.");
        root->Close(root);
        return;
    }
    if (EFI_ERROR(gBS->AllocatePool(EfiLoaderData, cap, (void **)&buf)) || !buf) {
        terminal_write_ascii_line("Out of memory for EFI file.");
        file->Close(file); root->Close(root);
        return;
    }
    for (;;) {
        UINTN chunk = cap - total;
        if (chunk == 0) break;
        st = file->Read(file, &chunk, buf + total);
        if (EFI_ERROR(st) || chunk == 0) break;
        total += chunk;
    }
    file->Close(file); root->Close(root);
    if (total == 0) { terminal_write_ascii_line("EFI file is empty."); gBS->FreePool(buf); return; }
    load_image = (EFI_LOAD_IMAGE_FN)gBS->LoadImage;
    start_image = (EFI_START_IMAGE_FN)gBS->StartImage;
    if (!load_image || !start_image) { terminal_write_ascii_line("LoadImage not available."); gBS->FreePool(buf); return; }
    st = load_image(0, gImageHandle, 0, buf, total, &image);
    if (EFI_ERROR(st) || !image) { terminal_write_ascii_line("LoadImage failed for that EFI file."); gBS->FreePool(buf); return; }
    serial_mark("BOOT_CHAINLOAD_START");
    st = start_image(image, 0, 0);
    serial_mark("BOOT_CHAINLOAD_RETURN");
    terminal_write_ascii_line("That EFI program exited. Back to the FisixOS bootloader.");
    gBS->FreePool(buf);
    (void)st;
}
static int boot_run_menu(void)
{
    for (;;) {
        int installed[16], installed_count = 0, other[32], other_count = 0;
        int i, live_num, install_num, choice_num;
        CHAR16 line[32];
        uint64_t choice = 0;
        boot_clear_screen();
        serial_mark("BOOT_MENU");
        boot_scan_all();
        terminal_write_ascii_line("FisixOS Bootloader");
        terminal_write_ascii_line("==================");
        if (boot_boot_fs >= 0) {
            char tmp[64]; size_t k = 0; const char *pre = "Booted from Disk ";
            while (pre[k]) tmp[k] = pre[k], k++;
            tmp[k++] = (char)('0' + boot_boot_fs); tmp[k] = 0;
            terminal_write_ascii_line(tmp);
        }
        terminal_write_ascii_line("");
        for (i = 0; i < boot_fs_count; i++) {
            if (boot_fss[i].has_bootx64 && boot_fss[i].has_kernel) installed[installed_count++] = i;
        }
        for (i = 0; i < boot_efi_count; i++) {
            int is_fisix_boot = 0;
            if (boot_fss[boot_efis[i].fs_index].has_kernel) {
                /* A Fisix BOOTX64 is an Installed entry, not an "other" entry. */
                const char *s = boot_efis[i].ascii;
                size_t len = 0; while (s[len]) len++;
                if (len >= 11) {
                    const char *tail = s + len - 11;
                    if ((tail[0]=='B'||tail[0]=='b') && (tail[1]=='O'||tail[1]=='o') && (tail[2]=='O'||tail[2]=='o') &&
                        (tail[3]=='T'||tail[3]=='t') && (tail[4]=='X'||tail[4]=='x') && (tail[5]=='6') && (tail[6]=='4') &&
                        tail[7]=='.' && (tail[8]=='E'||tail[8]=='e') && (tail[9]=='F'||tail[9]=='f') && (tail[10]=='I'||tail[10]=='i'))
                        is_fisix_boot = 1;
                }
            }
            if (!is_fisix_boot) other[other_count++] = i;
        }
        {
            int n = 1;
            for (i = 0; i < installed_count; i++) {
                install_write_num("", (uint64_t)n, "");
                /* install_write_num prints on its own line; the label
                   follows on the next line to keep the code small. */
                terminal_write_ascii("FisixOS - Installed (Disk ");
                { char d[4]; d[0] = (char)('0' + installed[i]); d[1] = 0; terminal_write_ascii(d); }
                terminal_write_ascii(boot_fss[installed[i]].has_users ? ", has users) " : ", no users yet) ");
                terminal_write_ascii_line(boot_fss[installed[i]].is_boot ? "[boot disk]" : "");
                n++;
            }
            live_num = n++;
            install_write_num("", (uint64_t)live_num, "");
            terminal_write_ascii_line("FisixOS - Liveboot (everything in RAM, nothing saved)");
#if FISIX_INSTALLED_BUILD
            /* Installed disk: no Installer entry at all. `n` is not
               advanced, so "other" EFI entries shift up by one and
               the other_idx calculation below must match. */
            install_num = -1;
#else
            install_num = n++;
            install_write_num("", (uint64_t)install_num, "");
            terminal_write_ascii_line("FisixOS - Installer (copy this FisixOS to a disk)");
#endif
            for (i = 0; i < other_count; i++) {
                install_write_num("", (uint64_t)n, "");
                terminal_write_ascii("Boot ");
                terminal_write_ascii(boot_efis[other[i]].ascii);
                terminal_write_ascii(" (Disk ");
                { char d[4]; d[0] = (char)('0' + boot_efis[other[i]].fs_index); d[1] = 0; terminal_write_ascii(d); }
                terminal_write_ascii_line(")");
                n++;
            }
            terminal_write_ascii_line("  0) FisixOS terminal (normal boot)");
#if !FISIX_INSTALLED_BUILD
            if (installed_count == 0) terminal_write_ascii_line("  (No installed FisixOS disk found yet - use Installer, then reboot.)");
#endif
        }
        terminal_write_ascii_line("");
        terminal_write_ascii("Choice (Enter = Installed, else normal): ");
        if (terminal_read_line(line, 32) == 0 || line[0] == 0) {
            if (installed_count > 0) {
                fisix_selected_fs = boot_fss[installed[0]].fs;
                fisix_live_boot = 0;
                serial_mark("BOOT_MODE_INSTALLED");
            } else {
                fisix_selected_fs = (boot_boot_fs >= 0) ? boot_fss[boot_boot_fs].fs : 0;
                fisix_live_boot = 0;
                serial_mark("BOOT_MODE_TERMINAL");
            }
            boot_clear_screen();
            return 0;
        }
        if (!install_parse_u64(line, &choice)) { terminal_write_ascii_line("Invalid choice."); continue; }
        choice_num = (int)choice;
        if (choice_num == 0) {
            fisix_selected_fs = (boot_boot_fs >= 0) ? boot_fss[boot_boot_fs].fs : 0;
            fisix_live_boot = 0;
            serial_mark("BOOT_MODE_TERMINAL");
            boot_clear_screen();
            return 0;
        }
        if (choice_num >= 1 && choice_num <= installed_count) {
            fisix_selected_fs = boot_fss[installed[choice_num - 1]].fs;
            fisix_live_boot = 0;
            serial_mark("BOOT_MODE_INSTALLED");
            boot_clear_screen();
            return 0;
        }
        if (choice_num == live_num) {
            fisix_selected_fs = (boot_boot_fs >= 0) ? boot_fss[boot_boot_fs].fs : 0;
            fisix_live_boot = 1;
            serial_mark("BOOT_MODE_LIVE");
            boot_clear_screen();
            return 0;
        }
        if (install_num > 0 && choice_num == install_num) {
            fisix_selected_fs = (boot_boot_fs >= 0) ? boot_fss[boot_boot_fs].fs : 0;
            fisix_live_boot = 0;
            serial_mark("BOOT_MODE_INSTALLER");
            terminal_install();
            fisix_selected_fs = 0;
            terminal_write_ascii_line("Press Enter to return to the bootloader menu.");
            terminal_read_line(line, 32);
            continue;
        }
        {
            /* First "other" number: installed + live + installer (if
               present). On an installed build there is no installer
               number, so other entries start one lower. */
            int other_idx = choice_num - (installed_count + 2 + (install_num > 0 ? 1 : 0));
            if (other_idx >= 0 && other_idx < other_count) {
                boot_chainload(other[other_idx]);
                terminal_write_ascii_line("Press Enter to return to the bootloader menu.");
                terminal_read_line(line, 32);
                continue;
            }
        }
        terminal_write_ascii_line("Invalid choice.");
    }
}

EFI_STATUS EFIAPI efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    serial_mark("EFI_MAIN_START");
    gImageHandle = ImageHandle;
    gST = SystemTable;
    gBS = SystemTable ? SystemTable->BootServices : NULL;
    serial_mark("EFI_MAIN_GBS_SET");
    boot_clear_screen();
    serial_mark("BOOT_CLEAR_OK");
    boot_run_menu();

    eosfs_init();
    serial_mark("EFI_MAIN_EOSFS_OK");
    accounts_init();
    serial_mark("EFI_MAIN_ACCOUNTS_OK");

    serial_mark("EFI_MAIN_BEFORE_BOOTMSG");
    terminal_write(boot_message);
    serial_mark("EFI_MAIN_AFTER_BOOTMSG");
    if (accounts_first_boot()) {
        terminal_first_time_setup();
    }
    terminal_run_batch();
    terminal_login();
    terminal_run_shell();

    kernel_boot_info_t boot_info = { 0, 0, 0, 0, 0, 0, 0 };
    efi_fill_driver_info(&boot_info);
    if (efi_load_kernel() != EFI_SUCCESS || efi_leave_boot_services(&boot_info) != EFI_SUCCESS) {
        terminal_write_line((const CHAR16[]){ 'f','a','i','l','e','d',' ','t','o',' ','s','t','a','r','t',' ','k','e','r','n','e','l',0 });
        return EFI_LOAD_ERROR;
    }

    ((kernel_entry_t)(uintptr_t)KERNEL_LOAD_ADDRESS)(&boot_info);
    return EFI_SUCCESS; /* kernel_startup does not return */
}

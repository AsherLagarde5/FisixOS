# FisixFF (Fisix File Freeze) and the FisixOS system folder

FisixFF is a SquashFS alternative, v1, stored only (no compression).
It is a single file that freezes a directory tree, so FisixOS can
carry its system files as one file instead of many loose files.

Format: see tools/make_ff.py. Header "FFS1", file count, then
per-file entries (name, offset, size), then file data.

## System folder layout inside a .ff

```
SYSTEM/BOOT/BOOTX64.EFI
SYSTEM/BOOT/KERNEL.BIN
SYSTEM/BOOT/STARTUP.NSH
SYSTEM/APPS/HELLO.FSE
SYSTEM/APPS/NANO.FSE
SYSTEM/DRIVERS/          (driver binaries / notes)
SYSTEM/USER/             (empty in the frozen image; user files go here)
```

The kernel reader is kernel/drivers/fisixff.c (ff_list, ff_read).
It does not mount the .ff as a filesystem yet; it lists and reads
files from a .ff image already in memory. That is v1, honest.

## FAT

kernel/drivers/fat.c is a read-only FAT16/FAT32 reader (8.3 names,
root scan, cluster chains). FAT16 was tested with a hand-built
image (HELLO.TXT read back). FAT32 code follows the same path
(root cluster chain) but was NOT tested with a real FAT32 image yet.

## DOS .com

kernel/drivers/dosemu.c is a minimal 8086 emulator for .com files:
MOV/ADD/SUB/CMP, jumps, CALL/RET, INT 20h, INT 21h AH=02/09/4C.
Tested: a hello-world .com prints "HELLO DOS" and exits.
No file I/O, no full DOS 4.0 kernel - that is a separate project.

## FSE maker

tools/fse-maker.py wraps tools/fisix-gcc (for .c) and
tools/make_fse.py (for .elf), plus `info` (parse the 64-byte
FSE1 header) and `new` (write a hello template).
Tested: apps/hello.c -> hello2.fse, entry 0x300000, code 240 bytes.

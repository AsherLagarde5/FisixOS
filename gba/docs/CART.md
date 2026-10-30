# The 32 MB cartridge + on-cart save — UPDATED 2026-10-29

Derek's decision this turn: drop the 8 GB custom cart. This is a
standard 32 MB GBA cart, full stop.

## Size math

- PC FisixOS ISO: 4.6 MB (EFI loader + 64-bit kernel + apps).
- GBA ROM window: 0x08000000-0x09FFFFFF = 32 MB maximum.
- This ARM port has no EFI, no x86 drivers, no PC GUI, so it is
  far smaller than 4.6 MB — target is a 256 KB padded ROM in v0.2,
  with 32 MB as the hard ceiling, not a goal. Plenty of ROM room
  left for more apps later, still under 32 MB.

## Save: SRAM on the cart

- SRAM at 0x0E000000, 32 KB, battery-backed on real carts.
- In mGBA, SRAM becomes the .sav file next to the .gba.
- Layout: magic "FSXG", count, then up to 16 files, each
  name 32 + length 4 + data 512 bytes = 548 bytes, total 8,768
  bytes, well under 32 KB.
- `fs_write()` flushes the whole table to SRAM immediately
  (write-through). No RAM-only save in v0.2.
- Files are capped at 512 bytes each in v0.2. That fits a
  profile, settings, and small text files. It does NOT fit
  large files — a Flash 128 KB save chip variant is the v3
  upgrade if Derek wants bigger saves, still a standard cart
  type, still a 32 MB ROM.

## Why swapping GBAs works

1. Power off GBA #1 — RAM is gone, nothing persistent was in it.
2. Move cart to GBA #2.
3. Boot — OS reads USER.PROF and the file table from cart SRAM.
4. Same user, same files.

The 8 GB paging design from the previous turn is superseded
and is NOT in this build. `cart.c` no longer has a bank
register; it has `cart_rom_ptr()` for the flat 32 MB window
and SRAM save functions only.

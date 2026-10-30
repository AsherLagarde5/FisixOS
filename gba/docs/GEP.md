# GEP — Game Boy Executable Program

GEP is FisixGBA's app format. It is NOT PC .fse.

- PC .fse: x86-64 code, magic FSE1, loaded at 0x300000, entry
  fse_main(api) with System V x86-64 calling convention.
  It cannot run on a GBA, and the GEP loader rejects it
  (magic check, error -1).
- GEP: ARM7TDMI code, magic GEP1 (0x31504547), loaded at
  0x02008000 in EWRAM, entry gep_main(api) with ARM AAPCS
  (first arg in r0). See src/gep.h for the full header and API.

## Header, 64 bytes, little-endian, packed

magic u32 = GEP1, version u32 = 1, entry_offset u32,
code_size u32, api_version u32 = 1, reserved[11] u32,
with reserved[0] = bss_size (bytes, zeroed by the loader).

## API, v1

Same shape as PC .fse v1/v2, GBA semantics:
print, print_u64, print_newline, read_key (returns a GBA
key mask, not a character), file_read, file_write,
dir_list, file_delete (-1, not supported), file_rename
(-1), file_mkdir (-1), file_exists, file_size, get_time
(all zero, no RTC), clear_screen, sleep_ms.

Limits, honestly:
- Code max 64 KB, .bss max 16 KB, load at 0x02008000.
  OS .bss ends at 0x020024d0 in v0.8, so the load address
  does not collide with the OS.
- Cart files are 512 bytes max, so a cart-stored GEP must
  be tiny. HELLO.GEP is 392 bytes total (328 code), so it
  fits in a cart file too, but v0.8 embeds it in the ROM
  so you can RUN it without a transfer first.
- A GEP app must return. It must not touch hardware
  registers directly or overwrite the loader.

## Building one

```
arm-none-eabi-gcc -mcpu=arm7tdmi -O2 -ffreestanding -nostdlib \
  -I src -Wl,-T,tools/gep.ld -nostartfiles \
  -o my.elf my.c -lgcc
python3 tools/make_gep.py my.elf my.gep
```

## Running one, in FisixGBA Terminal

```
LIST
RUN HELLO.GEP
```

RUN looks in cart files first, then in built-in GEPs.
Errors: -1 bad header (including PC .fse), -2 code out of
range, -3 not found, -4 .bss too big.

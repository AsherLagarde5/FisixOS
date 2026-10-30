# User Update Files — NOT patch files

An update file is one binary that updates the system.
A patch file is a batch of Terminal commands (see
docs/PATCH.md). They are different formats, on purpose,
and the checker refuses to mix them up.

## Modular OS

Core ROM = just enough to boot, plus what was already
supplied: boot, kernel, video, input, timer, cart,
Link, audio, Terminal, GEP loader, Patch interpreter,
Update checker. Everything else is a module: GEP apps,
patch scripts, text files, living in cart files or
embedded in ROM for demo. An update adds, replaces, or
deletes modules. It does not rewrite the core ROM on a
real cart — ROM is read-only hardware. On PC UEFI, the
same format can target files on disk.

The system will also provide base ISOs (PC UEFI) and
GBA ROMs (this build). A user update is separate from
that base: you make an update, you distribute it, a
user applies it to their base system.

## Format, .update

Header, 64 bytes, little-endian, packed:
magic UPD1 (0x31445055), version 1, platform, entry_count,
payload_crc16, reserved[11]=0.
Then each entry: name[32], action u32, data_len u32,
data[data_len].
Actions: 0 ADD (only if not present, otherwise skipped),
1 REPLACE (overwrite if present, add if not),
2 DELETE (remove, data_len must be 0).
A file is replaced only if the update lists it with
REPLACE. Nothing else changes. No blanket replace-all.

Build one:
```
python3 tools/make_update.py GBA out.update \
  ADD:DEMO2.PATCH:demo2.patch ADD:NOTE.TXT:note.txt
python3 tools/make_update.py UEFI out.update \
  REPLACE:KERNEL.BIN:kernel.bin
```

## Built-in checker

update_check() runs before anything is written, and
writes nothing on failure:
- bad magic (-1): not an update file. A .patch file
  run as an update lands here.
- bad version (-2)
- wrong platform (-3): platform does not match the
  system running it. Platforms: 1 GBA, 2 UEFI x86_64,
  3 C64, 4 ARM64. So a UEFI update on a GBA, a C64
  update on a GBA, a GBA update on UEFI, etc., all fail
  and change nothing. Terminal prints WRONG PLATFORM
  and the update's platform, e.g. UPDATE IS FOR: UEFI.
- truncated (-4), bad CRC (-5), bad entry (-6)

## Running one, in Terminal

```
UPDATE DEMO.UPDATE
UPDATE WRONG.UPDATE
LIST
```

DEMO.UPDATE (GBA, built in) adds DEMO2.PATCH and
NOTE.TXT. Apply it, then LIST shows them, then
PATCH DEMO2.PATCH runs the patch that came from the
update. Apply DEMO.UPDATE again and those entries are
skipped, because ADD only adds if not present.
WRONG.UPDATE (UEFI, built in) is rejected by the
checker, and LIST after it is unchanged — that is the
test that the checker works.

GBA limits, honestly:
- Cart files are 512 bytes max, so a GBA update entry
  must be <= 512 bytes, and a cart-stored update file
  itself must fit in a cart file (512 bytes). Built-in
  updates in ROM have no such cart limit for the update
  itself, only per-entry.
- Update entries are applied in order, to cart files.

# FisixOS built-in tools

Added 2026-10-25 at Derek's request, from his PC Desktop:

- `browsybrowser/` — Browsy browser, C++ source (top-level .cpp/.h only,
  22 files, 268 KB). Build artifacts, .git, and sub-project build
  outputs were NOT copied.
- `Pathify/` — Pathify, from stuffs.zip (1,001,180,630 bytes on disk,
  12,347 zip entries, mostly build artifacts). Source-only extract:
  Rust/C#/Go source, Cargo manifests, and scripts, excluding
  `target/` and `.git/`.
- `ReTerm/` — ReTerm, from stuffs2.zip (1,323,064,301 bytes on disk,
  13,349 zip entries, mostly build artifacts). Source-only extract,
  excluding `target/` and `.git/`.
- `Z/` — Z, from stuffs3.zip (49,191,956 bytes on disk, 892 entries).
  Source-only extract, excluding `target/` and `.git/`.

Total copied into this tree: about 1.3 MB of source, not the
2.37 GB of original zips/folders. Copying the full zips into a
FisixOS ISO would not fit (the unified ISO is ~34 MB, and the
installer ESP is capped at 2048 MB) and would mostly duplicate
Windows build artifacts.

## Status in FisixOS

- GUI catalog (`kernel/drivers/gui.c`): BROWSY, PATHIFY, RETERM, and Z
  are now in ALL APPS (56 apps total, was 52). Opening one shows its
  description, not a working app yet — honest stub, same as other
  GUI v2 stubs.
- Native port: NOT done yet. These are Windows/Linux C++ and Rust
  projects. FisixOS does not yet have a libc, syscall ABI, process
  loader, or filesystem support for running them natively, so they
  cannot be compiled into .fse apps as-is. The next step is to pick
  one (Z is the smallest) and port its core logic to a FisixOS .fse
  app via tools/fisix-gcc, rather than trying to port all four at once.

## Originals (not modified)

PC Desktop originals were only read, never deleted or modified:
browsybrowser/, stuffs.zip, stuffs2.zip, stuffs3.zip.
A source-only working copy was also left on the PC at
Desktop\builtin-src\ (plus browsy-src.tar, which still contains
build artifacts and should be deleted by Derek if he wants it gone).

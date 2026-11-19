# Debian .deb support in FisixOS

## What this is

A real `.deb` unpacker + installer in the FisixOS kernel
(`kernel/drivers/deb.c`, `deb.h`, `deb_inflate.c`), with a
terminal interface (`deb install/list/info/remove/run` in the
GUI terminal) and host tools for building and repacking test
packages. Language: C — the kernel build is C, and the ELF
loader logic below is a port of Rust/Go code into C so it
builds with the existing FisixOS build (see "Code reused").

## What a .deb is

An `ar` archive with three members:

```
debian-binary   "2.0\n"
control.tar.*   tar of control files (control, md5sums, ...)
data.tar.*      tar of the files to install
```

The tar members are usually compressed. FisixOS supports:

| Compression | In-kernel | Notes |
|---|---|---|
| none (plain tar) | yes | |
| gzip (.tar.gz) | yes | own RFC 1951 inflate in `deb_inflate.c` |
| xz (.tar.xz) | yes | xz-embedded, single-call mode, `deb_xz.c` + `kernel/drivers/xz/` |
| zstd (.tar.zst) | yes | zstd single-file decoder, `deb_zstd.c` + `kernel/drivers/zstd/` |
| bzip2 (.tar.bz2) | **no** | detected; repack with `tools/deb_repack_gz.py` |

Real Debian packages are almost always xz today (e.g.
`hello_2.10-5_amd64.deb` is `control.tar.xz` + `data.tar.xz`),
and those install directly now, with no host repacking: the
host test installs the real xz hello and the zstd repack of it
and verifies byte-identical extraction against the gzip
reference. Only bzip2 still needs the repack step.

## Compression decoders (stage 4, 2026-11-10)

Both decoders are vendored, freestanding (no libc, no heap),
and share the `deb_gunzip()` call contract (`0` ok, `-1` bad
input, `-2` output too small, `-3` unsupported option), so
`tar_decompress()` in `deb.c` uses them the same way as gzip.
Static memory, measured on the real hello .deb (host test
prints the peaks):

- **xz** — xz-embedded, public domain (upstream commit
  `ae63ae3a36ed01724674e8f3d750dc47bf125410`, 2024-12-30),
  `kernel/drivers/xz/` (unmodified upstream files) plus a
  FisixOS `xz_config.h` and the `deb_xz.c` wrapper. Only
  single-call mode + CRC64 + the x86 BCJ filter are enabled.
  The output buffer (deb.c's 2 MiB tar scratch) doubles as
  the LZMA2 dictionary, so any dictionary size the stream
  declares is fine and no dictionary buffer is allocated.
  Decoder-state pool: 64 KiB of `.bss`, measured peak
  29,816 bytes. SHA-256 integrity checks, the Delta filter,
  and non-x86 BCJ filters are not supported: those streams
  fail with a clear "unsupported option" error. License:
  `kernel/drivers/xz/COPYING`.
- **zstd** — facebook/zstd v1.5.7 single-file decoder
  (`zstddeclib.c`, generated with
  `build/single_file_libs/create_single_file_decoder.sh`),
  BSD-3-Clause/GPLv2 dual (see `kernel/drivers/zstd/LICENSE`
  and `COPYING`), plus the public `zstd.h`/`zstd_errors.h`.
  Exactly five lines are patched for the freestanding build
  (see the FISIXOS PATCH note at the top of `zstddeclib.c`):
  the `ZSTD_malloc`/`calloc`/`free` macros and the
  `XXH_malloc`/`XXH_free` hooks point at the static pool in
  `deb_zstd.c` instead of libc. One-shot decompression
  (`ZSTD_decompressDCtx`) decodes straight into the caller's
  output buffer and uses the decoded output as its history
  window, so a frame's window size never costs pool memory —
  an 8 MiB-window frame (zstd -19) decodes fine as long as
  its content fits the 2 MiB tar scratch (host test proves
  it with `tools/big19.tar.zst`). Decoder-state pool:
  256 KiB of `.bss`, measured peak 95,968 bytes (one
  allocation: the DCtx).

Two things that bit during this stage, recorded so nobody
re-learns them:

1. `deb_xz.c` must include `xz/xz_config.h`, not just
   `xz/xz.h`: `XZ_USE_CRC64` lives in the config, and without
   it `xz_crc64_init()` is never called, the CRC64 table
   stays zeroed, and every real Debian .xz fails its block
   check (the stream header CRC32 passes, so the failure
   looks like corrupt data, not a missing init).
2. The zstd pool does **not** need to cover the frame window.
   The first version sized it at 2 MiB + slack for the
   window; instrumentation showed one-shot mode makes
   exactly one allocation (the DCtx) and no window buffer,
   so the pool shrank to 256 KiB.

Attribution for both decoders is in `LICENSES/THIRD-PARTY.md`.

## Install model — read this before assuming files land on disk

## Install model — read this before assuming files land on disk

FisixOS has a FAT32 **writer** now (`kernel/drivers/fat.c`,
see `docs/RWFS-PARTITIONS-MULTIBOOT.md`), but no volume is
mounted in the kernel yet, so `proc` file_write still returns
-1 at runtime until a block driver mounts one. So "install"
here means:

1. The `.deb` is parsed and its files are extracted into a
   static 4 MiB RAM staging pool, and the package is recorded
   in an in-memory registry (max 8 packages, 256 files each).
   `deb list` / `deb info` / `deb remove` work on that registry.
   `deb remove` compacts the pool.
2. Persistence across boots comes from **build-time embedding**,
   the same way `.fse` apps already work:
   ```
   cp my.deb OSC/debs/
   python3 tools/embed_deb.py
   make kernel   # (rebuilds with the baked-in .deb)
   ```
   `deb_install_embedded()` installs baked-in packages the
   first time a `deb` command runs in the terminal.
3. **New in the read/write stage:** `deb_persist(name, img,
   len)` writes an installed package's staged files onto a
   FAT32 volume image (via the FAT32 writer), so the install
   survives a reboot once that volume is on disk. Host-proven
   (`tools/rwfs_host_test.c`: the test package's files are
   written to `tools/rwfs_test.img` and re-read byte-identical
   after a save/reload cycle). In the kernel GUI it becomes
   live the moment a volume is mounted (see `vfs.h`); there is
   no `deb persist` terminal command yet, because the GUI has
   no mounted volume to persist to.

There is no `apt`, no dependency resolution, and no
downloading: `Depends:` is parsed and shown by `deb info`,
but missing dependencies are reported, not fetched.

## Terminal commands

```
deb                    usage
deb list               installed packages (name + version)
deb info <name>        version, arch, Depends, description, files
deb install <name>     install a baked-in .deb by package name
deb remove <name>      uninstall (drops staged files)
deb run <pkg> <path>   run a file from an installed package
```

A registered Debian GUI app is the one exception to "dynamic
ELF runs as a Linux binary": `deb run` hands it to the native
LVGL bridge instead of running it in ring 3 (a GUI app's binary
can never run as an ELF — dynamic, needs ld-linux + X11 — so the
bridge re-implements its UI + logic). See `docs/DEB_GUI.md` —
first bridged app is xcalc (`deb run x11-apps ./usr/bin/xcalc`).
Ordinary (non-GUI) dynamic ELFs run in ring 3 — see
`docs/DYNLINK.md`.

## What runs, what does not

| File in the .deb | Result |
|---|---|
| FisixOS `.fse` | **Runs**, via the existing `proc_run_fse()` |
| Static Linux x86-64 ELF | **Runs in ring 3.** `ring3_run_elf()` (see `docs/RING3.md`) loads the PT_LOAD segments, zeroes `.bss`, builds the Linux initial stack, enters ring 3 via iretq, and serves Linux syscalls through the `syscall` instruction dispatcher. The embedded `test-fisixapp.deb` static hello prints `STATIC HELLO\n` and exits 0 — proven by the boot self-test in QEMU and by `tools/ring3_host_test.c` (120 checks). |
| Dynamic Linux ELF (normal Debian binaries) | **Runs in ring 3.** The kernel loads the main executable and its real interpreter (`ld-linux` from the embedded libc6 .deb); ld.so resolves `DT_NEEDED` libraries, relocates, and runs the program. The embedded `hello_2.10-5` prints `Hello, world!` and exits 0 — proven by the boot self-test in QEMU (see `docs/DYNLINK.md`). Dynamic binaries needing a library other than libc, or an unimplemented syscall, still fail — see `docs/DYNLINK.md` §7. |
| Anything else (text, docs, scripts) | Staged and readable via `deb_file_read()`; not executable. |

## The ELF part — what was reused, what is missing

Reused/adapted (all profanity stripped, per the FisixOS
professional-vibe rule):

- **Asher's Rust OS project** (Rust, Asher), `kernel/src/elf.rs`:
  ELF64 header validation (magic, class 64, little-endian,
  x86-64), PT_LOAD segment walk, `.bss` zeroing, Linux
  initial-stack layout (argc=1, argv[0]=NULL, envp NULL,
  auxv AT_NULL). Ported to C in `deb_elf_load()`. Its
  `kernel/src/syscall/mod.rs` syscall-number table is
  documented in `kernel/drivers/deb_linux_syscalls.h`
  (Linux number -> FisixOS number, where one exists).
  Note: that project only runs **static** ELFs too — it has
  no PT_INTERP/dynamic-linker handling either.
- **LetsGo-OS** (MIT, Copyright (c) 2022 Sansero),
  `kernel/elfloader.go` `LoadAuxVector`: the full auxv
  (AT_PAGESZ, AT_UID/GID/EUID/EGID, AT_ENTRY, AT_PHDR,
  AT_PHENT, AT_PHNUM, AT_RANDOM, AT_NULL), ported from
  32-bit to 64-bit in `deb_elf_load()`. Its `userspace.go`
  `InitUserMode` (GDT user segments + TSS) is the design
  reference for the ring-3 step below. LetsGo-OS itself is
  32-bit, so its loader was used as a reference, not copied
  verbatim. MIT notice for LetsGo-OS is in `LICENSES/` —
  keep it with any reuse.

What still does not run, and why:

1. **Ring-3 jump + page tables — DONE 2026-11-10.** See
   `docs/RING3.md`: GDT user segments (CS `0x23` / SS `0x1B`), TSS
   with kernel stack, frame allocator, own page tables, iretq entry,
   SYSCALL MSRs + dispatcher. The static hello runs and exits 0.
2. **Linux `syscall` instruction — DONE 2026-11-10.** See
   `docs/RING3.md` for the implemented / stub / unimplemented table.
   The FisixOS-native `int 0x80` gate (`kernel/drivers/syscall.c`)
   keeps its own numbers (EXIT 0, WRITE 1, READ 2, ...), which are
   **not** the Linux numbers (READ 0, WRITE 1, EXIT 60, ...); the
   Linux dispatcher is separate, in `kernel/drivers/linux_syscall.c`.
   `deb_linux_syscalls.h` maps the overlap.
3. **Dynamic linker / glibc — DONE 2026-11-10.** See
   `docs/DYNLINK.md`: the kernel runs the real `ld-linux` +
   `libc.so.6` from the embedded trixie `libc6` .deb. The
   dynamic hello prints `Hello, world!` and exits 0 in QEMU.

## Host test

The `.deb` host test (109 checks) plus the ring-3 host test
(120 checks, `tools/ring3_host_test.c` — frame allocator, page-table
walker, ELF loader, Linux syscall dispatcher against simulated RAM):

```sh
cd OSC
python3 tools/make_test_deb.py        # builds tools/test-fisixapp.deb
python3 tools/deb_repack_gz.py tools/hello_2.10-5_amd64.deb tools/hello-gz.deb
python3 tools/deb_repack_zst.py tools/hello_2.10-5_amd64.deb tools/hello-zst.deb
gcc -O1 -g -Wall -Wextra -Ikernel/drivers -Ikernel -Iboot \
    tools/deb_host_test.c kernel/drivers/deb.c \
    kernel/drivers/deb_inflate.c kernel/drivers/deb_xz.c \
    kernel/drivers/deb_zstd.c kernel/drivers/deb_embedded.c \
    kernel/drivers/deb_embedded_*_p*.c kernel/drivers/fat.c \
    kernel/drivers/xz/xz_crc32.c kernel/drivers/xz/xz_crc64.c \
    kernel/drivers/xz/xz_dec_stream.c kernel/drivers/xz/xz_dec_lzma2.c \
    kernel/drivers/xz/xz_dec_bcj.c kernel/drivers/zstd/zstddeclib.c \
    -o /tmp/deb_host_test
/tmp/deb_host_test
gcc -no-pie -DFISIX_HOST_TEST -I. -Ikernel -Ikernel/drivers -Iboot \
    -o /tmp/ring3_host_test tools/ring3_host_test.c tools/ring3_host_stubs.c \
    kernel/drivers/ring3.c kernel/drivers/linux_syscall.c \
    kernel/drivers/deb.c kernel/drivers/deb_inflate.c \
    kernel/drivers/memory.c kernel/drivers/deb_xz.c kernel/drivers/deb_zstd.c
/tmp/ring3_host_test
```

Notes: compile with `TMPDIR=~/tmp` (`/tmp` is a small tmpfs); the
ring-3 test needs `-no-pie` because its stubs fake `kernel_end` as the
absolute symbol `0x200000`. See `docs/RING3.md` for the QEMU proof.

The test uses real Debian packages
(`hello_2.10-5_amd64.deb`, downloaded from deb.debian.org,
and `x11-apps_7.7+9_amd64.deb`) and verifies: ar members,
control fields, byte-identical extraction, `.fse` runs, static
ELF loads with a correct auxv, dynamic ELF runs in ring 3 via
its interpreter (see `docs/DYNLINK.md`), the real xz hello installs directly
with no repack and its `/usr/bin/hello` is byte-identical to
the gz and zstd installs (49 files each), the xz and zstd
data tars are byte-identical to the gzip reference at the
decoder level, SHA-256-check xz is refused, truncated xz/zstd
fail, an 8 MiB-window zstd frame still decodes, `deb_persist`
writes fisixapp onto a fresh FAT32 image and README.txt reads
back byte-identical, x11-apps installs (118 files, xcalc
DYNAMIC with the X libraries named), corrupt input fails,
and remove works. 109 checks, 0 failures (2026-11-10).

## Files

- `kernel/drivers/deb.h`, `deb.c` — ar/tar/control/ELF/registry
- `kernel/drivers/deb_inflate.h`, `deb_inflate.c` — gzip inflate
- `kernel/drivers/deb_xz.h`, `deb_xz.c` — xz wrapper (static pool)
- `kernel/drivers/xz/` — vendored xz-embedded (public domain) +
  FisixOS `xz_config.h`; see `COPYING` in that directory
- `kernel/drivers/deb_zstd.h`, `deb_zstd.c` — zstd wrapper (static pool)
- `kernel/drivers/zstd/` — vendored zstd v1.5.7 single-file decoder
  (BSD-3-Clause/GPLv2) + `zstd.h`/`zstd_errors.h`; see `LICENSE`/`COPYING`
- `kernel/drivers/deb_linux_syscalls.h` — Linux/FisixOS syscall map
- `kernel/drivers/deb_embedded.h`, `deb_embedded.c` — baked-in .debs
  (the lookup table; the bytes live in `deb_embedded_<name>_p<NN>.c`
  piece files, split so each file is small enough to push to GitHub)
- `kernel/drivers/gui.c` — `deb` terminal command (small hook)
- `tools/deb_host_test.c`, `tools/make_test_deb.py`,
  `tools/deb_repack_gz.py`, `tools/deb_repack_zst.py`,
  `tools/embed_deb.py`
- `tools/hello_2.10-5_amd64.deb` — real Debian hello (xz)
- `tools/hello-zst.deb` — same hello repacked to zstd level 3
  (dpkg's default), built with `tools/deb_repack_zst.py`
- `tools/sha256check.tar.xz`, `tools/big19.tar.zst` — negative/
  window fixtures for the host test (see its header comment)
- `docs/DEB.md` — this file

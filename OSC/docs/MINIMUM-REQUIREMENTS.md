# FisixOS minimum requirements

Added 2026-11-19, at Asher's direction. These are the minimums —
a machine below any one of them is not supported. They are
minimums, not recommended specs.

| # | Requirement | Minimum |
|---|-------------|---------|
| 1 | RAM | 128 MB |
| 2 | Storage (ROM / disk) | 1 GB |
| 3 | CPU | Any 64-bit x86 CPU (x86-64 / AMD64) |
| 4 | Display | At least 32 colors |
| 5 | GPU (iGPU, dGPU, or eGPU) | Good enough to render 32 colors at a solid 32 FPS |

## What each one means

- **128 MB RAM:** system memory. FisixOS itself is small, but
  the kernel, framebuffer, and any program you run all live
  in RAM. Below 128 MB, do not expect it to boot or run.
  (Raised from 16 MB on 2026-11-19: a 32K Super Scaling
  framebuffer alone needs ~2.4 GB, so 16 MB was silly.)
- **1 GB ROM / storage:** disk space for FisixOS itself
  (bootloader, kernel, system files, and installed programs).
  This is storage, not RAM — a 1 GB drive / partition at
  minimum.
- **Any 64-bit x86 CPU:** x86-64 / AMD64 only. A 32-bit x86
  CPU does not meet it, and neither does ARM — the PC port
  is x86-64 (the ISO build is UEFI x86-64). "Any" means no
  specific brand or model is required: Intel or AMD, any
  64-bit x86 chip that can run in long mode.
- **At least 32 colors:** the display must be able to show
  32 distinct colors (5-bit color). A display that can only
  do fewer than 32 colors does not meet it.
- **GPU at 32 FPS in 32 colors:** whichever GPU the machine
  has — integrated (iGPU), discrete (dGPU), or external
  (eGPU) — it must be able to render the FisixOS display
  in 32 colors at a solid 32 frames per second, not
  stuttering or dropping below that.

## Enforcement — since 2026-11-19

These minimums are **enforced at boot** by
`fisix_check_minimum_requirements()` in `boot/efi_main.c`.
Picking Installed, Liveboot, or RAM Mode in the bootloader
menu runs the check first; a failure prints exactly which
requirement failed ("DOES NOT MEET MINIMUM REQUIREMENTS")
and returns to the menu instead of booting. A backstop in
`efi_main()` runs the same check before loading the kernel
for the terminal-shell "gui" path, which bypasses the menu.

How each one is measured at boot:

- **RAM:** UEFI GetMemoryMap total (conventional + loader
  + boot-services memory), must be >= 128 MB.
- **Storage:** the largest whole BlockIO disk present must
  be >= 1 GB. This is a machine minimum: the boot media
  itself can be small — the 22 MB ISO is boot media, not
  the machine's storage.
- **CPU:** CPUID leaf 0x80000001, EDX bit 29 (long mode).
  The loader only runs as a 64-bit UEFI app at all, so
  this is belt-and-braces.
- **Colors:** current GOP mode PixelFormat. 8-bit RGB/BGR
  = 16.7M colors (pass); BitMask counts R+G+B mask bits
  (need >= 5 bits = 32 colors); BltOnly has no
  framebuffer (fail).
- **FPS:** measured, not assumed. TSC is calibrated
  against BootServices Stall (100 ms), then 32 full-screen
  framebuffer fills are timed with RDTSC. If there is no
  Stall or no TSC, FPS cannot be measured: it prints a
  warning and passes rather than bricking the boot.

Not enforced: terminal shell mode, the Installer (the
machine you install FROM is not the machine that will
run it), and chainloading another OS.

## Current state — read this before assuming these are tested

- **Display today is UEFI GOP framebuffer only**
  (see `docs/REAL-HARDWARE-OPTIMIZATION.md`): no GPU
  acceleration yet — "Accelerated = 0 always" — so the
  32 FPS requirement is currently met (or not) by the
  framebuffer path, not by a GPU driver. A machine whose
  GOP framebuffer cannot hold 32 FPS in 32 colors does
  not meet the requirement, even if its GPU is fast on
  paper. GPU acceleration is separate work, in progress.
- QEMU test setups in the Makefile use `-m 512` (512 MB
  RAM), which is well above the 128 MB minimum, and
  attach a 2 GB sparse test disk (`qemu-test-disk.img`,
  gitignored) so the test VM meets the 1 GB storage
  minimum with room to spare. Without that disk, `make run` (tiny FAT
  directory drive) and `make run-iso` (22 MB CD) would
  both be refused. The Makefile change is build-verified
  only — QEMU is not installed on this VM, so no QEMU
  boot test has run with the new check yet.

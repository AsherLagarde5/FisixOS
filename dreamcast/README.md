# FisixDreamcast v0.1 — FisixOS port to Dreamcast, with VMU

SH-4 rewrite, not PC/GBA/C64 code carried over.
KallistiOS (KOS) homebrew SDK, not Sega SDK,
no Sega BIOS/ROMs bundled, no commercial games.

## Hardware
- Dreamcast: SH-4 200 MHz, 16 MB RAM, 8 MB VRAM,
  PowerVR2, AICA sound, GD-ROM/CD, Maple bus
- Controller: D-pad, analog stick, ABXY, 2 triggers,
  2 expansion slots, VMU window in front slot
- VMU: Sanyo LC86K87 8-bit, 48x32 monochrome LCD,
  196 bytes XRAM framebuffer, 6 buttons, 64 KB flash
  (32 KB program + 32 KB save), 512 bytes RAM

## v0.1 — what is in it
- Menu, Terminal, Pong (text demo), first-time setup
- TV: 640x480 text first, PowerVR GUI is v2
- VMU as SECOND screen, not duplicate:
  FISIX + user / current app / status / tick,
  updated on every app change, via Maple VMU API
  in real KOS build, via ASCII art in host test
- Save: VMU save data, USER.PROF, move VMU to
  another Dreamcast and profile moves with it
- VMU standalone mini-app (detached, 8-bit LC86
  code) is v0.2, not v0.1 — different CPU, different
  toolchain, see vmu/README (placeholder)

## Build / test, honestly
- Toolchain: DONE 2026-10-30. Official KallistiOS/KallistiOS,
  kos-chain stable profile: GCC 15.2.0, binutils 2.45.1,
  newlib 4.6.0.20260123, sh-elf-gcc works (see below).
  KOS itself built (KOS_EXIT:0, libkallisti.a 4.5 MB).
  FisixDreamcast built with KOS, no source changes
  needed (one pre-existing indentation warning only):
  fisixdreamcast.elf 1.7 MB, SHA-256
  f8e09029aa524663df21bde0af9242cbe1ed9f1331a9cd1b0971bd2c8898a034.
  .cdi packaged with mkdcdisc v0.0.4 (built from
  gitlab.com/simulant/mkdcdisc): fisixdreamcast-v0.1.cdi
  102 MB, SHA-256
  c03d087ebffc0af5df2d3a0faf9bb074e8cfa21302c1e57f93dd16309d44a703.
  mkdcdisc reported all steps done, including boot
  prerequisites check. Logs: toolchain-build.log,
  kos-build.log, kos-app-build.log in this folder.
- Flycast / real-hardware boot test: NOT done yet.
  Do NOT claim the .cdi is playable yet. Known v0.1
  limit: the text menu reads stdin (serial/console in
  KOS), it is NOT yet wired to the Dreamcast controller,
  so on a console/emulator it will not take controller
  input. Controller-input wiring + PVR text/GUI display
  is the next step, before calling v0.1 playable.
  GUI (gui.c), JPEG/video math (media.c) and the
  36-entry accessory table (accessories.c) ARE compiled
  into the .elf, but GUI PVR drawing, JPEG decoding and
  live Maple accessory drivers are NOT yet wired in —
  they are next steps, not claimed working on screen.
- Host test passed: setup TESTUSER, VMU updates
  on menu/terminal, SYSINFO, MEM, VMU, CALC 3*4=12,
  EXIT, SHUTDOWN.
- Real test needs: KOS build, .cdi packaging
  (mkdcdisc), Flycast emulator (loads .cdi, emulates
  VMU with .vmu file), or real Dreamcast with
  GDEMU/MODE or dc-tool. Not done here yet.

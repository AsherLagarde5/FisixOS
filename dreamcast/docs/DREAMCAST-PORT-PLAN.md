# FisixDreamcast — FisixOS port to the Sega Dreamcast, with VMU

## Hardware, verified from public specs

Dreamcast main:
- Hitachi SH-4, 200 MHz, 32-bit RISC, 360 MIPS
- 16 MB main RAM, 8 MB VRAM, 2 MB AICA audio RAM
- NEC PowerVR2 GPU, 100 MHz, ~3.5M polygons/sec
- Yamaha AICA sound, ARM7, 64 channels
- GD-ROM 1.2 GB, CD-ROM, MIL-CD boot for homebrew
- 4 controller ports, Maple bus
- Controller: D-pad, analog stick, 4 face buttons
  ABXY, 2 analog triggers, Start, 2 expansion slots,
  window for VMU screen in front slot
- VGA box output 480p, keyboard/mouse peripherals exist

VMU (Visual Memory Unit, in controller front slot):
- Sanyo LC86K87, 8-bit low-power CPU
- 48x32 monochrome LCD, plus 4 icons, 196 bytes
  XRAM as framebuffer
- 6 physical buttons, for use when detached
- 16 KB Mask-ROM BIOS-IPL
- 64 KB Flash: 32 KB for a single uploaded program,
  32 KB for save data (memory card)
- 512 bytes RAM: 256 system, 256 program
- 128 KB user storage quoted for the card as a whole,
  200 blocks usable in Dreamcast shell terms
- Two modes: docked in controller (screen visible
  through controller window, powered by Dreamcast),
  and detached (runs on its own battery, mini-games)

## What FisixDreamcast is, v0.1

An actual OS on Dreamcast, not a game:
- Boots from CD (.cdi, MIL-CD) via KallistiOS (KOS),
  the Dreamcast homebrew SDK / unikernel, not Sega's
  official SDK, no Sega BIOS/ROMs bundled.
- Main OS: menu, Terminal (shell), Pong, file manager,
  first-time setup, USER.PROF on VMU save data.
- TV screen: 640x480, PowerVR, text mode first,
  3D GUI is v2, not v0.1.
- Controller screen: VMU LCD is a SECOND screen,
  not a duplicate. v0.1 VMU use, docked:
  - Top line: FISIX + current user
  - Second line: current app (MENU / TERMINAL / PONG)
  - Third line: status (OK, or file count, or score)
  - Bottom: a tiny 48x32 icon / clock tick
  VMU LCD is updated via KOS Maple VMU API
  (vmu_draw_lcd / maple device, in KOS terms),
  not by writing directly to VMU hardware from
  the SH-4 — the VMU is a separate CPU, the
  Dreamcast talks to it over Maple bus.
- VMU standalone mini-app: v0.2, not v0.1.
  A separate 8-bit VMU program (32 KB max) that,
  when the VMU is removed from the controller,
  shows FISIX logo, user name, and a tiny Pong
  or clock, using its 6 buttons. That code is
  NOT SH-4 code, it is LC86 code, with a different
  toolchain, and it is not in v0.1.
- Save: VMU save data, 32 KB save area, plus
  VMU files on the card. Move the VMU to another
  Dreamcast controller and your profile moves
  with it — same "take it with you" idea as GBA
  cart, but the VMU is the carrier, not a disc.
  Disc (.cdi) itself is read-only, like a CD.

## Toolchain / emulator, honestly

- Toolchain: KallistiOS, sh-elf GCC for SH-4,
  built via utils/kos-chain, 30-60 minutes, from
  source, no prebuilt package in the usual KOS
  docs. KOS_BASE, KOS_CC_BASE, etc., in environ.sh.
- Emulator for test: Flycast (free, open source),
  also Redream, lxdream in older docs. Flycast
  loads .cdi, emulates VMU (with a .vmu file),
  shows VMU screen in a separate window / overlay.
- Real hardware test: burn .cdi with a tool that
  handles Dreamcast multi-session layout, or use
  GDEMU / MODE optical drive emulator, or dc-tool
  over serial / Broadband Adapter. Not done here.
- v0.1 in this VM: KOS toolchain is NOT built
  here yet (30-60 min build, not done in this turn),
  so v0.1 code is a KOS scaffold, with a host
  logic test using stub KOS headers, proving
  menu/Terminal/VMU-LCD logic, NOT a Dreamcast
  boot test. Do not claim v0.1 boots on Flycast
  or real Dreamcast yet — that needs KOS build +
  .cdi packaging + Flycast test, next step.

## Files, v0.1

- src/main.c: KOS main, menu, Terminal, Pong,
  setup, VMU LCD update (vmu_lcd_draw)
- src/vmu.h: VMU LCD API abstraction, 48x32,
  with host stub that prints VMU screen as ASCII art
- docs/DREAMCAST-PORT-PLAN.md: this file
- vmu/: placeholder for v0.2 standalone VMU app,
  with a README explaining LC86 vs SH-4 difference

## Update 2026-10-30 — GUI scope (Derek)
- DO make a 2D GUI for Dreamcast, because Dreamcast has room:
  640x480, 16 MB RAM, PowerVR2, vs GBA 240x160.
- GUI = YES, 2D only: icons for TERMINAL, PONG, ABOUT,
  SHUTDOWN, etc., PowerVR framebuffer / 2D, controller
  analog stick as mouse, D-pad navigation as fallback.
- 3D menus = NO, still explicitly NOT to do, would take
  forever. Do not start 3D menu work.
- Text menu in src/main.c stays as fallback, not removed.
  GUI is in addition to text menu, not a replacement
  that removes text fallback. VMU LCD logic stays.
- GUI is built AFTER toolchain builds, and is only
  claimed done if it actually compiles with KOS.

## Update 2026-10-30 — JPEG + video, Dreamcast ONLY
- Add JPEG viewer for Dreamcast: decode JPEG to PowerVR
  texture, display at 640x480, from VMU / disc / SD.
- Add generic/simple video player for Dreamcast:
  practical limit MPEG-1 / MJPEG style, low resolution,
  NOT 4K H.264 — SH-4 200 MHz cannot do that smoothly.
  Be honest about that limit in docs.
- PC version later, NOT by this job. Do NOT add JPEG/
  video to C64 or GBA. Do NOT modify fisixc64/ or
  fisixgba/ for this. Reason: Dreamcast has room,
  C64 (64KB) / GBA (256KB, 240x160) do not.
- If JPEG/video cannot be built in this pass, report
  as next step with library needed (stb_image / libjpeg,
  KOS video), do not claim it works unless it compiles
  with KOS and displays.

## Update 2026-10-30 — Dreamcast accessories, Dreamcast ONLY
Priority via KOS Maple bus where possible:
1. VMU (expand: second screen + save + 4x Memory Card
   variant + third-party memory cards)
2. Standard controller + Jump Pack / Rumble Pack
3. Keyboard + Mouse
4. Dreameye camera (Japan only, webcam + still + mic
   headset, Maple / serial)
5. Light Gun / Dream Blaster (VMU slot in gun, US
   third-party Mad Catz version)
6. Arcade Stick, Twin Stick (Virtual-On), Race
   Controller / steering wheel, Fishing Controller
   (motion + reel), Maracas Controller (Samba de Amigo,
   with mat), Microphone (Seaman, Dream Karaoke)
7. Densha de Go! train controller, Dance mat, Pop'n
   controller, DreamParaPara sensors, Mission Stick,
   Panther DC, VCD remote
8. Network: built-in 56k Modem, Broadband Adapter
   (BBA, Ethernet), Dreamphone, MIDI Interface Cable,
   Neo Geo Pocket / Dreamcast link cable, VS / Taisen
   cable, Coder's cable, Serial SD adapter
9. Video: VGA Box (480p), RF / SCART / AV cables
   (output modes, document as output support)
Honest limits:
- Unreleased: DVD Player, Zip Drive, Swatch Access,
  VMU MP3 Player — documented as unreleased / no
  hardware to test, stubs only, NOT claimed working.
- Japan-only / rare: Dreameye, Densha de Go, Karaoke —
  driver scaffolds with KOS Maple protocol, marked
  untested without hardware, NOT claimed tested.
- Do NOT add accessories to C64/GBA/TI-89, Dreamcast only.
- Previous constraints stay: 2D GUI yes, 3D menus NO,
  JPEG/video Dreamcast+PC only, text menu fallback stays.

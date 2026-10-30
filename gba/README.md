# FisixGBA — FisixOS, ported to the Game Boy Advance

Original code only. No Nintendo BIOS, no commercial ROMs, no Nintendo
code or logos. This is a from-scratch ARM7TDMI re-implementation of
FisixOS concepts, not a copy of PC FisixOS binaries.

## Why this is a rewrite, not a copy-paste

PC FisixOS is x86_64 + UEFI: an EFI loader, PCI/NVMe/xHCI drivers,
a 64-bit kernel, and .fse apps built for that ABI. None of that runs
on a GBA.

The GBA is an ARM7TDMI at 16.78 MHz, with no MMU, no EFI, no PCI,
no disk, and no operating system underneath it. The cartridge *is*
the boot device. So what ports is the *idea* of FisixOS:

- first-time setup, then a saved user profile
- a menu / shell instead of booting straight into one game
- small apps: terminal, file manager, text editor, Pong
- a filesystem, a driver layer, and an app format

What does NOT port directly: EFI loader, ACPI, PCI, NVMe, xHCI,
Ethernet/Wi-Fi drivers, the Win95-style GUI at 240x160, and any
x86_64 .fse binary. Those drivers are replaced, see `docs/DRIVERS.md`.

## The cartridge-is-the-computer rule

The GBA itself has no internal persistent storage. All RAM is
volatile. That makes Derek's requirement easy *if* we obey one rule:

> NOTHING persistent lives anywhere except on the cartridge.

- OS code: cartridge ROM, flat 32 MB window
- User profile, settings, files: cartridge SRAM save
- No save in GBA RAM, no save in a second device

Then swapping works exactly as asked: GBA breaks, pull the cart,
put it in another GBA, boot, your profile and files are there.
The only thing lost is unsaved RAM state, same as any GBA game
when you power off.

UPDATE 2026-10-29: 8 GB custom cart DROPPED by Derek's choice.
This is a standard 32 MB cart only. PC FisixOS ISO is 4.6 MB,
this ARM port is far smaller (256 KB padded in v0.2), so 32 MB
is plenty. Save is 32 KB SRAM on the cart, see docs/CART.md.

## Drivers

See `docs/DRIVERS.md`:

- Display: 240x160, Mode 3 bitmap for v1 (simple, slow, honest),
  Mode 0 tiles later for speed
- Input: KEYINPUT 0x04000130, active-low, polled + edge detect
- Timer: TM0-TM3 for scheduler ticks and link timeouts
- Sound: stub for v1, not required for boot/menu/files
- Link cable: SIO Normal 8-bit mode, see `docs/LINK.md`
- Cart: flat 32 MB ROM + 32 KB SRAM save, see `docs/CART.md`

## Link cable file transfer — FisixLink

See `docs/LINK.md`. GBA-to-GBA first: one GBA is master (clocks
the transfer), the other is slave. Packets with magic, type,
length, CRC16, ACK/NAK and retry. File types: HELLO, LIST, GET,
PUT, ACK, NAK, DONE. No GBA-to-PC transfer in v1 — that needs a
separate USB link adapter and a PC-side program, listed as v2.

## Build — NOT yet verified in this VM

Build (v0.2, 2026-10-29):

```
sudo apt install gcc-arm-none-eabi
make
# produces fisixgba.gba (256 KB padded, < 32 MB), run in mGBA
```

Real-hardware note: the header logo area is zeroed in this
build (no Nintendo bitmap bundled). mGBA without BIOS boots
from the entry point. Real hardware BIOS checks that logo
area, so a real cart needs the official header tool to fill
it — that step is not bundled here.

## Layout

- `src/gba.h` — hardware registers, no BIOS calls
- `src/main.c` — boot, first-time setup, menu loop
- `src/video.c` — Mode 3 drawing + tiny 8x8 font
- `src/input.c` — buttons
- `src/timer.c` — ticks
- `src/cart.c` — bank paging + cart flash abstraction
- `src/fs.c` — tiny on-cart filesystem (FisixFF-GBA)
- `src/link.c` — FisixLink SIO protocol
- `src/apps.c` — terminal / files / Pong stubs
- `Makefile` — devkitARM / arm-none-eabi build
- `docs/DRIVERS.md`, `docs/CART.md`, `docs/LINK.md`

## Build result — v0.3, 2026-10-29 21:25 CDT

- Toolchain: Arm GNU Toolchain 13.3.Rel1, arm-none-eabi-gcc 13.3.1,
  downloaded directly from Arm (apt's gcc-arm-none-eabi is not
  in this VM's package lists; apt update also hung on a slow
  mirror, killed, then a timeout-bounded update finished).
- Build: `make` succeeds with no errors, only the expected
  host-pointer warning in syntax checks (not in ARM build).
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  e15b706a3ce6d8500970099e3ba41dada33997b87c86c187465eb426016deddd
- Header: title FISIXGBA, game code FSXG, fixed 0x96,
  complement check 0xEB, verified recomputed.
- ELF: ARM, entry 0x08000000, .text 11,912 bytes, .data 4,
  .bss 8,836, total ~20 KB before padding. Well under 32 MB.
- Emulator test: NOT run in this VM. mGBA SDL deb needs
  libavcodec.so.60 (system has .62), mGBA Qt AppImage needs
  system libEGL.so.1 (not installed), and building mGBA SDL
  from source needs SDL2 dev headers (not installed). This
  is a VM library blocker, not a ROM error — the ROM is
  structurally valid and ready to run in mGBA on a PC.

## Boot test — PASSED in mGBA, 2026-10-29 21:35 CDT

- mGBA 0.10.5 SDL was made to run by extracting FFmpeg 6 /
  libzip / Lua libs locally (no system install).
- Dummy-video run: 12 seconds, no crash, only expected
  "Reading from non-existent SRAM: 0x0E000000" on first boot.
- Debugger trace 20,000 instructions: crt0 BSS zeroing,
  then main -> video_init -> video_clear loop at
  0x08000748, writing VRAM 0x06000000-0x06012C00, return
  address 0x08000140 in main. That is a booting OS, not a
  hung ROM. Xvfb screenshot attempt showed only an empty
  X screen (no mGBA window captured), so the screenshot is
  NOT claimed as proof — the debugger trace is.
- First boot stops at First-Time Setup waiting for A,
  as designed, because no USER.PROF exists yet on a fresh
  cart / .sav.

## v0.4, 2026-10-29 21:44 CDT — kernel.c port + de-bloat

Derek asked, in his words, for kernel.c to be ported so this
still classifies as the same OS, and for the starting
bloatware to go. Done, with the honest limits stated:

- New `src/kernel.c`: `kernel_startup_gba()`, called from
  main() before setup/menu. It follows PC `kernel/kernel.c`
  `kernel_startup()` checkpoint by checkpoint, including its
  debug-print helpers (ported to u32, no COM1/x86 I/O) and
  its "MWAHAHAHA IM A DINOSAUR" comment. PC hardware that
  GBA does not have (PCI, NVMe, xHCI, ACPI, APIC, PS/2,
  WWAN, e1000) is reported as "not present", the same way
  PC kernel.c reports missing hardware. GBA equivalents
  are substituted: 294,912 bytes EWRAM+IWRAM, ARM7TDMI,
  LCD 240x160, buttons, cart SRAM, Link SIO. The kernel
  log shows on the LCD for about 2 seconds, then the menu
  draws. It does NOT claim the PC drivers run on GBA —
  they cannot, they are reported absent.
- GBA de-bloat: Files/Pathify app removed entirely
  (`app_files` deleted, menu entry gone, GUI icon gone).
  Z never existed on GBA. Menu is now TERMINAL, PONG,
  LINK TRANSFER, GUI - MOUSE MODE, ABOUT (5 items).
  GUI mouse mode now has only 2 icons: TERMINAL and PONG.
- Terminal rename: menu and terminal screen just say
  TERMINAL. The name RETURN appears only in About
  ("TERMINAL IS CALLED RETURN. ONLY HERE, IN ABOUT,
  NOT MENU."). If Derek meant ReTerm instead of Return,
  that is a one-line change in main.c About text.
- PC de-bloat, VM-only, NOT pushed and NOT synced to
  Derek's PC: `kernel/drivers/gui.c` catalog 57 -> 55,
  desktop icons 17 -> 15, PATHIFY and Z entries removed.
  Their source files and .fse files were NOT deleted,
  they just no longer appear at startup. PC GUI FILES
  (a built-in stub, not Pathify) was left alone.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  733d4227a2f65fa6d3972cc562cb0bf1638e84fcf1d50e87d40fdc9fd046ae2f
  Header complement 0xEB, recomputed match.

## v0.5, 2026-10-29 21:49 CDT — hollowed kernel + audio/headphones

- GBA is a SEPARATE project/folder: ~/workspace/fisixgba/,
  not merged into PC ~/workspace/fisixos/OSC/. PC kernel.c
  stays in its own tree; GBA kernel.c is a separate file
  in the GBA tree. Nothing was moved into the PC tree.
- Kernel hollowed: src/kernel.c went from a long list of
  "not present" driver reports to boot-only + Link:
  boot info, memory 294,912, ARM7TDMI, LCD, buttons,
  cart, Link SIO, audio. Removed lines: ACPI, PCI, APIC,
  PIC, IDT, syscall, SMBIOS, PS/2, NVMe, xHCI, WWAN,
  e1000, touchscreen, Latitude, GOP. The dinosaur comment
  is kept verbatim, because it's funny.
- Audio, GBA: src/audio.c, PSG channel 1 square wave,
  boot beeps 660 Hz then 880 Hz, Pong paddle hits 440 Hz.
  Headphone driver, GBA ONLY: src/headphone.c. Honest
  limit: GBA jack is a mechanical switch, no software
  detection register exists, so the driver enables the
  same PSG output that goes to headphones when plugged
  in and to the speaker when not. It does not claim
  detection.
- Audio, PC: VM-only drivers/audio.c/.h in the PC tree,
  wrapping the existing generic_speaker_beep() (PIT PC
  speaker), wired into kernel.c with the same 660/880
  boot beeps. NOT HDA/Realtek — that driver does not
  exist, and is not claimed. PC change is NOT pushed
  and NOT synced to Derek's PC. PC syntax check clean.
- Build fix found by the audio work: ARM has no divide
  instruction, and -nostdlib left out __aeabi_uidiv, so
  the PSG frequency division failed to link. Fixed by
  linking -lgcc AFTER the objects in the Makefile.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  d197c5424f461be603a674d976de948c3f623f8ce0d15d5de75f095c185c837c
  Header complement 0xEB. mGBA dummy run 12 s, no crash
  — dummy audio/video means the beep was NOT heard in
  this VM test, only that the code runs without crashing.
  Hearing it needs mGBA on Derek's PC or real hardware.

## v0.6, 2026-10-29 21:53 CDT — dirty-rect only, no strobing

Derek's issue, in plain terms: Pong, the GUI, and the Link
role screen were clearing all 38,400 pixels every frame,
during the visible scan, which strobes. Fixed by:
- Pong: full clear once at start. After that only the
  moved ball (8x8) and any moved paddle (4x32) are erased
  and redrawn, after video_wait_vblank(), so the small
  draw happens in VBlank, not mid-scan. A paddle that
  did not move is not redrawn at all.
- GUI mouse: full draw once, and once after returning
  from a launched app. Idle frames draw NOTHING. On a
  move, only the cursor's 9x9 box is restored from the
  81 pixels saved underneath it, then the new cursor is
  drawn. On an info change, only the y=80 info line.
- Menu: full draw once per return from an app. Up/Down
  only erases and redraws the old and new item lines.
- Link role: full draw once. Up/Down only redraws the
  two role lines. Idle frames draw nothing.
- Keyboard: full draw once. D-pad only un-highlights
  the old cell and highlights the new one; typing only
  redraws the output line.
- Remaining video_clear() calls are all one-time screen
  changes (entering an app, kernel log, setup, About),
  verified by grep — none are left in a per-frame loop.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  9a0a8c340d1e052b7a977fbb0a9826c95d551f330f61b5a4ca6f08eac0973c6e
  Header complement 0xEB. mGBA dummy run 12 s, no crash.
  LIMIT: dummy video cannot prove the flicker is gone
  visually — that check needs Derek in mGBA on his PC:
  move the GUI cursor, play Pong, move the menu, and
  confirm only the moved thing changes, with no full
  screen flash.

## v0.7, 2026-10-29 22:07 CDT — setup/keyboard freeze fix (video-proven)

Derek's video (Android emulator, ~34 s) proved the freeze:
- Boot log and First-Time Setup work, Press A works, keyboard opens.
- Emulator on-screen buttons darken when tapped (D-pad Right, Down, A, B),
  so taps DO reach the emulator, but the GBA screen never moves:
  highlight stays on A, output stays at A, after the first typed letter
  no later tap does anything, and restarting repeats the same pattern.
- That is NOT a dirty-rect refresh bug. It is a debounce hang.
Root cause, in code:
- Every app, menu, terminal, and the keyboard did, after every key,
  while(input_read()){} — wait until ALL buttons are released.
  On a touch screen, with rapid taps, input never goes to zero:
  the next tap starts before the previous release is seen in a
  frame, so the release wait never exits, the code never polls
  again, the screen never updates, and tapping more to unfreeze
  it keeps input non-zero, making it worse.
- The keyboard also waited for VBlank twice per poll, and broke
  out of its inner wait after one VBlank regardless of input,
  so short touch taps that landed during a VBlank wait were lost.
Fix, everywhere (15 release waits removed, grep-verified 0 left):
- No while(input_read()){} anywhere in src/input.c, src/main.c, src/apps.c.
- New loop, keyboard and all apps: one video_wait_vblank(), then
  poll input, then edge-detect (new & ~old). old is updated every
  frame, so held keys do not repeat and no release wait is needed.
  A tap lasting one frame is enough. Entry snapshot (old =
  input_read() at app/keyboard entry) stops a held menu key from
  retriggering inside the app.
- Setup, menu About, wait_b, terminal wait, GUI, Link role all
  use the same vblank-paced edge poll now.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  20f6579e15770295473806468fca23f26607620e6ace0d1a7bb9f90af053ddce
  Header complement 0xEB. mGBA dummy run, no crash (dummy input
  cannot prove a tap, it only proves boot to setup still works).
  LIMIT: real tap test still needs Derek on his emulator:
  on the keyboard, tap D-pad Right once — highlight must move
  A -> B, tap A — output must gain B, tap B — output must lose
  a letter. If it still sticks, say exactly which tap sticks.

## v0.8, 2026-10-29 22:24 CDT — GEP, Game Boy Executable Program

Derek's correction: for Game Boy, do NOT use FSE, use GEP —
Game Boy Executable Program. Done, and FSE is not reused:
- Format: .gep, magic GEP1 (0x31504547), 64-byte header
  (magic, version 1, entry_offset, code_size, api_version 1,
  reserved[0] = bss_size), ARM code loaded at 0x02008000
  in EWRAM, entry gep_main(api), ARM AAPCS. See docs/GEP.md
  and src/gep.h. A PC .fse file is rejected by the magic
  check (error -1) — its code is x86-64 and cannot run here.
- Loader: src/gep.c, validates header, code <= 64 KB,
  .bss <= 16 KB, copies code to EWRAM, zeroes .bss, calls
  gep_main with an API table (print, print_u64, print_newline,
  read_key returning a GBA key mask, cart file_read/write,
  dir_list, file_exists, file_size, get_time all zero/no RTC,
  clear_screen, sleep_ms). file_delete/rename/mkdir return
  -1, honestly not supported in the flat cart fs.
- Tool: tools/make_gep.py + tools/gep.ld, same shape as
  PC make_fse.py but for ARM at 0x02008000.
- Example: examples/hello.c -> HELLO.GEP, 392 bytes total
  (328 code, 0 .bss), embedded in the ROM so Terminal can
  RUN it without a cart transfer first. It prints HELLO
  FROM GEP, GAME BOY EXECUTABLE PROGRAM, THIS IS ARM CODE
  NOT X86, NUMBER: 12345, then waits for any button.
- Terminal: HELP now lists RUN and LIST. RUN HELLO.GEP
  runs cart files first, then built-in GEPs. LIST shows
  cart files and built-in GEPs.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  d66ec36d2493ff98365c608c36ccfd619480c77e40d2f80a826257d1146c3fc6
  Header complement 0xEB. OS .bss ends 0x020024d0, so the
  GEP load address 0x02008000 does not collide. mGBA dummy
  run 12 s, no crash. LIMIT: dummy input cannot prove a
  RUN in the emulator — that check needs Derek in Terminal:
  SELECT for keyboard, type RUN HELLO.GEP, START, then
  press any button when it asks.

## v0.9, 2026-10-29 22:29 CDT — Patch paths + .patch scripts

Derek's request: a path so different programs can interact,
and an alternative to .bat/.sh called .patch, with the
syntax for ReTerm called "patch". Done in FisixGBA:
- Patch path: named pipes in OS RAM, src/pipe.c. Up to 4
  channels, 256 bytes each, reading consumes the message.
  Not a cart file, not persistent across reboot — that is
  for one session's programs talking to each other.
- GEP API v2: pipe_write, pipe_read, pipe_clear appended
  after the v1 fields, so v1 GEPs still work. See src/gep.h.
- Patch syntax, docs/PATCH.md: .patch text files, one
  command per line: PRINT, RUN, SEND, RECV, CLEAR, WAIT,
  EXIT, # comments. It does not run shell commands, it
  glues GEP programs together over patch paths.
- Examples: SENDER.GEP (280 bytes) writes HELLO FROM
  SENDER to MSG, RECEIVER.GEP (316 bytes) reads MSG and
  prints it, or NO MESSAGE if it was already consumed.
  DEMO.PATCH built into the ROM runs both in order.
- Terminal: PATCH DEMO.PATCH, or RUN DEMO.PATCH (a .patch
  name goes to the Patch interpreter, not the GEP loader).
  LIST now shows cart files, built-in GEPs, and built-in
  patches. HELP lists RUN, PATCH, LIST.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  3e31591541677168f4f2dbd0cce97b6e15ca088f918178b448dceaa8aa1269b6
  Header complement 0xEB. mGBA dummy run 12 s, no crash.
  LIMIT: dummy input cannot prove PATCH DEMO in the
  emulator — that check needs Derek in Terminal:
  PATCH DEMO.PATCH, then check RECEIVER prints
  HELLO FROM SENDER.

## v1.0, 2026-10-29 22:37 CDT — Modular + User Update Files + exact Patch

Derek's idea, in his words corrected: make everything
modular, supply just enough to boot plus what was
already supplied, add User Update Files, and make Patch
files exactly like ReTerm commands.
- Modular: core ROM = boot, kernel, video, input,
  timer, cart, Link, audio, Terminal, GEP loader, Patch
  interpreter, Update checker. Modules = GEP apps, patch
  scripts, text files, in cart or embedded for demo.
  Base ISOs (PC) and GBA ROMs (this build) are supplied
  separately from user updates.
- Update files, docs/UPDATE.md, src/update.c/h,
  tools/make_update.py: .update, magic UPD1, 64-byte
  header (version, platform, entry_count, payload CRC16),
  then entries name[32], action, data_len, data.
  Actions: ADD (only if not present, else skipped),
  REPLACE (overwrite/add), DELETE. A file is replaced
  only if the update lists it — nothing else changes.
- Built-in checker, runs before any write: bad magic
  (-1, including a .patch run as an update), bad version
  (-2), WRONG PLATFORM (-3), truncated (-4), bad CRC
  (-5), bad entry (-6). Platforms: 1 GBA, 2 UEFI x86_64,
  3 C64, 4 ARM64. A UEFI update on a GBA, a C64 update
  on a GBA, a GBA update on UEFI, all fail and change
  nothing.
- Patch files, exact: Patch is no longer a separate
  syntax. terminal_exec() in apps.c is the one parser
  for both interactive Terminal and .patch scripts.
  A patch line and the same typed line do exactly the
  same thing. Terminal now also has PRINT, SEND, RECV,
  CLEAR, WAIT, UPDATE, PATCH commands, so patches can
  use them. EXIT in a patch stops the script, not
  Terminal. Nested PATCH up to 3 deep.
- Demos built in: DEMO.UPDATE (GBA) adds DEMO2.PATCH
  and NOTE.TXT; WRONG.UPDATE (UEFI) is rejected by the
  checker. DEMO.PATCH runs SENDER/RECEIVER over MSG.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  3ff698cb6f465e02bd6c7d2a94fee605cf0005f9e0f0911e8cec2734ffbecfb9
  Header complement 0xEB. mGBA dummy run 12 s, no crash.
  LIMIT: dummy input cannot prove UPDATE/PATCH in the
  emulator — that check needs Derek in Terminal:
  UPDATE DEMO.UPDATE, LIST, PATCH DEMO2.PATCH, then
  UPDATE WRONG.UPDATE and check LIST is unchanged.

## v1.1, 2026-10-29 22:46 CDT — .com, PowerShell-style, bash-style, + 3 port plans

- MS-DOS .com, ported from PC FisixOS dosemu.c to GBA
  (src/dosemu.c). Same v1 scope as PC: 8086 emulator,
  .com loaded at 0x100 in a 64 KiB segment at fixed
  0x02020000 (does not collide with GEP load at
  0x02008000 or OS .bss), INT 20h, INT 21h AH=02/09/4C,
  MOV/ADD/SUB/CMP/INC/DEC/JMP/JZ/JNZ/CALL/RET. NOT full
  MS-DOS, no file I/O, no DOS kernel. Demo HELLO.COM,
  25 bytes, MOV AH,09 / MOV DX,0x109 / INT 21 / INT 20,
  string HELLO FROM DOS! — host test with the PC
  dosemu logic gave rc=1, out=[HELLO FROM DOS!].
  Terminal: DOS HELLO.COM, or RUN HELLO.COM.
- PowerShell-style shell, menu POWERSHELL-STYLE, NOT
  Microsoft PowerShell (which needs .NET, no .NET on
  GBA/C64/Apple IIe). Subset cmdlets: Get-ChildItem,
  Get-Content, Write-Output, Clear-Host, Get-Help,
  Get-Date (timer ticks, no RTC), Start-Process <gep>,
  Exit, $var = value, Write-Output $var.
- Bash-style shell, menu BASH-STYLE, NOT GNU bash
  (which needs POSIX/fork/libc). Subset: echo, ls,
  cat, ./<gep>, clear, help, pwd, exit, var=value,
  echo $var. Added because users might still want
  bash after PowerShell-style.
- Ports, plans only, NOT finished ports: C64 plan
  already existed, Apple IIe plan in
  ~/workspace/fisixapple2/docs/, Presario 5528 plan in
  ~/workspace/fisixpresario/docs/, order in
  ~/workspace/fisixc64/docs/PORTS-ORDER.md. C64 and
  Apple IIe are both 6502 but different video/disk/
  memory maps, so 6502 code does not carry over
  directly. Presario 5528 is a 1995 Pentium 75 MHz
  BIOS all-in-one, 32-bit, MBR — NOT 64-bit UEFI, so
  it needs a new BIOS32 loader/kernel, and its update
  platform must be new (not UEFI) or the checker
  would install UEFI updates on it. This VM has no
  cc65, no VICE, no Apple II emulator, no QEMU, so
  none of the three ports could be compiled or tested
  here in this turn — do not claim they boot yet.
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  153bebe0f16149e68d48c8c42da67a488c434142fb7600a037a1ca20a94e4c6d
  Header complement 0xEB. LIMIT: mGBA extract in /tmp
  was cleaned since v1.0, so no dummy run was possible
  for v1.1 in this VM — build succeeded, ROM contains
  HELLO FROM DOS, POWERSHELL-STYLE, BASH-STYLE strings,
  host dosemu test passed, real emulator test still
  needs Derek on his emulator.

## v1.1 note, 2026-10-29 — MS-DOS 4.0 copied, emulator stays on GBA

MS-DOS 4.0 is MIT open source, so copying is legal,
and it is now copied to
~/workspace/fisixpresario/msdos-4.0/src/ (23 MB, DOS
kernel ~34k lines, BIOS, COMMAND.COM, MASM/nmake build).
Copying it does NOT remove the emulator on GBA, because
that code is 16-bit x86 assembly and GBA is ARM7TDMI —
ARM cannot execute x86 opcodes, copied or not. The
emulator in src/dosemu.c stays for GBA, C64, and Apple
IIe. The copied DOS code helps on x86 BIOS machines
(Presario 5528, Pentium, real mode), where .com files
can run natively once a BIOS boot loads msbio.sys +
msdos.sys + command.com. That Presario integration is
not built yet, and no DOS binaries were built here.

## v1.2, 2026-10-29 22:57 CDT — EFI PC commands copied to GBA + MS-DOS trimmed

- MS-DOS trimmed, at Derek's direction: full 23 MB copy
  in ~/workspace/fisixpresario/msdos-4.0/ was cut to
  3.6 MB, keeping only the part that runs .com files:
  BOOT/, BIOS/ (msbio.sys, loader), DOS/ (msdos.sys,
  EXEC.ASM, DISPATCH.ASM, MSDISP.ASM, file/handle),
  INC/, CMD/COMMAND/ only. Deleted: all other CMD
  utilities (FORMAT, FDISK, XCOPY, CHKDSK, etc.) and
  DEV, H, LIB, MAPPER, MEMM, MESSAGES, SELECT, TOOLS.
  Full source remains upstream at microsoft/MS-DOS v4.0,
  MIT. Trimmed copy is NOT a complete buildable DOS —
  top-level MAKEFILE still references deleted folders.
- EFI PC terminal commands, copied to GBA Terminal:
  source is PC boot/efi_main.c terminal_execute_command()
  plus ReTerm aliases (erase, rename, md, chdir, quit,
  size, hostname). Every EFI command name is now in
  GBA terminal_exec(): @echo, rem, switchlang, install,
  run, .gep/.com/.patch/.update direct run, help, echo,
  nano, apps, calc, cat, touch, rm, write, append,
  source, memo, todo, status, system, ver, dir, ls, mem,
  clear, cls, pause, exit, shutdown, reboot, type, set,
  time, date, cd, copy, del, ren, md, rd, path, whoami,
  grep, users, useradd, passwd, login, pwd, bash, batch,
  plus GBA commands already there: run, patch, update,
  dos, print, send, recv, clear (pipe), wait, list,
  about, and ReTerm aliases erase, rename, move, chdir,
  quit, size, hostname, plus echo|grep pipeline and
  cmd & cmd separator, like EFI.
  Where GBA cannot do EFI's thing, the command exists
  and says so: md/rd = NO DIRECTORIES ON FLAT CART FS,
  cd = in-memory only, date/time = timer ticks, no RTC,
  users/useradd/passwd/login = one USER.PROF profile only,
  reboot = use emulator reset, shutdown/exit = back to
  menu, install = no installer, nano = real editor
  saving to NANO.TXT (better than EFI's stub), copy/del/
  ren/type/cat/touch/rm/write/append/source = real on
  cart files, 512 bytes max. Terminal cmd buffer raised
  32 -> 64 chars, PRESS B moved to y=152 so it no longer
  overwrites output at y=40, HELP now lists all commands.
  LIMIT: dummy mGBA run still not possible in this VM
  (/tmp extract cleaned), real command test needs Derek
  in Terminal, and long commands are still limited by
  the on-screen keyboard (64 chars, 28 visible).
- ROM: fisixgba.gba, 262,144 bytes (256 KB), SHA-256
  19acfe003192d26e57ee7c03161d1c92d8f9ed7c60653b07710e1ea83b3dcace
  Header complement 0xEB. Build succeeded, no apps.c
  warnings, ROM contains SWITCHLANG, USERADD, NANO,
  APPEND, HOSTNAME (lowercase), GREP strings.

## v1.3 — GBA/PC DOS division, 2026-10-29
GBA dosemu is SIMPLE .COM ONLY, by design, and will
NOT be expanded to any .com MS-DOS 4.0 can run.
Full .com is PC-only, via real-mode MS-DOS 4.0 boot
on x86 BIOS PCs (see ~/workspace/fisixos/OSC/docs/
PC-FULL-DOS-COM.md and ~/workspace/fisixpresario/).
Terminal now says on unsupported .com:
"COM UNSUPPORTED ON GBA. GBA DOS IS SIMPLE ONLY.
FULL DOS IS PC ONLY."
ROM 256 KB, SHA abe5bbacb15cf0705c2e72a6a1a40f929fc05e12f5c3e5395d098d5f3461eef6, header 0xEB.
No other GBA commands changed in v1.3.

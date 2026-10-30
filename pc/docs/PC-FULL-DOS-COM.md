# PC-only full .com support — any .com MS-DOS 4.0 can run

Division, 2026-10-29, at Derek's direction:
- GBA: simple .com only. ARM, 256 KB RAM, no VGA,
  no Sound Blaster, no BIOS, no FAT disk. GBA dosemu
  will NOT be expanded to full DOS.
- C64 / Apple IIe: same limit, by hardware. 6502,
  no x86 CPU, no VGA, no BIOS. Simple .com only, if
  at all, via an emulator. Full DOS is NOT for them.
- PC (x86): full .com support. PC hardware is x86,
  with VGA, BIOS, FAT disk, and enough RAM, so a
  .com file that plain MS-DOS 4.0 can run can run
  on a PC, natively, without an emulator.

## How full support works on PC — NOT via dosemu alone

PC dosemu.c (v1, in kernel/drivers/) is simple only,
same as GBA. It is NOT the full path, and expanding
it to full DOS inside a 64-bit UEFI kernel would not
work: UEFI long mode cannot run 16-bit real-mode
DOS code natively, and a DOS kernel needs BIOS,
FAT, VGA, and real-mode memory, which FisixOS UEFI
does not provide to DOS programs.

Full support on PC is a separate boot mode:
1. Boot a BIOS x86 PC (not UEFI-only, not GBA/C64)
   in real mode, from a boot sector / MBR.
2. Load msbio.sys + msdos.sys + command.com from
   the trimmed MS-DOS 4.0 copy in
   ~/workspace/fisixpresario/msdos-4.0/src/ (MIT,
   BOOT/, BIOS/, DOS/ with EXEC.ASM, INC/,
   CMD/COMMAND/ only — the part that runs .com files).
3. COMMAND.COM loads a .com file via EXEC, sets up
   its PSP at 0x100, and runs it natively on the
   x86 CPU. INT 21h, INT 10h, INT 16h, INT 13h, etc.,
   are real DOS/BIOS services, not reimplementations.
4. When the .com exits via INT 20h / INT 21h AH=4C,
   control returns to COMMAND.COM.

That is why "any .com MS-DOS 4.0 can run" is true
on PC and false on GBA: on PC, it is literally
MS-DOS running, on GBA it would have to be a full
PC emulator (CPU + BIOS + VGA + disk + sound),
which GBA hardware cannot do at usable speed.

## What still limits even PC full support

- Hardware the .com needs: a .com that needs VGA
  graphics needs VGA, a .com that needs Sound
  Blaster needs Sound Blaster, a .com that needs
  a mouse needs a mouse, etc. A modern UEFI-only
  PC without legacy BIOS boot, without VGA, or
  without those cards will not run every .com,
  even though it is x86. The Presario 5528
  (Pentium 75, BIOS, VGA, IDE, PS/2) is the
  reference BIOS PC for this, see
  ~/workspace/fisixpresario/docs/.
- .exe files are NOT .com files. A .exe that needs
  a DOS extender, protected mode, or Windows will
  not run under plain MS-DOS 4.0 either, so it is
  out of scope for "any .com".
- This DOS boot mode is NOT built yet in this
  repo. No msdos.sys / msbio.sys / command.com
  binaries were built here (building MS-DOS 4.0
  needs MASM + nmake on DOS/Windows), and no
  BIOS boot loader for it was written here.
  The trimmed source, the division, and this plan
  are done. The boot integration is the next PC
  step, after C64, not a claim that it boots now.

## GBA / C64 / Apple IIe — what they keep

- GBA: dosemu v1, simple text .com only, Terminal
  DOS / RUN .com, HELLO.COM demo. Unsupported
  .com files now say "GBA DOS IS SIMPLE ONLY.
  FULL DOS IS PC ONLY." in Terminal.
- C64 / Apple IIe: no DOS .com support planned
  as a native feature, because they are 6502, not
  x86. If a .com is ever run there, it would be
  via a simple emulator, same limit as GBA, not
  full DOS.

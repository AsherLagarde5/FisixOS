# Porting GNU Nano to FisixOS - honest status

## What is done in this change

- `apps/nano.c` -> `nano.fse`, a Fisix-native Nano-like editor.
  Build: `tools/fisix-gcc apps/nano.c -o nano.fse`
  Run in FisixOS, after login, in the loader terminal:
  `run nano.fse`, type a filename, edit lines,
  Ctrl-X = save and exit, Ctrl-C = exit without saving.
  It uses the extended .fse API (read_key, file_read, file_write
  in `boot/fse.h`), so it works on a writable FisixOS disk,
  and correctly fails to save on liveboot / read-only media.

## What is NOT the full GNU Nano yet

Real GNU Nano (nano 8.x) needs, in order:

1. A POSIX libc: stdio, termios, signals, malloc, etc.
2. ncurses: terminal capability database, screen drawing,
   keyboard escape-sequence parsing.
3. A terminal driver in FisixOS: a real console with
   cursor movement, colours, and raw key input, not just
   UEFI ConOut text output.
4. A kernel filesystem + process model, so Nano can run
   as a normal process (see PORTING-GCC-RUST.md, stage 2).

FisixOS today has (almost) none of those in the kernel:
its "nano" before this change was a loader-terminal stub
that printed "nano-like editor entered" and did not load
or save any file. The `nano.fse` above replaces that stub
with a real, if simple, editor you can use today.

## Real GNU Nano port, in order

1. Finish PORTING-GCC-RUST.md stage 2 (syscalls, libc, processes).
2. Port ncurses to the Fisix terminal, or write a minimal
   Fisix curses that maps Nano's calls to Fisix ConOut/GOP.
3. Configure GNU Nano with `--without-ncurses` / the Fisix
   curses, cross-compile it as a .fse/process, and test
   Ctrl-O / Ctrl-X / search / syntax highlighting one
   feature at a time. Do not expect the full Nano UI to
   work on the first try: its screen redraw assumes a
   Unix terminal, which FisixOS does not yet emulate.

Until then, use `nano.fse` for editing on FisixOS, and
use the host's real Nano to edit FisixOS source on your PC.

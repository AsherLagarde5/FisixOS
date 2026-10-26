# Porting GCC and Rust/Cargo to FisixOS - honest status

## What is done in this change (stage 1, cross-target)

GCC and Rust running on your PC, producing FisixOS programs:

- `.fse` format: see `boot/fse.h`. A 64-byte header + x86-64 code,
  loaded at 0x300000, entry `fse_main(fisix_api_t *api)`.
- GCC: `tools/fisix-gcc apps/hello.c -o hello.fse`
  This uses the host `x86_64-linux-gnu-gcc` as a freestanding
  cross-compiler, with `tools/fse.ld` and `tools/make_fse.py`.
  It is a GCC *target* port, not GCC *running on* FisixOS.
- Rust: `tools/x86_64-unknown-fisix.json` is a custom Rust
  target (no_std, panic=abort, x86_64-unknown-none LLVM target),
  with a starter example in `tools/rust-fse-example.rs`.
  Build it with rustc on the host, link with `fse.ld`,
  then `make_fse.py` to get a `.fse`. A full `cargo` project
  with a FisixOS std library is stage 2, below.

Both are tested the same way in this change: copy the `.fse`
to the FisixOS disk/ESP, boot FisixOS, log in, then in the
loader terminal: `run hello.fse` (or just `hello.fse`).

## What is NOT done, and why it is not a one-file port

GCC, Cargo, and Rust's std, running *on* FisixOS, need all of:

1. A kernel syscall ABI (e.g. int 0x80 / syscall instruction,
   numbers for open/read/write/close/exit/mmap/fork/exec, etc).
   FisixOS today has no userspace processes and no syscalls:
   the kernel is entered once from the loader and never returns
   to a shell, and the loader terminal runs before ExitBootServices.
2. A libc for FisixOS (newlib, musl, or a Fisix libc) implementing
   open/read/write, malloc (sbrk/mmap), signals, etc, on top of (1).
3. A process loader in the kernel that can load an ELF/.fse,
   set up a stack and address space, and switch to user mode.
4. A filesystem driver in the kernel (today storage is
   detection-only for NVMe/xHCI, and file access lives in the
   UEFI loader, which is gone after ExitBootServices).
5. For Rust std / Cargo: a Rust std port on top of (1)-(2),
   plus a Cargo target that can run the compiler's build
   scripts on FisixOS, which in turn needs (1)-(4).

Claiming "GCC is ported" without (1)-(5) would be false.
Stage 1 above is the real, testable step that unblocks
writing FisixOS programs in C and Rust today.

## Stage 2 roadmap, in order

1. Kernel syscall table + user-mode switch + ELF/.fse process loader.
2. Minimal Fisix libc: write, read, open, close, exit, sbrk.
3. Port newlib/musl to that libc, or grow the Fisix libc.
4. Build a GCC cross toolchain with `--target=x86_64-fisix`
   (binutils + GCC + that libc), then build GCC itself
   *for* FisixOS and run it as a .fse/process on FisixOS.
5. Rust: `x86_64-unknown-fisix` std port, then Cargo
   (which needs a working Rust std + filesystem + processes).

Do not start stage 2 by trying to compile GCC's source
inside FisixOS today: it will fail at the first `open()`,
because there is no `open()` yet.

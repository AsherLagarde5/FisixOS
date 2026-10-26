#!/usr/bin/env python3
"""Build a .fse (FiSix Executable) from an ELF file.

Usage: python3 make_fse.py input.elf output.fse

The ELF must be linked with tools/fse.ld (base 0x300000, entry
fse_main). This tool extracts the flat binary with objcopy,
finds fse_main with nm, and writes the 64-byte FSE1 header
(see boot/fse.h) followed by the binary.

Entry offset in the file = 64 + (fse_main_addr - 0x300040).
The loader loads the binary at 0x300000, so the entry address
becomes 0x300000 + (entry_offset - 64) = fse_main_addr. Correct.
"""
import struct, subprocess, sys, os

LOAD = 0x300000
LINK_BASE = 0x300000
HEADER_SIZE = 64

def main():
    if len(sys.argv) != 3:
        print("usage: make_fse.py input.elf output.fse", file=sys.stderr)
        return 2
    elf, out = sys.argv[1], sys.argv[2]
    # flat binary
    tmp = out + ".bin"
    subprocess.check_call(["objcopy", "-O", "binary", elf, tmp])
    # symbol
    nm = subprocess.check_output(["nm", elf], text=True)
    entry = None
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[2] == "fse_main":
            entry = int(parts[0], 16)
            break
    if entry is None:
        print("make_fse: no fse_main symbol in ELF", file=sys.stderr)
        return 1
    with open(tmp, "rb") as f:
        code = f.read()
    os.remove(tmp)
    entry_offset = HEADER_SIZE + (entry - LINK_BASE)
    if entry_offset < HEADER_SIZE or entry_offset >= HEADER_SIZE + len(code):
        print(f"make_fse: entry {entry:#x} out of range, link with tools/fse.ld", file=sys.stderr)
        return 1
    hdr = struct.pack("<IIIII", 0x31455346, 1, entry_offset, len(code), 2)
    hdr += b"\x00" * (HEADER_SIZE - len(hdr))
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(code)
    print(f"fse: {out} entry={entry:#x} code={len(code)} bytes")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

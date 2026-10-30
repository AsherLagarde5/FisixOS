#!/usr/bin/env python3
"""Build a .gep (Game Boy Executable Program) from an ELF file.

Usage: python3 make_gep.py input.elf output.gep

The ELF must be linked with tools/gep.ld (base 0x02008000, entry
gep_main). This tool extracts the flat binary with objcopy,
finds gep_main with nm, computes .bss size from __bss_start /
__bss_end, and writes the 64-byte GEP1 header (see src/gep.h)
followed by the binary.

Entry offset in the file = 64 + (gep_main_addr - 0x02008000).
The loader loads the binary at 0x02008000, so the entry address
becomes 0x02008000 + (entry_offset - 64) = gep_main_addr. Correct.
"""
import struct, subprocess, sys, os

LOAD = 0x02008000
HEADER_SIZE = 64

def main():
    if len(sys.argv) != 3:
        print("usage: make_gep.py input.elf output.gep", file=sys.stderr)
        return 2
    elf, out = sys.argv[1], sys.argv[2]
    objcopy = os.environ.get("GEP_OBJCOPY", "arm-none-eabi-objcopy")
    nm_tool = os.environ.get("GEP_NM", "arm-none-eabi-nm")
    tmp = out + ".bin"
    subprocess.check_call([objcopy, "-O", "binary", elf, tmp])
    nm = subprocess.check_output([nm_tool, elf], text=True)
    entry = None
    bss_start = None
    bss_end = None
    for line in nm.splitlines():
        parts = line.split()
        if len(parts) >= 3:
            if parts[2] == "gep_main":
                entry = int(parts[0], 16)
            elif parts[2] == "__bss_start":
                bss_start = int(parts[0], 16)
            elif parts[2] == "__bss_end":
                bss_end = int(parts[0], 16)
    if entry is None:
        print("make_gep: no gep_main symbol in ELF", file=sys.stderr)
        return 1
    bss_size = 0
    if bss_start is not None and bss_end is not None and bss_end > bss_start:
        bss_size = bss_end - bss_start
    with open(tmp, "rb") as f:
        code = f.read()
    os.remove(tmp)
    entry_offset = HEADER_SIZE + (entry - LOAD)
    if entry_offset < HEADER_SIZE or entry_offset >= HEADER_SIZE + len(code):
        print(f"make_gep: entry {entry:#x} out of range, link with tools/gep.ld", file=sys.stderr)
        return 1
    if len(code) > 64 * 1024:
        print("make_gep: code too big for GEP (64 KB max)", file=sys.stderr)
        return 1
    hdr = struct.pack("<IIIII", 0x31504547, 1, entry_offset, len(code), 2)
    hdr += struct.pack("<I", bss_size)
    hdr += b"\x00" * (HEADER_SIZE - len(hdr))
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(code)
    print(f"gep: {out} entry={entry:#x} code={len(code)} bytes bss={bss_size} bytes")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

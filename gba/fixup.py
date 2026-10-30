#!/usr/bin/env python3
"""Patch GBA header complement check and pad ROM to 256 KB.
Complement: chk = -(0x19 + sum(bytes 0xA0..0xBC)) & 0xFF, stored at 0xBD.
Usage: fixup.py in.bin out.gba
Logo area (0x04..0x9F) is left zeroed: no Nintendo bitmap bundled.
mGBA without BIOS boots from entry; real hardware needs the
official header tool for that area."""
import sys
src, dst = sys.argv[1], sys.argv[2]
data = bytearray(open(src, 'rb').read())
assert len(data) >= 0xC0, "ROM too small for header"
chk = 0x19
for i in range(0xA0, 0xBD):
    chk += data[i]
chk = (-chk) & 0xFF
data[0xBD] = chk
# Pad to 256 KB minimum, a standard small GBA ROM size.
while len(data) < 256 * 1024:
    data.append(0xFF)
assert len(data) <= 32 * 1024 * 1024, "ROM exceeds 32 MB GBA limit"
open(dst, 'wb').write(data)
print(f"header checksum 0x{chk:02X}, size {len(data)} bytes ({len(data)/1024:.0f} KB)")

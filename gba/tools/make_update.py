#!/usr/bin/env python3
"""Build a User Update File (.update) from entry files.

Usage:
  python3 make_update.py <platform> <output.update> \
    <action>:<name>:<file> ...

platform: GBA, UEFI, C64, ARM64 (numbers 1,2,3,4)
action: ADD, REPLACE, DELETE (DELETE ignores <file>)
name: system file name, e.g. DEMO2.PATCH (max 31 chars)
file: path to content, for ADD/REPLACE (max 512 bytes on GBA)

Header is 64 bytes: magic UPD1, version 1, platform,
entry_count, payload_crc16, reserved[11]=0.
Entries follow, each: name[32], action u32, data_len u32,
data[data_len]. CRC16 is poly 0x1021, init 0xFFFF, over
all entry bytes after the header, same as FisixLink CRC.
"""
import struct, sys

PLATFORMS = {"GBA": 1, "UEFI": 2, "C64": 3, "ARM64": 4}
ACTIONS = {"ADD": 0, "REPLACE": 1, "DELETE": 2}

def crc16(data):
    crc = 0xFFFF
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def main():
    if len(sys.argv) < 4:
        print("usage: make_update.py <platform> <output.update> <action>:<name>:<file> ...", file=sys.stderr)
        return 2
    platform_name = sys.argv[1].upper()
    if platform_name not in PLATFORMS:
        print(f"make_update: unknown platform {platform_name}", file=sys.stderr)
        return 1
    out = sys.argv[2]
    entries = b""
    count = 0
    for spec in sys.argv[3:]:
        parts = spec.split(":", 2)
        if len(parts) < 2:
            print(f"make_update: bad entry {spec}", file=sys.stderr)
            return 1
        action_name, name = parts[0].upper(), parts[1].upper()
        if action_name not in ACTIONS:
            print(f"make_update: bad action {action_name}", file=sys.stderr)
            return 1
        data = b""
        if action_name != "DELETE":
            if len(parts) < 3:
                print(f"make_update: {spec} needs a file", file=sys.stderr)
                return 1
            with open(parts[2], "rb") as f:
                data = f.read()
            if len(data) > 512:
                print(f"make_update: {name} too big for GBA cart entry (512 max)", file=sys.stderr)
                return 1
        name_b = name.encode()[:31]
        name_b = name_b + b"\x00" * (32 - len(name_b))
        entries += name_b + struct.pack("<II", ACTIONS[action_name], len(data)) + data
        count += 1
    hdr = struct.pack("<IIIII", 0x31445055, 1, PLATFORMS[platform_name], count, crc16(entries))
    hdr += b"\x00" * (64 - len(hdr))
    with open(out, "wb") as f:
        f.write(hdr)
        f.write(entries)
    print(f"update: {out} platform={platform_name} entries={count} bytes={64+len(entries)} crc={crc16(entries):04x}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())

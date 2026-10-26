#!/usr/bin/env python3
"""FisixFF (Fisix File Freeze) - a SquashFS alternative, v1, stored only.
Format, little-endian:
  header: magic "FFS1" (4), u32 file_count, u32 reserved=0
  for each file: u16 name_len, name bytes (UTF-8, no NUL, e.g. SYSTEM/APPS/TERMINAL.FSE),
                 u32 offset (from start of file), u32 size
  then file data, in entry order, uncompressed in v1.
System folder layout inside a .ff (see tools/FISIXFF.md):
  SYSTEM/BOOT/BOOTX64.EFI, SYSTEM/BOOT/KERNEL.BIN, SYSTEM/BOOT/STARTUP.NSH
  SYSTEM/APPS/HELLO.FSE, SYSTEM/APPS/NANO.FSE
  SYSTEM/DRIVERS/ (driver .bin / .txt notes)
  SYSTEM/USER/ (empty by default, user files go here, not in the frozen image)
Usage: python3 make_ff.py <dir> <out.ff>
       python3 make_ff.py --list <file.ff>
"""
import os, struct, sys
MAGIC=b"FFS1"
def pack(src, out):
    files=[]
    for root, dirs, names in os.walk(src):
        dirs.sort(); names.sort()
        for n in names:
            full=os.path.join(root,n)
            rel=os.path.relpath(full, src).replace(os.sep, "/")
            files.append((rel, full))
    header_size=12
    entries_size=sum(2+len(r.encode())+8 for r,_ in files)
    offset=header_size+entries_size
    data=b""; entries=[]
    for rel, full in files:
        with open(full,"rb") as f: d=f.read()
        entries.append((rel, offset, len(d))); data+=d; offset+=len(d)
    with open(out,"wb") as f:
        f.write(MAGIC); f.write(struct.pack("<II", len(files), 0))
        for rel, off, sz in entries:
            nb=rel.encode(); f.write(struct.pack("<H", len(nb))); f.write(nb); f.write(struct.pack("<II", off, sz))
        f.write(data)
    print(f"ff: {out} {len(files)} files")
    return 0
def list_ff(path):
    with open(path,"rb") as f: d=f.read()
    assert d[:4]==MAGIC, "not a FisixFF file"
    count, _ = struct.unpack("<II", d[4:12]); pos=12; print(f"{count} files")
    for _ in range(count):
        (nl,) = struct.unpack("<H", d[pos:pos+2]); pos+=2; name=d[pos:pos+nl].decode(); pos+=nl; off, sz = struct.unpack("<II", d[pos:pos+8]); pos+=8; print(f"{name} offset={off} size={sz}")
    return 0
if __name__=="__main__":
    if len(sys.argv)==3 and sys.argv[1]=="--list": raise SystemExit(list_ff(sys.argv[2]))
    if len(sys.argv)!=3: print("usage: make_ff.py <dir> <out.ff>  OR  make_ff.py --list <file.ff>", file=sys.stderr); raise SystemExit(2)
    raise SystemExit(pack(sys.argv[1], sys.argv[2]))

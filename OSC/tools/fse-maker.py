#!/usr/bin/env python3
"""FSE maker - build, list, and check .fse files.
This wraps the existing tools/make_fse.py + tools/fisix-gcc, and adds:
  fse-maker.py build <src.c> <out.fse>   (same as make_fse, with checks)
  fse-maker.py info <file.fse>          (parse 64-byte header, print entry/code_size/api)
  fse-maker.py new <name>               (write a hello .fse C template)
Header: 64 bytes, magic "FSE1", version 1, entry_offset, code_size, api_version <= 2.
A .fse is not a .com, not an ELF, not a PE - it is a flat FisixOS program
loaded at 0x300000 and called as int fse_main(fisix_api_t *api).
"""
import os, struct, subprocess, sys
HERE=os.path.dirname(os.path.abspath(__file__))
def info(path):
    with open(path,"rb") as f: d=f.read()
    if len(d)<64 or d[:4]!=b"FSE1": print("not a .fse file"); return 1
    ver, entry, code, api = struct.unpack("<IIII", d[4:20])
    print(f"magic=FSE1 version={ver} entry_offset={entry} code_size={code} api_version={api} file_size={len(d)}")
    return 0 if api<=2 else 1
def build(src, out):
    # .c -> fisix-gcc (which compiles and calls make_fse.py)
    # .elf -> make_fse.py directly
    if src.endswith(".c"):
        r=subprocess.run([os.path.join(HERE,"fisix-gcc"), src, "-o", out])
    else:
        r=subprocess.run([sys.executable, os.path.join(HERE,"make_fse.py"), src, out])
    if r.returncode!=0: return r.returncode
    return info(out)
def new(name):
    src=f"""#include "fse.h"
int fse_main(fisix_api_t *api) {{
    api->print("Hello from {name}!\\n");
    api->print_u64(42 + 1);
    api->print_newline();
    return 0;
}}
"""
    with open(name,"w") as f: f.write(src)
    print(f"wrote {name}"); return 0
if __name__=="__main__":
    if len(sys.argv)==3 and sys.argv[1]=="info": raise SystemExit(info(sys.argv[2]))
    if len(sys.argv)==4 and sys.argv[1]=="build": raise SystemExit(build(sys.argv[2], sys.argv[3]))
    if len(sys.argv)==3 and sys.argv[1]=="new": raise SystemExit(new(sys.argv[2]))
    print("usage: fse-maker.py build <src.c> <out.fse> | info <file.fse> | new <file.c>", file=sys.stderr); raise SystemExit(2)

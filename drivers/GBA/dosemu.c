/* MS-DOS .com support, ported from PC FisixOS dosemu.c.
   MS-DOS itself was open-sourced by Microsoft (MS-DOS 4.0
   on GitHub), but this is NOT MS-DOS: it is a minimal
   8086 emulator for simple .com programs, same scope as
   the PC version, ported to ARM7TDMI.
 *
 * .com files are flat: loaded at offset 0x100 in a 64 KiB
 * segment, PSP at 0x0000, CS=DS=ES=SS=0, SP=0xFFFE, IP=0x100.
 * Supported in v1, same as PC: MOV reg,imm / reg,reg,
 * ADD/SUB/CMP reg,imm and reg,reg, INC/DEC, JMP/JZ/JNZ,
 * CALL/RET, INT 20h (exit), INT 21h AH=02 (print DL),
 * AH=09 (print $-terminated string at DS:DX), AH=4C
 * (exit with AL). Anything else stops with
 * DOS_EMU_UNSUPPORTED.
 * GBA LIMIT, BY DESIGN, 2026-10-29: this is NOT a full DOS,
 * and it will NOT be expanded to any .com that MS-DOS 4.0
 * can run. GBA is ARM, not x86, with 256 KB RAM, no VGA,
 * no Sound Blaster, no BIOS, no FAT disk. Full .com
 * support is PC-only (x86 BIOS, real MS-DOS 4.0 boot,
 * see ~/workspace/fisixpresario/msdos-4.0/ and PC docs).
 * This GBA version runs simple .com programs only: no
 * file I/O, no DOS 4.0 kernel, no real-mode switching.
 * It runs simple .com programs that print text and exit,
 * which covers most hello-world / demo .com files.
 *
 * GBA memory layout: the emulator's 64 KiB segment lives
 * at a fixed address, 0x02020000, NOT in .bss. OS .bss ends
 * around 0x02002500, GEP code loads at 0x02008000 with up
 * to 64 KB code + 16 KB .bss (ending 0x02018000), so
 * 0x02020000 does not collide with either. A .com program
 * and a GEP program are not run at the same time.
 */
#include "gba.h"
#include "hello_com.h"

#define DOS_EMU_OK 0
#define DOS_EMU_EXIT 1
#define DOS_EMU_UNSUPPORTED 2
#define DOS_EMU_TRUNCATED 3

static u8 *dos_mem = (u8*)0x02020000u;
static u16 dos_reg[8]; /* AX CX DX BX SP BP SI DI, index by reg code */
static u16 dos_ip;
static int dos_flag_z, dos_flag_c;
static char *dos_out_buf;
static unsigned int dos_out_len, dos_out_cap;

static void dos_out_char(char c) {
  if (dos_out_len + 1 < dos_out_cap) {
    dos_out_buf[dos_out_len++] = c;
    dos_out_buf[dos_out_len] = 0;
  }
}
static u8 dos_fetch8(void) { return dos_mem[dos_ip++]; }
static u16 dos_fetch16(void) {
  u16 v = (u16)(dos_mem[dos_ip] | (dos_mem[dos_ip + 1] << 8));
  dos_ip += 2;
  return v;
}
static u16 *dos_reg16(int r) { return &dos_reg[r]; }

int dosemu_run(const u8 *com, unsigned int com_len, char *out, unsigned int out_size, int *exit_code) {
  unsigned int i;
  int steps = 0;
  if (!com || com_len > 0xFE00 || !out || out_size == 0) return DOS_EMU_TRUNCATED;
  for (i = 0; i < 65536; i++) dos_mem[i] = 0;
  dos_mem[0] = 0xCD; dos_mem[1] = 0x20; /* PSP: INT 20h at 0 */
  for (i = 0; i < com_len; i++) dos_mem[0x100 + i] = com[i];
  dos_reg[0] = 0; dos_reg[1] = 0; dos_reg[2] = 0; dos_reg[3] = 0;
  dos_reg[4] = 0xFFFE; dos_reg[5] = 0; dos_reg[6] = 0; dos_reg[7] = 0;
  dos_ip = 0x100; dos_flag_z = 0; dos_flag_c = 0;
  dos_out_buf = out; dos_out_len = 0; dos_out_cap = out_size; out[0] = 0;
  if (exit_code) *exit_code = 0;
  while (steps++ < 1000000) {
    u8 op = dos_fetch8();
    if (op >= 0xB0 && op <= 0xB7) {
      int r = op - 0xB0; u8 v = dos_fetch8();
      if (r < 4) dos_reg[r] = (u16)((dos_reg[r] & 0xFF00) | v);
      else dos_reg[r - 4] = (u16)((dos_reg[r - 4] & 0x00FF) | (v << 8));
    } else if (op >= 0xB8 && op <= 0xBF) {
      *dos_reg16(op - 0xB8) = dos_fetch16();
    } else if (op == 0xCD) {
      u8 n = dos_fetch8();
      if (n == 0x20) { if (exit_code) *exit_code = 0; return DOS_EMU_EXIT; }
      if (n == 0x21) {
        u8 ah = (u8)(dos_reg[0] >> 8);
        if (ah == 0x02) { dos_out_char((char)(dos_reg[2] & 0xFF)); }
        else if (ah == 0x09) {
          u16 a = dos_reg[2];
          while (dos_mem[a] != '$' && a < 65535) { dos_out_char((char)dos_mem[a]); a++; }
        }
        else if (ah == 0x4C) { if (exit_code) *exit_code = (int)(dos_reg[0] & 0xFF); return DOS_EMU_EXIT; }
        else return DOS_EMU_UNSUPPORTED;
      } else return DOS_EMU_UNSUPPORTED;
    } else if (op == 0xEB) { int d = (int)(signed char)dos_fetch8(); dos_ip = (u16)(dos_ip + d); }
    else if (op == 0x74) { int d = (int)(signed char)dos_fetch8(); if (dos_flag_z) dos_ip = (u16)(dos_ip + d); }
    else if (op == 0x75) { int d = (int)(signed char)dos_fetch8(); if (!dos_flag_z) dos_ip = (u16)(dos_ip + d); }
    else if (op == 0xE8) {
      int d = (int)(short)dos_fetch16();
      dos_mem[--dos_reg[4]] = (u8)(dos_ip >> 8); dos_mem[--dos_reg[4]] = (u8)(dos_ip & 0xFF);
      dos_ip = (u16)(dos_ip + d);
    }
    else if (op == 0xC3) {
      u16 lo = dos_mem[dos_reg[4]++]; u16 hi = dos_mem[dos_reg[4]++];
      dos_ip = (u16)(lo | (hi << 8));
    }
    else if (op >= 0x40 && op <= 0x47) { (*dos_reg16(op - 0x40))++; }
    else if (op >= 0x48 && op <= 0x4F) { (*dos_reg16(op - 0x48))--; }
    else if (op == 0x89 || op == 0x8B || op == 0x01 || op == 0x03 || op == 0x29 || op == 0x2B || op == 0x39 || op == 0x3B) {
      u8 modrm = dos_fetch8(); int mod = modrm >> 6; int r = (modrm >> 3) & 7; int rm = modrm & 7;
      if (mod != 3) return DOS_EMU_UNSUPPORTED;
      if (op == 0x89) *dos_reg16(rm) = *dos_reg16(r);
      else if (op == 0x8B) *dos_reg16(r) = *dos_reg16(rm);
      else if (op == 0x01) *dos_reg16(rm) = (u16)(*dos_reg16(rm) + *dos_reg16(r));
      else if (op == 0x03) *dos_reg16(r) = (u16)(*dos_reg16(r) + *dos_reg16(rm));
      else if (op == 0x29) *dos_reg16(rm) = (u16)(*dos_reg16(rm) - *dos_reg16(r));
      else if (op == 0x2B) *dos_reg16(r) = (u16)(*dos_reg16(r) - *dos_reg16(rm));
      else if (op == 0x39 || op == 0x3B) {
        u16 a = op == 0x39 ? *dos_reg16(rm) : *dos_reg16(r);
        u16 b = op == 0x39 ? *dos_reg16(r) : *dos_reg16(rm);
        dos_flag_z = (a == b); dos_flag_c = (a < b);
      }
    } else if (op == 0x83 || op == 0x81) {
      u8 modrm = dos_fetch8(); int mod = modrm >> 6; int ext = (modrm >> 3) & 7; int rm = modrm & 7;
      u16 imm = op == 0x83 ? (u16)(signed char)dos_fetch8() : dos_fetch16();
      if (mod != 3) return DOS_EMU_UNSUPPORTED;
      if (ext == 0) *dos_reg16(rm) = (u16)(*dos_reg16(rm) + imm);
      else if (ext == 5) *dos_reg16(rm) = (u16)(*dos_reg16(rm) - imm);
      else if (ext == 7) { dos_flag_z = (*dos_reg16(rm) == imm); dos_flag_c = (*dos_reg16(rm) < imm); }
      else return DOS_EMU_UNSUPPORTED;
    } else if (op == 0x3C) {
      u8 imm = dos_fetch8(); u8 al = (u8)dos_reg[0];
      dos_flag_z = (al == imm); dos_flag_c = (al < imm);
    } else if (op == 0x05 || op == 0x2D || op == 0x3D) {
      u16 imm = dos_fetch16();
      if (op == 0x05) dos_reg[0] = (u16)(dos_reg[0] + imm);
      else if (op == 0x2D) dos_reg[0] = (u16)(dos_reg[0] - imm);
      else { dos_flag_z = (dos_reg[0] == imm); dos_flag_c = (dos_reg[0] < imm); }
    } else if (op == 0x90) {
      /* NOP */
    } else return DOS_EMU_UNSUPPORTED;
  }
  return DOS_EMU_UNSUPPORTED;
}

static char dos_last_out[256];
const char *dosemu_last_output(void) { return dos_last_out; }

/* Embedded .com demo, plus cart lookup. */
static const struct { const char *name; const unsigned char *data; unsigned int len; } dos_embedded[] = {
  { "HELLO.COM", hello_com_data, hello_com_len },
};
int dosemu_list_embedded(char names[][32], int max) {
  int i, n = 0;
  for (i = 0; i < (int)(sizeof(dos_embedded) / sizeof(dos_embedded[0])) && n < max; i++) {
    int j;
    for (j = 0; dos_embedded[i].name[j] && j < 31; j++) names[n][j] = dos_embedded[i].name[j];
    names[n][j] = 0;
    n++;
  }
  return n;
}
static int dos_name_eq(const char *a, const char *b) {
  int i;
  for (i = 0; a[i] || b[i]; i++) {
    char ca = a[i], cb = b[i];
    if (ca >= 'a' && ca <= 'z') ca = (char)(ca - 'a' + 'A');
    if (cb >= 'a' && cb <= 'z') cb = (char)(cb - 'a' + 'A');
    if (ca != cb) return 0;
  }
  return 1;
}
/* Returns dosemu_run result, or -1 if not found. Output is
   in out (256 bytes, caller supplied in Terminal). */
int dosemu_run_file(const char *name) {
  static u8 buf[512];
  int n, i, ec = 0, rc;
  if (!name) return -1;
  dos_last_out[0] = 0;
  n = fs_read(name, buf, sizeof(buf));
  if (n >= 0) {
    rc = dosemu_run(buf, (unsigned int)n, dos_last_out, sizeof(dos_last_out), &ec);
    return rc;
  }
  for (i = 0; i < (int)(sizeof(dos_embedded) / sizeof(dos_embedded[0])); i++)
    if (dos_name_eq(name, dos_embedded[i].name)) {
      rc = dosemu_run(dos_embedded[i].data, dos_embedded[i].len, dos_last_out, sizeof(dos_last_out), &ec);
      return rc;
    }
  return -1;
}

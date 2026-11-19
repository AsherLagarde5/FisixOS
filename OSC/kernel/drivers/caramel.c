#include "caramel.h"
/* Caramel — absolutely evil DOS emulation. See caramel.h. */
static uint8_t mem[65536];
static uint16_t reg[8]; /* AX CX DX BX SP BP SI DI, index by reg code */
static uint16_t ip;
static int flag_z, flag_c;
static char *out_buf; static size_t out_len, out_cap;
static void out_char(char c){ if (out_len+1 < out_cap) { out_buf[out_len++]=c; out_buf[out_len]=0; } }
static void out_str(const char *s){ while (*s) out_char(*s++); }
/* Evil: laugh first, then stop. MWAHAHA, like `make death`. */
static int evil_unsupported(void){ out_str("MWAHAHA! Caramel says no."); return CARAMEL_UNSUPPORTED; }
static uint8_t fetch8(void){ return mem[ip++]; }
static uint16_t fetch16(void){ /* 16-bit wrap: at ip==0xFFFF the high byte
    is mem[0], not one past the 64 KiB array (mem[0x10000]). */
    uint16_t v = mem[ip] | (mem[(uint16_t)(ip + 1)] << 8); ip += 2; return v; }
static uint16_t *reg16(int r){ return &reg[r]; }
static uint16_t rd16(const uint8_t *p){ return (uint16_t)(p[0] | (p[1] << 8)); }
int caramel_run(const uint8_t *com, size_t com_len, char *out, size_t out_size, int *exit_code)
{
    size_t i; int steps=0;
    int is_mz = 0; uint16_t entry_ip = 0x100; uint16_t entry_sp = 0xFFFE;
    if (!com || com_len>0xFE00 || !out || out_size==0) return CARAMEL_TRUNCATED;
    /* Set up output first: evil_unsupported() laughs into it,
     * including for a bad MZ header below, so it must exist
     * before any evil_unsupported() return. */
    out_buf=out; out_len=0; out_cap=out_size; out[0]=0;
    /* Tiny MZ .exe: "MZ" + a relocation-free header that fits. */
    if (com_len >= 64 && com[0] == 'M' && com[1] == 'Z') {
        size_t hdr_bytes = (size_t)rd16(com + 8) * 16;
        uint16_t relocs = rd16(com + 6);
        if (hdr_bytes > com_len || hdr_bytes < 32) return CARAMEL_TRUNCATED;
        if (relocs != 0) return evil_unsupported();
        if (com_len - hdr_bytes > 0xFE00) return CARAMEL_TRUNCATED;
        is_mz = 1;
        entry_ip = (uint16_t)(0x100 + rd16(com + 20)); /* e_ip */
        entry_sp = rd16(com + 16); /* e_sp, 0 means use 0xFFFE */
        if (entry_sp == 0) entry_sp = 0xFFFE;
        /* e_cs / e_ss are ignored: flat model, CS=SS=0. */
        for (i=0;i<65536;i++) mem[i]=0;
        mem[0]=0xCD; mem[1]=0x20; /* PSP: INT 20h at 0 */
        for (i=0;i<com_len-hdr_bytes;i++) mem[0x100+i]=com[hdr_bytes+i];
    } else {
        for (i=0;i<65536;i++) mem[i]=0;
        mem[0]=0xCD; mem[1]=0x20; /* PSP: INT 20h at 0 */
        for (i=0;i<com_len;i++) mem[0x100+i]=com[i];
    }
    (void)is_mz;
    reg[0]=0; reg[1]=0; reg[2]=0; reg[3]=0; reg[4]=entry_sp; reg[5]=0; reg[6]=0; reg[7]=0;
    ip=entry_ip; flag_z=0; flag_c=0; if (exit_code) *exit_code=0;
    while (steps++ < 1000000) {
        uint8_t op = fetch8();
        if (op>=0xB0 && op<=0xB7) { /* MOV r8, imm8 */
            int r = op-0xB0; uint8_t v = fetch8();
            if (r<4) reg[r]=(reg[r]&0xFF00)|v; else reg[r-4]=(reg[r-4]&0x00FF)|(v<<8);
        } else if (op>=0xB8 && op<=0xBF) { /* MOV r16, imm16 */
            *reg16(op-0xB8)=fetch16();
        } else if (op==0xCD) { /* INT imm8 */
            uint8_t n = fetch8();
            if (n==0x20) { if (exit_code) *exit_code=0; return CARAMEL_EXIT; }
            if (n==0x21) {
                uint8_t ah = (uint8_t)(reg[0]>>8);
                if (ah==0x00) { if (exit_code) *exit_code=0; return CARAMEL_EXIT; }
                else if (ah==0x02) { out_char((char)(reg[2]&0xFF)); }
                else if (ah==0x09) { uint16_t a=reg[2]; while (mem[a]!='$' && a<65535) { out_char((char)mem[a]); a++; } }
                else if (ah==0x0E) { reg[0]=(reg[0]&0xFF00)|2; /* AL = 2 logical drives */ }
                else if (ah==0x19) { reg[0]=(reg[0]&0xFF00)|0; /* AL = 0, drive A: */ }
                else if (ah==0x25) { /* set vector AL, handler DS:DX (DS=0) */
                    uint16_t vec = (uint8_t)reg[0]; uint16_t h = reg[2];
                    mem[vec*4]=(uint8_t)(h&0xFF); mem[vec*4+1]=(uint8_t)(h>>8);
                    mem[vec*4+2]=0; mem[vec*4+3]=0;
                }
                else if (ah==0x30) { reg[0]=(uint16_t)((CARAMEL_DOS_MINOR<<8)|CARAMEL_DOS_MAJOR); /* 7.10 */ }
                else if (ah==0x35) { /* get vector AL -> ES:BX, ES=0 */
                    uint16_t vec = (uint8_t)reg[0];
                    reg[3]=(uint16_t)(mem[vec*4] | (mem[vec*4+1]<<8));
                }
                else if (ah==0x4C) { if (exit_code) *exit_code=(int)(reg[0]&0xFF); return CARAMEL_EXIT; }
                else return evil_unsupported();
            } else return evil_unsupported();
        } else if (op==0xEB) { int8_t d=(int8_t)fetch8(); ip=(uint16_t)(ip+d); }
        else if (op==0x74) { int8_t d=(int8_t)fetch8(); if (flag_z) ip=(uint16_t)(ip+d); }
        else if (op==0x75) { int8_t d=(int8_t)fetch8(); if (!flag_z) ip=(uint16_t)(ip+d); }
        else if (op==0xE8) { int16_t d=(int16_t)fetch16(); mem[--reg[4]]=(uint8_t)(ip>>8); mem[--reg[4]]=(uint8_t)(ip&0xFF); ip=(uint16_t)(ip+d); }
        else if (op==0xC3) { uint16_t lo=mem[reg[4]++]; uint16_t hi=mem[reg[4]++]; ip=(uint16_t)(lo|(hi<<8)); }
        else if (op>=0x40 && op<=0x47) { (*reg16(op-0x40))++; }
        else if (op>=0x48 && op<=0x4F) { (*reg16(op-0x48))--; }
        else if (op==0x89 || op==0x8B || op==0x01 || op==0x03 || op==0x29 || op==0x2B || op==0x39 || op==0x3B) {
            uint8_t modrm = fetch8(); int mod = modrm>>6; int r = (modrm>>3)&7; int rm = modrm&7;
            if (mod!=3) return evil_unsupported();
            if (op==0x89) *reg16(rm)=*reg16(r);
            else if (op==0x8B) *reg16(r)=*reg16(rm);
            else if (op==0x01) *reg16(rm)=(uint16_t)(*reg16(rm)+*reg16(r));
            else if (op==0x03) *reg16(r)=(uint16_t)(*reg16(r)+*reg16(rm));
            else if (op==0x29) *reg16(rm)=(uint16_t)(*reg16(rm)-*reg16(r));
            else if (op==0x2B) *reg16(r)=(uint16_t)(*reg16(r)-*reg16(rm));
            else if (op==0x39 || op==0x3B) { uint16_t a = op==0x39 ? *reg16(rm) : *reg16(r); uint16_t b = op==0x39 ? *reg16(r) : *reg16(rm); flag_z = (a==b); flag_c = (a<b); }
        } else if (op==0x83 || op==0x81) {
            uint8_t modrm = fetch8(); int mod = modrm>>6; int ext = (modrm>>3)&7; int rm = modrm&7; uint16_t imm = op==0x83 ? (uint16_t)(int8_t)fetch8() : fetch16();
            if (mod!=3) return evil_unsupported();
            if (ext==0) *reg16(rm)=(uint16_t)(*reg16(rm)+imm); else if (ext==5) *reg16(rm)=(uint16_t)(*reg16(rm)-imm); else if (ext==7) { flag_z=(*reg16(rm)==imm); flag_c=(*reg16(rm)<imm); } else return evil_unsupported();
        } else if (op==0x3C) { uint8_t imm=fetch8(); uint8_t al=(uint8_t)reg[0]; flag_z=(al==imm); flag_c=(al<imm); }
        else if (op==0x05 || op==0x2D || op==0x3D) { uint16_t imm=fetch16(); if (op==0x05) reg[0]=(uint16_t)(reg[0]+imm); else if (op==0x2D) reg[0]=(uint16_t)(reg[0]-imm); else { flag_z=(reg[0]==imm); flag_c=(reg[0]<imm); } }
        else if (op==0x90) { /* NOP */ }
        else return evil_unsupported();
    }
    return evil_unsupported();
}

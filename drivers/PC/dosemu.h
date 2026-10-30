#ifndef FISIX_DOSEMU_H
#define FISIX_DOSEMU_H
#include <stdint.h>
#include <stddef.h>
/* Minimal 8086 emulator for MS-DOS .com programs, v1.
 * .com files are flat: loaded at offset 0x100 in a 64 KiB segment,
 * PSP at 0x0000, CS=DS=ES=SS=0, SP=0xFFFE, IP=0x100.
 * Supported in v1: MOV reg,imm / reg,reg / reg,mem? (no, see below),
 * ADD/SUB/CMP reg,imm and reg,reg, INC/DEC, JMP/JZ/JNZ/JC/JNC/JA/JB,
 * CALL/RET, PUSH/POP (basic), INT 20h (exit), INT 21h AH=02 (print DL),
 * AH=09 (print $-terminated string at DS:DX), AH=4C (exit with AL),
 * AH=0E? no. Anything else stops with DOS_EMU_UNSUPPORTED.
 * PC-ONLY FULL DOS NOTE, 2026-10-29: this v1 emulator is
 * simple .com only, same as GBA. Full .com support — any
 * .com that plain MS-DOS 4.0 can run — is PC-only, and
 * it is NOT via this emulator alone: on x86 BIOS PCs,
 * real MS-DOS 4.0 (trimmed copy in
 * ~/workspace/fisixpresario/msdos-4.0/src/, MIT) boots
 * in real mode and runs .com files natively, with VGA,
 * BIOS, FAT, etc., which GBA/C64/Apple IIe hardware
 * cannot provide. See PC-FULL-DOS-COM.md in this repo
 * docs, and the Presario DOS boot plan. GBA/C64/Apple
 * IIe versions stay simple .com only, by design.
 * A full DOS 4.0 reimplementation would need INT 21h file, memory,
 * and process services, plus a 16-bit CPU with segmentation and
 * real-mode hardware - that is a separate project, not v1.
 */
#define DOS_EMU_OK 0
#define DOS_EMU_EXIT 1
#define DOS_EMU_UNSUPPORTED 2
#define DOS_EMU_TRUNCATED 3
int dosemu_run(const uint8_t *com, size_t com_len, char *out, size_t out_size, int *exit_code);
#endif

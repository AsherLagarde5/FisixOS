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
 * This is NOT a full DOS: no file I/O, no DOS 4.0 kernel, no
 * real-mode switching. It runs simple .com programs that print
 * text and exit, which covers most hello-world / demo .com files.
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

#ifndef FISIX_CARAMEL_H
#define FISIX_CARAMEL_H
#include <stdint.h>
#include <stddef.h>
/* Caramel — the DOS emulation layer. Spelled C-A-R-A-M-E-L,
 * like a banana caramelized on a pan: sweet on the outside,
 * absolutely evil on the inside.
 *
 * Minimal 8086 emulator for MS-DOS .com programs, plus tiny
 * MZ .exe programs (see below). .com files are flat: loaded
 * at offset 0x100 in a 64 KiB segment, PSP at 0x0000,
 * CS=DS=ES=SS=0, SP=0xFFFE, IP=0x100.
 *
 * ABSOLUTELY EVIL: if a program asks Caramel for something
 * it does not do, Caramel does not just stop quietly — it
 * laughs at the program first ("MWAHAHA! Caramel says no.")
 * into its output, then stops with CARAMEL_UNSUPPORTED.
 * That is the same evil as FisixOS's `make death`
 * ("You have been terminatered." + MWAHAHA_<random>.txt).
 *
 * Supported CPU in v1: MOV reg,imm / reg,reg, ADD/SUB/CMP
 * reg,imm and reg,reg, INC/DEC, JMP/JZ/JNZ/JC/JNC/JA/JB,
 * CALL/RET, PUSH/POP (basic), INT 20h (exit),
 * INT 21h AH=00 (terminate), AH=02 (print DL),
 * AH=09 (print $-terminated string at DS:DX),
 * AH=0E (select disk, reports 2 logical drives),
 * AH=19 (current drive, A:), AH=25 (set interrupt vector),
 * AH=30 (get DOS version — reports 7.10, FreeDOS-style),
 * AH=35 (get interrupt vector, ES=0), AH=4C (exit with AL).
 * Anything else stops with CARAMEL_UNSUPPORTED, after laughing.
 *
 * OS/2 + FreeDOS COMPAT, 2026-11-19: OS/2 1.x programs that
 * run as DOS programs (in the OS/2 DOS box) use this same
 * INT 21h API, so AH=30 / 25 / 35 / 19 / 0E / 00 are the
 * ones they most often probe first — that is what was added.
 * FreeDOS .com programs get the same calls, and FreeDOS
 * .exe programs get the tiny MZ loader in caramel_run():
 * MZ header parsed, image loaded flat at 0x100, entry at
 * 0x100 + e_ip, SP from e_sp. HONEST LIMITS, not full compat:
 * - MZ only if it has ZERO relocations (e_crlc == 0) and
 *   fits in the 64 KiB flat segment; anything bigger or
 *   relocated is UNSUPPORTED (with laughter).
 * - No segmentation, no NE-format OS/2 executables
 *   (16-bit protected-mode OS/2 .exe is NOT supported),
 *   no DOS file I/O yet (INT 21h AH=3D/3E/3F/40/4E etc.),
 *   no date/time calls (no RTC source in this layer).
 * PC-ONLY FULL DOS NOTE, 2026-10-29: full .com support —
 * any .com that plain MS-DOS 4.0 can run — is PC-only, and
 * it is NOT via this emulator alone: on x86 BIOS PCs,
 * real MS-DOS 4.0 boots in real mode and runs .com files
 * natively, with VGA, BIOS, FAT, etc. See PC-FULL-DOS-COM.md.
 */
#define CARAMEL_OK 0
#define CARAMEL_EXIT 1
#define CARAMEL_UNSUPPORTED 2
#define CARAMEL_TRUNCATED 3
/* DOS version Caramel reports via INT 21h AH=30h. */
#define CARAMEL_DOS_MAJOR 7
#define CARAMEL_DOS_MINOR 10
/* Run a .com program, or a tiny relocation-free MZ .exe.
 * Auto-detects "MZ" at the start of the buffer. */
int caramel_run(const uint8_t *com, size_t com_len, char *out, size_t out_size, int *exit_code);
/* Old names, kept as aliases so existing callers keep working. */
#define DOS_EMU_OK CARAMEL_OK
#define DOS_EMU_EXIT CARAMEL_EXIT
#define DOS_EMU_UNSUPPORTED CARAMEL_UNSUPPORTED
#define DOS_EMU_TRUNCATED CARAMEL_TRUNCATED
#define dosemu_run caramel_run
#endif

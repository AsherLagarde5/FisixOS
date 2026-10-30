/* FisixGBA crt0: 32-bit ARM7TDMI entry.
   Header layout: branch, 156-byte logo area (ZEROED here —
   no Nintendo bitmap bundled; mGBA without BIOS boots from
   the entry point, real hardware needs the official header
   tool to fill the logo area, see README), title, codes,
   fixed byte, complement check filled by fixup.py. */
    .arm
    .text
    .global _start
_start:
    b   start_code
    .space 156
    .ascii "FISIXGBA    "   /* 12-byte title, padded */
    .ascii "FSXG"          /* game code */
    .ascii "01"            /* maker code */
    .byte 0x96            /* fixed value */
    .byte 0x00            /* main unit code */
    .byte 0x00            /* device type */
    .space 7              /* reserved */
    .byte 0x00            /* software version */
    .byte 0x00            /* complement check, patched by fixup.py */
    .byte 0x00, 0x00      /* reserved checksum, header ends at 0xC0 */
start_code:
    /* Stacks: IRQ 0x03007F00, SVC/System 0x03007F00-ish, use IWRAM top */
    msr cpsr_c, #0xD2      /* IRQ mode, IRQ/FIQ disabled */
    ldr sp, =0x03007F00
    msr cpsr_c, #0xD3      /* SVC mode */
    ldr sp, =0x03007E00
    msr cpsr_c, #0xDF      /* System mode */
    ldr sp, =0x03007D00
    /* Copy .data from ROM (LMA) to EWRAM (VMA), zero .bss */
    ldr r0, =__data_start
    ldr r1, =__data_end
    ldr r2, =__data_load
1:
    cmp r0, r1
    ldrlo r3, [r2], #4
    strlo r3, [r0], #4
    blo 1b
    ldr r0, =__bss_start
    ldr r1, =__bss_end
    mov r2, #0
2:
    cmp r0, r1
    strlo r2, [r0], #4
    blo 2b
    bl main
hang:
    b hang

ENTRY(_start)

SECTIONS
{
    /* Link the kernel to load at low memory for BIOS booting */
    . = 0x2000;

    .text : ALIGN(4096) {
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }
}
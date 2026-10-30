ENTRY(_start)
SECTIONS {
    . = 0x40080000;
    .text : {
        *(.text)
        *(.text.*)
    }
    .rodata : {
        *(.rodata)
        *(.rodata.*)
    }
    .data : {
        *(.data)
        *(.data.*)
    }
    .bss : {
        *(.bss)
        *(.bss.*)
        *(COMMON)
    }
}

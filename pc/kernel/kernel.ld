ENTRY(_start)

SECTIONS {
  /* 2MB Load Base */
  . = 0x200000;

  .text : ALIGN(4096) {
    *(.text .text.*)
  }

  .rodata : ALIGN(4096) {
    *(.rodata .rodata.*)
  }

  .data : ALIGN(4096) {
    *(.data .data.*)
  }

  .bss : ALIGN(4096) {
    *(COMMON)
    *(.bss .bss.*)
  }

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
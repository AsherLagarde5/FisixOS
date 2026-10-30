/* FisixGBA linker: ROM 0x08000000 (32 MB window, we use <256 KB),
   EWRAM 0x02000000 for .data/.bss, IWRAM stacks in crt0. */
ENTRY(_start)
SECTIONS {
  .text 0x08000000 : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  }
  __data_load = LOADADDR(.data);
  .data 0x02000000 : AT(ADDR(.text) + SIZEOF(.text)) {
    __data_start = .;
    *(.data*)
    . = ALIGN(4);
    __data_end = .;
  }
  .bss : {
    __bss_start = .;
    *(.bss*)
    *(COMMON)
    . = ALIGN(4);
    __bss_end = .;
  }
}

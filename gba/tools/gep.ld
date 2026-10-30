/* GEP app linker: code+data loaded at 0x02008000 by the loader.
   .bss is NOT in the binary; its size is stored in the header
   reserved[0] by make_gep.py and zeroed by the loader. */
ENTRY(gep_main)
SECTIONS {
  .text 0x02008000 : {
    *(.text*)
    *(.rodata*)
    . = ALIGN(4);
  }
  .data : AT(ADDR(.text) + SIZEOF(.text)) {
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

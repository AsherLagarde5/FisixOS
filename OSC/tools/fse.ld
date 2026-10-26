/* Linker script for .fse programs. Base is load address + header. */
ENTRY(fse_main)
SECTIONS {
  . = 0x300000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(.bss*) *(COMMON) }
}

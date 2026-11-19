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
    __bss_start = .;
    *(COMMON)
    *(.bss .bss.*)
    __bss_end = .;
  }

  /* End of the kernel image. The ring-3 frame allocator
     (kernel/drivers/ring3.c) never hands out pages below this. */
  kernel_end = .;

  /DISCARD/ : {
    *(.note*)
    *(.comment*)
    *(.eh_frame*)
  }
}
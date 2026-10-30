OUTPUT_FORMAT("elf64-x86-64", "elf64-x86-64", "elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(efi_main)

SECTIONS
{
    /* Link at 0x101000 so objcopy (image base 0x100000) puts .text at
       RVA 0x1000, after the PE headers. Linking at exactly 0x100000
       put .text at RVA 0, which OVMF rejects as Unsupported. */
    . = 0x101000;

    .text : {
        *(.text .text.*)
    }
    . = ALIGN(0x1000);

    .rodata : {
        *(.rodata .rodata.*)
    }
    . = ALIGN(0x1000);

    .data : {
        *(.data .data.*)
    }
    . = ALIGN(0x1000);

    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    }

    .reloc : {
        *(.reloc)
    }

    /DISCARD/ : {
        *(.note*)
        *(.comment*)
        *(.eh_frame*)
    }
}

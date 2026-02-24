OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS {
    requests PT_LOAD FLAGS((1 << 1) | (1 << 2));   /* RW  */
    text     PT_LOAD FLAGS((1 << 0) | (1 << 2));   /* RX  */
    rodata   PT_LOAD FLAGS((1 << 2));               /* R   */
    data     PT_LOAD FLAGS((1 << 1) | (1 << 2));   /* RW  */
}

SECTIONS {
    . = 0xffffffff80000000;

    .requests : {
        KEEP(*(.requests_start_marker))
        KEEP(*(.requests))
        KEEP(*(.requests_end_marker))
    } :requests

    . = ALIGN(4096);
    .text : { *(.text .text.*) } :text

    . = ALIGN(4096);
    .rodata : { *(.rodata .rodata.*) } :rodata

    . = ALIGN(4096);
    .data : { *(.data .data.*) } :data

    . = ALIGN(4096);
    .bss : { *(.bss .bss.*) *(COMMON) } :data
}

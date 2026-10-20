ENTRY(_start)

MEMORY
{
    FLASH (rx)  : ORIGIN = 0x88000000, LENGTH = 0x90000
    SRAM  (rwx) : ORIGIN = 0x81C01000, LENGTH = 0xD000
}

SECTIONS
{
    .boot_hdr : {
        KEEP(*(.boot_header))
    } > FLASH

    .text : {
        *(.text._start)
        *(.text*)
        *(.rodata*)
    } > FLASH

    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.data*)
        . = ALIGN(4);
        _data_end = .;
    } > SRAM AT > FLASH
    _data_load = LOADADDR(.data);

    .bss : {
        . = ALIGN(4);
        _bss_start = .;
        *(.bss*)
        *(COMMON)
        . = ALIGN(4);
        _bss_end = .;
    } > SRAM
}

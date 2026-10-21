ENTRY(_start)

MEMORY
{
    FLASH   (rx)  : ORIGIN = 0x88000000, LENGTH = 0x90000
    IRQSRAM (rwx) : ORIGIN = 0x81C00280, LENGTH = 0x200
    SRAM    (rwx) : ORIGIN = 0x81C01000, LENGTH = 0xD000
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

    .irqsram : {
        . = ALIGN(4);
        _irqsram_start = .;
        KEEP(*(.irq_handler_280))
        *(.irqsram*)
        . = ALIGN(4);
        _irqsram_end = .;
    } > IRQSRAM AT > FLASH
    _irqsram_load = LOADADDR(.irqsram);

    .data : {
        . = ALIGN(4);
        _data_start = .;
        *(.ramfunc*)            /* code that runs while flash is unavailable */
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

ENTRY(_start)

MEMORY
{
    SRAM_CTX  (rw)  : ORIGIN = 0x81C000A0, LENGTH = 0x20
    SRAM_CODE (rwx) : ORIGIN = 0x81C00280, LENGTH = 0x5000
    SRAM_MAP  (rw)  : ORIGIN = 0x81C05F30, LENGTH = 0x100
}

SECTIONS
{
    .context : {
        LONG(0x000000FF)
        LONG(_start)
        LONG(0x81C0F000)
        LONG(0)
        LONG(0)
        LONG(0)
    } > SRAM_CTX

    .text : {
        *(.text._start)
        *(.text*)
    } > SRAM_CODE

    .rodata : {
        *(.rodata*)
    } > SRAM_CODE

    .data : {
        *(.data*)
    } > SRAM_CODE

    .bss : {
        *(.bss*)
        *(COMMON)
    } > SRAM_CODE

    .fastpf_map : {
        *(.fastpf_map)
    } > SRAM_MAP
}

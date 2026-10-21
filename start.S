/*
 * RDA8955 (MIPS XCPU) FreeRTOS Boot Entry
 */

    .section .boot_header, "a"
    .globl _boot_header
_boot_header:
    .word 0xD9EF0045    /* ROM Magic Tag */
    .word 0x00000000    /* Reserved */
    .word 0x00000000    /* Reserved */
    .word 0x00000000    /* Reserved */

    .section .text._start, "ax"
    .globl _start
    .set noreorder
    .set noat
_start:
    /* ROM monitor leaves IE=1/BEV=1; the ROM IRQ handler traps on anything but USB. */
    mtc0  $zero, $12
    mtc0  $zero, $13
    nop
    /* Set up temporary initial stack in SRAM */
    lui   $sp, 0x81C0
    ori   $sp, $sp, 0x7F00

    /* Copy .irqsram exception vectors from FLASH to SRAM */
    la    $a0, _irqsram_load
    la    $a1, _irqsram_start
    la    $a2, _irqsram_end
    beq   $a1, $a2, 11f
    nop
10:
    lw    $t0, 0($a0)
    sw    $t0, 0($a1)
    addiu $a0, $a0, 4
    addiu $a1, $a1, 4
    sltu  $t1, $a1, $a2
    bnez  $t1, 10b
    nop
11:
    /*
     * With BEV=0 the XCPU (R3000-style) vectors exceptions to 0x81C00200
     * (DRAM_HANDLER in 8955_hard.xml's cp0 header, rebased like the ROM's
     * 0x81E00180). Install only "j _freertos_irq_entry; nop" there, through the
     * uncached alias, so the ROM data around it (0x194-0x1E8, 0x220-0x27C) stays intact.
     */
    la    $t0, _freertos_irq_entry
    srl   $t0, $t0, 2
    li    $t1, 0x03FFFFFF
    and   $t0, $t0, $t1
    lui   $t1, 0x0800
    or    $t0, $t0, $t1
    lui   $t2, 0xA1C0
    sw    $t0, 0x200($t2)
    sw    $zero, 0x204($t2)
    /* Copy initialized .data from FLASH to SRAM */
    la    $a0, _data_load
    la    $a1, _data_start
    la    $a2, _data_end
    beq   $a1, $a2, 2f
    nop
1:
    lw    $t0, 0($a0)
    sw    $t0, 0($a1)
    addiu $a0, $a0, 4
    addiu $a1, $a1, 4
    sltu  $t1, $a1, $a2
    bnez  $t1, 1b
    nop
2:
    /* Clear BSS */
    la    $a0, _bss_start
    la    $a1, _bss_end
    sltu  $t1, $a0, $a1
    beqz  $t1, 4f
    nop
3:
    sw    $zero, 0($a0)
    addiu $a0, $a0, 4
    sltu  $t1, $a0, $a1
    bnez  $t1, 3b
    nop
4:
    /* Jump to C main */
    la    $t0, main
    jalr  $t0
    nop

halt:
    b     halt
    nop

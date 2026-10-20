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
    /* Set up temporary initial stack in SRAM */
    lui   $sp, 0x81C0
    ori   $sp, $sp, 0x7F00

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

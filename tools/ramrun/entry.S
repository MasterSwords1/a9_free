    .set noreorder
    .set noat
    .section .text._start, "ax"
    .globl _start
_start:
    addiu $sp, $sp, -32
    sw    $ra, 28($sp)
    sw    $s0, 24($sp)
    la    $t0, ramrun_main
    jalr  $t0
    nop
    lw    $s0, 24($sp)
    lw    $ra, 28($sp)
    addiu $sp, $sp, 32
    jr    $ra
    nop

/*
 * MIPS32r2 FreeRTOS Port Assembly for RDA8955 (Coolsand XCPU)
 * Aligned with reverse-engineered 120-byte hardware exception context.
 */

    .set nomips16
    .set noreorder
    .set noat

    .extern pxCurrentTCB
    .extern vTaskSwitchContext
    .global vPortStartFirstTask
    .global vPortYield

    .section .text.vPortStartFirstTask, "ax"
    .ent vPortStartFirstTask
vPortStartFirstTask:
    /* Load pxCurrentTCB address, then its pxTopOfStack */
    la    $t0, pxCurrentTCB
    lw    $t0, 0($t0)
    lw    $sp, 0($t0)

    /* Restore registers from task stack (120-byte frame) */
    lw    $k0, 116($sp)    /* EPC / Task function entry point */
    lw    $a0, 112($sp)    /* a0 / pvParameters */
    lw    $fp, 108($sp)    /* s8 / fp */
    lw    $s7, 104($sp)
    lw    $s6, 100($sp)
    lw    $s5, 96($sp)
    lw    $s4, 92($sp)
    lw    $s3, 88($sp)
    lw    $s2, 84($sp)
    lw    $s1, 80($sp)
    lw    $s0, 76($sp)
    lw    $t1, 72($sp)     /* HI */
    mthi  $t1
    lw    $t0, 68($sp)     /* LO */
    mtlo  $t0
    lw    $ra, 64($sp)
    lw    $t9, 60($sp)
    lw    $t8, 56($sp)
    lw    $t7, 52($sp)
    lw    $t6, 48($sp)
    lw    $t5, 44($sp)
    lw    $t4, 40($sp)
    lw    $t3, 36($sp)
    lw    $t2, 32($sp)
    lw    $t1, 28($sp)
    lw    $a3, 24($sp)
    lw    $a2, 20($sp)
    lw    $a1, 16($sp)
    lw    $v1, 12($sp)
    lw    $v0, 8($sp)
    lw    $t0, 4($sp)
    lw    $1,  0($sp)      /* at */

    addiu $sp, $sp, 120

    jr    $k0
    nop
    .end vPortStartFirstTask


    .section .text.vPortYield, "ax"
    .ent vPortYield
vPortYield:
    /* Ensure pxCurrentTCB is valid before yielding */
    la    $k0, pxCurrentTCB
    lw    $k1, 0($k0)
    bnez  $k1, 1f
    nop

    /* Scheduler not running or no current task - return immediately */
    jr    $ra
    nop

1:
    /* Create 120-byte stack frame */
    addiu $sp, $sp, -120
    sw    $ra, 116($sp)    /* Resume PC is return address */
    sw    $a0, 112($sp)
    sw    $fp, 108($sp)
    sw    $s7, 104($sp)
    sw    $s6, 100($sp)
    sw    $s5, 96($sp)
    sw    $s4, 92($sp)
    sw    $s3, 88($sp)
    sw    $s2, 84($sp)
    sw    $s1, 80($sp)
    sw    $s0, 76($sp)
    mfhi  $k0
    sw    $k0, 72($sp)
    mflo  $k0
    sw    $k0, 68($sp)
    sw    $ra, 64($sp)
    sw    $t9, 60($sp)
    sw    $t8, 56($sp)
    sw    $t7, 52($sp)
    sw    $t6, 48($sp)
    sw    $t5, 44($sp)
    sw    $t4, 40($sp)
    sw    $t3, 36($sp)
    sw    $t2, 32($sp)
    sw    $t1, 28($sp)
    sw    $a3, 24($sp)
    sw    $a2, 20($sp)
    sw    $a1, 16($sp)
    sw    $v1, 12($sp)
    sw    $v0, 8($sp)
    sw    $t0, 4($sp)
    sw    $1,  0($sp)

    /* Save sp to pxCurrentTCB */
    la    $k0, pxCurrentTCB
    lw    $k1, 0($k0)
    sw    $sp, 0($k1)

    /* Call FreeRTOS scheduler (with 16-byte stack frame for O32 ABI) */
    addiu $sp, $sp, -16
    jal   vTaskSwitchContext
    nop
    addiu $sp, $sp, 16

    /* Load sp from new pxCurrentTCB */
    la    $k0, pxCurrentTCB
    lw    $k1, 0($k0)
    lw    $sp, 0($k1)

    /* Restore context */
    lw    $k0, 116($sp)    /* Target resume PC */
    lw    $a0, 112($sp)
    lw    $fp, 108($sp)
    lw    $s7, 104($sp)
    lw    $s6, 100($sp)
    lw    $s5, 96($sp)
    lw    $s4, 92($sp)
    lw    $s3, 88($sp)
    lw    $s2, 84($sp)
    lw    $s1, 80($sp)
    lw    $s0, 76($sp)
    lw    $t1, 72($sp)
    mthi  $t1
    lw    $t0, 68($sp)
    mtlo  $t0
    lw    $ra, 64($sp)
    lw    $t9, 60($sp)
    lw    $t8, 56($sp)
    lw    $t7, 52($sp)
    lw    $t6, 48($sp)
    lw    $t5, 44($sp)
    lw    $t4, 40($sp)
    lw    $t3, 36($sp)
    lw    $t2, 32($sp)
    lw    $t1, 28($sp)
    lw    $a3, 24($sp)
    lw    $a2, 20($sp)
    lw    $a1, 16($sp)
    lw    $v1, 12($sp)
    lw    $v0, 8($sp)
    lw    $t0, 4($sp)
    lw    $1,  0($sp)

    addiu $sp, $sp, 120

    jr    $k0
    nop
    .end vPortYield

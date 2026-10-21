/*
 * FreeRTOS port assembly for RDA8955 (Coolsand XCPU, R3000-style CP0)
 *
 * Every switched-out task holds one 128-byte frame on its stack:
 *   words 0..29  registers (layout of the vendor's 120-byte exception frame,
 *                EPC / resume address at offset 116)
 *   word  30     CP0 Status to restore, in "exception entry" form: the task's
 *                interrupt enable is in IEp, IEc is 0; rfe on return pops it back
 *   word  31     the task's critical-section nesting count
 *
 * The IRQ entry and vPortYield build the same frame and share one restore path,
 * so a task can be resumed by either, and both run the scheduler with IE off.
 */

    .set nomips16
    .set noreorder
    .set noat

#define FRAME_SIZE      128
#define FRAME_EPC       116
#define FRAME_STATUS    120
#define FRAME_NESTING   124

    .extern pxCurrentTCB
    .extern vTaskSwitchContext
    .extern uxCriticalNesting
    .global vPortStartFirstTask
    .global vPortYield

/* Save everything except EPC/Status (callers store those) into the frame at $sp. */
.macro SAVE_GPRS
    sw    $1,   0($sp)       /* at */
    sw    $t0,  4($sp)
    sw    $v0,  8($sp)
    sw    $v1,  12($sp)
    sw    $a1,  16($sp)
    sw    $a2,  20($sp)
    sw    $a3,  24($sp)
    sw    $t1,  28($sp)
    sw    $t2,  32($sp)
    sw    $t3,  36($sp)
    sw    $t4,  40($sp)
    sw    $t5,  44($sp)
    sw    $t6,  48($sp)
    sw    $t7,  52($sp)
    sw    $t8,  56($sp)
    sw    $t9,  60($sp)
    sw    $ra,  64($sp)
    mflo  $k1
    sw    $k1,  68($sp)
    mfhi  $k1
    sw    $k1,  72($sp)
    sw    $s0,  76($sp)
    sw    $s1,  80($sp)
    sw    $s2,  84($sp)
    sw    $s3,  88($sp)
    sw    $s4,  92($sp)
    sw    $s5,  96($sp)
    sw    $s6, 100($sp)
    sw    $s7, 104($sp)
    sw    $fp, 108($sp)
    sw    $a0, 112($sp)
    la    $k1, uxCriticalNesting
    lw    $k1, 0($k1)
    sw    $k1, FRAME_NESTING($sp)
    /* pxCurrentTCB->pxTopOfStack = sp */
    la    $k1, pxCurrentTCB
    lw    $k1, 0($k1)
    sw    $sp, 0($k1)
.endm

    .section .text.vPortRestoreContext, "ax"
    .ent vPortRestoreContext
/* Resume pxCurrentTCB from its frame. Must be entered with interrupts disabled. */
vPortRestoreContext:
    la    $k0, pxCurrentTCB
    lw    $k0, 0($k0)
    lw    $sp, 0($k0)

    lw    $k0, FRAME_NESTING($sp)
    la    $k1, uxCriticalNesting
    sw    $k0, 0($k1)

    lw    $k0, FRAME_STATUS($sp)
    mtc0  $k0, $12
    nop

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
    lw    $k0, 72($sp)
    mthi  $k0
    lw    $k0, 68($sp)
    mtlo  $k0
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
    lw    $k0, FRAME_EPC($sp)
    addiu $sp, $sp, FRAME_SIZE

    jr    $k0
    rfe                      /* delay slot: pop IEp back into IEc */
    .end vPortRestoreContext


    .section .text.vPortStartFirstTask, "ax"
    .ent vPortStartFirstTask
vPortStartFirstTask:
    mtc0  $zero, $12         /* interrupts off until the first frame's rfe */
    nop
    j     vPortRestoreContext
    nop
    .end vPortStartFirstTask


    .section .text.vPortYield, "ax"
    .ent vPortYield
vPortYield:
    /*
     * Enter "exception form" first: IE/KU stack shifted left by two, IEc = 0.
     * This disables interrupts and records the caller's IE in IEp.
     */
    mfc0  $k0, $12
    li    $k1, ~0x3F
    and   $k1, $k0, $k1
    andi  $k0, $k0, 0x0F
    sll   $k0, $k0, 2
    or    $k0, $k0, $k1
    mtc0  $k0, $12
    nop

    addiu $sp, $sp, -FRAME_SIZE
    sw    $k0, FRAME_STATUS($sp)
    sw    $ra, FRAME_EPC($sp)    /* resume at the caller */
    SAVE_GPRS

    addiu $sp, $sp, -16          /* O32 argument area */
    jal   vTaskSwitchContext
    nop
    addiu $sp, $sp, 16

    j     vPortRestoreContext
    nop
    .end vPortYield


    /*
     * Exception entry. With BEV=0 the CPU vectors to 0x81C00200, where start.S
     * installs "j _freertos_irq_entry". The vendor image keeps its handler here at
     * 0x81C00280; 0x81C00194-0x81C001E8 and 0x81C00220-0x81C0027C hold ROM data.
     */
    .section .irq_handler_280, "ax"
    .global _freertos_irq_entry
    .extern prvSystemInterruptHandler
    .ent _freertos_irq_entry
_freertos_irq_entry:
    addiu $sp, $sp, -FRAME_SIZE
    mfc0  $k0, $14               /* EPC */
    sw    $k0, FRAME_EPC($sp)
    mfc0  $k0, $12               /* Status already in exception form */
    sw    $k0, FRAME_STATUS($sp)
    SAVE_GPRS

    addiu $sp, $sp, -16
    jal   prvSystemInterruptHandler
    nop
    addiu $sp, $sp, 16

    j     vPortRestoreContext
    nop
    .end _freertos_irq_entry

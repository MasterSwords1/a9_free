/*
 * MIPS32r2 FreeRTOS Port C Implementation for RDA8955
 */

#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"
#include "rtc.h"
#include "gpio.h"

extern void send_host_event(unsigned int evt);

/*
 * Critical section nesting of the running task. Saved in and restored from each
 * task's context frame by port_asm.S; non-zero until the scheduler starts so that
 * critical sections before then leave interrupts disabled.
 */
volatile UBaseType_t uxCriticalNesting = 0xaaaaaaaa;

/* Task exit error handler if task returns from its function */
void prvTaskExitError(void)
{
    while (1) {
        portNOP();
    }
}

/*
 * Initialises the stack of a new task with the 128-byte context frame used by
 * port_asm.S (see the layout there).
 */
#define FRAME_WORDS     32
#define INITIAL_STATUS  0x0000FF04u   /* IM = all, IEp = 1: rfe enables interrupts */

StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters)
{
    /* 8-byte alignment */
    pxTopOfStack = (StackType_t *)(((uint32_t)pxTopOfStack) & ~0x7);
    pxTopOfStack -= FRAME_WORDS;

    for (int i = 0; i < FRAME_WORDS; i++) {
        pxTopOfStack[i] = 0;
    }

    pxTopOfStack[29] = (StackType_t)pxCode;            /* EPC: task entry */
    pxTopOfStack[28] = (StackType_t)pvParameters;      /* a0 */
    pxTopOfStack[16] = (StackType_t)prvTaskExitError;  /* ra */
    pxTopOfStack[30] = INITIAL_STATUS;                 /* CP0 Status */
    pxTopOfStack[31] = 0;                              /* critical nesting */

    return pxTopOfStack;
}

/*
 * Timer and System IRQ Hardware Registers
 */
#define REG_SYS_IRQ_BASE       0xA1A01000
#define REG_SYS_IRQ_CAUSE      (*(volatile uint32_t *)(REG_SYS_IRQ_BASE + 0x00))
#define REG_SYS_IRQ_STATUS     (*(volatile uint32_t *)(REG_SYS_IRQ_BASE + 0x04))
#define REG_SYS_IRQ_MASK_SET   (*(volatile uint32_t *)(REG_SYS_IRQ_BASE + 0x08))
#define REG_SYS_IRQ_MASK_CLR   (*(volatile uint32_t *)(REG_SYS_IRQ_BASE + 0x0C))
#define REG_SYS_IRQ_SC         (*(volatile uint32_t *)(REG_SYS_IRQ_BASE + 0x14))

#define REG_TIMER_BASE         0xA1A02000
#define REG_OSTIMER_CTRL       (*(volatile uint32_t *)(REG_TIMER_BASE + 0x00))
#define REG_TIMER_IRQ_MASK_SET (*(volatile uint32_t *)(REG_TIMER_BASE + 0x18))
#define REG_TIMER_IRQ_CLR      (*(volatile uint32_t *)(REG_TIMER_BASE + 0x20))
#define REG_TIMER_IRQ_CAUSE    (*(volatile uint32_t *)(REG_TIMER_BASE + 0x24))

static void prvSetupTimerInterrupt(void)
{
    /* Clear any pending OS timer IRQ */
    REG_TIMER_IRQ_CLR = 1;

    /*
     * 16,384 Hz clock:
     * LoadVal = 16384 / configTICK_RATE_HZ (e.g., 164 for 100 Hz = 10 ms tick)
     * Bit 30: Load (immediate)
     * Bit 28: Repeat (auto-reload)
     * Bit 24: Enable
     */
    uint32_t load_val = (16384 / configTICK_RATE_HZ);
    if (load_val == 0) load_val = 1;

    REG_OSTIMER_CTRL = (1 << 30) | (1 << 28) | (1 << 24) | (load_val & 0x00FFFFFF);

    /* Unmask OSTimer interrupt in Timer module */
    REG_TIMER_IRQ_MASK_SET = 1;

    /* Unmask SYS_IRQ_OS_TIMER (bit 10) in System IRQ controller */
    REG_SYS_IRQ_MASK_SET = (1 << 10);

    /*
     * SC.IntEnable is a semaphore: a read returns it and clears it (disabling the
     * CPU interrupt line), a write restores it. Make sure it is enabled.
     */
    REG_SYS_IRQ_SC = 1;
}

/*
 * C-level Hardware Exception & Interrupt Dispatcher
 * Called directly from _freertos_irq_entry in port_asm.S
 */
void prvSystemInterruptHandler(void)
{
    uint32_t cause = REG_SYS_IRQ_CAUSE;

    /* Bit 10: SYS_IRQ_OS_TIMER */
    if (cause & (1 << 10)) {
        /* Clear timer IRQ in Timer module */
        REG_TIMER_IRQ_CLR = 1;

        /* Increment FreeRTOS tick and switch context if a higher priority task is unblocked */
        if (xTaskIncrementTick() != pdFALSE) {
            vTaskSwitchContext();
        }
    }

    /* Bit 7: SYS_IRQ_GPIO */
    if (cause & (1 << 7)) {
        gpio_irq();
    }

    /* Bit 11: SYS_IRQ_CALENDAR (RTC alarm / interval) */
    if (cause & (1 << 11)) {
        rtc_irq();
    }
}

/*
 * Starts the FreeRTOS scheduler
 */
BaseType_t xPortStartScheduler(void)
{
    extern void vPortStartFirstTask(void);

    send_host_event(0x66660001);

    /* Configure hardware 100 Hz timer tick */
    prvSetupTimerInterrupt();

    /* Start the first task */
    vPortStartFirstTask();

    /* Should never get here */
    prvTaskExitError();
    return pdFALSE;
}

void vPortEndScheduler(void)
{
    prvTaskExitError();
}

void vPortEnterCritical(void)
{
    portDISABLE_INTERRUPTS();
    uxCriticalNesting++;
}

void vPortExitCritical(void)
{
    if (uxCriticalNesting > 0) {
        uxCriticalNesting--;
        if (uxCriticalNesting == 0) {
            portENABLE_INTERRUPTS();
        }
    }
}

/* Minimal string functions for standalone execution */
void *memset(void *s, int c, size_t n)
{
    unsigned char *p = (unsigned char *)s;
    while (n--) {
        *p++ = (unsigned char)c;
    }
    return s;
}

void *memcpy(void *dest, const void *src, size_t n)
{
    unsigned char *d = (unsigned char *)dest;
    const unsigned char *s = (const unsigned char *)src;
    while (n--) {
        *d++ = *s++;
    }
    return dest;
}

size_t strlen(const char *s)
{
    size_t len = 0;
    while (*s++) {
        len++;
    }
    return len;
}

/* With configSUPPORT_DYNAMIC_ALLOCATION=0, no FreeRTOS heap is needed! */
void *pvPortMalloc(size_t xWantedSize)
{
    (void)xWantedSize;
    return NULL;
}

void vPortFree(void *pv)
{
    (void)pv;
}

size_t xPortGetFreeHeapSize(void)
{
    return 0;
}

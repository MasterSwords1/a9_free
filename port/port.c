/*
 * MIPS32r2 FreeRTOS Port C Implementation for RDA8955
 */

#include "FreeRTOS.h"
#include "task.h"
#include "portmacro.h"

#define DEBUG_HOST_EVENT (*(volatile unsigned int *)0xA1A3F008)
extern void send_host_event(unsigned int evt);

/* Critical section nesting counter */
static volatile UBaseType_t uxCriticalNesting = 0xaaaaaaaa;

/* Task exit error handler if task returns from its function */
void prvTaskExitError(void)
{
    while (1) {
        portNOP();
    }
}

/*
 * Initialises the stack of a new task using the verified RDA8955 120-byte frame.
 */
StackType_t *pxPortInitialiseStack(StackType_t *pxTopOfStack, TaskFunction_t pxCode, void *pvParameters)
{
    /* 8-byte alignment */
    pxTopOfStack = (StackType_t *)(((uint32_t)pxTopOfStack) & ~0x7);

    /* Allocate space for 30 words (120 bytes) */
    pxTopOfStack -= 30;

    /* Clear all registers */
    for (int i = 0; i < 30; i++) {
        pxTopOfStack[i] = 0;
    }

    /* Word 29 (offset 116): EPC / Task Entry Point */
    pxTopOfStack[29] = (StackType_t)pxCode;

    /* Word 28 (offset 112): a0 / pvParameters */
    pxTopOfStack[28] = (StackType_t)pvParameters;

    /* Word 16 (offset 64): RA / Return address */
    pxTopOfStack[16] = (StackType_t)prvTaskExitError;

    return pxTopOfStack;
}

/*
 * Starts the FreeRTOS scheduler
 */
BaseType_t xPortStartScheduler(void)
{
    extern void vPortStartFirstTask(void);

    send_host_event(0x66660001);

    uxCriticalNesting = 0;

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

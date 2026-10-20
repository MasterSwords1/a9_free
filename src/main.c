/*
 * Official FreeRTOS Application for RDA8955 (MIPS32r2 XCPU)
 * 100% Standalone Open-Source Firmware Liberation
 */

#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#define REG_SYS_APB_BASE         0xA1A00000
#define REG_IOMUX_BASE           0xA1A2A000

/* GPIO Controller */
#define GPIO_BASE                (REG_SYS_APB_BASE + 0x3000)
#define GPIO_OEN_SET_OUT         (*(volatile unsigned int *)(GPIO_BASE + 0x04))
#define GPIO_VAL                 (*(volatile unsigned int *)(GPIO_BASE + 0x0C))
#define GPIO_SET                 (*(volatile unsigned int *)(GPIO_BASE + 0x10))
#define GPIO_CLR                 (*(volatile unsigned int *)(GPIO_BASE + 0x14))

#define LED_BLUE_PIN             27
#define LED_GREEN_PIN            28

/* System & Peripheral Clocks */
#define SYS_CTRL_CLK_SYS_ENABLE  (*(volatile unsigned int *)(REG_SYS_APB_BASE + 0x18))
#define SYS_CTRL_CLK_PER_ENABLE  (*(volatile unsigned int *)(REG_SYS_APB_BASE + 0x24))

/* Hardware 16384 Hz Timer */
#define HW_TIMER_UPTIME          (*(volatile unsigned int *)0xA1A02014)

/* Host Debug & Trace Events */
#define DEBUG_HOST_MODE          (*(volatile unsigned int *)0xA1A3F00C)
#define DEBUG_HOST_EVENT         (*(volatile unsigned int *)0xA1A3F008)

__attribute__((noinline)) void delay_cycles(unsigned int cycles)
{
    while (cycles--) {
        __asm__ volatile ("nop");
    }
}

__attribute__((noinline)) void delay_ms(unsigned int ms)
{
    unsigned int start = HW_TIMER_UPTIME;
    unsigned int ticks = (ms * 16384) / 1000;
    if (ticks == 0) ticks = 1;
    while ((HW_TIMER_UPTIME - start) < ticks) {
        __asm__ volatile ("nop");
    }
}

__attribute__((noinline)) void send_host_event(unsigned int evt)
{
    unsigned int timeout = 50000;
    while (((DEBUG_HOST_EVENT & 0x01) == 0) && timeout--) {
        __asm__ volatile ("nop");
    }
    DEBUG_HOST_EVENT = evt;
    delay_cycles(50000);
}

static void hardware_init(void)
{
    /* Enable peripheral clocks */
    SYS_CTRL_CLK_SYS_ENABLE = 0xFFFFFFFF;
    SYS_CTRL_CLK_PER_ENABLE = 0xFFFFFFFF;
    DEBUG_HOST_MODE = 1;

    /*
     * Configure IOMUX for GPIO pins:
     * Pins 0..7 (0x90..0xAC): Function 0 = GPIO
     * Pins 25..29 KEYIN_0..4 (0xB0..0xC0): Function 1 = GPIO 25..29
     * Pins 30..34 KEYOUT_0..4 (0xC4..0xD4): Function 1 = GPIO 30..34
     */
    for (unsigned int off = 0x90; off <= 0xAC; off += 4) {
        *(volatile unsigned int *)(REG_IOMUX_BASE + off) = (3 << 18) | 0;
    }
    for (unsigned int off = 0xB0; off <= 0xD4; off += 4) {
        *(volatile unsigned int *)(REG_IOMUX_BASE + off) = (3 << 18) | 1;
    }

    /* Configure GPIO direction: Output */
    GPIO_OEN_SET_OUT = 0xFFFFFFFF;
    *(volatile unsigned int *)(GPIO_BASE + 0x3C) = 0x07;

    /* Initialize all pins LOW */
    GPIO_CLR = 0xFFFFFFFF;
    *(volatile unsigned int *)(GPIO_BASE + 0x4C) = 0x07;
}

/*
 * Task 1: Heartbeat Task
 */
static void vHeartbeatTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;
    int state = 0;

    send_host_event(0x1111AAAA);

    for (;;) {
        state = !state;
        if (state) {
            GPIO_SET = (1 << LED_BLUE_PIN) | (1 << 25) | (1 << 26) | (1 << 29);
        } else {
            GPIO_CLR = (1 << LED_BLUE_PIN) | (1 << 25) | (1 << 26) | (1 << 29);
        }

        count++;
        send_host_event(0x11110000 | (state ? 1 : 0));
        delay_ms(500);

        taskYIELD();
    }
}

/*
 * Task 2: Worker Task
 */
static void vWorkerTask(void *pvParameters)
{
    (void)pvParameters;
    uint32_t count = 0;
    int state = 0;

    send_host_event(0x2222AAAA);

    for (;;) {
        state = !state;
        if (state) {
            GPIO_SET = (1 << LED_GREEN_PIN) | (1 << 30) | (1 << 31);
        } else {
            GPIO_CLR = (1 << LED_GREEN_PIN) | (1 << 30) | (1 << 31);
        }

        count++;
        send_host_event(0x22220000 | (state ? 1 : 0));
        delay_ms(500);

        taskYIELD();
    }
}

/*
 * FreeRTOS Static Allocation: Idle Task Memory Callback
 */
void vApplicationGetIdleTaskMemory(StaticTask_t **ppxIdleTaskTCBBuffer,
                                   StackType_t **ppxIdleTaskStackBuffer,
                                   uint32_t *pulIdleTaskStackSize)
{
    static StaticTask_t xIdleTaskTCB __attribute__((aligned(8)));
    static StackType_t uxIdleTaskStack[configMINIMAL_STACK_SIZE] __attribute__((aligned(8)));

    send_host_event(0xBA5E0060);
    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/*
 * FreeRTOS Idle Hook:
 */
void vApplicationIdleHook(void)
{
    static uint32_t idle_ticks = 0;
    idle_ticks++;

    delay_cycles(50000);

    /* Increment FreeRTOS tick */
    xTaskIncrementTick();

    if ((idle_ticks % 10) == 0) {
        send_host_event(0x44440000 | (xTaskGetTickCount() & 0xFFFF));
    }

    /* Yield so any newly unblocked task runs immediately! */
    taskYIELD();
}

/* Static Task TCBs and Stacks */
static StaticTask_t s_hb_tcb __attribute__((aligned(8)));
static StackType_t  s_hb_stack[256] __attribute__((aligned(8)));
static StaticTask_t s_wk_tcb __attribute__((aligned(8)));
static StackType_t  s_wk_stack[256] __attribute__((aligned(8)));

int main(void)
{
    hardware_init();

    /* Banner event */
    send_host_event(0xBA5E0001);
    send_host_event(0xBA5E0002);

    send_host_event(0xBA5E0003);
    memset(&s_hb_tcb, 0, sizeof(s_hb_tcb));
    send_host_event(0xBA5E0004);
    memset(s_hb_stack, 0xA5, sizeof(s_hb_stack));
    send_host_event(0xBA5E0005);

    /* Create FreeRTOS Tasks with 100% Static Allocation (Zero Heap) */
    TaskHandle_t h1 = xTaskCreateStatic(vHeartbeatTask, "Heartbeat", 256, NULL, 1, s_hb_stack, &s_hb_tcb);
    if (h1 != NULL) {
        send_host_event(0xBA5E0110);
    } else {
        send_host_event(0xBA5E0010);
    }

    TaskHandle_t h2 = xTaskCreateStatic(vWorkerTask, "Worker", 256, NULL, 1, s_wk_stack, &s_wk_tcb);
    if (h2 != NULL) {
        send_host_event(0xBA5E0120);
    } else {
        send_host_event(0xBA5E0020);
    }

    /* Start FreeRTOS Scheduler */
    send_host_event(0xBA5E0050);

    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {
        send_host_event(0xDEADDEAD);
        delay_cycles(500000);
    }

    return 0;
}

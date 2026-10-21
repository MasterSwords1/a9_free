/*
 * Official FreeRTOS Application for RDA8955 (MIPS32r2 XCPU)
 * 100% Standalone Open-Source Firmware Liberation
 *
 * Features:
 * - Preemptive Hardware Timer Tick (100 Hz / 10 ms resolution)
 * - Standalone Cold Boot from SPI Flash (0x08000000)
 * - Concurrent FreeRTOS Tasks with Static Allocation (Zero Heap)
 * - Interactive Bidirectional Serial Shell (CLI) on /dev/ttyUSB0
 */

#include "FreeRTOS.h"
#include "task.h"
#include "uart.h"
#include "uart2.h"
#include "pmu.h"
#include "gps.h"
#include "system.h"

extern void *memset(void *s, int c, size_t n);

#define WDT_DEFAULT_MS  4000

/*
 * The watchdog is fed only from the idle task: if any task monopolises the CPU or
 * interrupts stay disabled, idle never runs and the chip resets.
 */
void vApplicationIdleHook(void)
{
    wdt_kick();
}

#define REG_SYS_APB_BASE         0xA1A00000
#define REG_IOMUX_BASE           0xA1A2A000

/* GPIO Controller */
#define GPIO_BASE                (REG_SYS_APB_BASE + 0x3000)
#define GPIO_OEN_SET_OUT         (*(volatile uint32_t *)(GPIO_BASE + 0x04))
#define GPIO_VAL                 (*(volatile uint32_t *)(GPIO_BASE + 0x0C))
#define GPIO_SET                 (*(volatile uint32_t *)(GPIO_BASE + 0x10))
#define GPIO_CLR                 (*(volatile uint32_t *)(GPIO_BASE + 0x14))

#define LED_BLUE_PIN             27
#define LED_GREEN_PIN            28

/* System & Peripheral Clocks */
#define SYS_CTRL_CLK_SYS_ENABLE  (*(volatile uint32_t *)(REG_SYS_APB_BASE + 0x18))
#define SYS_CTRL_CLK_PER_ENABLE  (*(volatile uint32_t *)(REG_SYS_APB_BASE + 0x24))

/* Hardware 16384 Hz Counter */
#define HW_TIMER_UPTIME          (*(volatile uint32_t *)0xA1A02014)

/* Host Debug & Trace Events */
#define DEBUG_HOST_MODE          (*(volatile uint32_t *)0xA1A3F00C)
#define DEBUG_HOST_EVENT         (*(volatile uint32_t *)0xA1A3F008)

void send_host_event(uint32_t evt)
{
    uint32_t timeout = 50000;
    while (((DEBUG_HOST_EVENT & 0x01) == 0) && timeout--) {
        __asm__ volatile ("nop");
    }
    DEBUG_HOST_EVENT = evt;
}

static void hardware_init(void)
{
    /* Enable all peripheral and system clocks */
    SYS_CTRL_CLK_SYS_ENABLE = 0xFFFFFFFF;
    SYS_CTRL_CLK_PER_ENABLE = 0xFFFFFFFF;
    DEBUG_HOST_MODE = 1;

    /*
     * IOMUX: only the LED pads are taken over. GPIO 27/28 are function 1 of pads
     * KEYIN_2 (0xB8) / KEYIN_3 (0xBC); drive strength 3. Other pads keep their
     * reset functions so peripherals (UART2/GPS, 32 kHz out) can claim them.
     */
    *(volatile uint32_t *)(REG_IOMUX_BASE + 0xB8) = (3 << 18) | 1;
    *(volatile uint32_t *)(REG_IOMUX_BASE + 0xBC) = (3 << 18) | 1;

    /* LEDs as outputs, initially off */
    GPIO_OEN_SET_OUT = (1 << LED_BLUE_PIN) | (1 << LED_GREEN_PIN);
    GPIO_CLR = (1 << LED_BLUE_PIN) | (1 << LED_GREEN_PIN);

    /* Initialize Host Debug UART for raw 8N1 serial communication */
    uart_init();
}

/*
 * Task 1: Heartbeat Task (Blue LED Blink via Preemptive vTaskDelay)
 */
static void vHeartbeatTask(void *pvParameters)
{
    (void)pvParameters;
    int state = 0;

    for (;;) {
        state = !state;
        if (state) {
            GPIO_SET = (1 << LED_BLUE_PIN);
        } else {
            GPIO_CLR = (1 << LED_BLUE_PIN);
        }

        send_host_event(0x11110000 | (state ? 1 : 0));
            vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*
 * Task 2: Worker Task (Green LED Blink via Preemptive vTaskDelay)
 */
static void vWorkerTask(void *pvParameters)
{
    (void)pvParameters;
    int state = 0;

    for (;;) {
        state = !state;
        if (state) {
            GPIO_SET = (1 << LED_GREEN_PIN);
        } else {
            GPIO_CLR = (1 << LED_GREEN_PIN);
        }

        send_host_event(0x22220000 | (state ? 1 : 0));
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/*
 * Helper String & Number Utilities for CLI
 */
static int str_eq(const char *a, const char *b)
{
    while (*a && *b && (*a == *b)) {
        a++;
        b++;
    }
    return (*a == '\0' && *b == '\0');
}

static int str_starts_with(const char *s, const char *prefix)
{
    while (*prefix) {
        if (*s++ != *prefix++) return 0;
    }
    return 1;
}

static uint32_t parse_hex(const char *s)
{
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s += 2;
    uint32_t val = 0;
    while (*s) {
        char c = *s++;
        if (c >= '0' && c <= '9') val = (val << 4) | (c - '0');
        else if (c >= 'a' && c <= 'f') val = (val << 4) | (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val = (val << 4) | (c - 'A' + 10);
        else break;
    }
    return val;
}

static uint32_t parse_dec(const char *s)
{
    uint32_t val = 0;
    while (*s >= '0' && *s <= '9') val = val * 10 + (uint32_t)(*s++ - '0');
    return val;
}

static void print_hex(uint32_t val)
{
    char buf[11];
    buf[0] = '0';
    buf[1] = 'x';
    for (int i = 7; i >= 0; i--) {
        uint8_t nib = (val >> (i * 4)) & 0xF;
        buf[2 + (7 - i)] = (nib < 10) ? ('0' + nib) : ('A' + nib - 10);
    }
    buf[10] = '\0';
    uart_puts(buf);
}

static void print_dec(uint32_t val)
{
    if (val == 0) {
        uart_putc('0');
        return;
    }
    char buf[12];
    int idx = 0;
    while (val > 0) {
        buf[idx++] = '0' + (val % 10);
        val /= 10;
    }
    for (int i = idx - 1; i >= 0; i--) {
        uart_putc(buf[i]);
    }
}

/* Signed fixed-point value with 7 decimals (degrees * 1e7) */
static void print_fixed7(int32_t v)
{
    uint32_t u = v < 0 ? (uint32_t)(-v) : (uint32_t)v;
    if (v < 0) uart_putc('-');
    print_dec(u / 10000000);
    uart_putc('.');
    uint32_t frac = u % 10000000;
    for (uint32_t div = 1000000; div; div /= 10) {
        uart_putc('0' + (frac / div) % 10);
    }
}

/*
 * Task 3: Interactive Serial CLI Shell
 */
static void cli_handle_command(char *cmd)
{
    /* Skip leading whitespace */
    while (*cmd == ' ') cmd++;
    if (*cmd == '\0') return;

    if (str_eq(cmd, "help")) {
        uart_puts("Available commands:\r\n");
        uart_puts("  help                - Show this command reference\r\n");
        uart_puts("  status              - Display kernel & hardware status\r\n");
        uart_puts("  uptime              - Show system uptime and tick count\r\n");
        uart_puts("  led blue on|off     - Control Blue LED (GPIO 27)\r\n");
        uart_puts("  led green on|off    - Control Green LED (GPIO 28)\r\n");
        uart_puts("  peek <hex_addr>     - Read 32-bit MMIO register\r\n");
        uart_puts("  poke <addr> <val>   - Write 32-bit MMIO register\r\n");
        uart_puts("  gps on|off          - Power the GPS receiver (PMU LDO + 32k clock)\r\n");
        uart_puts("  gps                 - Show GPS fix and NMEA statistics\r\n");
        uart_puts("  gps raw             - Show the most recent raw NMEA bytes\r\n");
        uart_puts("  pmu <hex_reg>       - Read a PMU register over ISPI\r\n");
        uart_puts("  pmuw <reg> <val>    - Write a PMU register (expert)\r\n");
        uart_puts("  reboot              - Soft-reset the whole chip\r\n");
        uart_puts("  wdt [on [ms]|off]   - Watchdog status / control (fed by the idle task)\r\n");
        uart_puts("  hang                - Spin with interrupts off (watchdog test)\r\n");
    } else if (str_eq(cmd, "status")) {
        uart_puts("Kernel: FreeRTOS 10.x (Preemptive)\r\n");
        uart_puts("Target: RDA8955 (MIPS32r2 XCPU @ 312 MHz)\r\n");
        uart_puts("Tick Rate: "); print_dec(configTICK_RATE_HZ); uart_puts(" Hz (10 ms)\r\n");
        uart_puts("Ticks elapsed: "); print_dec(xTaskGetTickCount()); uart_puts("\r\n");
        uart_puts("Hardware Uptime Timer: "); print_hex(HW_TIMER_UPTIME); uart_puts("\r\n");
        uart_puts("Last reset: "); uart_puts(sys_reset_cause_str(sys_reset_cause()));
        uart_puts(" ("); print_hex(sys_reset_cause()); uart_puts(")\r\n");
        uart_puts("Watchdog: ");
        if (wdt_running()) { print_dec(wdt_timeout_ms()); uart_puts(" ms\r\n"); }
        else uart_puts("stopped\r\n");
    } else if (str_eq(cmd, "uptime")) {
        uint32_t ticks = xTaskGetTickCount();
        uint32_t sec = ticks / configTICK_RATE_HZ;
        uart_puts("Uptime: "); print_dec(sec); uart_puts(" seconds (");
        print_dec(ticks); uart_puts(" ticks)\r\n");
    } else if (str_starts_with(cmd, "led blue on")) {
        GPIO_SET = (1 << LED_BLUE_PIN);
        uart_puts("Blue LED ON\r\n");
    } else if (str_starts_with(cmd, "led blue off")) {
        GPIO_CLR = (1 << LED_BLUE_PIN);
        uart_puts("Blue LED OFF\r\n");
    } else if (str_starts_with(cmd, "led green on")) {
        GPIO_SET = (1 << LED_GREEN_PIN);
        uart_puts("Green LED ON\r\n");
    } else if (str_starts_with(cmd, "led green off")) {
        GPIO_CLR = (1 << LED_GREEN_PIN);
        uart_puts("Green LED OFF\r\n");
    } else if (str_starts_with(cmd, "peek ")) {
        uint32_t addr = parse_hex(cmd + 5);
        uint32_t val = *(volatile uint32_t *)addr;
        uart_puts("["); print_hex(addr); uart_puts("] = "); print_hex(val); uart_puts("\r\n");
    } else if (str_starts_with(cmd, "poke ")) {
        char *p = cmd + 5;
        uint32_t addr = parse_hex(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint32_t val = parse_hex(p);
        *(volatile uint32_t *)addr = val;
        uart_puts("Wrote ["); print_hex(addr); uart_puts("] = "); print_hex(val); uart_puts("\r\n");
    } else if (str_starts_with(cmd, "pmuw ")) {
        char *p = cmd + 5;
        uint16_t reg = (uint16_t)parse_hex(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        uint16_t val = (uint16_t)parse_hex(p);
        pmu_write(reg, val);
        uart_puts("PMU["); print_hex(reg); uart_puts("] <- "); print_hex(val);
        uart_puts(", readback "); print_hex(pmu_read(reg)); uart_puts("\r\n");
    } else if (str_starts_with(cmd, "pmu ")) {
        uint16_t reg = (uint16_t)parse_hex(cmd + 4);
        uart_puts("PMU["); print_hex(reg); uart_puts("] = "); print_hex(pmu_read(reg)); uart_puts("\r\n");
    } else if (str_eq(cmd, "gps on") || str_eq(cmd, "gps off")) {
        int on = str_eq(cmd, "gps on");
        int ok = gps_power(on);
        uart_puts(on ? "GPS power ON" : "GPS power OFF");
        uart_puts(ok ? " (LDO verified)\r\n" : " (LDO readback MISMATCH)\r\n");
    } else if (str_eq(cmd, "gps raw")) {
        static char raw[160];
        unsigned n = gps_raw_copy(raw, sizeof(raw));
        for (unsigned i = 0; i < n; i++) {
            char c = raw[i];
            uart_putc((c >= 0x20 && c <= 0x7E) || c == '\r' || c == '\n' ? c : '.');
        }
        uart_puts("\r\n");
    } else if (str_eq(cmd, "gps")) {
        nmea_fix_t *f = &gps_state()->fix;
        uart_puts("GPS: "); uart_puts(gps_is_on() ? "on" : "off");
        uart_puts(", bytes "); print_dec(gps_bytes());
        uart_puts(", task loops "); print_dec(gps_loops());
        uart_puts(", sentences ok "); print_dec(f->sentences_ok);
        uart_puts(" bad "); print_dec(f->sentences_bad); uart_puts("\r\n");
        uart_puts("Fix quality "); print_dec(f->fix_quality);
        uart_puts(", sats "); print_dec(f->sats);
        uart_puts(", RMC "); uart_puts(f->rmc_valid ? "valid" : "void");
        uart_puts(", UTC "); print_dec(f->utc_hhmmss); uart_puts("\r\n");
        uart_puts("Lat "); print_fixed7(f->lat_e7);
        uart_puts("  Lon "); print_fixed7(f->lon_e7);
        uart_puts("  Alt "); print_dec((uint32_t)(f->alt_dm < 0 ? -f->alt_dm : f->alt_dm) / 10); uart_puts(" m\r\n");
    } else if (str_eq(cmd, "reboot")) {
        uart_puts("Rebooting system...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        sys_soft_reset();
    } else if (str_eq(cmd, "wdt")) {
        uart_puts("Watchdog: ");
        if (wdt_running()) {
            uart_puts("running, timeout "); print_dec(wdt_timeout_ms()); uart_puts(" ms\r\n");
        } else {
            uart_puts("stopped\r\n");
        }
    } else if (str_eq(cmd, "wdt off")) {
        wdt_stop();
        uart_puts("Watchdog stopped\r\n");
    } else if (str_starts_with(cmd, "wdt on")) {
        uint32_t ms = WDT_DEFAULT_MS;
        if (cmd[6] == ' ') ms = parse_dec(cmd + 7);
        wdt_start(ms);
        uart_puts("Watchdog running, timeout "); print_dec(wdt_timeout_ms()); uart_puts(" ms\r\n");
    } else if (str_eq(cmd, "hang")) {
        uart_puts("Hanging with interrupts off; the watchdog should reset the chip...\r\n");
        vTaskDelay(pdMS_TO_TICKS(50));
        taskDISABLE_INTERRUPTS();
        for (;;) {
            __asm__ volatile ("nop");
        }
    } else {
        uart_puts("Unknown command: '");
        uart_puts(cmd);
        uart_puts("'. Type 'help' for available commands.\r\n");
    }
}

static void vShellTask(void *pvParameters)
{
    (void)pvParameters;
    static char line_buf[64];
    int line_idx = 0;

    /* Print shell banner */
    uart_puts("\r\n");
    uart_puts("=========================================================\r\n");
    uart_puts("  a9_free: 100% Open-Source FreeRTOS Firmware for RDA8955\r\n");
    uart_puts("  Preemptive Hardware Tick (100 Hz) | Serial CLI Active\r\n");
    uart_puts("=========================================================\r\n");
    uart_puts("Last reset: "); uart_puts(sys_reset_cause_str(sys_reset_cause())); uart_puts("\r\n");
    uart_puts("Type 'help' for commands.\r\n\r\n");
    uart_puts("a9> ");

    for (;;) {
        int c = uart_getc();
        if (c >= 0) {
            if (c == '\r' || c == '\n') {
                uart_puts("\r\n");
                line_buf[line_idx] = '\0';
                cli_handle_command(line_buf);
                line_idx = 0;
                uart_puts("a9> ");
            } else if (c == '\b' || c == 0x7F) {
                if (line_idx > 0) {
                    line_idx--;
                    uart_puts("\b \b");
                }
            } else if (c >= 0x20 && c <= 0x7E) {
                if (line_idx < (int)(sizeof(line_buf) - 1)) {
                    line_buf[line_idx++] = (char)c;
                    uart_putc((char)c);
                }
            }
        } else {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
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

    *ppxIdleTaskTCBBuffer = &xIdleTaskTCB;
    *ppxIdleTaskStackBuffer = uxIdleTaskStack;
    *pulIdleTaskStackSize = configMINIMAL_STACK_SIZE;
}

/* Static Task TCBs and Stacks */
static StaticTask_t s_hb_tcb __attribute__((aligned(8)));
static StackType_t  s_hb_stack[256] __attribute__((aligned(8)));

static StaticTask_t s_wk_tcb __attribute__((aligned(8)));
static StackType_t  s_wk_stack[256] __attribute__((aligned(8)));

static StaticTask_t s_cli_tcb __attribute__((aligned(8)));
static StackType_t  s_cli_stack[512] __attribute__((aligned(8)));

int main(void)
{
    sys_init();
    hardware_init();

    send_host_event(0xBA5E0001);

    memset(&s_hb_tcb, 0, sizeof(s_hb_tcb));
    memset(&s_wk_tcb, 0, sizeof(s_wk_tcb));
    memset(&s_cli_tcb, 0, sizeof(s_cli_tcb));

    /* Create Heartbeat Task (Priority 1) */
    xTaskCreateStatic(vHeartbeatTask, "Heartbeat", 256, NULL, 1, s_hb_stack, &s_hb_tcb);

    /* Create Worker Task (Priority 1) */
    xTaskCreateStatic(vWorkerTask, "Worker", 256, NULL, 1, s_wk_stack, &s_wk_tcb);

    /* GPS NMEA reader (idle until "gps on") */
    gps_init_task();

    /* Create Interactive Serial Shell Task (Priority 2) */
    xTaskCreateStatic(vShellTask, "Shell", 512, NULL, 2, s_cli_stack, &s_cli_tcb);

    wdt_start(WDT_DEFAULT_MS);

    /* Start FreeRTOS Scheduler with Hardware Preemption */
    send_host_event(0xBA5E0050);

    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {
        send_host_event(0xDEADDEAD);
    }

    return 0;
}

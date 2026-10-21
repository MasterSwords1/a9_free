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
#include "clock.h"
#include "rtc.h"
#include "flash.h"
#include "settings.h"
#include "gpadc.h"
#include "gpio.h"
#include "pwm.h"

extern void *memset(void *s, int c, size_t n);

static settings_t s_cfg;
static int        s_cfg_loaded;   /* 0 = from flash, -1 = defaults */

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

static void print_dec(uint32_t val);

static void print_2d(uint32_t v)
{
    uart_putc((char)('0' + (v / 10) % 10));
    uart_putc((char)('0' + v % 10));
}

static void print_time(const rtc_time_t *t)
{
    static const char *wd[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    print_dec(t->year); uart_putc('-'); print_2d(t->month); uart_putc('-'); print_2d(t->day);
    uart_putc(' ');
    print_2d(t->hour); uart_putc(':'); print_2d(t->min); uart_putc(':'); print_2d(t->sec);
    uart_putc(' ');
    uart_puts(wd[rtc_weekday(t->year, t->month, t->day)]);
}

/* "YYYY-MM-DD HH:MM:SS" */
static int parse_datetime(const char *p, rtc_time_t *t)
{
    uint32_t v[6] = {0};
    for (int i = 0; i < 6; i++) {
        if (*p < '0' || *p > '9') return -1;
        while (*p >= '0' && *p <= '9') v[i] = v[i] * 10 + (uint32_t)(*p++ - '0');
        if (i < 5) { if (*p != '-' && *p != ' ' && *p != ':' && *p != 'T') return -1; p++; }
    }
    t->year = (uint16_t)v[0]; t->month = (uint8_t)v[1]; t->day = (uint8_t)v[2];
    t->hour = (uint8_t)v[3]; t->min = (uint8_t)v[4]; t->sec = (uint8_t)v[5];
    return 0;
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
        uart_puts("  clock               - Show the system clock and measure it\r\n");
        uart_puts("  clock set <MHz>     - Switch the system clock (26..312 MHz steps)\r\n");
        uart_puts("  date [set <ts>]     - Show / set the RTC (YYYY-MM-DD HH:MM:SS)\r\n");
        uart_puts("  alarm [in <s>|off]  - RTC alarm status / arm / disarm\r\n");
        uart_puts("  cfg [clock|wdt|note <v>|save|erase] - Persistent settings\r\n");
        uart_puts("  flash [test]        - Flash status / erase-write-verify benchmark\r\n");
        uart_puts("  adc [ch|cal]        - Read GPADC channels (ch7 = VBAT) / eFuse calibration\r\n");
        uart_puts("  gpio <pin> [in|0|1] - Read / drive a GPIO\r\n");
        uart_puts("  gpio irqtest [pin]  - Edge-interrupt self test (default pin 3)\r\n");
        uart_puts("  pwm <0..255>|off    - PWL1 on GPIO_2 (duty sensed on GPIO_3 via jumper)\r\n");
        uart_puts("  reboot              - Soft-reset the whole chip\r\n");
        uart_puts("  wdt [on [ms]|off]   - Watchdog status / control (fed by the idle task)\r\n");
        uart_puts("  hang                - Spin with interrupts off (watchdog test)\r\n");
    } else if (str_eq(cmd, "status")) {
        uart_puts("Kernel: FreeRTOS 10.x (Preemptive)\r\n");
        uart_puts("Target: RDA8955 (XCPU @ "); print_dec(clock_sys_mhz()); uart_puts(" MHz)\r\n");
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
    } else if (str_eq(cmd, "cfg")) {
        uart_puts("Settings ("); uart_puts(s_cfg_loaded == 0 ? "loaded from flash" : "defaults, none saved");
        uart_puts("):\r\n  clock "); print_dec(s_cfg.clock_mhz); uart_puts(" MHz\r\n  wdt   ");
        print_dec(s_cfg.wdt_ms); uart_puts(" ms\r\n  note  \""); uart_puts(s_cfg.note); uart_puts("\"\r\n");
    } else if (str_starts_with(cmd, "cfg clock ")) {
        s_cfg.clock_mhz = parse_dec(cmd + 10);
        uart_puts("clock -> "); print_dec(s_cfg.clock_mhz); uart_puts(" MHz (cfg save, then reboot)\r\n");
    } else if (str_starts_with(cmd, "cfg wdt ")) {
        s_cfg.wdt_ms = parse_dec(cmd + 8);
        uart_puts("wdt -> "); print_dec(s_cfg.wdt_ms); uart_puts(" ms (cfg save, then reboot)\r\n");
    } else if (str_starts_with(cmd, "cfg note ")) {
        const char *src = cmd + 9;
        unsigned i = 0;
        while (src[i] && i < sizeof(s_cfg.note) - 1) { s_cfg.note[i] = src[i]; i++; }
        s_cfg.note[i] = '\0';
        uart_puts("note set (cfg save to keep it)\r\n");
    } else if (str_eq(cmd, "cfg save")) {
        uint32_t t0 = HW_TIMER_UPTIME;
        int rc = settings_save(&s_cfg);
        uint32_t dt = HW_TIMER_UPTIME - t0;
        if (rc == 0) s_cfg_loaded = 0;
        uart_puts(rc == 0 ? "Saved and verified" : "Save FAILED"); uart_puts(" ("); print_dec(dt * 1000 / 16384); uart_puts(" ms)\r\n");
    } else if (str_eq(cmd, "cfg erase")) {
        uart_puts(settings_erase() == 0 ? "Settings erased (defaults on next boot)\r\n" : "Erase FAILED\r\n");
    } else if (str_eq(cmd, "flash")) {
        uint32_t st = flash_status();
        uart_puts("Flash SR2:SR1 = "); print_hex(st); uart_puts("  storage window ");
        print_hex(FLASH_STORAGE_BASE); uart_puts(" + "); print_hex(FLASH_STORAGE_SIZE); uart_puts("\r\n");
    } else if (str_eq(cmd, "flash test")) {
        /* Erase / write / verify the last storage sector and time each step */
        static uint8_t buf[256];
        const uint32_t a = FLASH_STORAGE_BASE + FLASH_STORAGE_SIZE - FLASH_SECTOR_SIZE;
        uint32_t t0 = HW_TIMER_UPTIME;
        int rc = flash_erase(a, FLASH_SECTOR_SIZE);
        uint32_t t1 = HW_TIMER_UPTIME;
        uint32_t bad = 0;
        for (uint32_t off = 0; off < FLASH_SECTOR_SIZE && rc == 0; off += sizeof(buf)) {
            flash_read(a + off, buf, sizeof(buf));
            for (unsigned i = 0; i < sizeof(buf); i++) if (buf[i] != 0xFF) bad++;
        }
        uint32_t t2 = HW_TIMER_UPTIME;
        for (uint32_t off = 0; off < FLASH_SECTOR_SIZE && rc == 0; off += sizeof(buf)) {
            for (unsigned i = 0; i < sizeof(buf); i++) buf[i] = (uint8_t)((off + i) * 7 + (off >> 8));
            rc = flash_write(a + off, buf, sizeof(buf));
        }
        uint32_t t3 = HW_TIMER_UPTIME;
        for (uint32_t off = 0; off < FLASH_SECTOR_SIZE && rc == 0; off += sizeof(buf)) {
            flash_read(a + off, buf, sizeof(buf));
            for (unsigned i = 0; i < sizeof(buf); i++) if (buf[i] != (uint8_t)((off + i) * 7 + (off >> 8))) bad++;
        }
        uart_puts(rc ? "flash op FAILED" : (bad ? "VERIFY FAILED" : "OK"));
        uart_puts(": erase 4 KB "); print_dec((t1 - t0) * 1000 / 16384);
        uart_puts(" ms, blank check "); print_dec((t2 - t1) * 1000 / 16384);
        uart_puts(" ms, write 4 KB "); print_dec((t3 - t2) * 1000 / 16384);
        uart_puts(" ms, bad bytes "); print_dec(bad); uart_puts("\r\n");
    } else if (str_starts_with(cmd, "gpio irqtest")) {
        /* Toggle a pin as an output with both-edge interrupts and count them */
        uint32_t pin = cmd[12] == ' ' ? parse_dec(cmd + 13) : 3;
        if (pin >= GPIO_PINS || pin == LED_BLUE_PIN || pin == LED_GREEN_PIN) {
            uart_puts("Use a free pin 0..34 (not the LEDs)\r\n");
        } else {
            gpio_mux(pin);
            gpio_set_output(pin, 0);
            gpio_irq_reset(pin);
            gpio_irq_config(pin, 1, 1, 0, 0);
            uint32_t readback_ok = 0;
            for (int i = 0; i < 10; i++) {
                gpio_write(pin, 1); vTaskDelay(1); readback_ok += gpio_read(pin) == 1;
                gpio_write(pin, 0); vTaskDelay(1); readback_ok += gpio_read(pin) == 0;
            }
            gpio_irq_config(pin, 0, 0, 0, 0);
            gpio_set_input(pin);
            uart_puts("GPIO"); print_dec(pin); uart_puts(": 20 edges driven, "); print_dec(gpio_irq_count(pin));
            uart_puts(" interrupts, pad readback "); print_dec(readback_ok); uart_puts("/20\r\n");
        }
    } else if (str_starts_with(cmd, "gpio ")) {
        /* gpio <pin> [in|0|1] */
        char *p = cmd + 5;
        uint32_t pin = parse_dec(p);
        while (*p && *p != ' ') p++;
        while (*p == ' ') p++;
        if (pin >= GPIO_PINS) { uart_puts("pin 0..34\r\n"); }
        else {
            if (*p == '0' || *p == '1') { gpio_mux(pin); gpio_set_output(pin, *p == '1'); }
            else if (str_eq(p, "in")) { gpio_mux(pin); gpio_set_input(pin); }
            uart_puts("GPIO"); print_dec(pin); uart_puts(" = "); print_dec((uint32_t)gpio_read(pin));
            uart_puts(", irqs "); print_dec(gpio_irq_count(pin)); uart_puts("\r\n");
        }
    } else if (str_eq(cmd, "pwm off")) {
        pwl1_release();
        uart_puts("PWL1 off, GPIO_2 back to GPIO\r\n");
    } else if (str_starts_with(cmd, "pwm ")) {
        /*
         * Set PWL1 on GPIO_2. The GPIO input path is disconnected while the pad
         * runs PWL1, so the duty is measured on a second pin: jumper GPIO_2 to
         * GPIO_3 (sense pin, input) to verify.
         */
        uint32_t level = parse_dec(cmd + 4);
        if (level > 255) level = 255;
        gpio_mux(3);
        gpio_set_input(3);
        pwl1_set(level);
        vTaskDelay(2);
        uint32_t high = 0, n = 200000;
        for (uint32_t i = 0; i < n; i++) high += (uint32_t)gpio_read(3);
        uart_puts("PWL1 level "); print_dec(level); uart_puts("/255 (");
        print_dec(level * 1000 / 255); uart_puts(" permille); GPIO_3 sense: ");
        print_dec(high / (n / 1000)); uart_puts(" permille high");
        if (high == 0 && level > 0) uart_puts(" (no jumper GPIO_2 -> GPIO_3?)");
        uart_puts("\r\n");
    } else if (str_eq(cmd, "adc cal")) {
        uint16_t lo, hi;
        gpadc_calib(&lo, &hi);
        uart_puts("GPADC eFuse calibration: raw "); print_dec(lo); uart_puts(" @ 1133 mV, raw ");
        print_dec(hi); uart_puts(" @ 1400 mV"); uart_puts(lo < hi ? "\r\n" : " (INVALID)\r\n");
    } else if (str_eq(cmd, "adc") || str_starts_with(cmd, "adc ")) {
        uint32_t first = 0, last = GPADC_CHANNELS - 1;
        if (cmd[3] == ' ') first = last = parse_dec(cmd + 4);
        for (uint32_t ch = first; ch <= last && ch < GPADC_CHANNELS; ch++) {
            uint16_t raw = gpadc_read_raw(ch);
            int32_t mv = gpadc_raw_to_mv(raw);
            uart_puts("ch"); print_dec(ch); uart_puts(": ");
            if (raw == GPADC_BAD) { uart_puts("timeout\r\n"); continue; }
            uart_puts("raw "); print_dec(raw);
            if (mv >= 0) { uart_puts("  "); print_dec((uint32_t)mv); uart_puts(" mV"); }
            if (ch == GPADC_CH_VBAT && mv >= 0) { uart_puts("  (VBAT ~"); print_dec((uint32_t)mv * 3); uart_puts(" mV)"); }
            uart_puts("\r\n");
        }
    } else if (str_eq(cmd, "date")) {
        rtc_time_t t;
        if (rtc_get(&t) != 0) {
            uart_puts("RTC not set (use: date set YYYY-MM-DD HH:MM:SS)\r\n");
        } else {
            print_time(&t); uart_puts("  (hw weekday "); print_dec(t.wday); uart_puts(")\r\n");
        }
    } else if (str_starts_with(cmd, "date set ")) {
        rtc_time_t t;
        if (parse_datetime(cmd + 9, &t) != 0 || rtc_set(&t) != 0) {
            uart_puts("Invalid date; use: date set YYYY-MM-DD HH:MM:SS (2000-2127)\r\n");
        } else {
            rtc_get(&t); uart_puts("RTC set: "); print_time(&t); uart_puts("\r\n");
        }
    } else if (str_eq(cmd, "alarm")) {
        uart_puts("Alarm: "); uart_puts(rtc_alarm_enabled() ? "armed" : "off");
        uart_puts(", fired "); print_dec(rtc_alarm_count()); uart_puts(" time(s)\r\n");
    } else if (str_eq(cmd, "alarm off")) {
        rtc_alarm_disable();
        uart_puts("Alarm off\r\n");
    } else if (str_starts_with(cmd, "alarm in ")) {
        rtc_time_t t;
        uint32_t secs = parse_dec(cmd + 9);
        if (rtc_get(&t) != 0) {
            uart_puts("RTC not set\r\n");
        } else if (secs == 0 || secs >= 86400) {
            uart_puts("Use: alarm in <1..86399 seconds>\r\n");
        } else {
            rtc_add_seconds(&t, secs);
            if (rtc_set_alarm(&t) != 0) uart_puts("Alarm busy\r\n");
            else { uart_puts("Alarm at "); print_time(&t); uart_puts("\r\n"); }
        }
    } else if (str_eq(cmd, "u2baud")) {
        /* Time 16 bytes leaving the UART2 TX FIFO (loopback on, GPS unaffected) */
        uart2_init_9600();
        volatile uint32_t *u2 = (volatile uint32_t *)UART2_BASE;
        u2[0] |= (1u << 24);
        uint32_t t0 = HW_TIMER_UPTIME;
        for (int i = 0; i < 16; i++) u2[2] = 0x55;
        while (((u2[1] >> 8) & 0x1F) != 16 || (u2[1] & (1u << 14)));  /* TX empty, idle */
        uint32_t dt = HW_TIMER_UPTIME - t0;
        u2[0] &= ~(1u << 24);
        while (u2[1] & 0x3F) (void)u2[2];                                 /* drain RX */
        uart_puts("UART2: 160 bits in "); print_dec(dt); uart_puts(" ticks -> ~");
        print_dec(dt ? (160u * 16384u) / dt : 0); uart_puts(" baud\r\n");
    } else if (str_starts_with(cmd, "clock set ")) {
        uint32_t want = parse_dec(cmd + 10);
        uint32_t got = clock_set_sys_mhz(want);
        if (got) { uart_puts("System clock: "); print_dec(got); uart_puts(" MHz\r\n"); }
        else uart_puts("Unsupported; use 26 39 52 78 89 104 113 125 139 156 178 208 250 312\r\n");
    } else if (str_eq(cmd, "clock")) {
        uart_puts("System clock (registers): "); print_dec(clock_sys_mhz());
        uart_puts(" MHz, PLL "); uart_puts(clock_pll_locked() ? "locked" : "off"); uart_puts("\r\n");
        /*
         * Count iterations of a 3-instruction loop (addiu/bnez/nop) over
         * 1/16 s of the 16384 Hz hardware timer; with the loop in the
         * I-cache, loop instructions per second approximate the core clock.
         */
        uint32_t iters = 4000000u, t0, t1;
        taskENTER_CRITICAL();
        t0 = HW_TIMER_UPTIME;
        __asm__ volatile (
            ".set noreorder\n"
            "1: addiu %0, %0, -1\n"
            "   bnez  %0, 1b\n"
            "   nop\n"
            ".set reorder\n"
            : "+r"(iters));
        t1 = HW_TIMER_UPTIME;
        taskEXIT_CRITICAL();
        uint32_t dt = t1 - t0;               /* 16384 Hz ticks */
        /* instructions/s = 3 * 4e6 * 16384 / dt, scaled to kHz to fit 32 bits */
        uint32_t khz = (uint32_t)((3u * 4000u * 16384u) / dt);
        uart_puts("Loop: 4000000 x 3 instr in "); print_dec(dt);
        uart_puts(" timer ticks -> ~"); print_dec(khz / 1000); uart_puts(".");
        print_dec((khz % 1000) / 100); uart_puts(" MHz effective\r\n");
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
        uint32_t ms = 4000;
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
    if (s_cfg_loaded == 0 && s_cfg.note[0]) { uart_puts("Note: "); uart_puts(s_cfg.note); uart_puts("\r\n"); }
    uart_puts("Type 'help' for commands.\r\n\r\n");
    uart_puts("a9> ");

    uint32_t alarms_seen = rtc_alarm_count();

    for (;;) {
        if (rtc_alarm_count() != alarms_seen) {
            alarms_seen = rtc_alarm_count();
            rtc_time_t now;
            uart_puts("\r\n*** RTC alarm");
            if (rtc_get(&now) == 0) { uart_puts(" at "); print_time(&now); }
            uart_puts(" ***\r\na9> ");
        }
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

    /* Persistent settings (defaults: 312 MHz, 4 s watchdog) */
    s_cfg_loaded = settings_load(&s_cfg);

    /* The ROM leaves the PLL off and the CPU on the 26 MHz crystal */
    if (!clock_set_sys_mhz(s_cfg.clock_mhz))
        clock_set_sys_mhz(312);

    send_host_event(0xBA5E0001);

    memset(&s_hb_tcb, 0, sizeof(s_hb_tcb));
    memset(&s_wk_tcb, 0, sizeof(s_wk_tcb));
    memset(&s_cli_tcb, 0, sizeof(s_cli_tcb));

    /* Create Heartbeat Task (Priority 1) */
    xTaskCreateStatic(vHeartbeatTask, "Heartbeat", 256, NULL, 1, s_hb_stack, &s_hb_tcb);

    /* Create Worker Task (Priority 1) */
    xTaskCreateStatic(vWorkerTask, "Worker", 256, NULL, 1, s_wk_stack, &s_wk_tcb);

    gpadc_init();

    /* GPS NMEA reader (idle until "gps on") */
    gps_init_task();

    /* Create Interactive Serial Shell Task (Priority 2) */
    xTaskCreateStatic(vShellTask, "Shell", 512, NULL, 2, s_cli_stack, &s_cli_tcb);

    if (s_cfg.wdt_ms)
        wdt_start(s_cfg.wdt_ms);

    /* Start FreeRTOS Scheduler with Hardware Preemption */
    send_host_event(0xBA5E0050);

    vTaskStartScheduler();

    /* Should never reach here */
    while (1) {
        send_host_event(0xDEADDEAD);
    }

    return 0;
}

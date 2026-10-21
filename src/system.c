/*
 * RDA8955 reset cause, soft reset and watchdog
 */

#include "system.h"

#define REG_SYS_CTRL_BASE      0xA1A00000
#define REG_DBG                (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x00))
#define REG_SYS_RST_SET        (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x04))
#define REG_RESET_CAUSE        (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0xA0))
#define REG_DBG_UNLOCK         0x00A50001u   /* vendor unlock value for protected regs */
#define SYS_RST_SOFT           0x80000000u

#define REG_SYS_IRQ_SC         (*(volatile uint32_t *)0xA1A01014)

/* ROM's copy of Reset_Cause, saved before boot (g_bootResetCause, as read by the
 * vendor hal_SysGetResetCause); the hardware register no longer holds it. */
#define ROM_BOOT_RESET_CAUSE   (*(volatile uint32_t *)0xA1C001A4)

#define REG_TIMER_BASE         0xA1A02000
#define REG_WDT_CTRL           (*(volatile uint32_t *)(REG_TIMER_BASE + 0x08))
#define REG_WDT_LOADVAL        (*(volatile uint32_t *)(REG_TIMER_BASE + 0x0C))
#define WDT_START              (1u << 0)
#define WDT_ENABLED            (1u << 3)
#define WDT_STOP               (1u << 4)
#define WDT_RELOAD             (1u << 16)
#define WDT_LOADVAL_MAX        0x00FFFFFFu   /* 32 kHz clocks */

#define REG_SYS_IRQ_WAKEUP_MASK (*(volatile uint32_t *)0xA1A01018)
#define REG_SYS_IRQ_CPU_SLEEP   (*(volatile uint32_t *)0xA1A0101C)
#define WAKE_SOURCES            ((1u << 7) | (1u << 10) | (1u << 11))   /* GPIO, OS timer, calendar */
#define HW_TIMER_UPTIME         (*(volatile uint32_t *)0xA1A02014)   /* 16384 Hz */

static uint32_t s_reset_cause;
static volatile int      s_sleep_on = 1;
static volatile uint32_t s_slept, s_naps, s_stats_t0;
static uint32_t s_wdt_timeout_ms;

void sys_init(void)
{
    s_reset_cause = ROM_BOOT_RESET_CAUSE &
        (RESET_CAUSE_WATCHDOG | RESET_CAUSE_SOFT | RESET_CAUSE_HOST_DEBUG | RESET_CAUSE_ALARM);
    /* Writing 1 to bit 0 clears the watchdog / soft / host-debug causes. */
    REG_DBG = REG_DBG_UNLOCK;
    REG_RESET_CAUSE = RESET_CAUSE_WATCHDOG;
}

uint32_t sys_reset_cause(void)
{
    return s_reset_cause;
}

const char *sys_reset_cause_str(uint32_t cause)
{
    /* Same priority as the vendor hal_SysGetResetCause */
    if (cause & RESET_CAUSE_WATCHDOG)   return "watchdog";
    if (cause & RESET_CAUSE_HOST_DEBUG) return "host debug";
    if (cause & RESET_CAUSE_ALARM)      return "RTC alarm";
    if (cause & RESET_CAUSE_SOFT)       return "soft reset";
    return "power-on";
}

void sys_soft_reset(void)
{
    (void)REG_SYS_IRQ_SC;          /* reading SC masks all interrupts */
    __asm__ volatile ("mtc0 $zero, $12\n\tnop" ::: "memory");
    REG_DBG = REG_DBG_UNLOCK;
    REG_SYS_RST_SET = SYS_RST_SOFT;
    for (;;) {
        __asm__ volatile ("nop");
    }
}

void wdt_start(uint32_t timeout_ms)
{
    /* 0xFFFFFF clocks at 32 kHz = 511 s; split to stay within 32-bit math */
    if (timeout_ms > 511000u) timeout_ms = 511000u;
    uint32_t ticks = (timeout_ms / 1000u) * 32768u + ((timeout_ms % 1000u) * 32768u) / 1000u;
    if (ticks == 0) ticks = 1;
    if (ticks > WDT_LOADVAL_MAX) ticks = WDT_LOADVAL_MAX;
    s_wdt_timeout_ms = timeout_ms;

    REG_WDT_LOADVAL = ticks;
    REG_WDT_CTRL = (REG_WDT_CTRL & WDT_ENABLED) ? WDT_RELOAD : WDT_START;
}

void wdt_kick(void)
{
    if (REG_WDT_CTRL & WDT_ENABLED)
        REG_WDT_CTRL = WDT_RELOAD;
}

void wdt_stop(void)
{
    REG_WDT_CTRL = WDT_STOP;
    REG_WDT_LOADVAL = 0;
    s_wdt_timeout_ms = 0;
}

int wdt_running(void)
{
    return (REG_WDT_CTRL & WDT_ENABLED) != 0;
}

uint32_t wdt_timeout_ms(void)
{
    return s_wdt_timeout_ms;
}

/*
 * The clock stops right after the write; keep the instructions around it in
 * SRAM so nothing is fetched from flash across the gate (as the vendor does).
 */
__attribute__((section(".ramfunc"), noinline))
static void ram_cpu_sleep(void)
{
    REG_SYS_IRQ_CPU_SLEEP = 1;
    (void)REG_SYS_IRQ_CPU_SLEEP;
    __asm__ volatile ("nop\n\tnop\n\tnop\n\tnop" ::: "memory");
}

void sys_sleep_init(void)
{
    REG_SYS_IRQ_WAKEUP_MASK |= WAKE_SOURCES;
    s_stats_t0 = HW_TIMER_UPTIME;
}

void sys_idle_sleep(void)
{
    if (!s_sleep_on)
        return;
    uint32_t status;
    /* Interrupts off across the gate: a pending wake source restarts the clock,
     * then restoring Status takes the interrupt. */
    __asm__ volatile ("mfc0 %0, $12\n\tnop" : "=r"(status));
    __asm__ volatile ("mtc0 %0, $12\n\tnop" :: "r"(status & ~1u) : "memory");
    uint32_t t0 = HW_TIMER_UPTIME;
    ram_cpu_sleep();
    s_slept += HW_TIMER_UPTIME - t0;
    s_naps++;
    __asm__ volatile ("mtc0 %0, $12\n\tnop" :: "r"(status) : "memory");
}

void sys_sleep_enable(int on)
{
    s_sleep_on = on;
}

int sys_sleep_enabled(void)
{
    return s_sleep_on;
}

void sys_sleep_stats(uint32_t *asleep, uint32_t *elapsed, uint32_t *naps)
{
    *asleep = s_slept;
    *elapsed = HW_TIMER_UPTIME - s_stats_t0;
    *naps = s_naps;
}

void sys_sleep_stats_reset(void)
{
    s_slept = 0;
    s_naps = 0;
    s_stats_t0 = HW_TIMER_UPTIME;
}

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

static uint32_t s_reset_cause;
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

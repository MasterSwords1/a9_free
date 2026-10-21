#ifndef SYSTEM_H
#define SYSTEM_H

#include <stdint.h>

/*
 * RDA8955 reset and watchdog control, following the vendor hal_SysSoftReset,
 * hal_SysGetResetCause and hal_TimWatchDog* (sys_ctrl / timer in 8955_hard.xml).
 */

/* sys_ctrl.Reset_Cause bits */
#define RESET_CAUSE_WATCHDOG    (1u << 0)
#define RESET_CAUSE_SOFT        (1u << 4)
#define RESET_CAUSE_HOST_DEBUG  (1u << 8)
#define RESET_CAUSE_ALARM       (1u << 12)

/* Reset cause latched at boot (read once by sys_init). */
uint32_t    sys_reset_cause(void);
const char *sys_reset_cause_str(uint32_t cause);
void        sys_init(void);

/* Whole-chip soft reset; does not return. */
void        sys_soft_reset(void) __attribute__((noreturn));

/* Watchdog: reset the chip unless kicked within timeout_ms. */
void        wdt_start(uint32_t timeout_ms);
void        wdt_kick(void);
void        wdt_stop(void);
int         wdt_running(void);
uint32_t    wdt_timeout_ms(void);

#endif /* SYSTEM_H */

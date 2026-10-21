#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/*
 * RDA8955 system clock: PLL bring-up and system frequency selection, following
 * the vendor boot_SysSetupPLL / hal_SysSetupSystemClock (sys_ctrl Pll_Ctrl,
 * Sel_Clock, Cfg_Clk_Sys in 8955_hard.xml). Only the system/CPU clock is moved
 * to the PLL; SPI flash and the memory bridge stay on the slow clock.
 */

/* Switch the system clock to `mhz` (one of the Cfg_Clk_Sys steps, 26..312).
 * Returns the frequency actually selected in MHz, or 0 if unsupported. */
uint32_t clock_set_sys_mhz(uint32_t mhz);
/* Nominal system clock in MHz from the current register state. */
uint32_t clock_sys_mhz(void);
int      clock_pll_locked(void);

#endif /* CLOCK_H */

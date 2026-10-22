#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>

/*
 * RDA8955 system clock: PLL bring-up and system frequency selection, following
 * the vendor boot_SysSetupPLL / hal_SysSetupSystemClock (sys_ctrl Pll_Ctrl,
 * Sel_Clock, Cfg_Clk_Sys in 8955_hard.xml). The system, memory bridge and SPI
 * flash clocks move to the PLL together, as boot_SysSetupPLL does.
 *
 * 312 MHz is not reliable at the boot core voltage: the vendor raises VCORE
 * (pmd_SetCoreVoltage*) before high clocks and we do not yet, and at 312 MHz the
 * firmware takes random address/bus/reserved-instruction faults within seconds.
 * 26..250 MHz ran clean in testing; the default is 250 MHz.
 */
#define CLOCK_SAFE_MAX_MHZ  250

/* Switch the system clock to `mhz` (one of the Cfg_Clk_Sys steps, 26..312).
 * Returns the frequency actually selected in MHz, or 0 if unsupported. */
uint32_t clock_set_sys_mhz(uint32_t mhz);
/* Nominal system clock in MHz from the current register state. */
uint32_t clock_sys_mhz(void);
int      clock_pll_locked(void);

#endif /* CLOCK_H */

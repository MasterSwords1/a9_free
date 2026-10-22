/*
 * RDA8955 PLL and system clock control
 */

#include "clock.h"
#include "FreeRTOS.h"
#include "task.h"

#define REG_SYS_CTRL_BASE   0xA1A00000
#define REG_DBG             (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x00))
#define REG_PLL_CTRL        (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x44))
#define REG_SEL_CLOCK       (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x48))
#define REG_CFG_CLK_SYS     (*(volatile uint32_t *)(REG_SYS_CTRL_BASE + 0x4C))

#define REG_DBG_UNLOCK      0x00A50001u
#define REG_DBG_LOCK        0x00A50000u

#define PLL_ENABLE          (1u << 0)
#define PLL_LOCK_NO_RESET   (1u << 4)
#define PLL_CLK_FAST_ENABLE (1u << 12)

/* ISPI (PMU bus) clock divider, ISPI cfg[25:16]; boot_UpdateIspiDivider keeps
 * the ISPI input at or below 26 MHz: n = ceil(sysclk / 26 MHz) - 1. */
#define REG_ISPI_CFG        (*(volatile uint32_t *)0xA1A1300C)
#define ISPI_DIV_KEEP_MASK  0xEC00FFFFu

#define SEL_SYS_SLOW        (1u << 1)
#define SEL_PLL_LOCKED      (1u << 8)
#define SEL_SPIFLASH_SLOW   (1u << 10)
#define SEL_MEM_BRIDGE_SLOW (1u << 11)

/* Cfg_Clk_Sys.Sys_Freq index -> MHz */
static const uint16_t s_sys_freq_mhz[14] = {
    26, 39, 52, 78, 89, 104, 113, 125, 139, 156, 178, 208, 250, 312
};

int clock_pll_locked(void)
{
    return (REG_SEL_CLOCK & SEL_PLL_LOCKED) != 0;
}

uint32_t clock_sys_mhz(void)
{
    if (REG_SEL_CLOCK & SEL_SYS_SLOW)
        return 26;
    uint32_t idx = REG_CFG_CLK_SYS & 0xF;
    return idx < 14 ? s_sys_freq_mhz[idx] : 0;
}

static void ispi_update_divider(uint32_t mhz)
{
    uint32_t n = (mhz + 25) / 26;
    n = n ? n - 1 : 0;
    REG_ISPI_CFG = (REG_ISPI_CFG & ISPI_DIV_KEEP_MASK) | ((n & 0x3FF) << 16);
}

/*
 * Move the system, memory bridge and SPI flash clocks onto the PLL together, as
 * the vendor's boot_SysSetupPLL does (it clears all of Sel_Clock), instead of
 * leaving the memory bridge and flash on the 26 MHz slow clock. The domains keep
 * their own dividers (Cfg_Clk_Mem_Bridge 78 MHz, Cfg_Clk_SpiFlash 52 MHz with the
 * ROM's SPI clk_divider 8); flash reads get about twice as fast.
 *
 * Runs from SRAM: it retimes the path code is fetched through. No calls, no
 * .rodata.
 */
__attribute__((section(".ramfunc"), noinline))
static void ram_select_pll(uint32_t idx)
{
    REG_CFG_CLK_SYS = idx;
    REG_SEL_CLOCK = REG_SEL_CLOCK & ~(SEL_SYS_SLOW | SEL_SPIFLASH_SLOW | SEL_MEM_BRIDGE_SLOW);
    for (volatile int d = 0; d < 10; d++);
}

uint32_t clock_set_sys_mhz(uint32_t mhz)
{
    int idx = -1;
    for (int i = 0; i < 14; i++) {
        if (s_sys_freq_mhz[i] == mhz) idx = i;
    }
    if (idx < 0)
        return 0;

    taskENTER_CRITICAL();
    REG_DBG = REG_DBG_UNLOCK;

    /* Raise the ISPI divider before the clock goes up; lower it after it goes down */
    uint32_t old_mhz = clock_sys_mhz();
    if (mhz > old_mhz)
        ispi_update_divider(mhz);

    if (idx == 0) {
        /* 26 MHz: run the system from the slow (crystal) clock */
        REG_SEL_CLOCK = REG_SEL_CLOCK | SEL_SYS_SLOW;
    } else {
        if (!(REG_SEL_CLOCK & SEL_PLL_LOCKED)) {
            REG_PLL_CTRL = 0;
            REG_PLL_CTRL = PLL_ENABLE | PLL_LOCK_NO_RESET;
            while (!(REG_SEL_CLOCK & SEL_PLL_LOCKED));
            REG_PLL_CTRL |= PLL_CLK_FAST_ENABLE;
        }
        ram_select_pll((uint32_t)idx);
    }
    for (volatile int d = 0; d < 10; d++);
    if (mhz <= old_mhz)
        ispi_update_divider(mhz);

    REG_DBG = REG_DBG_LOCK;
    taskEXIT_CRITICAL();
    return clock_sys_mhz();
}

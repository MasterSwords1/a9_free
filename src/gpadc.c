/*
 * RDA8955 GPADC driver
 */

#include "gpadc.h"
#include "pmu.h"
#include "FreeRTOS.h"
#include "task.h"

#define PMU_EFUSE_CTRL       0x21   /* efuse_and_otp_setting_1 */
#define PMU_EFUSE_DATA       0x3F
#define PMU_GPADC_CTRL       0x3B   /* misc_3BH */

#define EFUSE_SENSE          0xC000u   /* efuse_sense_dr | efuse_sense_reg */
#define EFUSE_SEL_MASK       0x3000u   /* efuse_sel_word_reg */

#define GPADC_ENABLE         0x0001u   /* gpadc_enable */
#define GPADC_CH_MASK        0x01FEu   /* gpadc_en[7:0] (one-hot, bit ch+1) */
#define GPADC_CLK_DIV_EN     0x1000u
#define GPADC_START_TIME_MASK 0xE000u  /* gpadc_start_time_sel: 0 = fastest */

#define REG_GPADC_DATA       (*(volatile uint32_t *)0xA1A06014)   /* pwm.GPADC_DATA */
#define GPADC_VALID          (1u << 10)

#define HW_TIMER_UPTIME      (*(volatile uint32_t *)0xA1A02014)   /* 16384 Hz */

/* Vendor two-point calibration references (ADC input voltage, uV) */
#define CAL_LOW_UV           1133000
#define CAL_HIGH_UV          1400000

static uint16_t s_cal_low, s_cal_high;
static int      s_calibrated;

static void delay_ticks(uint32_t t)
{
    uint32_t t0 = HW_TIMER_UPTIME;
    while (HW_TIMER_UPTIME - t0 < t);
}

/* eFuse word `n` (vendor helper used by pmd_PmuInitCharger) */
static uint16_t efuse_read(uint32_t n)
{
    uint16_t v = (uint16_t)((pmu_read(PMU_EFUSE_CTRL) & ~EFUSE_SEL_MASK) | ((n & 3) << 12));
    pmu_write(PMU_EFUSE_CTRL, v);
    pmu_write(PMU_EFUSE_CTRL, v | EFUSE_SENSE);
    delay_ticks(32);                                  /* ~2 ms sense time */
    pmu_write(PMU_EFUSE_CTRL, v & (uint16_t)~EFUSE_SENSE);
    return pmu_read(PMU_EFUSE_DATA);
}

/* bits [13:10] select one extra bit to OR in (1 << (n - 1)); keep 10 bits */
static uint16_t efuse_decode(uint16_t v)
{
    uint32_t e = (v & 0x3C00u) >> 10;
    uint32_t extra = e ? (1u << (e - 1)) : 0;
    return (uint16_t)((v | extra) & 0x3FF);
}

int gpadc_init(void)
{
    s_cal_low  = efuse_decode(efuse_read(1));
    s_cal_high = efuse_decode(efuse_read(2));
    s_calibrated = s_cal_low < s_cal_high;
    return s_calibrated ? 0 : -1;
}

void gpadc_calib(uint16_t *low, uint16_t *high)
{
    *low = s_cal_low;
    *high = s_cal_high;
}

/* Wait for the data register to change to a new valid conversion (~1 ms each
 * at the fastest interval); 0 on timeout. */
static uint32_t next_conversion(uint32_t prev)
{
    uint32_t t0 = HW_TIMER_UPTIME;
    while (HW_TIMER_UPTIME - t0 < 820) {          /* ~50 ms */
        uint32_t d = REG_GPADC_DATA;
        if ((d & GPADC_VALID) && d != prev)
            return d;
    }
    return REG_GPADC_DATA & GPADC_VALID ? REG_GPADC_DATA : 0;
}

uint16_t gpadc_read_raw(uint32_t ch)
{
    if (ch >= GPADC_CHANNELS)
        return GPADC_BAD;

    /*
     * The block converts the selected channel periodically (gpadc_start_time_sel;
     * the boot value 4 is one conversion per ~250 ms) and the data register holds
     * the last result, possibly of the previously selected channel. Select the
     * fastest interval, drop the first result after the switch, average 8, then
     * restore the previous setting.
     */
    uint16_t saved = pmu_read(PMU_GPADC_CTRL);
    uint16_t ctrl = (uint16_t)((saved & ~(GPADC_CH_MASK | GPADC_ENABLE | GPADC_START_TIME_MASK)) |
                               GPADC_CLK_DIV_EN | GPADC_ENABLE | (2u << ch));
    pmu_write(PMU_GPADC_CTRL, ctrl);
    delay_ticks(4);

    uint32_t d = next_conversion(REG_GPADC_DATA);    /* first result: discard */
    uint32_t sum = 0;
    int n = 0;
    for (; d && n < 8; n++) {
        d = next_conversion(d);
        if (!d) break;
        sum += d & 0x3FF;
    }
    pmu_write(PMU_GPADC_CTRL, saved);
    if (n < 8)
        return GPADC_BAD;
    return (uint16_t)((sum + 4) / 8);
}

int32_t gpadc_raw_to_mv(uint16_t raw)
{
    if (!s_calibrated || raw == GPADC_BAD)
        return -1;
    int32_t lo = s_cal_low, hi = s_cal_high;
    int32_t slope_uv = (CAL_HIGH_UV - CAL_LOW_UV) / (hi - lo);            /* uV per LSB */
    int32_t offset_mv = (hi * CAL_LOW_UV - lo * CAL_HIGH_UV) / (1000 * (hi - lo));
    return (int32_t)raw * slope_uv / 1000 + offset_mv;
}

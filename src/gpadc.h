#ifndef GPADC_H
#define GPADC_H

#include <stdint.h>

/*
 * RDA8955 GPADC (10-bit general-purpose ADC), following the vendor
 * hal_AnaGpadcGetRaw / Gpadc2Volt and pmd_GpadcEnableChannel / PmuInitCharger.
 * Channel select is in PMU misc_3BH, the result in pwm.GPADC_DATA.
 * Channel 7 is the battery (VBAT through a 1/3 divider on the vendor board).
 */
#define GPADC_CHANNELS     8
#define GPADC_CH_VBAT      7
#define GPADC_BAD          0xFFFF

int      gpadc_init(void);                 /* loads eFuse calibration; 0 if calibrated */
uint16_t gpadc_read_raw(uint32_t ch);      /* 0..1023, GPADC_BAD on timeout */
int32_t  gpadc_raw_to_mv(uint16_t raw);    /* ADC input in mV, -1 if uncalibrated */
void     gpadc_calib(uint16_t *low, uint16_t *high);   /* raw @1133 mV, @1400 mV */

#endif /* GPADC_H */

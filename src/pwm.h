#ifndef PWM_H
#define PWM_H

#include <stdint.h>

/*
 * PWL1 pulse-width light output (LFSR-dithered, 0..255) on pad GPIO_2
 * (function 3), following the vendor hal_PwlSelLevel. The A9G LED pads have
 * no PWM function, so this drives GPIO_2 on the header.
 */
void pwl1_set(uint32_t level);     /* 0 = off (forced low), 255 = forced high */
void pwl1_release(void);           /* pad back to GPIO */

#endif /* PWM_H */

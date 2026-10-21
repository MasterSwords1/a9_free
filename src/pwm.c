/*
 * RDA8955 PWL1 output
 */

#include "pwm.h"

#define REG_PWL1_CONFIG     (*(volatile uint32_t *)0xA1A0600C)
#define PWL1_EN_H           (1u << 16)
#define PWL1_FORCE_L        (1u << 17)
#define PWL1_FORCE_H        (1u << 18)
#define PWL1_SET_OE         (1u << 20)
#define PWL1_CLR_OE         (1u << 21)

#define REG_PAD_GPIO_2      (*(volatile uint32_t *)0xA1A2A098)
#define PAD_FN_PWL1         3
#define PAD_FN_GPIO         0

void pwl1_set(uint32_t level)
{
    if (level == 0)
        REG_PWL1_CONFIG = PWL1_FORCE_L | PWL1_SET_OE;
    else if (level >= 255)
        REG_PWL1_CONFIG = PWL1_FORCE_H | PWL1_SET_OE;
    else
        REG_PWL1_CONFIG = PWL1_EN_H | PWL1_SET_OE | (level & 0xFF);
    REG_PAD_GPIO_2 = (REG_PAD_GPIO_2 & ~0xFu) | PAD_FN_PWL1;
}

void pwl1_release(void)
{
    REG_PWL1_CONFIG = PWL1_CLR_OE;
    REG_PAD_GPIO_2 = (REG_PAD_GPIO_2 & ~0xFu) | PAD_FN_GPIO;
}

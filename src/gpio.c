/*
 * RDA8955 GPIO and GPIO interrupts
 */

#include "gpio.h"

#define GPIO_BASE        0xA1A03000
#define R(off)           (*(volatile uint32_t *)(GPIO_BASE + (off)))

/* low bank (0..31) / high bank (32..34) register offsets */
#define OEN_SET_OUT(h)   ((h) ? 0x3C : 0x04)
#define OEN_SET_IN(h)    ((h) ? 0x40 : 0x08)
#define VAL(h)           ((h) ? 0x44 : 0x0C)
#define SET(h)           ((h) ? 0x48 : 0x10)
#define CLR(h)           ((h) ? 0x4C : 0x14)
#define INT_R_SET(h)     ((h) ? 0x1C : 0x18)
#define INT_R_CLR(h)     ((h) ? 0x54 : 0x50)
#define INT_F_SET(h)     ((h) ? 0x5C : 0x58)
#define INT_F_CLR(h)     ((h) ? 0x64 : 0x60)
#define DBN_SET(h)       ((h) ? 0x6C : 0x68)
#define DBN_CLR(h)       ((h) ? 0x74 : 0x70)
#define MODE_SET(h)      ((h) ? 0x7C : 0x78)
#define MODE_CLR(h)      ((h) ? 0x84 : 0x80)
#define INT_STATUS_L     0x24
#define INT_STATUS_H     0x88
#define INT_CLR_L        0x20
#define INT_CLR_H        0x8C

#define REG_SYS_IRQ_MASK_SET (*(volatile uint32_t *)0xA1A01008)
#define SYS_IRQ_GPIO         (1u << 7)

static volatile uint32_t s_irq_count[GPIO_PINS];

/*
 * IOMUX pad of each GPIO and its GPIO function (8955_hard.xml iomux):
 * GPIO_0..7 = pads 0x90..0xAC fn 0, GPIO_25..29 = KEYIN_0..4 (0xB0..0xC0) fn 1,
 * GPIO_30..34 = KEYOUT_0..4 (0xC4..0xD4) fn 1. Other pins are not muxed here.
 */
#define IOMUX_BASE       0xA1A2A000

int gpio_mux(uint32_t pin)
{
    uint32_t off, fn;
    if (pin <= 7)                      { off = 0x90 + 4 * pin;        fn = 0; }
    else if (pin >= 25 && pin <= 34)   { off = 0xB0 + 4 * (pin - 25); fn = 1; }
    else return -1;
    volatile uint32_t *pad = (volatile uint32_t *)(IOMUX_BASE + off);
    *pad = (*pad & ~0xFu) | fn;
    return 0;
}

static inline int hi(uint32_t pin) { return pin >= 32; }
static inline uint32_t bit(uint32_t pin) { return 1u << (pin & 31); }

void gpio_set_output(uint32_t pin, int level)
{
    gpio_write(pin, level);
    R(OEN_SET_OUT(hi(pin))) = bit(pin);
}

void gpio_set_input(uint32_t pin)
{
    R(OEN_SET_IN(hi(pin))) = bit(pin);
}

int gpio_read(uint32_t pin)
{
    return (R(VAL(hi(pin))) & bit(pin)) != 0;
}

void gpio_write(uint32_t pin, int level)
{
    R(level ? SET(hi(pin)) : CLR(hi(pin))) = bit(pin);
}

void gpio_irq_config(uint32_t pin, int rising, int falling, int debounce, int level)
{
    int h = hi(pin);
    uint32_t b = bit(pin);
    R(h ? INT_CLR_H : INT_CLR_L) = b;              /* drop a stale cause */
    R(rising   ? INT_R_SET(h) : INT_R_CLR(h)) = b;
    R(falling  ? INT_F_SET(h) : INT_F_CLR(h)) = b;
    R(debounce ? DBN_SET(h)   : DBN_CLR(h))   = b;
    R(level    ? MODE_SET(h)  : MODE_CLR(h))  = b;
    if (rising || falling)
        REG_SYS_IRQ_MASK_SET = SYS_IRQ_GPIO;
}

uint32_t gpio_irq_count(uint32_t pin)
{
    return pin < GPIO_PINS ? s_irq_count[pin] : 0;
}

void gpio_irq_reset(uint32_t pin)
{
    if (pin < GPIO_PINS) s_irq_count[pin] = 0;
}

void gpio_irq(void)
{
    uint32_t lo = R(INT_STATUS_L), hi_st = R(INT_STATUS_H);
    R(INT_CLR_L) = lo;
    R(INT_CLR_H) = hi_st;
    for (uint32_t p = 0; p < 32; p++)
        if (lo & (1u << p)) s_irq_count[p]++;
    for (uint32_t p = 32; p < GPIO_PINS; p++)
        if (hi_st & (1u << (p - 32))) s_irq_count[p]++;
}

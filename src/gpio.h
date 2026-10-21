#ifndef GPIO_H
#define GPIO_H

#include <stdint.h>

/*
 * RDA8955 GPIO (0..34) with edge/level interrupts, following the vendor
 * hal_GpioSetOut / SetIn / Get / IrqSetMask / IrqHandler. Pins 0..31 use the
 * _l registers, 32..34 the _h ones. The pad must be muxed to its GPIO function.
 */
#define GPIO_PINS  35

/* Route the pin's pad to its GPIO function; -1 if this pin's pad is not known. */
int      gpio_mux(uint32_t pin);
void     gpio_set_output(uint32_t pin, int level);
void     gpio_set_input(uint32_t pin);
int      gpio_read(uint32_t pin);              /* pad level */
void     gpio_write(uint32_t pin, int level);
/* Enable/disable interrupts on a pin. level=1 selects level mode, else edge. */
void     gpio_irq_config(uint32_t pin, int rising, int falling, int debounce, int level);
uint32_t gpio_irq_count(uint32_t pin);
void     gpio_irq_reset(uint32_t pin);
/* Called by the system IRQ dispatcher for SYS_IRQ_GPIO. */
void     gpio_irq(void);

#endif /* GPIO_H */

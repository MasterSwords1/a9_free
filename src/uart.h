#ifndef UART_H
#define UART_H

#include <stdint.h>
#include <stddef.h>

/*
 * RDA8955 UART Driver
 * Supports DEBUG_UART (USB-Serial Console on /dev/ttyUSB0) and UART1 (GPIO_0/GPIO_1)
 */

#define DEBUG_UART_BASE   0xA1A14000

/*
 * Shell input mailbox. While the debug host is enabled it owns the debug UART's
 * receive side, so the host delivers keystrokes by SXS memory writes instead:
 * word 0 = head (host), word 1 = tail (firmware), words 2.. = one char per slot.
 * Lives above the initial stack top (0x81C07F00), accessed uncached.
 */
#define SHELL_RX_ADDR     0xA1C07F00
#define SHELL_RX_SLOTS    32
#define UART1_BASE        0xA1A15000

void uart_init(void);
void uart_putc(char c);
void uart_puts(const char *s);
int  uart_getc(void);
int  uart_has_data(void);

#endif /* UART_H */

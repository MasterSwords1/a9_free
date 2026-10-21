/*
 * RDA8955 UART Driver Implementation
 * Freestanding, 100% open-source direct register driver
 */

#include "uart.h"

#define REG_DEBUG_UART_CTRL   (*(volatile uint32_t *)(DEBUG_UART_BASE + 0x00))
#define REG_DEBUG_UART_STATUS (*(volatile uint32_t *)(DEBUG_UART_BASE + 0x04))
#define REG_DEBUG_UART_DATA   (*(volatile uint32_t *)(DEBUG_UART_BASE + 0x08))

#define SHELL_RX              ((volatile uint32_t *)SHELL_RX_ADDR)

void uart_init(void)
{
    /*
     * The DEBUG_UART is shared with the debug host (SXS) protocol. Its reset
     * value already carries the host framing (SW flow control, BackSlash), so
     * only make sure the block is enabled for 8-bit data and the TX driver is on.
     */
    uint32_t ctrl = REG_DEBUG_UART_CTRL;
    ctrl |= (1 << 1) | (1 << 0);
    ctrl &= ~(1u << 26);
    REG_DEBUG_UART_CTRL = ctrl;

    SHELL_RX[0] = 0;
    SHELL_RX[1] = 0;
}

void uart_putc(char c)
{
    if (c == '\n') {
        uart_putc('\r');
    }

    /* Wait while TX FIFO is full (level >= 16) */
    while (((REG_DEBUG_UART_STATUS >> 8) & 0x1F) >= 16) {
        __asm__ volatile ("nop");
    }

    REG_DEBUG_UART_DATA = (uint8_t)c;
}

void uart_puts(const char *s)
{
    while (*s) {
        uart_putc(*s++);
    }
}

int uart_has_data(void)
{
    /* Bits 4:0 is RX FIFO level */
    return (REG_DEBUG_UART_STATUS & 0x1F) > 0;
}

int uart_getc(void)
{
    if (uart_has_data()) {
        return (int)(REG_DEBUG_UART_DATA & 0xFF);
    }

    uint32_t tail = SHELL_RX[1];
    if (SHELL_RX[0] != tail) {
        int c = (int)(SHELL_RX[2 + (tail % SHELL_RX_SLOTS)] & 0xFF);
        SHELL_RX[1] = tail + 1;
        return c;
    }
    return -1;
}

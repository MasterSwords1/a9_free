/*
 * RDA8955 UART2 driver (receive-only use for the GPS NMEA stream)
 */

#include "uart2.h"

#define REG_UART2_CTRL      (*(volatile uint32_t *)(UART2_BASE + 0x00))
#define REG_UART2_STATUS    (*(volatile uint32_t *)(UART2_BASE + 0x04))
#define REG_UART2_DATA      (*(volatile uint32_t *)(UART2_BASE + 0x08))
#define REG_UART2_IRQ_MASK  (*(volatile uint32_t *)(UART2_BASE + 0x0C))
#define REG_UART2_CMD_SET   (*(volatile uint32_t *)(UART2_BASE + 0x18))
#define REG_UART2_CMD_CLR   (*(volatile uint32_t *)(UART2_BASE + 0x1C))

/* sys_ctrl Cfg_Clk_Uart[2]: baud = 52 MHz * Num / Denom / 4 */
#define REG_CFG_CLK_UART2   (*(volatile uint32_t *)0xA1A00070)
#define UART_DIV_9600       0x00152801u   /* Num 1, Denom 1354 (vendor table) */

/* IOMUX: UART2 RXD/TXD are function 1 of pads GPIO_4 / GPIO_5 */
#define REG_PAD_GPIO_4      (*(volatile uint32_t *)0xA1A2A0A0)
#define REG_PAD_GPIO_5      (*(volatile uint32_t *)0xA1A2A0A4)

void uart2_init_9600(void)
{
    REG_UART2_CTRL = 0;
    REG_PAD_GPIO_4 = 1;
    REG_PAD_GPIO_5 = 1;
    REG_CFG_CLK_UART2 = UART_DIV_9600;
    REG_UART2_CMD_CLR = 0x3F;
    REG_UART2_CMD_SET = 0xC0;
    REG_UART2_IRQ_MASK = 0;
    /* Enable, 8 data bits, 1 stop bit, no parity, BCLK = SCLK / 4 */
    REG_UART2_CTRL = (1 << 1) | (1 << 0);
}

int uart2_getc(void)
{
    if (REG_UART2_STATUS & 0x3F)
        return (int)(REG_UART2_DATA & 0xFF);
    return -1;
}

uint32_t uart2_status(void)
{
    return REG_UART2_STATUS;
}

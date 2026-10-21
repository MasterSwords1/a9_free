/*
 * RDA8955 PMU (ISPI) register access
 */

#include "pmu.h"
#include "FreeRTOS.h"
#include "task.h"

#define ISPI_BASE        0xA1A13000
#define REG_ISPI_STATUS  (*(volatile uint32_t *)(ISPI_BASE + 0x04))
#define REG_ISPI_FIFO    (*(volatile uint32_t *)(ISPI_BASE + 0x08))

#define ISPI_TX_SPACE(s) (((s) >> 16) & 0x1F)
#define ISPI_RX_LEVEL(s) (((s) >> 24) & 0x1F)
#define ISPI_BUSY        0x01
#define ISPI_READ        0x82000000u
#define ISPI_WORD(reg)   ((((uint32_t)(reg) & 0x1FF) << 16))  /* cs 0 */

static void ispi_wait_idle(void)
{
    uint32_t s;
    do {
        s = REG_ISPI_STATUS;
    } while ((s & ISPI_BUSY) || ISPI_TX_SPACE(s) != 16);
}

uint16_t pmu_read(uint16_t reg)
{
    taskENTER_CRITICAL();
    while (ISPI_TX_SPACE(REG_ISPI_STATUS) == 0);
    REG_ISPI_FIFO = ISPI_WORD(reg) | ISPI_READ;
    ispi_wait_idle();
    while (ISPI_RX_LEVEL(REG_ISPI_STATUS) == 0);
    uint16_t v = (uint16_t)(REG_ISPI_FIFO & 0xFFFF);
    taskEXIT_CRITICAL();
    return v;
}

void pmu_write(uint16_t reg, uint16_t val)
{
    taskENTER_CRITICAL();
    while (ISPI_TX_SPACE(REG_ISPI_STATUS) == 0);
    REG_ISPI_FIFO = ISPI_WORD(reg) | val;
    ispi_wait_idle();
    taskEXIT_CRITICAL();
}

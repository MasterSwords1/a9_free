/*
 * Runtime SPI NOR flash erase / program / read
 */

#include "flash.h"
#include "FreeRTOS.h"
#include "task.h"

#define REG_SPI_BASE         0xA7FFF000
#define SPI_CMD_ADDR         (*(volatile uint32_t *)(REG_SPI_BASE + 0x00))
#define SPI_BLOCK_SIZE       (*(volatile uint32_t *)(REG_SPI_BASE + 0x04))
#define SPI_DATA_FIFO_WO     (*(volatile uint32_t *)(REG_SPI_BASE + 0x08))
#define SPI_DATA_FIFO_RO     (*(volatile uint32_t *)(REG_SPI_BASE + 0x0C))
#define SPI_READ_BACK        (*(volatile uint32_t *)(REG_SPI_BASE + 0x10))
#define SPI_CONFIG           (*(volatile uint32_t *)(REG_SPI_BASE + 0x14))
#define SPI_FIFO_CTRL        (*(volatile uint32_t *)(REG_SPI_BASE + 0x18))
#define SPI_AHB_CTRL         (*(volatile uint32_t *)(REG_SPI_BASE + 0x1C))
#define SPI_BLOCK_RX_MASK    0xFFFE00FFu
#define SPI_CONFIG_RX_MASK   0xFFF9FFFFu

#define FLASH_UNCACHED_BASE  0xA8000000u

/*
 * Everything below marked RAMFUNC runs from SRAM while the controller is in
 * manual mode (no instruction fetch from flash possible): no calls out, no
 * .rodata, helpers forced inline.
 */
#define RAMFUNC  __attribute__((section(".ramfunc"), noinline))
#define RAMINL   static inline __attribute__((always_inline))

static uint8_t s_bounce[FLASH_PAGE_SIZE];   /* SRAM copy of the page being written */

RAMINL void spi_wait_idle(void)
{
    while (SPI_DATA_FIFO_RO & 0x01);
    while (SPI_DATA_FIFO_RO & 0x01);
}

RAMINL void spi_cmd(uint32_t cmd, const uint8_t *tx, uint32_t len)
{
    spi_wait_idle();
    SPI_FIFO_CTRL = 2;
    spi_wait_idle();
    for (uint32_t i = 0; i < len; i++)
        SPI_DATA_FIFO_WO = tx[i];
    SPI_CMD_ADDR = cmd << 8;
    spi_wait_idle();
}

RAMINL uint32_t spi_read1(uint32_t cmd)
{
    spi_wait_idle();
    SPI_FIFO_CTRL = 3;
    spi_wait_idle();
    SPI_BLOCK_SIZE = (SPI_BLOCK_SIZE & SPI_BLOCK_RX_MASK) | (1u << 8);
    SPI_CONFIG &= SPI_CONFIG_RX_MASK;          /* 1 byte: CONFIG[18:17] = 0 */
    spi_wait_idle();
    SPI_CMD_ADDR = (cmd << 8) | 0x10000;
    spi_wait_idle();
    return SPI_READ_BACK >> 24;
}

RAMINL void spi_wait_wip(void)
{
    for (volatile int d = 0; d < 100; d++);
    while (spi_read1(0x05) & 0x01);
}

RAMINL int spi_write_enable(void)
{
    for (int i = 0; i < 1000; i++) {
        spi_cmd(0x06, 0, 0);
        if (spi_read1(0x05) & 0x02)
            return 0;
    }
    return -1;
}

RAMINL void spi_manual(int on)
{
    if (on) SPI_AHB_CTRL |= 0x10;
    else    SPI_AHB_CTRL &= ~0x11u;
}

RAMFUNC static int ram_erase_sector(uint32_t addr)
{
    uint8_t tx[3];
    tx[0] = (uint8_t)(addr >> 16); tx[1] = (uint8_t)(addr >> 8); tx[2] = (uint8_t)addr;
    spi_manual(1);
    spi_wait_wip();
    int rc = spi_write_enable();
    if (rc == 0) {
        spi_cmd(0x20, tx, 3);
        spi_wait_wip();
    }
    spi_manual(0);
    return rc;
}

RAMFUNC static int ram_program_page(uint32_t addr, const uint8_t *src, uint32_t len)
{
    spi_manual(1);
    spi_wait_wip();
    int rc = spi_write_enable();
    if (rc == 0) {
        spi_wait_idle();
        SPI_FIFO_CTRL = 2;
        spi_wait_idle();
        for (uint32_t i = 0; i < len; i++)
            SPI_DATA_FIFO_WO = src[i];
        SPI_CMD_ADDR = (addr << 8) | 0x02;     /* address rides in CMD_ADDR[31:8] */
        spi_wait_idle();
        spi_wait_wip();
    }
    spi_manual(0);
    return rc;
}

RAMFUNC static uint32_t ram_status(void)
{
    spi_manual(1);
    uint32_t v = (spi_read1(0x35) << 8) | spi_read1(0x05);
    spi_manual(0);
    return v;
}

static int in_storage(uint32_t addr, uint32_t len)
{
    return addr >= FLASH_STORAGE_BASE && len <= FLASH_STORAGE_SIZE &&
           addr - FLASH_STORAGE_BASE <= FLASH_STORAGE_SIZE - len;
}

int flash_erase(uint32_t addr, uint32_t len)
{
    if ((addr | len) & (FLASH_SECTOR_SIZE - 1) || !in_storage(addr, len))
        return -1;
    for (uint32_t a = addr; a < addr + len; a += FLASH_SECTOR_SIZE) {
        taskENTER_CRITICAL();
        int rc = ram_erase_sector(a);
        taskEXIT_CRITICAL();
        if (rc) return -1;
    }
    return 0;
}

int flash_write(uint32_t addr, const void *data, uint32_t len)
{
    const uint8_t *p = (const uint8_t *)data;
    if (!in_storage(addr, len))
        return -1;
    while (len) {
        uint32_t chunk = FLASH_PAGE_SIZE - (addr & (FLASH_PAGE_SIZE - 1));
        if (chunk > len) chunk = len;
        /* The source may itself be in flash (e.g. .rodata): bounce through SRAM */
        for (uint32_t i = 0; i < chunk; i++) s_bounce[i] = p[i];
        taskENTER_CRITICAL();
        int rc = ram_program_page(addr, s_bounce, chunk);
        taskEXIT_CRITICAL();
        if (rc) return -1;
        addr += chunk; p += chunk; len -= chunk;
    }
    return 0;
}

void flash_read(uint32_t addr, void *buf, uint32_t len)
{
    /* Uncached alias: never return stale cache lines for freshly written data */
    const volatile uint8_t *src = (const volatile uint8_t *)(FLASH_UNCACHED_BASE + addr);
    uint8_t *dst = (uint8_t *)buf;
    for (uint32_t i = 0; i < len; i++) dst[i] = src[i];
}

uint32_t flash_status(void)
{
    taskENTER_CRITICAL();
    uint32_t v = ram_status();
    taskEXIT_CRITICAL();
    return v;
}

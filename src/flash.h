#ifndef FLASH_H
#define FLASH_H

#include <stdint.h>

/*
 * Runtime SPI NOR flash access for the firmware itself. The firmware executes
 * in place from this flash, so erase/program run from SRAM (.ramfunc) with
 * interrupts disabled; the SPI protocol is the one verified in the RAM-run
 * programmer (vendor hal_SpiFlash*). Writes are confined to the storage window.
 */
#define FLASH_STORAGE_BASE   0x00380000u   /* flash offset */
#define FLASH_STORAGE_SIZE   0x00010000u   /* 16 x 4 KB sectors */
#define FLASH_SECTOR_SIZE    0x1000u
#define FLASH_PAGE_SIZE      256u

/* All addresses are flash offsets (0 = start of flash). Return 0 or -1. */
int      flash_erase(uint32_t addr, uint32_t len);   /* sector aligned */
int      flash_write(uint32_t addr, const void *data, uint32_t len);
void     flash_read(uint32_t addr, void *buf, uint32_t len);
uint32_t flash_status(void);                         /* SR2 << 8 | SR1 */

#endif /* FLASH_H */

#ifndef PMU_H
#define PMU_H

#include <stdint.h>

/*
 * RDA8955 PMU register access over ISPI (chip select 0), following the vendor
 * hal_IspiRegRead / hal_IspiRegWrite.
 */
uint16_t pmu_read(uint16_t reg);
void     pmu_write(uint16_t reg, uint16_t val);

/*
 * Core supply (VBUCK1) level in normal mode: PMU 0x4B Vbuck1_ldo_bit_nlp[11:8].
 * A smaller code is a higher voltage. The ROM leaves 8; the vendor firmware sets
 * 6 at boot (pmd_SetVcoreToDefaultVolt) and runs at 6 whenever any user asks for
 * more than the idle level (7). Only codes the vendor itself uses are accepted.
 */
#define PMU_VCORE_VENDOR_HIGH  6
#define PMU_VCORE_VENDOR_IDLE  7
#define PMU_VCORE_BOOT         8
int      pmu_set_vcore(unsigned code);   /* 0 on success, -1 if rejected/readback differs */
unsigned pmu_get_vcore(void);

#endif /* PMU_H */

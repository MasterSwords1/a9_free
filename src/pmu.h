#ifndef PMU_H
#define PMU_H

#include <stdint.h>

/*
 * RDA8955 PMU register access over ISPI (chip select 0), following the vendor
 * hal_IspiRegRead / hal_IspiRegWrite.
 */
uint16_t pmu_read(uint16_t reg);
void     pmu_write(uint16_t reg, uint16_t val);

#endif /* PMU_H */

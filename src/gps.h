#ifndef GPS_H
#define GPS_H

#include "nmea.h"

/*
 * A9G on-module GPS (GK9501) on UART2 @ 9600 8N1. Power-on sequence mirrors the
 * vendor GPS_Open: UART2 open, VIBR LDO on (pmd level 8 = PMU bit 0x0020,
 * pd_vibr_*), 32 kHz clock out (IOMUX pad KEYOUT_3 = DBG_CLK, Cfg_Clk_Out = 0x2F).
 */
void        gps_init_task(void);
int         gps_power(int on);
int         gps_is_on(void);
nmea_t     *gps_state(void);
uint32_t    gps_bytes(void);
uint32_t    gps_loops(void);
/* Copy the most recent raw UART2 bytes (oldest first); returns the count. */
unsigned    gps_raw_copy(char *dst, unsigned max);

#endif /* GPS_H */

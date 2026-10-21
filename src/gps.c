/*
 * GPS power control and NMEA reader task
 */

#include "gps.h"
#include "pmu.h"
#include "uart2.h"
#include "FreeRTOS.h"
#include "task.h"

/* PMU (8955_hard.xml "pmu" module): the GPS runs from the VIBR LDO */
#define PMU_REG_LDO_SETTINGS 0x02     /* bit 7 vibr_enable */
#define PMU_REG_LDO_ACTIVE   0x03     /* bit 5 pd_vibr_act (1 = off) */
#define PMU_REG_LDO_LP       0x08     /* bit 5 pd_vibr_lp */
#define PMU_REG_LDO_PON      0x0D     /* bit 5 pd_vibr_pon */
#define PMU_VIBR_ENABLE      0x0080
#define PMU_LDO_GPS          0x0020

#define REG_IOMUX_PAD_D0     (*(volatile uint32_t *)0xA1A2A0D0)
#define REG_CFG_CLK_OUT      (*(volatile uint32_t *)0xA1A00058)

static nmea_t   s_nmea;
static volatile int      s_on;
static volatile uint32_t s_bytes;
static volatile uint32_t s_loops;
static char     s_raw[160];           /* most recent raw bytes, ring */

static StaticTask_t s_gps_tcb __attribute__((aligned(8)));
static StackType_t  s_gps_stack[256] __attribute__((aligned(8)));

int gps_power(int on)
{
    uint16_t en  = pmu_read(PMU_REG_LDO_SETTINGS);
    uint16_t act = pmu_read(PMU_REG_LDO_ACTIVE);
    uint16_t lp  = pmu_read(PMU_REG_LDO_LP);
    uint16_t pon = pmu_read(PMU_REG_LDO_PON);

    if (on) {
        uart2_init_9600();
        nmea_init(&s_nmea);
        s_bytes = 0;
        /*
         * The vendor GPS_Open only clears the act/lp power-down bits because its
         * PMU init already set vibr_enable and left the power-on bank alone; a
         * ROM-booted chip needs those two as well.
         */
        pmu_write(PMU_REG_LDO_SETTINGS, en | PMU_VIBR_ENABLE);
        pmu_write(PMU_REG_LDO_ACTIVE, act & ~PMU_LDO_GPS);
        pmu_write(PMU_REG_LDO_LP, lp & ~PMU_LDO_GPS);
        pmu_write(PMU_REG_LDO_PON, pon & ~PMU_LDO_GPS);
        REG_IOMUX_PAD_D0 = 5;
        REG_CFG_CLK_OUT = 0x2F;
    } else {
        REG_CFG_CLK_OUT = 0;
        pmu_write(PMU_REG_LDO_ACTIVE, act | PMU_LDO_GPS);
        pmu_write(PMU_REG_LDO_LP, lp | PMU_LDO_GPS);
        pmu_write(PMU_REG_LDO_PON, pon | PMU_LDO_GPS);
        pmu_write(PMU_REG_LDO_SETTINGS, en & ~PMU_VIBR_ENABLE);
    }
    s_on = on;
    return (pmu_read(PMU_REG_LDO_ACTIVE) & PMU_LDO_GPS) == (on ? 0 : PMU_LDO_GPS);
}

int gps_is_on(void)
{
    return s_on;
}

nmea_t *gps_state(void)
{
    return &s_nmea;
}

uint32_t gps_bytes(void)
{
    return s_bytes;
}

uint32_t gps_loops(void)
{
    return s_loops;
}

unsigned gps_raw_copy(char *dst, unsigned max)
{
    uint32_t total = s_bytes;
    unsigned n = total < sizeof(s_raw) ? total : sizeof(s_raw);
    if (n > max) n = max;
    for (unsigned i = 0; i < n; i++)
        dst[i] = s_raw[(total - n + i) % sizeof(s_raw)];
    return n;
}

static void vGpsTask(void *pvParameters)
{
    (void)pvParameters;
    for (;;) {
        s_loops++;
        if (s_on) {
            int c;
            while ((c = uart2_getc()) >= 0) {
                s_raw[s_bytes % sizeof(s_raw)] = (char)c;
                s_bytes++;
                nmea_feed(&s_nmea, (char)c);
            }
        }
        /* 32-byte FIFO at 9600 baud fills in ~33 ms */
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void gps_init_task(void)
{
    nmea_init(&s_nmea);
    xTaskCreateStatic(vGpsTask, "GPS", 256, NULL, 1, s_gps_stack, &s_gps_tcb);
}

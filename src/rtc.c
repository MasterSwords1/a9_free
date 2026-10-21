/*
 * RDA8955 calendar / RTC driver
 */

#include "rtc.h"
#include "FreeRTOS.h"
#include "task.h"

#define CAL_BASE            0xA1A0A000
#define REG_CAL_CMD         (*(volatile uint32_t *)(CAL_BASE + 0x04))
#define REG_CAL_STATUS      (*(volatile uint32_t *)(CAL_BASE + 0x08))
#define REG_CAL_LOAD_L      (*(volatile uint32_t *)(CAL_BASE + 0x0C))
#define REG_CAL_LOAD_H      (*(volatile uint32_t *)(CAL_BASE + 0x10))
#define REG_CAL_CUR_L       (*(volatile uint32_t *)(CAL_BASE + 0x14))
#define REG_CAL_CUR_H       (*(volatile uint32_t *)(CAL_BASE + 0x18))
#define REG_CAL_ALARM_L     (*(volatile uint32_t *)(CAL_BASE + 0x1C))
#define REG_CAL_ALARM_H     (*(volatile uint32_t *)(CAL_BASE + 0x20))

#define CMD_CALENDAR_LOAD   (1u << 0)
#define CMD_ALARM_LOAD      (1u << 4)
#define CMD_ALARM_EN_SET    (1u << 5)
#define CMD_ALARM_EN_CLR    (1u << 6)
#define CMD_ALARM_CLR       (1u << 8)
#define CMD_ITV_IRQ_CLR     (1u << 9)

#define ST_ITV_CAUSE        (1u << 0)
#define ST_ALARM_CAUSE      (1u << 1)
#define ST_ALARM_ENABLE     (1u << 20)
#define ST_NOT_PROGRAMMED   (1u << 31)

#define REG_SYS_IRQ_MASK_SET (*(volatile uint32_t *)0xA1A01008)
#define REG_SYS_IRQ_MASK_CLR (*(volatile uint32_t *)0xA1A0100C)
#define SYS_IRQ_CALENDAR     (1u << 11)

static volatile uint32_t s_alarm_count;

static uint32_t pack_l(const rtc_time_t *t)
{
    return (t->sec & 63) | ((uint32_t)(t->min & 63) << 8) | ((uint32_t)(t->hour & 31) << 16);
}

static uint32_t pack_h(const rtc_time_t *t)
{
    return (t->day & 31) | ((uint32_t)(t->month & 15) << 8) |
           ((uint32_t)((t->year - 2000) & 127) << 16) | ((uint32_t)(t->wday & 7) << 24);
}

static int valid(const rtc_time_t *t)
{
    return t->sec < 60 && t->min < 60 && t->hour < 24 && t->day >= 1 && t->day <= 31 &&
           t->month >= 1 && t->month <= 12 && t->year >= 2000 && t->year <= 2127;
}

uint8_t rtc_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    /* Sakamoto's method, 0 = Sunday */
    static const uint8_t k[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    if (m < 3) y--;
    return (uint8_t)((y + y / 4 - y / 100 + y / 400 + k[m - 1] + d) % 7);
}

int rtc_set(const rtc_time_t *in)
{
    if (!valid(in) || (REG_CAL_CMD & CMD_CALENDAR_LOAD))
        return -1;
    rtc_time_t t = *in;
    /* Hardware counts ISO weekdays 1 (Mon) .. 7 (Sun); observed: Sat 6 -> Sun 7 */
    t.wday = rtc_weekday(t.year, t.month, t.day);
    if (t.wday == 0) t.wday = 7;
    REG_CAL_LOAD_L = pack_l(&t);
    REG_CAL_LOAD_H = pack_h(&t);
    REG_CAL_CMD = CMD_CALENDAR_LOAD;
    while (REG_CAL_STATUS & ST_NOT_PROGRAMMED);
    return 0;
}

int rtc_get(rtc_time_t *t)
{
    if (REG_CAL_STATUS & ST_NOT_PROGRAMMED)
        return -1;
    uint32_t h, l;
    /* Re-read until stable so a second boundary cannot tear the two words */
    do {
        h = REG_CAL_CUR_H;
        l = REG_CAL_CUR_L;
    } while (h != REG_CAL_CUR_H || l != REG_CAL_CUR_L);
    t->sec   = l & 63;
    t->min   = (l >> 8) & 63;
    t->hour  = (l >> 16) & 31;
    t->day   = h & 31;
    t->month = (h >> 8) & 15;
    t->year  = 2000 + ((h >> 16) & 127);
    t->wday  = (h >> 24) & 7;
    return 0;
}

int rtc_set_alarm(const rtc_time_t *t)
{
    if (!valid(t) || (REG_CAL_CMD & CMD_ALARM_LOAD))
        return -1;
    REG_CAL_ALARM_L = pack_l(t);
    REG_CAL_ALARM_H = pack_h(t);
    REG_CAL_CMD = CMD_ALARM_LOAD;
    while (REG_CAL_CMD & (CMD_ALARM_LOAD | CMD_ALARM_EN_SET | CMD_ALARM_EN_CLR));
    REG_CAL_CMD = CMD_ALARM_EN_SET;
    REG_SYS_IRQ_MASK_SET = SYS_IRQ_CALENDAR;
    return 0;
}

void rtc_alarm_disable(void)
{
    while (REG_CAL_CMD & (CMD_ALARM_LOAD | CMD_ALARM_EN_SET | CMD_ALARM_EN_CLR));
    REG_CAL_CMD = CMD_ALARM_EN_CLR | CMD_ALARM_CLR;
    REG_SYS_IRQ_MASK_CLR = SYS_IRQ_CALENDAR;
}

int rtc_alarm_enabled(void)
{
    return (REG_CAL_STATUS & ST_ALARM_ENABLE) != 0;
}

uint32_t rtc_alarm_count(void)
{
    return s_alarm_count;
}

void rtc_irq(void)
{
    uint32_t st = REG_CAL_STATUS;
    uint32_t ack = 0;
    if (st & ST_ALARM_CAUSE) {
        ack |= CMD_ALARM_CLR | CMD_ALARM_EN_CLR;   /* one-shot */
        s_alarm_count++;
    }
    if (st & ST_ITV_CAUSE)
        ack |= CMD_ITV_IRQ_CLR;
    REG_CAL_CMD = ack;
}

static uint8_t days_in_month(uint16_t y, uint8_t m)
{
    static const uint8_t d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) return 29;
    return d[m - 1];
}

void rtc_add_seconds(rtc_time_t *t, uint32_t secs)
{
    uint32_t s = t->sec + secs % 60;
    uint32_t m = t->min + (secs / 60) % 60 + s / 60;
    uint32_t h = t->hour + secs / 3600 + m / 60;
    t->sec = s % 60;
    t->min = m % 60;
    t->hour = h % 24;
    for (uint32_t carry = h / 24; carry; carry--) {
        if (++t->day > days_in_month(t->year, t->month)) {
            t->day = 1;
            if (++t->month > 12) { t->month = 1; t->year++; }
        }
    }
    t->wday = rtc_weekday(t->year, t->month, t->day);
}

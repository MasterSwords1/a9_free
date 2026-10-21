#ifndef RTC_H
#define RTC_H

#include <stdint.h>

/*
 * RDA8955 calendar (RTC) block at 0xA1A0A000, following the vendor
 * hal_TimRtcSetTime / GetTime / SetAlarm / AlarmEnable / IrqHandler.
 * Runs from the 32 kHz domain; keeps time across CPU resets while powered.
 */
typedef struct {
    uint8_t  sec;     /* 0-59 */
    uint8_t  min;     /* 0-59 */
    uint8_t  hour;    /* 0-23 */
    uint8_t  day;     /* 1-31 */
    uint8_t  month;   /* 1-12 */
    uint16_t year;    /* 2000-2127 */
    uint8_t  wday;    /* raw hardware weekday: 1 = Monday .. 7 = Sunday */
} rtc_time_t;

int      rtc_set(const rtc_time_t *t);     /* 0 ok, -1 busy/invalid */
int      rtc_get(rtc_time_t *t);           /* 0 ok, -1 never programmed */
int      rtc_set_alarm(const rtc_time_t *t);
void     rtc_alarm_disable(void);
int      rtc_alarm_enabled(void);
/* Number of alarms fired since boot (incremented from the IRQ). */
uint32_t rtc_alarm_count(void);
/* Called by the system IRQ dispatcher for SYS_IRQ_CALENDAR. */
void     rtc_irq(void);
uint8_t  rtc_weekday(uint16_t year, uint8_t month, uint8_t day);
/* Add `secs` (< 1 day) to a time, carrying into the date. */
void     rtc_add_seconds(rtc_time_t *t, uint32_t secs);

#endif /* RTC_H */

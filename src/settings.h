#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* Persistent settings record in the first sector of the flash storage window. */
typedef struct {
    uint32_t magic;          /* SETTINGS_MAGIC */
    uint16_t version;
    uint16_t size;           /* sizeof(settings_t) */
    uint32_t crc;            /* CRC-32 of the bytes after this field */
    uint32_t clock_mhz;      /* boot system clock */
    uint32_t wdt_ms;         /* watchdog timeout, 0 = off */
    char     note[64];       /* free text */
} settings_t;

#define SETTINGS_MAGIC    0x46433941u   /* "A9CF" */
#define SETTINGS_VERSION  1

/* Load from flash into `s`; on a missing/corrupt record fill defaults and return -1. */
int  settings_load(settings_t *s);
int  settings_save(settings_t *s);      /* computes magic/size/crc */
int  settings_erase(void);
void settings_defaults(settings_t *s);

#endif /* SETTINGS_H */

/*
 * Persistent settings (one CRC-protected record in flash)
 */

#include "settings.h"
#include "flash.h"
#include "clock.h"

#define SETTINGS_ADDR  FLASH_STORAGE_BASE

static uint32_t crc32(const uint8_t *p, uint32_t len)
{
    uint32_t c = 0xFFFFFFFFu;
    while (len--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1)));
    }
    return ~c;
}

static uint32_t record_crc(const settings_t *s)
{
    const uint8_t *body = (const uint8_t *)&s->clock_mhz;
    return crc32(body, sizeof(*s) - (uint32_t)(body - (const uint8_t *)s));
}

void settings_defaults(settings_t *s)
{
    uint8_t *p = (uint8_t *)s;
    for (unsigned i = 0; i < sizeof(*s); i++) p[i] = 0;
    s->clock_mhz = CLOCK_SAFE_MAX_MHZ;
    s->wdt_ms = 4000;
}

int settings_load(settings_t *s)
{
    flash_read(SETTINGS_ADDR, s, sizeof(*s));
    if (s->magic == SETTINGS_MAGIC && s->version == SETTINGS_VERSION &&
        s->size == sizeof(*s) && s->crc == record_crc(s)) {
        s->note[sizeof(s->note) - 1] = '\0';
        return 0;
    }
    settings_defaults(s);
    return -1;
}

int settings_save(settings_t *s)
{
    s->magic = SETTINGS_MAGIC;
    s->version = SETTINGS_VERSION;
    s->size = sizeof(*s);
    s->note[sizeof(s->note) - 1] = '\0';
    s->crc = record_crc(s);
    if (flash_erase(SETTINGS_ADDR, FLASH_SECTOR_SIZE) || flash_write(SETTINGS_ADDR, s, sizeof(*s)))
        return -1;
    settings_t check;
    flash_read(SETTINGS_ADDR, &check, sizeof(check));
    const uint8_t *a = (const uint8_t *)s, *b = (const uint8_t *)&check;
    for (unsigned i = 0; i < sizeof(*s); i++)
        if (a[i] != b[i]) return -1;
    return 0;
}

int settings_erase(void)
{
    return flash_erase(SETTINGS_ADDR, FLASH_SECTOR_SIZE);
}

#ifndef NMEA_H
#define NMEA_H

#include <stdint.h>

/*
 * Minimal NMEA-0183 parser: feeds on a byte stream, validates the checksum and
 * keeps the latest GGA/RMC fix. No heap, no floating point (fixed point only).
 */

typedef struct {
    int32_t  lat_e7;        /* degrees * 1e7, south negative */
    int32_t  lon_e7;        /* degrees * 1e7, west negative */
    int32_t  alt_dm;        /* altitude in decimetres */
    int32_t  speed_cknots;  /* RMC speed over ground, knots * 100 */
    uint8_t  fix_quality;   /* GGA: 0 = none, 1 = GPS, 2 = DGPS ... */
    uint8_t  sats;          /* GGA: satellites used */
    uint8_t  rmc_valid;     /* RMC status 'A' */
    uint32_t utc_hhmmss;    /* from the last GGA/RMC */
    uint32_t sentences_ok;
    uint32_t sentences_bad; /* checksum or format errors */
} nmea_fix_t;

typedef struct {
    char       line[96];
    uint8_t    len;
    uint8_t    active;
    nmea_fix_t fix;
} nmea_t;

void nmea_init(nmea_t *n);
/* Returns 1 when this byte completed a valid sentence, else 0. */
int  nmea_feed(nmea_t *n, char c);

#endif /* NMEA_H */

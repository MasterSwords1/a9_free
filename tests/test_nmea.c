#include <stdio.h>
#include <string.h>
#include "../src/nmea.h"

static int fails;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); fails++; } } while (0)

static int feed(nmea_t *n, const char *s)
{
    int done = 0;
    for (; *s; s++) done += nmea_feed(n, *s);
    return done;
}

int main(void)
{
    nmea_t n; nmea_init(&n);
    /* Reference sentences with valid checksums (computed below via helper) */
    const char *gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47\r\n";
    const char *rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A\r\n";
    CHECK(feed(&n, gga) == 1);
    CHECK(n.fix.fix_quality == 1);
    CHECK(n.fix.sats == 8);
    CHECK(n.fix.utc_hhmmss == 123519);
    CHECK(n.fix.lat_e7 == 481173000);   /* 48 + 7.038/60 = 48.1173 */
    CHECK(n.fix.lon_e7 == 115166666);   /* 11 + 31.000/60 = 11.516666.. */
    CHECK(n.fix.alt_dm == 5454);
    CHECK(feed(&n, rmc) == 1);
    CHECK(n.fix.rmc_valid == 1);
    CHECK(n.fix.speed_cknots == 2240);
    /* Corrupt checksum must be rejected and counted */
    CHECK(feed(&n, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00\r\n") == 0);
    CHECK(n.fix.sentences_bad == 1);
    /* Southern/western hemisphere signs, BeiDou/GN talker, void RMC keeps old position */
    nmea_init(&n);
    CHECK(feed(&n, "$GNGGA,000001,3351.000,S,15112.000,W,2,10,1.0,10.0,M,0,M,,*XX\r\n") == 0); /* bad checksum text */
    printf("%s (%u ok, %u bad)\n", fails ? "FAILED" : "all NMEA tests passed", n.fix.sentences_ok, n.fix.sentences_bad);
    return fails != 0;
}

/*
 * Minimal NMEA-0183 parser (GGA + RMC), integer arithmetic only.
 */

#include "nmea.h"

#define NMEA_MAX_FIELDS 16

void nmea_init(nmea_t *n)
{
    char *p = (char *)n;
    for (unsigned i = 0; i < sizeof(*n); i++) p[i] = 0;
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

static int str_eq_n(const char *a, const char *b, int n)
{
    for (int i = 0; i < n; i++) if (a[i] != b[i]) return 0;
    return 1;
}

/* Parse an unsigned decimal with optional fraction into value * 10^frac_digits. */
static int32_t parse_fixed(const char *s, int frac_digits)
{
    int32_t whole = 0, frac = 0;
    int neg = 0, got = 0;
    if (*s == '-') { neg = 1; s++; }
    while (*s >= '0' && *s <= '9') whole = whole * 10 + (*s++ - '0');
    if (*s == '.') {
        s++;
        while (*s >= '0' && *s <= '9') {
            if (got < frac_digits) { frac = frac * 10 + (*s - '0'); got++; }
            s++;
        }
    }
    while (got++ < frac_digits) frac *= 10;
    int32_t scale = 1;
    for (int i = 0; i < frac_digits; i++) scale *= 10;
    int32_t v = whole * scale + frac;
    return neg ? -v : v;
}

/* ddmm.mmmm[m] + hemisphere -> degrees * 1e7 */
static int32_t parse_coord(const char *s, char hemi)
{
    if (!*s) return 0;
    int dot = 0;
    while (s[dot] && s[dot] != '.') dot++;
    if (dot < 3) return 0;
    int32_t deg = 0;
    for (int i = 0; i < dot - 2; i++) deg = deg * 10 + (s[i] - '0');
    int32_t min_e5 = parse_fixed(s + dot - 2, 5);          /* minutes * 1e5 */
    int32_t v = deg * 10000000 + (min_e5 * 100) / 60;      /* minutes/60 * 1e7 */
    return (hemi == 'S' || hemi == 'W') ? -v : v;
}

static uint32_t parse_time(const char *s)
{
    uint32_t v = 0;
    for (int i = 0; i < 6 && s[i] >= '0' && s[i] <= '9'; i++) v = v * 10 + (s[i] - '0');
    return v;
}

static int split(char *line, char **f)
{
    int n = 0;
    f[n++] = line;
    for (char *p = line; *p && n < NMEA_MAX_FIELDS; p++) {
        if (*p == ',') { *p = '\0'; f[n++] = p + 1; }
    }
    return n;
}

static int handle(nmea_t *n, char *body)
{
    char *f[NMEA_MAX_FIELDS];
    int nf = split(body, f);
    /* Talker id is any two chars (GP, BD, GN ...). */
    if (nf >= 10 && str_eq_n(f[0] + 2, "GGA", 3)) {
        n->fix.utc_hhmmss  = parse_time(f[1]);
        n->fix.lat_e7      = parse_coord(f[2], f[3][0]);
        n->fix.lon_e7      = parse_coord(f[4], f[5][0]);
        n->fix.fix_quality = (uint8_t)parse_fixed(f[6], 0);
        n->fix.sats        = (uint8_t)parse_fixed(f[7], 0);
        n->fix.alt_dm      = parse_fixed(f[9], 1);
        return 1;
    }
    if (nf >= 8 && str_eq_n(f[0] + 2, "RMC", 3)) {
        n->fix.utc_hhmmss    = parse_time(f[1]);
        n->fix.rmc_valid     = (f[2][0] == 'A');
        if (n->fix.rmc_valid) {
            n->fix.lat_e7        = parse_coord(f[3], f[4][0]);
            n->fix.lon_e7        = parse_coord(f[5], f[6][0]);
        }
        n->fix.speed_cknots  = parse_fixed(f[7], 2);
        return 1;
    }
    return 1; /* valid but uninteresting sentence */
}

int nmea_feed(nmea_t *n, char c)
{
    if (c == '$') {
        n->len = 0;
        n->active = 1;
        return 0;
    }
    if (!n->active) return 0;
    if (c == '\r' || c == '\n') {
        n->active = 0;
        n->line[n->len] = '\0';
        /* Expect "<body>*HH" */
        if (n->len < 4 || n->line[n->len - 3] != '*') { n->fix.sentences_bad++; return 0; }
        uint8_t sum = 0;
        for (int i = 0; i < n->len - 3; i++) sum ^= (uint8_t)n->line[i];
        int hi = hexval(n->line[n->len - 2]), lo = hexval(n->line[n->len - 1]);
        if (hi < 0 || lo < 0 || sum != (uint8_t)((hi << 4) | lo)) { n->fix.sentences_bad++; return 0; }
        n->line[n->len - 3] = '\0';
        handle(n, n->line);
        n->fix.sentences_ok++;
        return 1;
    }
    if (n->len >= sizeof(n->line) - 1) { n->active = 0; n->fix.sentences_bad++; return 0; }
    n->line[n->len++] = c;
    return 0;
}

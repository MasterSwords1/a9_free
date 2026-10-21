#ifndef A9_FREE_STRING_H
#define A9_FREE_STRING_H

/* Minimal freestanding <string.h>; implementations are in port/port.c. */
#include <stddef.h>

void  *memset(void *s, int c, size_t n);
void  *memcpy(void *dest, const void *src, size_t n);
size_t strlen(const char *s);

#endif /* A9_FREE_STRING_H */

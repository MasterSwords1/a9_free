#ifndef UART2_H
#define UART2_H

#include <stdint.h>

/*
 * RDA8955 UART2: wired internally to the A9G's GPS receiver.
 * Register layout and init sequence follow the vendor hal_UartOpen.
 */
#define UART2_BASE        0xA1A16000

void     uart2_init_9600(void);
int      uart2_getc(void);
uint32_t uart2_status(void);

#endif /* UART2_H */

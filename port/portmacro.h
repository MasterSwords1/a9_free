#ifndef PORTMACRO_H
#define PORTMACRO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Type definitions. */
#define portCHAR                char
#define portFLOAT               float
#define portDOUBLE              double
#define portLONG                long
#define portSHORT               short
#define portSTACK_TYPE          uint32_t
#define portBASE_TYPE           long
#define portPOINTER_SIZE_TYPE   uint32_t

typedef portSTACK_TYPE StackType_t;
typedef long BaseType_t;
typedef unsigned long UBaseType_t;

#if( configUSE_16_BIT_TICKS == 1 )
    typedef uint16_t TickType_t;
    #define portMAX_DELAY ( TickType_t ) 0xffff
#else
    typedef uint32_t TickType_t;
    #define portMAX_DELAY ( TickType_t ) 0xffffffffUL
    #define portTICK_TYPE_IS_ATOMIC 1
#endif

/* Architecture specifics. */
#define portSTACK_GROWTH        ( -1 )
#define portTICK_PERIOD_MS      ( ( TickType_t ) 1000 / configTICK_RATE_HZ )
#define portBYTE_ALIGNMENT      8

/* Critical section management. */
extern void vPortEnterCritical( void );
extern void vPortExitCritical( void );
#define portENTER_CRITICAL()    vPortEnterCritical()
#define portEXIT_CRITICAL()     vPortExitCritical()

#define portDISABLE_INTERRUPTS() __asm__ volatile ( "" ::: "memory" )
#define portENABLE_INTERRUPTS()  __asm__ volatile ( "" ::: "memory" )

/* Yield definition. */
extern void vPortYield( void );
#define portYIELD()             vPortYield()

/* Task function macros. */
#define portTASK_FUNCTION_PROTO( vFunction, pvParameters ) void vFunction( void *pvParameters )
#define portTASK_FUNCTION( vFunction, pvParameters ) void vFunction( void *pvParameters )

#define portNOP()               __asm__ volatile ( "nop" )

#ifdef __cplusplus
}
#endif

#endif /* PORTMACRO_H */

#ifndef UART_RX_H
#define UART_RX_H

#include <stdint.h>

/* One slot is reserved to distinguish full from empty: 255 queued bytes. */
#define UART_RX_RING_SIZE 256U

typedef enum
{
    UART_RX_EMPTY,
    UART_RX_BYTE,
    UART_RX_OVERFLOW,
    UART_RX_ERROR
} UartRxResult;

/* Call once after MX_USART1_UART_Init, before transmitting the banner.
 * This module owns RXNE/error interrupts; do not mix HAL receive APIs with it. */
void UartRx_Init(void);
/* USART1 IRQ only: read status/data and enqueue, without parsing or TX. */
void UartRx_IRQHandler(void);
/* Main loop only. A loss result flushes the queue; resync the parser at CR/LF. */
UartRxResult UartRx_Pop(uint8_t *byte);

#endif

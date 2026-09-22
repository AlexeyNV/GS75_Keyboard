#ifndef TEST_USART_H
#define TEST_USART_H
#include <stdint.h>

typedef struct
{
    volatile uint32_t SR, DR, CR1, CR3;
} USART_TypeDef;
typedef struct { USART_TypeDef *Instance; } UART_HandleTypeDef;
typedef enum { HAL_OK, HAL_TIMEOUT } HAL_StatusTypeDef;
extern UART_HandleTypeDef huart1;
#define HAL_MAX_DELAY 0xFFFFFFFFU

#define USART_SR_PE   (1U << 0)
#define USART_SR_FE   (1U << 1)
#define USART_SR_NE   (1U << 2)
#define USART_SR_ORE  (1U << 3)
#define USART_SR_RXNE (1U << 5)
#define UART_IT_RXNE 1U
#define UART_IT_ERR 2U
#define USART1_IRQn 37

uint32_t __get_PRIMASK(void);
void __disable_irq(void);
void __set_PRIMASK(uint32_t mask);
void HAL_NVIC_ClearPendingIRQ(int irq);
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority);
void HAL_NVIC_EnableIRQ(int irq);
void Test_ClearRxFlags(UART_HandleTypeDef *uart);
void Test_EnableRxInterrupt(UART_HandleTypeDef *uart, uint32_t interrupt);
#define __HAL_UART_CLEAR_OREFLAG(uart) Test_ClearRxFlags(uart)
#define __HAL_UART_ENABLE_IT(uart, interrupt) Test_EnableRxInterrupt(uart, interrupt)

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t length, uint32_t timeout);
uint32_t HAL_GetTick(void);
uint32_t HAL_RCC_GetSysClockFreq(void);
#endif

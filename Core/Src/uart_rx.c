#include "uart_rx.h"
#include "usart.h"

/* Single producer (USART1 IRQ), single consumer (main loop). */
static volatile uint8_t ring[UART_RX_RING_SIZE];
static volatile uint32_t head;
static volatile uint32_t tail;
static volatile UartRxResult loss;

void UartRx_Init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    head = 0;
    tail = 0;
    loss = UART_RX_EMPTY;

    /* SR followed by DR clears any stale RXNE/ORE/FE/NE/PE from startup. */
    __HAL_UART_CLEAR_OREFLAG(&huart1);
    HAL_NVIC_ClearPendingIRQ(USART1_IRQn);
    /* Higher priority than SysTick (15): RX cannot wait for a 233 us scan
     * when bytes can arrive every ~87 us. No scan configuration is changed. */
    HAL_NVIC_SetPriority(USART1_IRQn, 5, 0);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_RXNE);
    __HAL_UART_ENABLE_IT(&huart1, UART_IT_ERR);
    HAL_NVIC_EnableIRQ(USART1_IRQn);
    __set_PRIMASK(primask);
}

void UartRx_IRQHandler(void)
{
    /* RX-only interrupt handling. TX remains HAL polling, with no TX IRQ.
     * Reading SR then DR acknowledges the byte and any receive error flags. */
    uint32_t status = huart1.Instance->SR;
    uint32_t errors = status & (USART_SR_ORE | USART_SR_FE | USART_SR_NE | USART_SR_PE);
    if ((status & USART_SR_RXNE) == 0U && errors == 0U)
    {
        return;
    }
    uint8_t byte = (uint8_t)huart1.Instance->DR;
    if (errors != 0U)
    {
        loss = UART_RX_ERROR;
        return;
    }
    if (loss != UART_RX_EMPTY)
    {
        /* Until main acknowledges the gap, the stream is untrustworthy.
         * Delimiter recognition belongs to the main-loop parser. */
        return;
    }

    uint32_t next = (head + 1U) % UART_RX_RING_SIZE;
    if (next == tail)
    {
        loss = UART_RX_OVERFLOW;
        return;
    }
    ring[head] = byte;
    head = next;
}

UartRxResult UartRx_Pop(uint8_t *byte)
{
    /* Only a few loads/stores with interrupts masked. Preserve the caller's
     * mask. No parsing, formatting or UART transmit inside this section. */
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    UartRxResult result = loss;
    if (result != UART_RX_EMPTY)
    {
        tail = head;
        loss = UART_RX_EMPTY;
    }
    else if (tail != head)
    {
        *byte = ring[tail];
        tail = (tail + 1U) % UART_RX_RING_SIZE;
        result = UART_RX_BYTE;
    }
    __set_PRIMASK(primask);
    return result;
}

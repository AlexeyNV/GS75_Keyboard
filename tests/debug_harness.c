#include "matrix.h"
#include "matrix_debug.h"
#include "uart_rx.h"
#include "keyboard_runtime.h"
#include "usb_keyboard.h"
#include "usart.h"
#include <string.h>

static USART_TypeDef registers;
UART_HandleTypeDef huart1 = { &registers };
const uint8_t Matrix_DriveFpc[MATRIX_ROWS] = {17, 18, 19, 20, 21, 22, 23, 24};
const uint8_t Matrix_SenseFpc[MATRIX_COLS] =
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 25, 26};
static MatrixSnapshot snapshot;
static uint32_t tick;
static uint32_t primask;
static uint32_t irq_priority;
static uint8_t irq_enabled;
static uint8_t in_irq;
static uint32_t harness_errors;
static uint8_t mutate_on_send;
static uint8_t power_fault_on_send;
void PowerTest_Set(uint32_t code, uint32_t raw, int result, uint32_t flags);
void Test_PowerFaultDuringTx(void) { power_fault_on_send = 1; }
static char output[32768];
static size_t output_length;
static char during_tx[2048];
static size_t during_tx_length;
static size_t during_tx_index;
static uint32_t logical_during_tx[16][MATRIX_ROWS];
static uint32_t logical_tx_length;
static uint32_t logical_tx_index;

void Test_LogicalSample(const uint32_t rows[MATRIX_ROWS])
{
    memcpy(snapshot.raw, rows, sizeof(snapshot.raw));
    memcpy(snapshot.debounced, rows, sizeof(snapshot.debounced));
    AntiGhost_Analyze(rows, &snapshot.ghost);
    snapshot.scan_count++;
    snapshot.timestamp_ms = ++tick;
    KeyboardRuntime_Tick(); /* Real post-scan hook, including during TX. */
}

void Test_PlanLogicalDuringTx(const uint32_t *rows, uint32_t samples)
{
    if (samples > 16U) { harness_errors++; return; }
    memcpy(logical_during_tx, rows, samples * sizeof(logical_during_tx[0]));
    logical_tx_length = samples;
    logical_tx_index = 0;
}

void Matrix_GetSnapshot(MatrixSnapshot *result) { *result = snapshot; }
uint32_t HAL_GetTick(void) { return tick; }
void Test_SetTick(uint32_t value) { tick = value; }
uint32_t HAL_RCC_GetSysClockFreq(void) { return 96000000U; }
uint32_t __get_PRIMASK(void) { return primask; }
void __disable_irq(void) { primask = 1; }
void Test_UsbOnUnmask(void);
void __set_PRIMASK(uint32_t mask)
{
    primask = mask;
    if (!mask) { Test_UsbOnUnmask(); }
}
void HAL_NVIC_ClearPendingIRQ(int irq) { (void)irq; }
void HAL_NVIC_SetPriority(int irq, uint32_t priority, uint32_t subpriority)
{
    (void)irq;
    (void)subpriority;
    irq_priority = priority;
}
void HAL_NVIC_EnableIRQ(int irq) { (void)irq; irq_enabled = 1; }
void Test_ClearRxFlags(UART_HandleTypeDef *uart) { uart->Instance->SR = 0; }
void Test_EnableRxInterrupt(UART_HandleTypeDef *uart, uint32_t interrupt)
{
    if (interrupt == UART_IT_RXNE) { uart->Instance->CR1 |= UART_IT_RXNE; }
    if (interrupt == UART_IT_ERR) { uart->Instance->CR3 |= UART_IT_ERR; }
}

static void Receive(uint8_t byte, uint32_t error)
{
    if (primask || !irq_enabled || !(registers.CR1 & UART_IT_RXNE))
    {
        harness_errors++;
        return;
    }
    registers.DR = byte;
    registers.SR = USART_SR_RXNE | error;
    in_irq = 1;
    UartRx_IRQHandler(); /* Actual production RX handler and ring buffer. */
    in_irq = 0;
    registers.SR = 0; /* Model hardware's SR/DR read-to-clear behavior. */
}

HAL_StatusTypeDef HAL_UART_Transmit(UART_HandleTypeDef *uart, uint8_t *data,
                                   uint16_t length, uint32_t timeout)
{
    (void)uart;
    (void)timeout;
    if (in_irq || primask) { harness_errors++; }
    for (uint16_t i = 0; i < length; i++)
    {
        if (output_length + 1U < sizeof(output))
        {
            output[output_length++] = (char)data[i];
            output[output_length] = '\0';
        }
        else { harness_errors++; }
        /* Full-duplex byte arrivals while main is stuck in a blocking TX.
         * Never run the parser here; nested replies would fail the tests. */
        if (during_tx_index < during_tx_length)
        {
            Receive((uint8_t)during_tx[during_tx_index++], 0);
        }
    }
    if (power_fault_on_send)
    {
        power_fault_on_send = 0;
        PowerTest_Set(1, 62, 1, 11); /* FAULT changes while main is in TX. */
    }
    if (logical_tx_index < logical_tx_length)
    {
        Test_LogicalSample(logical_during_tx[logical_tx_index++]);
    }
    if (mutate_on_send)
    {
        memset(snapshot.raw, 0, sizeof(snapshot.raw));
        memset(snapshot.debounced, 0, sizeof(snapshot.debounced));
        snapshot.scan_count++;
    }
    return HAL_OK;
}

void Test_ClearOutput(void)
{
    output_length = 0;
    output[0] = '\0';
}
const char *Test_Output(void) { return output; }
uint32_t Test_HarnessErrors(void) { return harness_errors; }
uint32_t Test_IrqPriority(void) { return irq_priority; }

void Test_Reset(void)
{
    memset(&snapshot, 0, sizeof(snapshot));
    memset(&registers, 0, sizeof(registers));
    tick = 0;
    primask = 0;
    irq_enabled = 0;
    in_irq = 0;
    harness_errors = 0;
    during_tx_length = during_tx_index = 0;
    logical_tx_length = logical_tx_index = 0;
    mutate_on_send = 0;
    Test_ClearOutput();
    UsbKeyboard_Init();
    KeyboardRuntime_Init();
    MatrixDebug_Init();
    Test_ClearOutput();
}

void Test_Feed(const char *text)
{
    while (*text)
    {
        Receive((uint8_t)*text++, 0);
        MatrixDebug_Process();
    }
}

void Test_ReceiveOnly(const char *text)
{
    while (*text) { Receive((uint8_t)*text++, 0); }
}

void Test_QueueDuringTx(const char *text)
{
    size_t length = strlen(text);
    if (length >= sizeof(during_tx)) { harness_errors++; return; }
    memcpy(during_tx, text, length);
    during_tx_length = length;
    during_tx_index = 0;
}

void Test_RxError(uint32_t error) { Receive('?', error); }

void Test_Drain(void)
{
    for (uint32_t i = 0; i < UART_RX_RING_SIZE * 2U; i++)
    {
        MatrixDebug_Process();
    }
}

void Test_SetSnapshot(const MatrixSnapshot *value) { snapshot = *value; }
void Test_MutateOnSend(uint8_t enable) { mutate_on_send = enable; }
void Test_Poll(uint32_t now_ms)
{
    tick = now_ms;
    MatrixDebug_Process();
}

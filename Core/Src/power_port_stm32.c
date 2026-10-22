#include "power_port.h"
#include "board_power_config.h"
#include "main.h"
#include "usb_device.h"
#include "usb_descriptors.h"
/* Custom-owned ADC1 and five GPIOs. No DMA, ADC IRQ, RGB SPI or timer. */
void PowerPort_Init(void)
{
#if GS75_BOARD_REV2
    __HAL_RCC_GPIOA_CLK_ENABLE(); __HAL_RCC_GPIOB_CLK_ENABLE(); __HAL_RCC_GPIOC_CLK_ENABLE();
    /* Latch LOW before switching the pin to output. External 100k holds reset OFF. */
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, GPIO_PIN_RESET);
    GPIO_InitTypeDef g = {0};
    g.Pin = GPIO_PIN_9; g.Mode = GPIO_MODE_OUTPUT_PP; g.Pull = GPIO_NOPULL; g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_6 | GPIO_PIN_7; g.Mode = GPIO_MODE_INPUT;
    HAL_GPIO_Init(GPIOC, &g);
    g.Pin = GPIO_PIN_4; HAL_GPIO_Init(GPIOB, &g);
    g.Pin = GPIO_PIN_3; g.Mode = GPIO_MODE_ANALOG; HAL_GPIO_Init(GPIOA, &g);
    __HAL_RCC_ADC1_CLK_ENABLE();
    ADC1->CR2 = 0; ADC1->CR1 = 0;
    /* PCLK2=96 MHz /4=24 MHz. 480-cycle sampling accommodates 1k/100n RC.
     * ADON startup settles before first 10ms scheduled conversion. */
    ADC->CCR = (ADC->CCR & ~ADC_CCR_ADCPRE) | ADC_CCR_ADCPRE_0;
    ADC1->SMPR2 = 7U << 9; ADC1->SQR1 = 0; ADC1->SQR2 = 0; ADC1->SQR3 = 3;
    ADC1->SR = 0; ADC1->CR2 = ADC_CR2_ADON;
#endif
}
uint32_t PowerPort_Cc(void)
{
#if GS75_BOARD_REV2
    uint32_t idr = GPIOC->IDR;
    return ((idr & GPIO_PIN_6) ? 2U : 0U) | ((idr & GPIO_PIN_7) ? 1U : 0U);
#else
    return 3;
#endif
}
uint8_t PowerPort_Fault(void)
{
#if GS75_BOARD_REV2
    return (GPIOB->IDR & GPIO_PIN_4) == 0;
#else
    return 0;
#endif
}
uint8_t PowerPort_Healthy(void)
{
#if GS75_BOARD_REV2
    return ((GPIOC->MODER >> 18) & 3U) == 1U &&
           (GPIOC->MODER & ((3U << 12) | (3U << 14))) == 0 &&
           (GPIOB->MODER & (3U << 8)) == 0 && ((GPIOA->MODER >> 6) & 3U) == 3U &&
           (ADC1->CR2 & ADC_CR2_ADON) != 0 && ADC1->SQR3 == 3U;
#else
    return 0;
#endif
}
void PowerPort_Enable(uint8_t on)
{
#if GS75_BOARD_REV2
    /* Defense in depth: even direct port calls cannot bypass normal-build OFF. */
    uint8_t allowed = on && GS75_RGB_POWER_BENCH_TEST && GS75_POWER_HARDWARE_VERIFIED;
    HAL_GPIO_WritePin(GPIOC, GPIO_PIN_9, allowed ? GPIO_PIN_SET : GPIO_PIN_RESET);
#else
    (void)on;
#endif
}
void PowerPort_EmergencyOff(void)
{
#if GS75_BOARD_REV2
    /* Do not enter HAL or touch the power manager from a fault handler. If the
     * GPIO clock/bus is unusable this cannot guarantee an OFF transition. */
    if ((RCC->AHB1ENR & RCC_AHB1ENR_GPIOCEN) != 0U)
    { GPIOC->BSRR = (uint32_t)GPIO_PIN_9 << 16; }
#endif
}
void PowerPort_AdcStart(void)
{
#if GS75_BOARD_REV2
    ADC1->SR = 0; ADC1->CR2 |= ADC_CR2_SWSTART;
#endif
}
int PowerPort_AdcPoll(uint32_t *raw)
{
#if GS75_BOARD_REV2
    if (ADC1->SR & ADC_SR_OVR) { return -1; }
    if (ADC1->SR & ADC_SR_EOC) { *raw = ADC1->DR; return 1; }
#else
    (void)raw;
#endif
    return 0;
}
void PowerPort_AdcAbort(void)
{
#if GS75_BOARD_REV2
    /* Single conversion completed or timed out; re-arm on next scheduled tick. */
    ADC1->CR2 &= ~ADC_CR2_ADON; ADC1->SR = 0; ADC1->CR2 = ADC_CR2_ADON;
#endif
}
void PowerPort_Usb(uint8_t *configured, uint8_t *suspended, uint32_t *declared_ma)
{
    uint8_t state = UsbDevice.dev_state;
    *configured = state == USBD_STATE_CONFIGURED;
    *suspended = state == USBD_STATE_SUSPENDED;
    uint16_t length; uint8_t *descriptor = UsbDescriptors_Config(&length);
    *declared_ma = length >= 9 ? 2U * descriptor[8] : 0;
}
uint32_t PowerPort_Now(void) { return HAL_GetTick(); }

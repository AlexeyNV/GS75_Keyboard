#include "matrix.h"

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} MatrixPin;

static const MatrixPin matrix_pins[MATRIX_LINES] =
{
    {FPC01_GPIO_Port, FPC01_Pin},
    {FPC02_GPIO_Port, FPC02_Pin},
    {FPC03_GPIO_Port, FPC03_Pin},
    {FPC04_GPIO_Port, FPC04_Pin},
    {FPC05_GPIO_Port, FPC05_Pin},
    {FPC06_GPIO_Port, FPC06_Pin},
    {FPC07_GPIO_Port, FPC07_Pin},
    {FPC08_GPIO_Port, FPC08_Pin},
    {FPC09_GPIO_Port, FPC09_Pin},
    {FPC10_GPIO_Port, FPC10_Pin},
    {FPC11_GPIO_Port, FPC11_Pin},
    {FPC12_GPIO_Port, FPC12_Pin},
    {FPC13_GPIO_Port, FPC13_Pin},
    {FPC14_GPIO_Port, FPC14_Pin},
    {FPC15_GPIO_Port, FPC15_Pin},
    {FPC16_GPIO_Port, FPC16_Pin},
    {FPC17_GPIO_Port, FPC17_Pin},
    {FPC18_GPIO_Port, FPC18_Pin},
    {FPC19_GPIO_Port, FPC19_Pin},
    {FPC20_GPIO_Port, FPC20_Pin},
    {FPC21_GPIO_Port, FPC21_Pin},
    {FPC22_GPIO_Port, FPC22_Pin},
    {FPC23_GPIO_Port, FPC23_Pin},
    {FPC24_GPIO_Port, FPC24_Pin},
    {FPC25_GPIO_Port, FPC25_Pin},
    {FPC26_GPIO_Port, FPC26_Pin}
};

static uint32_t raw_matrix[MATRIX_LINES];

static uint32_t pin_number(uint16_t pin)
{
    return __builtin_ctz(pin);
}

static void Matrix_SetInput(uint32_t index)
{
    GPIO_TypeDef *port = matrix_pins[index].port;
    uint32_t pos = pin_number(matrix_pins[index].pin);

    /* MODER = 00 -> Input */
    port->MODER &= ~(3UL << (pos * 2));
}

static void Matrix_SetOutputLow(uint32_t index)
{
    GPIO_TypeDef *port = matrix_pins[index].port;
    uint16_t pin = matrix_pins[index].pin;
    uint32_t pos = pin_number(pin);

    /*
     * Сначала выставляем ODR=0,
     * чтобы при переключении в OUTPUT не возник HIGH.
     */
    port->BSRR = ((uint32_t)pin << 16);

    /* Open Drain */
    port->OTYPER |= pin;

    /* MODER = 01 -> General Purpose Output */
    port->MODER &= ~(3UL << (pos * 2));
    port->MODER |=  (1UL << (pos * 2));
}

static uint8_t Matrix_Read(uint32_t index)
{
    return
        (matrix_pins[index].port->IDR &
         matrix_pins[index].pin)
        ? 1 : 0;
}

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks =
        us * (HAL_RCC_GetHCLKFreq() / 1000000UL);

    while ((DWT->CYCCNT - start) < ticks)
    {
    }
}

void Matrix_Init(void)
{
    /*
     * Включаем DWT cycle counter.
     * Он нужен для коротких задержек порядка микросекунд.
     */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;

    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    for (uint32_t i = 0; i < MATRIX_LINES; i++)
    {
        raw_matrix[i] = 0;

        /*
         * CubeMX уже настроил:
         * INPUT + PULL-UP.
         */
        Matrix_SetInput(i);
    }
}

void Matrix_Scan(void)
{
    for (uint32_t src = 0; src < MATRIX_LINES; src++)
    {
        uint32_t state = 0;

        Matrix_SetOutputLow(src);

        /*
         * Даем сигналам установиться.
         */
        delay_us(8);

        for (uint32_t dst = 0; dst < MATRIX_LINES; dst++)
        {
            if (dst == src)
                continue;

            if (Matrix_Read(dst) == 0)
            {
                state |= (1UL << dst);
            }
        }

        raw_matrix[src] = state;

        /*
         * Возвращаем линию в INPUT + PULL-UP.
         */
        Matrix_SetInput(src);

        delay_us(1);
    }
}

const uint32_t *Matrix_GetRaw(void)
{
    return raw_matrix;
}
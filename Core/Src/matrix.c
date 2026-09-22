#include "matrix.h"
#include "matrix_debounce.h"
#include "main.h"
#include <string.h>

typedef struct
{
    GPIO_TypeDef *port;
    uint16_t pin;
} MatrixPin;

const uint8_t Matrix_DriveFpc[MATRIX_ROWS] = {17, 18, 19, 20, 21, 22, 23, 24};
const uint8_t Matrix_SenseFpc[MATRIX_COLS] =
    {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 25, 26};

static const MatrixPin drive_pins[MATRIX_ROWS] =
{
    {FPC17_GPIO_Port, FPC17_Pin}, {FPC18_GPIO_Port, FPC18_Pin},
    {FPC19_GPIO_Port, FPC19_Pin}, {FPC20_GPIO_Port, FPC20_Pin},
    {FPC21_GPIO_Port, FPC21_Pin}, {FPC22_GPIO_Port, FPC22_Pin},
    {FPC23_GPIO_Port, FPC23_Pin}, {FPC24_GPIO_Port, FPC24_Pin}
};

static const MatrixPin sense_pins[MATRIX_COLS] =
{
    {FPC01_GPIO_Port, FPC01_Pin}, {FPC02_GPIO_Port, FPC02_Pin},
    {FPC03_GPIO_Port, FPC03_Pin}, {FPC04_GPIO_Port, FPC04_Pin},
    {FPC05_GPIO_Port, FPC05_Pin}, {FPC06_GPIO_Port, FPC06_Pin},
    {FPC07_GPIO_Port, FPC07_Pin}, {FPC08_GPIO_Port, FPC08_Pin},
    {FPC09_GPIO_Port, FPC09_Pin}, {FPC10_GPIO_Port, FPC10_Pin},
    {FPC11_GPIO_Port, FPC11_Pin}, {FPC12_GPIO_Port, FPC12_Pin},
    {FPC13_GPIO_Port, FPC13_Pin}, {FPC14_GPIO_Port, FPC14_Pin},
    {FPC15_GPIO_Port, FPC15_Pin}, {FPC16_GPIO_Port, FPC16_Pin},
    {FPC25_GPIO_Port, FPC25_Pin}, {FPC26_GPIO_Port, FPC26_Pin}
};

static MatrixDebounce debounce;
static volatile MatrixSnapshot published;
static volatile uint8_t initialized;
static uint32_t ticks_per_us;

static void SetInput(const MatrixPin *pin)
{
    uint32_t pos = (uint32_t)__builtin_ctz(pin->pin);
    pin->port->MODER &= ~(3UL << (pos * 2U));
}

static void SetOutputLow(const MatrixPin *pin)
{
    uint32_t pos = (uint32_t)__builtin_ctz(pin->pin);
    /* Latch LOW and select open-drain before enabling the output. */
    pin->port->BSRR = (uint32_t)pin->pin << 16U;
    pin->port->OTYPER |= pin->pin;
    pin->port->MODER = (pin->port->MODER & ~(3UL << (pos * 2U))) |
                      (1UL << (pos * 2U));
}

static void DelayUs(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    while ((uint32_t)(DWT->CYCCNT - start) < us * ticks_per_us)
    {
    }
}

void Matrix_Init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    initialized = 0;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    ticks_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
    MatrixDebounce_Init(&debounce);
    MatrixSnapshot empty = {0};
    published = empty;

    /* MX_GPIO_Init configures all 26 pull-ups. Sense pins remain inputs
     * throughout scanning. Only one drive pin at a time becomes OD LOW. */
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        SetInput(&drive_pins[row]);
    }
    for (uint32_t col = 0; col < MATRIX_COLS; col++)
    {
        SetInput(&sense_pins[col]);
    }
    initialized = 1;
    __set_PRIMASK(primask);
}

void Matrix_Tick(void)
{
    if (!initialized)
    {
        return;
    }

    uint32_t start = DWT->CYCCNT;
    uint32_t raw[MATRIX_ROWS] = {0};
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        SetOutputLow(&drive_pins[row]);
        DelayUs(8);
        for (uint32_t col = 0; col < MATRIX_COLS; col++)
        {
            if ((sense_pins[col].port->IDR & sense_pins[col].pin) == 0U)
            {
                raw[row] |= 1UL << col;
            }
        }
        SetInput(&drive_pins[row]);
        DelayUs(1);
    }

    uint32_t now = HAL_GetTick();
    MatrixDebounce_Update(&debounce, raw, now);
    AntiGhostState ghost;
    AntiGhost_Analyze(debounce.stable, &ghost);
    AntiGhostState previous = published.ghost;
    if (memcmp(&ghost, &previous, sizeof(ghost)) != 0)
    {
        published.ghost_changes++;
    }
    published.ghost = ghost;
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        published.raw[row] = raw[row];
        published.debounced[row] = debounce.stable[row];
    }
    published.timestamp_ms = now;
    published.scan_count++;
    uint32_t elapsed_us = (uint32_t)(DWT->CYCCNT - start) / ticks_per_us;
    if (elapsed_us > published.max_scan_us)
    {
        published.max_scan_us = elapsed_us;
    }
}

void Matrix_GetSnapshot(MatrixSnapshot *snapshot)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *snapshot = published;
    __set_PRIMASK(primask);
}

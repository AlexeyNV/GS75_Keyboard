#include "runtime_timing.h"
#include "main.h"
static volatile uint32_t max_us, cycles_per_us;
void RuntimeTiming_Init(void)
{
    /* Matrix_Init has already enabled DWT. Do not reset its counter. */
    max_us = 0;
    cycles_per_us = HAL_RCC_GetHCLKFreq() / 1000000U;
}
uint32_t RuntimeTiming_Begin(void) { return DWT->CYCCNT; }
void RuntimeTiming_End(uint32_t start)
{
    if (cycles_per_us)
    {
        uint32_t elapsed = (uint32_t)(DWT->CYCCNT - start) / cycles_per_us;
        if (elapsed > max_us) { max_us = elapsed; }
    }
}
uint32_t RuntimeTiming_MaxUs(void) { return max_us; }

#ifndef MATRIX_H
#define MATRIX_H

#include <stdint.h>
#include "matrix_config.h"
#include "anti_ghosting.h"

typedef struct
{
    uint32_t raw[MATRIX_ROWS];
    uint32_t debounced[MATRIX_ROWS];
    AntiGhostState ghost; /* Debounced matrix, without suppression. */
    uint32_t ghost_changes;
    uint32_t scan_count;
    uint32_t timestamp_ms;
    uint32_t max_scan_us;
} MatrixSnapshot;

extern const uint8_t Matrix_DriveFpc[MATRIX_ROWS];
extern const uint8_t Matrix_SenseFpc[MATRIX_COLS];

/* Call after MX_GPIO_Init and clock configuration. */
void Matrix_Init(void);
/* Called only by the 1 ms SysTick USER CODE hook; safe before init. */
void Matrix_Tick(void);
/* Atomic copy for the main loop; no live pointers into ISR-owned state. */
void Matrix_GetSnapshot(MatrixSnapshot *snapshot);

#endif

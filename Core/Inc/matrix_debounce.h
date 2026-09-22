#ifndef MATRIX_DEBOUNCE_H
#define MATRIX_DEBOUNCE_H

#include <stdint.h>
#include "matrix_config.h"

typedef struct
{
    uint32_t candidate[MATRIX_ROWS];
    uint32_t stable[MATRIX_ROWS];
    uint32_t changed_at[MATRIX_ROWS][MATRIX_COLS];
} MatrixDebounce;

void MatrixDebounce_Init(MatrixDebounce *state);
void MatrixDebounce_Update(MatrixDebounce *state,
                           const uint32_t raw[MATRIX_ROWS], uint32_t now_ms);

#endif

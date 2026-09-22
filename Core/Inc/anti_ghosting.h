#ifndef ANTI_GHOSTING_H
#define ANTI_GHOSTING_H

#include <stdint.h>
#include "matrix_config.h"

typedef struct
{
    uint32_t ambiguous[MATRIX_ROWS];
    uint32_t row_pair_count;
    uint32_t rectangle_count;
} AntiGhostState;

/* Diagnostic only: never modifies the input or chooses a phantom key. */
void AntiGhost_Analyze(const uint32_t matrix[MATRIX_ROWS], AntiGhostState *result);

#endif

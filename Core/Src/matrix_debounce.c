#include "matrix_debounce.h"
#include <string.h>

void MatrixDebounce_Init(MatrixDebounce *state)
{
    memset(state, 0, sizeof(*state));
}

void MatrixDebounce_Update(MatrixDebounce *state,
                           const uint32_t raw[MATRIX_ROWS], uint32_t now_ms)
{
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        uint32_t sample = raw[row] & MATRIX_COL_MASK;
        uint32_t changed = sample ^ state->candidate[row];
        state->candidate[row] = sample;

        for (uint32_t col = 0; col < MATRIX_COLS; col++)
        {
            uint32_t bit = 1UL << col;
            if ((changed & bit) != 0U)
            {
                state->changed_at[row][col] = now_ms;
            }
            else if (((sample ^ state->stable[row]) & bit) != 0U &&
                     (uint32_t)(now_ms - state->changed_at[row][col]) >= MATRIX_DEBOUNCE_MS)
            {
                /* Symmetric per-key debounce; subtraction handles tick wrap. */
                state->stable[row] = (state->stable[row] & ~bit) | (sample & bit);
            }
        }
    }
}

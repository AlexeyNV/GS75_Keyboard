#include "anti_ghosting.h"
#include <string.h>

void AntiGhost_Analyze(const uint32_t matrix[MATRIX_ROWS], AntiGhostState *result)
{
    memset(result, 0, sizeof(*result));

    for (uint32_t first = 0; first < MATRIX_ROWS; first++)
    {
        for (uint32_t second = first + 1U; second < MATRIX_ROWS; second++)
        {
            uint32_t common = matrix[first] & matrix[second] & MATRIX_COL_MASK;
            if ((common & (common - 1U)) == 0U)
            {
                continue;
            }

            /* Two rows sharing >=2 columns form ambiguous rectangles.
             * Include unmapped coordinates: filtering before detection
             * would hide electrical paths. No key suppression. */
            result->ambiguous[first] |= common;
            result->ambiguous[second] |= common;
            result->row_pair_count++;

            uint32_t columns = 0;
            while (common != 0U)
            {
                common &= common - 1U;
                columns++;
            }
            result->rectangle_count += columns * (columns - 1U) / 2U;
        }
    }
}

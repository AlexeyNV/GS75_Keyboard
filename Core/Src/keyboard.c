#include "keyboard.h"
#include <string.h>

void Keyboard_Init(KeyboardState *state)
{
    memset(state, 0, sizeof(*state));
}

uint8_t Keyboard_KeyIsSet(const uint32_t keys[KEY_STATE_WORDS], KeyId key)
{
    if (key == KEY_NONE || (uint32_t)key >= KEY_ID_COUNT)
    {
        return 0;
    }
    uint32_t index = (uint32_t)key - 1U;
    return (keys[index / 32U] & (1UL << (index % 32U))) != 0U;
}

void Keyboard_Update(KeyboardState *state,
                     const uint32_t debounced[MATRIX_ROWS],
                     const uint32_t ambiguous[MATRIX_ROWS])
{
    KeyboardState next = {0};
    for (uint32_t row = 0; row < MATRIX_ROWS; row++)
    {
        for (uint32_t col = 0; col < MATRIX_COLS; col++)
        {
            uint32_t coordinate = 1UL << col;
            if ((debounced[row] & coordinate) == 0U)
            {
                continue; /* Electrically absent: release even in ambiguity. */
            }
            KeyId key = Keymap_At(row, col);
            if (key == KEY_NONE)
            {
                continue;
            }
            uint32_t index = (uint32_t)key - 1U;
            uint32_t word = index / 32U;
            uint32_t bit = 1UL << (index % 32U);
            if ((ambiguous[row] & coordinate) != 0U)
            {
                next.ambiguous[word] |= bit;
                if ((state->pressed[word] & bit) == 0U)
                {
                    next.suppressed[word] |= bit;
                    continue; /* Do not guess which new corner is physical. */
                }
            }
            /* Outside ambiguity accept exactly the mapped electrical state.
             * Inside it retain only previously confirmed compatible keys. */
            next.pressed[word] |= bit;
        }
    }
    *state = next;
}

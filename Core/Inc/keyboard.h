#ifndef KEYBOARD_H
#define KEYBOARD_H

#include "keymap.h"

typedef struct
{
    /* Bit (KeyId - 1), so KEY_NONE never has a bit. */
    uint32_t pressed[KEY_STATE_WORDS];
    uint32_t suppressed[KEY_STATE_WORDS];
    uint32_t ambiguous[KEY_STATE_WORDS]; /* Includes retained pressed keys. */
} KeyboardState;

void Keyboard_Init(KeyboardState *state);
/* Feed every debounced sample in order. Ambiguous must come from the same
 * full 8x18 matrix, BEFORE ignoring unmapped coordinates. */
void Keyboard_Update(KeyboardState *state,
                     const uint32_t debounced[MATRIX_ROWS],
                     const uint32_t ambiguous[MATRIX_ROWS]);
uint8_t Keyboard_KeyIsSet(const uint32_t keys[KEY_STATE_WORDS], KeyId key);

#endif

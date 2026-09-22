#ifndef KEYBOARD_RUNTIME_H
#define KEYBOARD_RUNTIME_H

#include "keyboard.h"

typedef struct
{
    KeyboardState keys;
    uint32_t scan_count;
    uint32_t timestamp_ms;
} KeyboardSnapshot;

void KeyboardRuntime_Init(void);
/* SysTick only, after Matrix_Tick. No UART, parsing, or main-loop polling. */
void KeyboardRuntime_Tick(void);
void KeyboardRuntime_GetSnapshot(KeyboardSnapshot *snapshot);

#endif

#include "keyboard_runtime.h"
#include "matrix.h"
#include "main.h"
#include "usb_keyboard.h"

static KeyboardState state;
static volatile KeyboardSnapshot published;
static volatile uint8_t initialized;

void KeyboardRuntime_Init(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    initialized = 0;
    Keyboard_Init(&state);
    KeyboardSnapshot empty = {0};
    published = empty;
    initialized = 1;
    __set_PRIMASK(primask);
}

void KeyboardRuntime_Tick(void)
{
    if (!initialized)
    {
        return;
    }
    MatrixSnapshot matrix;
    Matrix_GetSnapshot(&matrix);
    if (matrix.scan_count == published.scan_count)
    {
        return; /* No completed scan yet, or this sample was already consumed. */
    }

    /* Electrical ghost detection includes unmapped coordinates. Mapping the
     * matrix before detection would hide some phantom paths. */
    Keyboard_Update(&state, matrix.debounced, matrix.ghost.ambiguous);
    UsbKeyboard_Capture(&state);
    published.keys = state;
    published.scan_count = matrix.scan_count;
    published.timestamp_ms = matrix.timestamp_ms;
}

void KeyboardRuntime_GetSnapshot(KeyboardSnapshot *snapshot)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    *snapshot = published;
    __set_PRIMASK(primask);
}

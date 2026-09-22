#include "hid_report.h"
#include "hid_keymap.h"
#include <string.h>

void HidReport_AddUsage(HidEffectiveState *state, uint8_t usage)
{
    state->modifiers |= HidKeymap_Modifier(usage);
    if (usage >= HID_USAGE_MIN && usage <= HID_USAGE_MAX)
    {
        uint32_t bit = usage - HID_USAGE_MIN;
        state->bitmap[bit / 8U] |= (uint8_t)(1U << (bit % 8U));
    }
}

void HidReport_BuildNkro(const HidEffectiveState *state, HidNkroReport *report)
{
    report->modifiers = state->modifiers;
    memcpy(report->bitmap, state->bitmap, sizeof(report->bitmap));
    report->bitmap[HID_BITMAP_BYTES - 1U] &= 1U; /* Seven constant padding bits. */
}

void HidReport_BuildBoot(const HidEffectiveState *state, HidBootReport *report)
{
    memset(report, 0, sizeof(*report));
    report->modifiers = state->modifiers;
    uint32_t count = 0;
    for (uint32_t usage = HID_USAGE_MIN; usage <= HID_USAGE_MAX; usage++)
    {
        uint32_t bit = usage - HID_USAGE_MIN;
        if ((state->bitmap[bit / 8U] & (1U << (bit % 8U))) != 0U)
        {
            if (count == sizeof(report->keys))
            {
                memset(report->keys, 0x01, sizeof(report->keys)); /* ErrorRollOver */
                return;
            }
            report->keys[count++] = (uint8_t)usage;
        }
    }
}

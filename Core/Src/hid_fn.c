#include "hid_fn.h"
#include "hid_keymap.h"
#include <string.h>

void HidFn_Translate(const KeyboardState *logical, HidEffectiveState *effective)
{
    uint8_t consumer;
    HidFn_TranslateOutputs(logical, effective, &consumer);
}

void HidFn_TranslateOutputs(const KeyboardState *logical, HidEffectiveState *effective,
                            uint8_t *consumer)
{
    memset(effective, 0, sizeof(*effective));
    *consumer = 0;
    uint8_t fn = Keyboard_KeyIsSet(logical->pressed, KEY_FN);
    for (uint32_t id = 1; id < KEY_ID_COUNT; id++)
    {
        if (!Keyboard_KeyIsSet(logical->pressed, (KeyId)id)) { continue; }
        uint8_t usage = HidKeymap_Usage((KeyId)id);
        if (fn)
        {
            switch ((KeyId)id)
            {
                case KEY_PAGE_UP_HOME: usage = 0x4A; break;
                case KEY_PAGE_DOWN_END: usage = 0x4D; break;
                case KEY_PRINT_SCREEN: usage = 0x9A; break;
                case KEY_PAUSE_BREAK:
                    /* Deterministically synthesize Left Ctrl; preserve real modifiers. */
                    HidReport_AddUsage(effective, 0xE0);
                    break;
                case KEY_ARROW_LEFT: usage = 0; *consumer |= HID_CONSUMER_VOLUME_DOWN; break;
                case KEY_ARROW_RIGHT: usage = 0; *consumer |= HID_CONSUMER_VOLUME_UP; break;
                case KEY_NUMPAD_0: usage = 0; *consumer |= HID_CONSUMER_MUTE; break;
                default: break;
            }
        }
        HidReport_AddUsage(effective, usage);
    }
}

#ifndef HID_FN_H
#define HID_FN_H
#include "keyboard.h"
#include "hid_report.h"
#define HID_CONSUMER_MUTE 0x01U
#define HID_CONSUMER_VOLUME_UP 0x02U
#define HID_CONSUMER_VOLUME_DOWN 0x04U
#define HID_CONSUMER_MASK 0x07U
/* Only accepted keys. Never modifies the input or suppression policy. */
void HidFn_Translate(const KeyboardState *logical, HidEffectiveState *effective);
/* Both outputs come from the SAME accepted snapshot. No matrix mutation. */
void HidFn_TranslateOutputs(const KeyboardState *logical, HidEffectiveState *keyboard,
                            uint8_t *consumer);
#endif

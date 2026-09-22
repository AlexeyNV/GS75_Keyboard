#ifndef HID_KEYMAP_H
#define HID_KEYMAP_H
#include "keymap.h"
/* Page 0x07 usage; zero means internal/unmapped. IDs remain unchanged. */
uint8_t HidKeymap_Usage(KeyId key);
uint8_t HidKeymap_Modifier(uint8_t usage);
#endif

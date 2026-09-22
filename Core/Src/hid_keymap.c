#include "hid_keymap.h"

/* Page 0x07. Reviewed fixture: docs/HID_KEYMAP.csv. */
static const uint8_t usages[KEY_ID_COUNT] =
{
    0, /* KEY_NONE */
    0x29, /* 1: KEY_ESC */
    0x3A, /* 2: KEY_F1 */
    0x3B, /* 3: KEY_F2 */
    0x3C, /* 4: KEY_F3 */
    0x3D, /* 5: KEY_F4 */
    0x3E, /* 6: KEY_F5 */
    0x3F, /* 7: KEY_F6 */
    0x40, /* 8: KEY_F7 */
    0x41, /* 9: KEY_F8 */
    0x42, /* 10: KEY_F9 */
    0x43, /* 11: KEY_F10 */
    0x44, /* 12: KEY_F11 */
    0x45, /* 13: KEY_F12 */
    0x46, /* 14: KEY_PRINT_SCREEN */
    0x47, /* 15: KEY_SCROLL_LOCK */
    0x48, /* 16: KEY_PAUSE_BREAK */
    0x49, /* 17: KEY_INSERT */
    0x4C, /* 18: KEY_DELETE */
    0x4B, /* 19: KEY_PAGE_UP_HOME */
    0x4E, /* 20: KEY_PAGE_DOWN_END */
    0x35, /* 21: KEY_GRAVE */
    0x1E, /* 22: KEY_DIGIT_1 */
    0x1F, /* 23: KEY_DIGIT_2 */
    0x20, /* 24: KEY_DIGIT_3 */
    0x21, /* 25: KEY_DIGIT_4 */
    0x22, /* 26: KEY_DIGIT_5 */
    0x23, /* 27: KEY_DIGIT_6 */
    0x24, /* 28: KEY_DIGIT_7 */
    0x25, /* 29: KEY_DIGIT_8 */
    0x26, /* 30: KEY_DIGIT_9 */
    0x27, /* 31: KEY_DIGIT_0 */
    0x2D, /* 32: KEY_MINUS */
    0x2E, /* 33: KEY_EQUAL */
    0x2A, /* 34: KEY_BACKSPACE */
    0x53, /* 35: KEY_NUM_LOCK */
    0x54, /* 36: KEY_NUMPAD_DIVIDE */
    0x55, /* 37: KEY_NUMPAD_MULTIPLY */
    0x56, /* 38: KEY_NUMPAD_MINUS */
    0x2B, /* 39: KEY_TAB */
    0x14, /* 40: KEY_Q */
    0x1A, /* 41: KEY_W */
    0x08, /* 42: KEY_E */
    0x15, /* 43: KEY_R */
    0x17, /* 44: KEY_T */
    0x1C, /* 45: KEY_Y */
    0x18, /* 46: KEY_U */
    0x0C, /* 47: KEY_I */
    0x12, /* 48: KEY_O */
    0x13, /* 49: KEY_P */
    0x2F, /* 50: KEY_LEFT_BRACKET */
    0x30, /* 51: KEY_RIGHT_BRACKET */
    0x31, /* 52: KEY_BACKSLASH */
    0x5F, /* 53: KEY_NUMPAD_7_HOME */
    0x60, /* 54: KEY_NUMPAD_8_UP */
    0x61, /* 55: KEY_NUMPAD_9_PGUP */
    0x57, /* 56: KEY_NUMPAD_PLUS */
    0x39, /* 57: KEY_CAPS_LOCK */
    0x04, /* 58: KEY_A */
    0x16, /* 59: KEY_S */
    0x07, /* 60: KEY_D */
    0x09, /* 61: KEY_F */
    0x0A, /* 62: KEY_G */
    0x0B, /* 63: KEY_H */
    0x0D, /* 64: KEY_J */
    0x0E, /* 65: KEY_K */
    0x0F, /* 66: KEY_L */
    0x33, /* 67: KEY_SEMICOLON */
    0x34, /* 68: KEY_APOSTROPHE */
    0x28, /* 69: KEY_ENTER */
    0x5C, /* 70: KEY_NUMPAD_4_LEFT */
    0x5D, /* 71: KEY_NUMPAD_5 */
    0x5E, /* 72: KEY_NUMPAD_6_RIGHT */
    0xE1, /* 73: KEY_LEFT_SHIFT */
    0x1D, /* 74: KEY_Z */
    0x1B, /* 75: KEY_X */
    0x06, /* 76: KEY_C */
    0x19, /* 77: KEY_V */
    0x05, /* 78: KEY_B */
    0x11, /* 79: KEY_N */
    0x10, /* 80: KEY_M */
    0x36, /* 81: KEY_COMMA */
    0x37, /* 82: KEY_PERIOD */
    0x38, /* 83: KEY_SLASH */
    0xE5, /* 84: KEY_RIGHT_SHIFT */
    0x52, /* 85: KEY_ARROW_UP */
    0x59, /* 86: KEY_NUMPAD_1_END */
    0x5A, /* 87: KEY_NUMPAD_2_DOWN */
    0x5B, /* 88: KEY_NUMPAD_3_PGDN */
    0x58, /* 89: KEY_NUMPAD_ENTER */
    0xE0, /* 90: KEY_LEFT_CTRL */
    0x00, /* 91: KEY_FN */
    0xE2, /* 92: KEY_LEFT_ALT */
    0x2C, /* 93: KEY_SPACE */
    0x64, /* 94: KEY_PIPE_BACKSLASH */
    0xE6, /* 95: KEY_RIGHT_ALTGR */
    0xE7, /* 96: KEY_WIN */
    0xE4, /* 97: KEY_RIGHT_CTRL */
    0x50, /* 98: KEY_ARROW_LEFT */
    0x51, /* 99: KEY_ARROW_DOWN */
    0x4F, /* 100: KEY_ARROW_RIGHT */
    0x62, /* 101: KEY_NUMPAD_0 */
    0x63, /* 102: KEY_NUMPAD_DECIMAL */
};

uint8_t HidKeymap_Usage(KeyId key)
{
    return (uint32_t)key < KEY_ID_COUNT ? usages[key] : 0;
}

uint8_t HidKeymap_Modifier(uint8_t usage)
{
    return usage >= 0xE0U && usage <= 0xE7U ? (uint8_t)(1U << (usage - 0xE0U)) : 0;
}

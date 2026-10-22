# Planned RGB backlight shortcuts

**Design note only. None of the RGB/backlight actions below is implemented.**
The keyboard firmware does not drive the RGB controller, configure its
SPI/PWM/interrupt lines or enable +5V_RGB. The separate RGB FPC pinout scanner
remains a different project; this document does not establish its protocol.

| Proposed keyboard chord | Planned action | Status |
|---|---|---|
| Fn + Up Arrow (`KEY_FN` + `KEY_ARROW_UP`) | Increase brightness | Planned |
| Fn + Down Arrow (`KEY_FN` + `KEY_ARROW_DOWN`) | Decrease brightness | Planned |
| Fn + Up Arrow + Down Arrow | Toggle backlight on/off | Planned |
| Fn + Numpad Plus (`KEY_NUMPAD_PLUS`) | Next lighting mode | Planned |
| Fn + Numpad Minus (`KEY_NUMPAD_MINUS`) | Previous lighting mode | Planned |

The simultaneous Up+Down toggle will need explicit precedence over the two
brightness actions and one action per chord transition. This is a future
behavioral requirement, not a claim about the present firmware. A future
implementation also needs a verified RGB controller protocol, independent
power-budget and suspend behavior, and hardware protection tests before
enabling the branch.

Existing and user-observed shortcuts are **separate from RGB**:

| Existing chord | Current action |
|---|---|
| Fn + Left/Right Arrow | Consumer Control volume down/up |
| Fn + Numpad 0 | Consumer Control mute |

See [USB HID](USB_HID.md) for the implemented Fn translation and
[Rev.2 hardware record](POWER_TEST_RECORD.md) for the limited board results.

#ifndef HID_REPORT_H
#define HID_REPORT_H
#include <stdint.h>
#define HID_USAGE_MIN 0x04U
#define HID_USAGE_MAX 0xA4U
#define HID_BITMAP_BYTES ((HID_USAGE_MAX - HID_USAGE_MIN + 1U + 7U) / 8U)
#define HID_BOOT_SIZE 8U
#define HID_NKRO_SIZE (1U + HID_BITMAP_BYTES)
#define HID_IN_PACKET_SIZE 32U
#define HID_PROTOCOL_BOOT 0U
#define HID_PROTOCOL_REPORT 1U
typedef struct { uint8_t modifiers; uint8_t bitmap[HID_BITMAP_BYTES]; } HidEffectiveState;
typedef struct { uint8_t modifiers, reserved, keys[6]; } HidBootReport;
typedef struct { uint8_t modifiers; uint8_t bitmap[HID_BITMAP_BYTES]; } HidNkroReport;
_Static_assert(sizeof(HidBootReport) == 8U, "Boot report must be eight bytes");
_Static_assert(sizeof(HidNkroReport) == 22U, "161 usage bits plus modifiers");
_Static_assert(HID_IN_PACKET_SIZE >= sizeof(HidNkroReport), "IN packet too small");
void HidReport_AddUsage(HidEffectiveState *state, uint8_t usage);
void HidReport_BuildNkro(const HidEffectiveState *state, HidNkroReport *report);
void HidReport_BuildBoot(const HidEffectiveState *state, HidBootReport *report);
#endif

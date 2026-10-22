#ifndef USB_C_DETECT_H
#define USB_C_DETECT_H
#include <stdint.h>
/* Encoded OUT1:bit1, OUT2:bit0. All four electrical codes are legal. */
typedef struct {
    uint32_t raw, stable, valid, raw_since_ms, changed_ms, attaches, detaches;
} UsbCDetect;
void UsbCDetect_Init(UsbCDetect *s, uint32_t now);
void UsbCDetect_Update(UsbCDetect *s, uint32_t raw, uint32_t now, uint8_t gap);
const char *UsbCDetect_Name(uint32_t code);
#endif

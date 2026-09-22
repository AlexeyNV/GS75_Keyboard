#ifndef USB_DESCRIPTORS_H
#define USB_DESCRIPTORS_H
#include "usbd_def.h"
/* ST HID example identity, DEVELOPMENT ONLY. See docs/USB_HID.md. */
#ifndef GS75_USB_VID
#define GS75_USB_VID 0x0483U
#endif
#ifndef GS75_USB_PID
#define GS75_USB_PID 0x5710U
#endif
extern USBD_DescriptorsTypeDef UsbDescriptors;
extern uint8_t UsbReportDescriptor[];
extern const uint16_t UsbReportDescriptorSize;
extern uint8_t UsbConsumerDescriptor[];
extern const uint16_t UsbConsumerDescriptorSize;
uint8_t *UsbDescriptors_Config(uint16_t *length);
uint8_t *UsbDescriptors_Hid(uint16_t *length);
uint8_t *UsbDescriptors_ConsumerHid(uint16_t *length);
void UsbDescriptors_Init(const uint32_t uid[3]);
#endif

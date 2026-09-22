#ifndef USB_DEVICE_H
#define USB_DEVICE_H
#include "usbd_core.h"
extern USBD_HandleTypeDef UsbDevice;
extern USBD_ClassTypeDef UsbKeyboardClass;
void UsbDevice_Init(void);
void UsbDevice_IRQHandler(void);
/* Called only from USB control IRQ, with UART interrupts still enabled. */
uint8_t UsbDevice_CancelIn(void);
uint8_t UsbDevice_CancelEndpoint(uint8_t ep);
#endif

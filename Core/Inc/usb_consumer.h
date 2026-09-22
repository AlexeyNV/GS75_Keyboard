#ifndef USB_CONSUMER_H
#define USB_CONSUMER_H
#include <stdint.h>
#define HID_CONSUMER_PACKET_SIZE 8U
typedef struct
{
    uint32_t tx_count, last_report, busy, resync_count;
} UsbConsumerInfo;
/* Init/reset and TrySend are serialized by the shared scheduler's IRQ mask. */
void UsbConsumer_Init(void);
void UsbConsumer_Reset(void);
uint8_t UsbConsumer_TrySend(uint8_t bits, uint8_t force);
uint8_t UsbConsumer_Busy(void);
uint8_t UsbConsumer_LastBits(void);
void UsbConsumer_Complete(void);
void UsbConsumer_GetInfo(UsbConsumerInfo *info);
uint8_t UsbConsumerPort_Send(uint8_t *data);
uint8_t UsbConsumerPort_Cancel(void);
#endif

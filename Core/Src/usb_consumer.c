#include "usb_consumer.h"
#include "hid_fn.h"
#include "main.h"

static volatile uint8_t busy, valid, last_bits;
static volatile uint32_t tx_count, resync_count;
/* HAL writes whole FIFO words even for a 1-byte report. Keep padded storage. */
static uint8_t transfer[HID_CONSUMER_PACKET_SIZE];
_Static_assert(HID_CONSUMER_PACKET_SIZE >= 4U, "Rounded FIFO read storage");

void UsbConsumer_Reset(void) { busy = valid = last_bits = 0; }
void UsbConsumer_Init(void) { UsbConsumer_Reset(); tx_count = resync_count = 0; }
uint8_t UsbConsumer_Busy(void) { return busy; }
uint8_t UsbConsumer_LastBits(void) { return last_bits; }

uint8_t UsbConsumer_TrySend(uint8_t bits, uint8_t force)
{
    bits &= HID_CONSUMER_MASK;
    if (!force && valid && bits == last_bits) { return 1; }
    if (busy) { return 0; }
    transfer[0] = bits;
    if (!UsbConsumerPort_Send(transfer)) { return 0; }
    busy = valid = 1;
    last_bits = bits;
    if (force) { resync_count++; }
    return 1;
}

void UsbConsumer_Complete(void)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (busy) { busy = 0; tx_count++; }
    __set_PRIMASK(mask);
}

void UsbConsumer_GetInfo(UsbConsumerInfo *info)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    info->tx_count = tx_count;
    info->last_report = last_bits;
    info->busy = busy;
    info->resync_count = resync_count;
    __set_PRIMASK(mask);
}

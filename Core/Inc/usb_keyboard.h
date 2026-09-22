#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H
#include "keyboard.h"
#include "hid_report.h"
#define USB_KEYBOARD_QUEUE_CAPACITY 64U
typedef struct
{
    uint32_t state, configured, protocol, idle, led_mask, tx_count;
    uint32_t queue_depth, queue_max, queue_overflow, resync_count, last_report_size;
} UsbKeyboardInfo;
void UsbKeyboard_Init(void);
/* Post-scan ISR: enqueue accepted bits only; never calls USB APIs. */
void UsbKeyboard_Capture(const KeyboardState *state);
void UsbKeyboard_Task(uint32_t now_ms);
/* USB control/completion callbacks, also callable by host regression harness. */
void UsbKeyboard_Reset(void);
void UsbKeyboard_Configure(uint8_t configured);
void UsbKeyboard_Resync(void);
uint8_t UsbKeyboard_SetProtocol(uint8_t protocol);
uint8_t UsbKeyboard_GetProtocol(void);
void UsbKeyboard_SetIdle(uint8_t duration);
uint8_t UsbKeyboard_GetIdle(void);
void UsbKeyboard_SetLeds(uint8_t leds);
uint8_t UsbKeyboard_GetLeds(void);
void UsbKeyboard_TxComplete(uint32_t now_ms);
uint16_t UsbKeyboard_GetReport(uint8_t *report);
uint8_t UsbKeyboard_GetConsumerBits(void);
void UsbKeyboard_GetInfo(UsbKeyboardInfo *info);
/* Send runs under a SHORT interrupt mask: no waits, delays or UART.
 * Cancel runs in USB control context with UART interrupts enabled. */
uint8_t UsbKeyboardPort_Send(uint8_t *report, uint16_t length);
uint8_t UsbKeyboardPort_Cancel(void);
uint32_t UsbKeyboardPort_State(void);
#endif

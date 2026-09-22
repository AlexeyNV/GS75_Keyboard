#include "usb_descriptors.h"
#include "hid_report.h"
#include "usb_consumer.h"

uint8_t UsbReportDescriptor[] =
{
    0x05,0x01, 0x09,0x06, 0xA1,0x01, /* Generic Desktop / Keyboard / Application */
    0x05,0x07, 0x19,0xE0, 0x29,0xE7, /* Eight modifiers */
    0x15,0x00, 0x25,0x01, 0x75,0x01, 0x95,0x08, 0x81,0x02,
    0x19,HID_USAGE_MIN, 0x29,HID_USAGE_MAX,
    0x75,0x01, 0x95,0xA1, 0x81,0x02, /* 161 keyboard usage bits */
    0x75,0x01, 0x95,0x07, 0x81,0x03, /* Seven constant padding bits */
    0x05,0x08, 0x19,0x01, 0x29,0x05, /* LEDs: Num/Caps/Scroll/Compose/Kana */
    0x75,0x01, 0x95,0x05, 0x91,0x02,
    0x75,0x01, 0x95,0x03, 0x91,0x03,
    0xC0
};
const uint16_t UsbReportDescriptorSize = sizeof(UsbReportDescriptor);
uint8_t UsbConsumerDescriptor[] =
{
    0x05,0x0C, 0x09,0x01, 0xA1,0x01, /* Consumer / Consumer Control / Application */
    0x15,0x00, 0x25,0x01,
    0x09,0xE2, 0x09,0xE9, 0x09,0xEA, /* Mute, Volume Increment, Volume Decrement */
    0x75,0x01, 0x95,0x03, 0x81,0x02,
    0x75,0x01, 0x95,0x05, 0x81,0x03, /* Five constant padding bits */
    0xC0
};
const uint16_t UsbConsumerDescriptorSize = sizeof(UsbConsumerDescriptor);
static uint8_t config[] =
{
    9,2, 59,0, 2,1,0, 0x80,USBD_MAX_POWER,
    9,4, 0,0,1, 3,1,1,0, /* One HID Boot Keyboard interface */
    9,0x21, 0x11,0x01,0,1,0x22, sizeof(UsbReportDescriptor),0,
    7,5, 0x81,3, HID_IN_PACKET_SIZE,0,1,
    9,4, 1,0,1, 3,0,0,0, /* Independent non-Boot Consumer interface */
    9,0x21, 0x11,0x01,0,1,0x22, sizeof(UsbConsumerDescriptor),0,
    7,5, 0x82,3, HID_CONSUMER_PACKET_SIZE,0,1
};
_Static_assert(sizeof(config) == 59U, "Two-interface configuration descriptor length");
_Static_assert(sizeof(UsbReportDescriptor) < 256U, "Descriptor length encoding");
_Static_assert(sizeof(UsbConsumerDescriptor) < 256U, "Consumer descriptor length encoding");
static uint8_t device[] =
{
    18,1,0x00,0x02, 0,0,0,64,
    LOBYTE(GS75_USB_VID),HIBYTE(GS75_USB_VID),
    LOBYTE(GS75_USB_PID),HIBYTE(GS75_USB_PID),
    0x00,0x01,1,2,3,1
};
static uint8_t language[] = {4,3,0x09,0x04};
static uint8_t string_buffer[USBD_MAX_STR_DESC_SIZ];
static char serial[25];

void UsbDescriptors_Init(const uint32_t uid[3])
{
    static const char hex[] = "0123456789ABCDEF";
    for (uint32_t word = 0; word < 3U; word++)
    {
        for (uint32_t digit = 0; digit < 8U; digit++)
        {
            serial[word * 8U + digit] = hex[(uid[word] >> (28U - digit * 4U)) & 15U];
        }
    }
    serial[24] = '\0';
}

uint8_t *UsbDescriptors_Config(uint16_t *length) { *length = sizeof(config); return config; }
uint8_t *UsbDescriptors_Hid(uint16_t *length) { *length = 9; return config + 18; }
uint8_t *UsbDescriptors_ConsumerHid(uint16_t *length) { *length = 9; return config + 43; }
static uint8_t *Device(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = sizeof(device); return device; }
static uint8_t *Language(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; *length = sizeof(language); return language; }
static uint8_t *String(const char *text, uint16_t *length)
{
    uint16_t n = 2;
    while (*text && n + 1U < sizeof(string_buffer))
    {
        string_buffer[n++] = (uint8_t)*text++;
        string_buffer[n++] = 0;
    }
    string_buffer[0] = (uint8_t)n;
    string_buffer[1] = 3;
    *length = n;
    return string_buffer;
}
static uint8_t *Manufacturer(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; return String("GS75 Adapter", length); }
static uint8_t *Product(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; return String("GS75 Keyboard Adapter", length); }
static uint8_t *Serial(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; return String(serial, length); }
static uint8_t *Interface(USBD_SpeedTypeDef speed, uint16_t *length)
{ (void)speed; return String("Boot / NKRO Keyboard", length); }
USBD_DescriptorsTypeDef UsbDescriptors =
{ Device, Language, Manufacturer, Product, Serial, Interface, Interface };

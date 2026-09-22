/* Real ST core + real HID class; only the FS peripheral/endpoint is modeled. */
#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_keyboard.h"
#include "runtime_timing.h"
#include <string.h>

USBD_HandleTypeDef UsbDevice;
static uint8_t stalled[32], blocked[3], pending[3];
static uint8_t *pending_data[3], *out_data;
static uint32_t pending_size[3], out_size, rx_size, errors;
static uint8_t control_reply[256], reports[4096][HID_NKRO_SIZE];
static uint32_t control_size, report_count, report_sizes[4096];
static uint8_t consumer_reports[4096];
static uint32_t consumer_count;
static uint8_t event_ep[8192], event_data[8192][HID_NKRO_SIZE];
static uint32_t event_count, event_sizes[8192];
static uint8_t interleave;
void Test_SetTick(uint32_t tick);

static uint32_t Index(uint8_t ep) { return (ep & 15U) + ((ep & 0x80U) ? 16U : 0U); }
uint32_t RuntimeTiming_MaxUs(void) { return 0; }
uint8_t UsbDevice_CancelEndpoint(uint8_t ep)
{ if (ep != 1 && ep != 2) { return 0; } pending[ep] = 0; return 1; }
uint8_t UsbDevice_CancelIn(void) { return UsbDevice_CancelEndpoint(1); }
void Test_UsbBlock(uint8_t value) { blocked[1] = value; }
void Test_ConsumerBlock(uint8_t value) { blocked[2] = value; }
uint32_t Test_ConsumerCount(void) { return consumer_count; }
uint8_t Test_ConsumerReport(uint32_t index) { return index < consumer_count ? consumer_reports[index] : 0; }
uint32_t Test_UsbEventCount(void) { return event_count; }
uint8_t Test_UsbEventEp(uint32_t index) { return index < event_count ? event_ep[index] : 0; }
uint32_t Test_UsbEventSize(uint32_t index) { return index < event_count ? event_sizes[index] : 0; }
const uint8_t *Test_UsbEventData(uint32_t index) { return index < event_count ? event_data[index] : NULL; }
uint32_t Test_UsbErrors(void) { return errors; }
uint32_t Test_UsbCount(void) { return report_count; }
uint32_t Test_UsbSize(uint32_t index) { return index < report_count ? report_sizes[index] : 0; }
const uint8_t *Test_UsbReport(uint32_t index) { return index < report_count ? reports[index] : NULL; }
uint32_t Test_ControlSize(void) { return control_size; }
const uint8_t *Test_ControlData(void) { return control_reply; }
uint8_t Test_ControlStalled(void) { return stalled[0] || stalled[16]; }
uint8_t Test_UsbPending(void) { return pending[1]; }
uint8_t Test_ConsumerPending(void) { return pending[2]; }

void Test_UsbSetup(uint8_t bm, uint8_t request, uint16_t value, uint16_t index, uint16_t length)
{
    uint8_t setup[8] = {bm, request, LOBYTE(value), HIBYTE(value),
                       LOBYTE(index), HIBYTE(index), LOBYTE(length), HIBYTE(length)};
    control_size = 0;
    stalled[0] = stalled[16] = 0;
    out_data = NULL;
    out_size = rx_size = 0;
    (void)USBD_LL_SetupStage(&UsbDevice, setup);
}
void Test_PlanUsbInterleave(uint8_t action) { interleave = action; }
void Test_UsbOnUnmask(void)
{
    uint8_t action = interleave;
    interleave = 0; /* Allow nested critical sections inside the injected ISR. */
    if (action == 1) { Test_UsbSetup(0x21, 0x0b, 0, 0, 0); }
    if (action == 2)
    {
        KeyboardState s = {0};
        for (uint32_t i = 0; i < 80U; i++)
        {
            s.pressed[(KEY_B - 1U) / 32U] = (i & 1U) ? 0 : 1UL << ((KEY_B - 1U) % 32U);
            UsbKeyboard_Capture(&s);
        }
    }
    if (action == 3) { Test_UsbSetup(0x21, 0x0a, 0, 0, 0); }
}
void Test_UsbOutput(uint8_t value, uint32_t size)
{
    rx_size = size;
    if (out_data && out_size && size <= out_size)
    {
        out_data[0] = value;
        (void)USBD_LL_DataOutStage(&UsbDevice, 0, out_data);
    }
    else { errors++; }
}
void Test_UsbCompleteEndpoint(uint8_t ep, uint32_t now)
{
    Test_SetTick(now);
    if ((ep != 1 && ep != 2) || UsbDevice.dev_state != USBD_STATE_CONFIGURED) { return; }
    if (pending[ep])
    {
        if (report_count >= 4096U || consumer_count >= 4096U || event_count >= 8192U)
        { errors++; return; }
        if (ep == 1)
        {
            memcpy(reports[report_count], pending_data[ep], pending_size[ep]);
            report_sizes[report_count++] = pending_size[ep];
        }
        else { consumer_reports[consumer_count++] = pending_data[ep][0]; }
        event_ep[event_count] = ep;
        event_sizes[event_count] = pending_size[ep];
        memcpy(event_data[event_count++], pending_data[ep], pending_size[ep]);
        pending[ep] = 0;
        (void)USBD_LL_DataInStage(&UsbDevice, ep, pending_data[ep]);
    }
}
void Test_UsbComplete(uint32_t now)
{ Test_UsbCompleteEndpoint(1, now); Test_UsbCompleteEndpoint(2, now); }
void Test_UsbInit(void)
{
    memset(&UsbDevice, 0, sizeof(UsbDevice));
    memset(stalled, 0, sizeof(stalled));
    memset(blocked, 0, sizeof(blocked));
    memset(pending, 0, sizeof(pending));
    consumer_count = event_count = 0;
    report_count = errors = control_size = 0;
    uint32_t uid[3] = {0x12345678,0x90ABCDEF,0x12345678};
    UsbDescriptors_Init(uid);
    (void)USBD_Init(&UsbDevice, &UsbDescriptors, 0);
    (void)USBD_RegisterClass(&UsbDevice, &UsbKeyboardClass);
    (void)USBD_LL_SetSpeed(&UsbDevice, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(&UsbDevice);
    UsbKeyboard_Reset();
    Test_UsbSetup(0x00, USB_REQ_SET_ADDRESS, 1, 0, 0);
    Test_UsbSetup(0x00, USB_REQ_SET_CONFIGURATION, 1, 0, 0);
}
void Test_UsbBusReset(void)
{
    memset(pending, 0, sizeof(pending));
    (void)USBD_LL_Reset(&UsbDevice);
    UsbKeyboard_Reset();
}
void Test_UsbSuspend(void) { (void)USBD_LL_Suspend(&UsbDevice); }
void Test_UsbResume(void) { (void)USBD_LL_Resume(&UsbDevice); }

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *dev) { (void)dev; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *dev) { (void)dev; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *dev) { (void)dev; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *dev) { (void)dev; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t type, uint16_t size)
{ (void)dev; (void)ep; (void)type; (void)size; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; if (ep == 0x81 || ep == 0x82) { pending[ep & 15U] = 0; } return USBD_OK; }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; if (ep == 0x81 || ep == 0x82) { pending[ep & 15U] = 0; } return USBD_OK; }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; stalled[Index(ep)] = 1; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; stalled[Index(ep)] = 0; return USBD_OK; }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; return stalled[Index(ep)]; }
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *dev, uint8_t address)
{ (void)dev; (void)address; return USBD_OK; }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t *data, uint32_t size)
{
    (void)dev;
    if ((ep & 15U) == 0U)
    {
        control_size = size;
        if (size > sizeof(control_reply)) { errors++; return USBD_FAIL; }
        if (size) { memcpy(control_reply, data, size); }
        return USBD_OK;
    }
    if (ep != 0x81 && ep != 0x82) { errors++; return USBD_FAIL; }
    uint8_t number = ep & 15U;
    if (blocked[number]) { return USBD_BUSY; }
    if (pending[number] || size > HID_NKRO_SIZE || (number == 2 && size != 1))
    { errors++; return USBD_FAIL; }
    pending_data[number] = data; /* Model asynchronous use, not an immediate copy. */
    pending_size[number] = size;
    pending[number] = 1;
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t *data, uint32_t size)
{ (void)dev; (void)ep; out_data = data; out_size = size; return USBD_OK; }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *dev, uint8_t ep)
{ (void)dev; (void)ep; return rx_size; }
void USBD_LL_Delay(uint32_t delay) { (void)delay; }

#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_keyboard.h"
#include "usb_consumer.h"
#include "usbd_ctlreq.h"
#include "usbd_ioreq.h"

static uint8_t control[HID_IN_PACKET_SIZE]; /* Survives Setup; rounded FIFO reads. */
static uint8_t output_pending;
static uint8_t class_context;

static uint8_t Init(USBD_HandleTypeDef *dev, uint8_t config)
{
    (void)config;
    if (USBD_LL_OpenEP(dev, 0x81, USBD_EP_TYPE_INTR, HID_IN_PACKET_SIZE) != USBD_OK)
    { return USBD_FAIL; }
    if (USBD_LL_OpenEP(dev, 0x82, USBD_EP_TYPE_INTR, HID_CONSUMER_PACKET_SIZE) != USBD_OK)
    { (void)USBD_LL_CloseEP(dev, 0x81); return USBD_FAIL; }
    dev->ep_in[1].is_used = 1;
    dev->ep_in[1].bInterval = 1;
    dev->ep_in[2].is_used = 1;
    dev->ep_in[2].bInterval = 1;
    dev->pClassData = &class_context;
    dev->pClassDataCmsit[0] = &class_context;
    output_pending = 0;
    UsbKeyboard_Configure(1);
    return USBD_OK;
}
static uint8_t DeInit(USBD_HandleTypeDef *dev, uint8_t config)
{
    (void)config;
    if (dev->ep_in[1].is_used) { (void)USBD_LL_CloseEP(dev, 0x81); }
    if (dev->ep_in[2].is_used) { (void)USBD_LL_CloseEP(dev, 0x82); }
    dev->ep_in[1].is_used = 0;
    dev->ep_in[2].is_used = 0;
    dev->pClassData = NULL;
    dev->pClassDataCmsit[0] = NULL;
    output_pending = 0;
    UsbKeyboard_Configure(0);
    return USBD_OK;
}
static uint8_t Reject(USBD_HandleTypeDef *dev, USBD_SetupReqTypedef *req)
{ USBD_CtlError(dev, req); return USBD_FAIL; }
static uint8_t Reply(USBD_HandleTypeDef *dev, USBD_SetupReqTypedef *req,
                     uint8_t *data, uint16_t length)
{ return (uint8_t)USBD_CtlSendData(dev, data, MIN(length, req->wLength)); }

static uint8_t ConsumerSetup(USBD_HandleTypeDef *dev, USBD_SetupReqTypedef *req)
{
    if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
    {
        if (dev->dev_state != USBD_STATE_CONFIGURED) { return Reject(dev, req); }
        if (req->bmRequest == 0xA1 && req->bRequest == 0x01 && req->wValue == 0x0100 && req->wLength)
        { control[0] = UsbKeyboard_GetConsumerBits(); return Reply(dev, req, control, 1); }
        /* Optional idle-zero handshake for generic HID hosts. No Consumer timer.
         * Nonzero idle, protocol and output requests are unsupported (STALL). */
        if (req->bmRequest == 0xA1 && req->bRequest == 0x02 && req->wValue == 0 && req->wLength == 1)
        { control[0] = 0; return Reply(dev, req, control, 1); }
        if (req->bmRequest == 0x21 && req->bRequest == 0x0A && req->wValue == 0 && req->wLength == 0)
        { return USBD_OK; }
    }
    else if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD)
    {
        if (req->bmRequest == 0x81 && req->bRequest == USB_REQ_GET_DESCRIPTOR && req->wLength)
        {
            if (req->wValue == 0x2200)
            { return Reply(dev, req, UsbConsumerDescriptor, UsbConsumerDescriptorSize); }
            if (req->wValue == 0x2100)
            { uint16_t n; uint8_t *p = UsbDescriptors_ConsumerHid(&n); return Reply(dev, req, p, n); }
        }
        if (dev->dev_state == USBD_STATE_CONFIGURED && req->wValue == 0)
        {
            if (req->bmRequest == 0x81 && req->bRequest == USB_REQ_GET_INTERFACE && req->wLength == 1)
            { control[0] = 0; return Reply(dev, req, control, 1); }
            if (req->bmRequest == 0x01 && req->bRequest == USB_REQ_SET_INTERFACE && req->wLength == 0)
            { return USBD_OK; }
            if (req->bmRequest == 0x81 && req->bRequest == USB_REQ_GET_STATUS && req->wLength == 2)
            { control[0] = control[1] = 0; return Reply(dev, req, control, 2); }
        }
    }
    return Reject(dev, req);
}

static uint8_t Setup(USBD_HandleTypeDef *dev, USBD_SetupReqTypedef *req)
{
    output_pending = 0; /* A new setup aborts an unfinished output transfer. */
    /* The core handles endpoint HALT, then notifies the class. Re-sync after
     * CLEAR_FEATURE so a cancelled transfer cannot keep the scheduler busy. */
    if (req->bmRequest == 0x02 && req->wIndex == 0x81 && req->wValue == 0 && req->wLength == 0)
    {
        if (req->bRequest == USB_REQ_SET_FEATURE) { return USBD_OK; }
        if (req->bRequest == USB_REQ_CLEAR_FEATURE)
        { return UsbKeyboard_SetProtocol(UsbKeyboard_GetProtocol()) ? USBD_OK : Reject(dev, req); }
    }
    if (req->bmRequest == 0x02 && req->wIndex == 0x82 && req->wValue == 0 && req->wLength == 0)
    {
        if (req->bRequest == USB_REQ_SET_FEATURE) { return USBD_OK; }
        if (req->bRequest == USB_REQ_CLEAR_FEATURE)
        {
            if (!UsbConsumerPort_Cancel()) { return Reject(dev, req); }
            uint32_t mask = __get_PRIMASK();
            __disable_irq();
            UsbConsumer_Reset();
            UsbKeyboard_Resync();
            __set_PRIMASK(mask);
            return USBD_OK;
        }
    }
    /* One ST class owns both interfaces: dispatch explicitly by full wIndex.
     * The non-composite ST core routes both endpoints to this class unchanged. */
    if (req->wIndex == 1U && (req->bmRequest & 0x1FU) == USB_REQ_RECIPIENT_INTERFACE)
    { return ConsumerSetup(dev, req); }
    if (req->wIndex != 0U || (req->bmRequest & 0x1FU) != USB_REQ_RECIPIENT_INTERFACE)
    { return Reject(dev, req); }
    if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_CLASS)
    {
        if (dev->dev_state != USBD_STATE_CONFIGURED) { return Reject(dev, req); }
        switch (req->bRequest)
        {
            case 0x01: /* GET_REPORT, ID zero only; input or LED output. */
                if (req->bmRequest != 0xA1 || (req->wValue & 255U) || !req->wLength)
                { break; }
                if (req->wValue == 0x0100)
                { uint16_t size = UsbKeyboard_GetReport(control); return Reply(dev, req, control, size); }
                if (req->wValue == 0x0200)
                { control[0] = UsbKeyboard_GetLeds(); return Reply(dev, req, control, 1); }
                break;
            case 0x02: /* GET_IDLE */
                if (req->bmRequest != 0xA1 || req->wValue || req->wLength != 1) { break; }
                control[0] = UsbKeyboard_GetIdle(); return Reply(dev, req, control, 1);
            case 0x03: /* GET_PROTOCOL */
                if (req->bmRequest != 0xA1 || req->wValue || req->wLength != 1) { break; }
                control[0] = UsbKeyboard_GetProtocol(); return Reply(dev, req, control, 1);
            case 0x09: /* SET_REPORT(Output): one byte, no ID. */
                if (req->bmRequest != 0x21 || req->wValue != 0x0200 || req->wLength != 1) { break; }
                output_pending = 1;
                return (uint8_t)USBD_CtlPrepareRx(dev, control, 1);
            case 0x0A: /* SET_IDLE, units of 4 ms. */
                if (req->bmRequest != 0x21 || (req->wValue & 255U) || req->wLength) { break; }
                UsbKeyboard_SetIdle((uint8_t)(req->wValue >> 8)); return USBD_OK;
            case 0x0B: /* SET_PROTOCOL */
                if (req->bmRequest != 0x21 || req->wValue > 1 || req->wLength) { break; }
                return UsbKeyboard_SetProtocol((uint8_t)req->wValue) ? USBD_OK : Reject(dev, req);
            default: break;
        }
    }
    else if ((req->bmRequest & USB_REQ_TYPE_MASK) == USB_REQ_TYPE_STANDARD)
    {
        if (req->bRequest == USB_REQ_GET_DESCRIPTOR && req->bmRequest == 0x81 &&
            (req->wValue & 255U) == 0U && req->wLength)
        {
            if (req->wValue == 0x2200)
            { return Reply(dev, req, UsbReportDescriptor, UsbReportDescriptorSize); }
            if (req->wValue == 0x2100)
            { uint16_t n; uint8_t *p = UsbDescriptors_Hid(&n); return Reply(dev, req, p, n); }
        }
        if (dev->dev_state == USBD_STATE_CONFIGURED && req->wValue == 0U)
        {
            if (req->bRequest == USB_REQ_GET_INTERFACE && req->bmRequest == 0x81 && req->wLength == 1)
            { control[0] = 0; return Reply(dev, req, control, 1); }
            if (req->bRequest == USB_REQ_SET_INTERFACE && req->bmRequest == 0x01 && req->wLength == 0)
            { return USBD_OK; }
            if (req->bRequest == USB_REQ_GET_STATUS && req->bmRequest == 0x81 && req->wLength == 2)
            { control[0] = control[1] = 0; return Reply(dev, req, control, 2); }
        }
    }
    return Reject(dev, req);
}
static uint8_t OutputReady(USBD_HandleTypeDef *dev)
{
    if (output_pending && USBD_LL_GetRxDataSize(dev, 0) == 1U)
    { UsbKeyboard_SetLeds(control[0]); }
    output_pending = 0;
    return USBD_OK;
}
static uint8_t DataIn(USBD_HandleTypeDef *dev, uint8_t ep)
{
    (void)dev;
    if (ep == 1U) { UsbKeyboard_TxComplete(HAL_GetTick()); }
    if (ep == 2U) { UsbConsumer_Complete(); }
    return USBD_OK;
}
USBD_ClassTypeDef UsbKeyboardClass =
{
    Init, DeInit, Setup, NULL, OutputReady, DataIn, NULL, NULL, NULL, NULL,
    NULL, UsbDescriptors_Config, NULL, NULL
};
uint8_t UsbKeyboardPort_Send(uint8_t *report, uint16_t length)
{
    if (UsbDevice.dev_state != USBD_STATE_CONFIGURED || !UsbDevice.ep_in[1].is_used ||
        USBD_LL_IsStallEP(&UsbDevice, 0x81)) { return 0; }
    return USBD_LL_Transmit(&UsbDevice, 0x81, report, length) == USBD_OK;
}
uint8_t UsbKeyboardPort_Cancel(void) { return UsbDevice_CancelIn(); }
uint32_t UsbKeyboardPort_State(void) { return UsbDevice.dev_state; }
uint8_t UsbConsumerPort_Send(uint8_t *data)
{
    if (UsbDevice.dev_state != USBD_STATE_CONFIGURED || !UsbDevice.ep_in[2].is_used ||
        USBD_LL_IsStallEP(&UsbDevice, 0x82)) { return 0; }
    return USBD_LL_Transmit(&UsbDevice, 0x82, data, 1) == USBD_OK;
}
uint8_t UsbConsumerPort_Cancel(void) { return UsbDevice_CancelEndpoint(2); }

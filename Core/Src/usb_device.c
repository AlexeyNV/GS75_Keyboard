/* STM32F411 FS port for the vendored ST USB Device core (CubeF4 V1.28.3).
 * Custom ownership: PA11/PA12 only, no Cube-generated USB initialization. */
#include "usb_device.h"
#include "usb_descriptors.h"
#include "usb_keyboard.h"

USBD_HandleTypeDef UsbDevice;
static PCD_HandleTypeDef pcd;
static USBD_StatusTypeDef Status(HAL_StatusTypeDef status)
{ return status == HAL_OK ? USBD_OK : USBD_FAIL; }

void UsbDevice_Init(void)
{
    uint32_t uid[3] = {HAL_GetUIDw0(), HAL_GetUIDw1(), HAL_GetUIDw2()};
    UsbDescriptors_Init(uid);
    if (USBD_Init(&UsbDevice, &UsbDescriptors, 0) != USBD_OK ||
        USBD_RegisterClass(&UsbDevice, &UsbKeyboardClass) != USBD_OK ||
        USBD_Start(&UsbDevice) != USBD_OK) { Error_Handler(); }
}
void UsbDevice_IRQHandler(void) { HAL_PCD_IRQHandler(&pcd); }

uint8_t UsbDevice_CancelIn(void)
{
    return UsbDevice_CancelEndpoint(1);
}

uint8_t UsbDevice_CancelEndpoint(uint8_t ep)
{
    if (ep != 1U && ep != 2U) { return 0; }
    if (!UsbDevice.ep_in[ep].is_used) { return 1; }
    /* Abort preserves the IN DATA toggle. Closing/reopening would reset it,
     * which SET_PROTOCOL must not do. UART priority 5 can preempt this wait. */
    if (HAL_PCD_EP_Abort(&pcd, (uint8_t)(0x80U | ep)) != HAL_OK) { return 0; }
    uint32_t USBx_BASE = (uint32_t)USB_OTG_FS;
    USBx_DEVICE->DIEPEMPMSK &= ~(1UL << ep);
    if (USB_FlushTxFifo(USB_OTG_FS, ep) != HAL_OK) { return 0; }
    USBx_INEP(ep)->DIEPINT = 0xFFFFFFFFU; /* Discard cancelled completion. */
    pcd.IN_ep[ep].xfer_count = 0;
    pcd.IN_ep[ep].xfer_len = 0;
    return 1;
}

void HAL_PCD_MspInit(PCD_HandleTypeDef *handle)
{
    if (handle->Instance != USB_OTG_FS) { return; }
    __HAL_RCC_GPIOA_CLK_ENABLE();
    GPIO_InitTypeDef pins = {0};
    pins.Pin = GPIO_PIN_11 | GPIO_PIN_12;
    pins.Mode = GPIO_MODE_AF_PP;
    pins.Pull = GPIO_NOPULL;
    pins.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
    pins.Alternate = GPIO_AF10_OTG_FS;
    HAL_GPIO_Init(GPIOA, &pins);
    __HAL_RCC_USB_OTG_FS_CLK_ENABLE();
    HAL_NVIC_SetPriority(OTG_FS_IRQn, 6, 0); /* USART1=5, SysTick=15 unchanged. */
    HAL_NVIC_EnableIRQ(OTG_FS_IRQn);
}
void HAL_PCD_MspDeInit(PCD_HandleTypeDef *handle)
{
    if (handle->Instance != USB_OTG_FS) { return; }
    HAL_NVIC_DisableIRQ(OTG_FS_IRQn);
    __HAL_RCC_USB_OTG_FS_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOA, GPIO_PIN_11 | GPIO_PIN_12);
}

USBD_StatusTypeDef USBD_LL_Init(USBD_HandleTypeDef *dev)
{
    pcd.Instance = USB_OTG_FS;
    pcd.Init.dev_endpoints = 4;
    pcd.Init.speed = PCD_SPEED_FULL;
    pcd.Init.dma_enable = DISABLE;
    pcd.Init.phy_itface = PCD_PHY_EMBEDDED;
    pcd.Init.Sof_enable = DISABLE;
    pcd.Init.low_power_enable = DISABLE;
    pcd.Init.lpm_enable = DISABLE;
    pcd.Init.vbus_sensing_enable = DISABLE; /* PA9 remains USART1 TX. */
    pcd.Init.use_dedicated_ep1 = DISABLE;
    pcd.pData = dev;
    dev->pData = &pcd;
    if (HAL_PCD_Init(&pcd) != HAL_OK) { return USBD_FAIL; }
    /* FIFO sizes are 32-bit words; 128 + 64 + 32 + 16 = 240 of 320 FS words. */
    if (HAL_PCDEx_SetRxFiFo(&pcd, 128) != HAL_OK ||
        HAL_PCDEx_SetTxFiFo(&pcd, 0, 64) != HAL_OK ||
        HAL_PCDEx_SetTxFiFo(&pcd, 1, 32) != HAL_OK ||
        HAL_PCDEx_SetTxFiFo(&pcd, 2, 16) != HAL_OK) { return USBD_FAIL; }
    return USBD_OK;
}
USBD_StatusTypeDef USBD_LL_DeInit(USBD_HandleTypeDef *dev) { return Status(HAL_PCD_DeInit(dev->pData)); }
USBD_StatusTypeDef USBD_LL_Start(USBD_HandleTypeDef *dev) { return Status(HAL_PCD_Start(dev->pData)); }
USBD_StatusTypeDef USBD_LL_Stop(USBD_HandleTypeDef *dev) { return Status(HAL_PCD_Stop(dev->pData)); }
USBD_StatusTypeDef USBD_LL_OpenEP(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t type, uint16_t size)
{ return Status(HAL_PCD_EP_Open(dev->pData, ep, size, type)); }
USBD_StatusTypeDef USBD_LL_CloseEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ return Status(HAL_PCD_EP_Close(dev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_FlushEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ return Status(HAL_PCD_EP_Flush(dev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_StallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ return Status(HAL_PCD_EP_SetStall(dev->pData, ep)); }
USBD_StatusTypeDef USBD_LL_ClearStallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{ return Status(HAL_PCD_EP_ClrStall(dev->pData, ep)); }
uint8_t USBD_LL_IsStallEP(USBD_HandleTypeDef *dev, uint8_t ep)
{
    PCD_HandleTypeDef *handle = dev->pData;
    return (ep & 0x80U) ? handle->IN_ep[ep & 15U].is_stall : handle->OUT_ep[ep & 15U].is_stall;
}
USBD_StatusTypeDef USBD_LL_SetUSBAddress(USBD_HandleTypeDef *dev, uint8_t address)
{ return Status(HAL_PCD_SetAddress(dev->pData, address)); }
USBD_StatusTypeDef USBD_LL_Transmit(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t *data, uint32_t size)
{ return Status(HAL_PCD_EP_Transmit(dev->pData, ep, data, size)); }
USBD_StatusTypeDef USBD_LL_PrepareReceive(USBD_HandleTypeDef *dev, uint8_t ep, uint8_t *data, uint32_t size)
{ return Status(HAL_PCD_EP_Receive(dev->pData, ep, data, size)); }
uint32_t USBD_LL_GetRxDataSize(USBD_HandleTypeDef *dev, uint8_t ep)
{ return HAL_PCD_EP_GetRxCount(dev->pData, ep); }
void USBD_LL_Delay(uint32_t milliseconds) { HAL_Delay(milliseconds); }

void HAL_PCD_SetupStageCallback(PCD_HandleTypeDef *handle)
{ (void)USBD_LL_SetupStage(handle->pData, (uint8_t *)handle->Setup); }
void HAL_PCD_DataOutStageCallback(PCD_HandleTypeDef *handle, uint8_t ep)
{ (void)USBD_LL_DataOutStage(handle->pData, ep, handle->OUT_ep[ep].xfer_buff); }
void HAL_PCD_DataInStageCallback(PCD_HandleTypeDef *handle, uint8_t ep)
{ (void)USBD_LL_DataInStage(handle->pData, ep, handle->IN_ep[ep].xfer_buff); }
void HAL_PCD_ResetCallback(PCD_HandleTypeDef *handle)
{
    (void)USBD_LL_SetSpeed(handle->pData, USBD_SPEED_FULL);
    (void)USBD_LL_Reset(handle->pData);
    UsbKeyboard_Reset();
}
void HAL_PCD_SuspendCallback(PCD_HandleTypeDef *handle) { (void)USBD_LL_Suspend(handle->pData); }
void HAL_PCD_ResumeCallback(PCD_HandleTypeDef *handle) { (void)USBD_LL_Resume(handle->pData); }
void HAL_PCD_ConnectCallback(PCD_HandleTypeDef *handle) { (void)USBD_LL_DevConnected(handle->pData); }
void HAL_PCD_DisconnectCallback(PCD_HandleTypeDef *handle)
{ (void)USBD_LL_DevDisconnected(handle->pData); UsbKeyboard_Reset(); }

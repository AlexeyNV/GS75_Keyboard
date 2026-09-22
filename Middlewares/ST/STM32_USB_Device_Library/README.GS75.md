# Vendored USB Device core

Source: local STM32Cube firmware package **STM32Cube FW_F4 V1.28.3**,
`Middlewares/ST/STM32_USB_Device_Library`.

Only the unmodified core is included: `usbd_core`, `usbd_ctlreq`, `usbd_ioreq`
sources/headers and `usbd_def.h`. The upstream `LICENSE.txt` is preserved.
The package's HID class is a mouse example; it stores protocol/idle but does
not provide the required Boot/NKRO keyboard, output LED and scheduling behavior.
The GS75 class is instead in `Core/Src/usbd_keyboard.c`.

The required `stm32f4xx_hal_pcd`, `stm32f4xx_hal_pcd_ex` and `stm32f4xx_ll_usb`
sources/headers were copied unmodified from the same package into the existing
HAL driver directories, under their existing ST license.

No dependency on the developer's local package directory is needed to build.
See `docs/USB_HID.md` for custom code ownership, prototype VID/PID and validation.

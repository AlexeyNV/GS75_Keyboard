#ifndef GS75_USBD_CONF_H
#define GS75_USBD_CONF_H
#include <main.h> /* Include-path selection also permits host hardware stubs. */
#include <string.h>
#define USBD_MAX_NUM_INTERFACES 2U
#define USBD_MAX_NUM_CONFIGURATION 1U
#define USBD_MAX_SUPPORTED_CLASS 1U
#define USBD_MAX_STR_DESC_SIZ 128U
#define USBD_SELF_POWERED 0U
#define USBD_MAX_POWER 50U /* USB electronics only, 100 mA; RGB disabled. */
#define USBD_DEBUG_LEVEL 0U
#define USBD_LPM_ENABLED 0U
#define USBD_SUPPORT_USER_STRING_DESC 0U
#define USBD_CLASS_USER_STRING_DESC 0U
#define USBD_CLASS_BOS_ENABLED 0U
#define USBD_USER_REGISTER_CALLBACK 0U
#define USBD_memset memset
#define USBD_memcpy memcpy
#define USBD_UsrLog(...) ((void)0)
#define USBD_ErrLog(...) ((void)0)
#define USBD_DbgLog(...) ((void)0)
#endif

#ifndef POWER_PORT_H
#define POWER_PORT_H
#include <stdint.h>
void PowerPort_Init(void);
uint32_t PowerPort_Cc(void);
uint8_t PowerPort_Fault(void);
uint8_t PowerPort_Healthy(void);
void PowerPort_Enable(uint8_t on);
/* Best-effort direct GPIO LOW for fatal exceptions; no HAL or scheduler state. */
void PowerPort_EmergencyOff(void);
/* poll: 0 pending, 1 conversion completed, -1 conversion error/timeout */
void PowerPort_AdcStart(void);
int PowerPort_AdcPoll(uint32_t *raw);
void PowerPort_AdcAbort(void);
void PowerPort_Usb(uint8_t *configured, uint8_t *suspended, uint32_t *declared_ma);
uint32_t PowerPort_Now(void);
#endif

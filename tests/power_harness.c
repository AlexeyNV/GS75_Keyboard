#include "power_port.h"
#include "power_manager.h"
#include "board_power_config.h"
#include <string.h>
static uint32_t tick, cc, adc, usb_budget, output;
static uint8_t fault, configured, suspended, healthy;
static int adc_result;
static char reply[8192];
void PowerTest_Init(void)
{
    tick = 0; cc = 1; adc = 62; usb_budget = 100; output = 0;
    fault = suspended = 0; configured = healthy = 1; adc_result = 1;
    reply[0] = 0; PowerManager_Init();
}
void PowerTest_Set(uint32_t code, uint32_t raw, int result, uint32_t flags)
{ cc = code; adc = raw; adc_result = result; fault = (flags & 1) != 0;
  configured = (flags & 2) != 0; suspended = (flags & 4) != 0; healthy = (flags & 8) != 0; }
void PowerTest_Tick(uint32_t now) { tick = now; PowerManager_Task(tick); }
uint32_t PowerTest_Output(void) { return output; }
void PowerTest_Get(PowerInfo *s) { *s = *PowerManager_Info(); }
static void Send(const char *text)
{ size_t n = strlen(reply); if (n + strlen(text) < sizeof(reply)) { memcpy(reply + n, text, strlen(text) + 1U); } }
const char *PowerTest_Command(const char *command)
{ reply[0] = 0; (void)PowerDebug_Command(command, Send); return reply; }
void PowerPort_Init(void) { output = 0; }
uint32_t PowerPort_Cc(void) { return cc; }
uint8_t PowerPort_Fault(void) { return fault; }
uint8_t PowerPort_Healthy(void) { return healthy; }
void PowerPort_Enable(uint8_t on)
{ output = on && GS75_BOARD_REV2 && GS75_RGB_POWER_BENCH_TEST && GS75_POWER_HARDWARE_VERIFIED; }
void PowerPort_AdcStart(void) { }
int PowerPort_AdcPoll(uint32_t *raw) { *raw = adc; return adc_result; }
void PowerPort_AdcAbort(void) { }
void PowerPort_Usb(uint8_t *c, uint8_t *s, uint32_t *budget)
{ *c = configured; *s = suspended; *budget = usb_budget; }
uint32_t PowerPort_Now(void) { return tick; }

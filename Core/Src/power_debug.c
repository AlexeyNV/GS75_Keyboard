#include "power_manager.h"
#include "power_port.h"
#include "board_power_config.h"
#include <stdio.h>
#include <string.h>
static void Number(void (*send)(const char *), const char *key, uint32_t value)
{ char line[80]; (void)snprintf(line, sizeof(line), "%s=%lu\r\n", key, (unsigned long)value); send(line); }
static void Text(void (*send)(const char *), const char *key, const char *value)
{ char line[96]; (void)snprintf(line, sizeof(line), "%s=%s\r\n", key, value); send(line); }
uint8_t PowerDebug_Command(const char *command, void (*send)(const char *))
{
    uint8_t pwr = strcmp(command, "PWRINFO") == 0;
    uint8_t current = strcmp(command, "CURRENT") == 0;
    uint8_t test = strcmp(command, "PWRTEST") == 0;
    if (pwr || current || test)
    {
        PowerManager_Task(PowerPort_Now());
        PowerInfo s = *PowerManager_Info(); /* Consistent snapshot before TX services power. */
        Text(send, "BEGIN", command);
        Number(send, "BOARD_REV2", GS75_BOARD_REV2);
        Number(send, "BENCH_TEST", GS75_RGB_POWER_BENCH_TEST);
        Number(send, "HARDWARE_VERIFIED", GS75_POWER_HARDWARE_VERIFIED);
        send("MEASUREMENT=PRELIMINARY\r\n");
        if (pwr || test)
        {
            Text(send, "STATE", PowerManager_State(s.state));
            Text(send, "INHIBIT", PowerManager_Reason(s.reason));
            Text(send, "LAST_OFF", PowerManager_Reason(s.last_off));
            Text(send, "LAST_FAULT", PowerManager_Reason(s.last_fault));
            Text(send, "CC_MODE", UsbCDetect_Name(s.cc.stable));
            Number(send, "CC_RAW", s.cc.raw); Number(send, "CC_STABLE", s.cc.stable);
            Number(send, "CC_VALID", s.cc.valid); Number(send, "CC_RAW_SINCE_MS", s.cc.raw_since_ms);
            Number(send, "CC_CHANGED_MS", s.cc.changed_ms);
            Number(send, "ATTACH_COUNT", s.cc.attaches); Number(send, "DETACH_COUNT", s.cc.detaches);
            Number(send, "USB_CONFIGURED", s.configured); Number(send, "USB_SUSPENDED", s.suspended);
            Number(send, "POLICY_LIMIT_MA", s.policy_ma); Number(send, "BUDGET_MA", s.budget_ma);
            Number(send, "RGB_POWER", s.rgb_on); Number(send, "FAULT_ACTIVE", s.fault);
            Number(send, "FAULT_COUNT", s.fault_count);
            Number(send, "BASELINE_MA", s.baseline_ma); Number(send, "BASELINE_VALID", s.baseline_valid);
            Number(send, "PORT_CONFIG_OK", PowerPort_Healthy());
            send("CC_WIRING_DIAG=LIMITED_NO_OPEN_WIRE_PROOF\r\n");
        }
        if (current || test)
        {
            const CurrentMonitor *c = &s.current;
            Number(send, "ADC_RAW", c->raw); Number(send, "ADC_MV", c->mv);
            Number(send, "CURRENT_MA", c->ma); Number(send, "FILTERED_MA", c->filtered_ma);
            Number(send, "MEAN_MA", c->mean_ma); Number(send, "MIN_MA", c->min_ma); Number(send, "MAX_MA", c->max_ma);
            Number(send, "VALID", c->valid); Number(send, "SATURATED", c->saturated);
            Number(send, "SAMPLES", c->samples); Number(send, "SAMPLE_MS", c->last_ms);
            Number(send, "VREF_MV", c->vref_mv); Number(send, "GAIN_PPM", c->gain_ppm);
            char line[64]; (void)snprintf(line, sizeof(line), "ZERO_MV=%ld\r\n", (long)c->zero_mv); send(line);
        }
        Text(send, "END", command); return 1;
    }
    PowerReason result;
    if (strcmp(command, "PWRON") == 0) { result = PowerManager_On(); }
    else if (strcmp(command, "PWROFF") == 0) { PowerManager_Off(); result = PWR_OK; }
    else if (strcmp(command, "PWRRESET") == 0) { result = PowerManager_Reset(); }
    else { return 0; }
    Text(send, command, PowerManager_Reason(result)); return 1;
}

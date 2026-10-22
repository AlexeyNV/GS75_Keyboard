#include "power_manager.h"
#include "power_port.h"
#include "board_power_config.h"
#include <string.h>
static PowerInfo info;
static uint32_t last_task, sample_started;
static uint8_t initialized, pending, hold_off, latched;
static void Stop(PowerReason reason, uint8_t latch)
{
    PowerPort_Enable(0); info.rgb_on = 0; info.last_off = reason;
    hold_off = 1;
    if (latch && !latched)
    {
        if (info.fault_count != UINT32_MAX) { info.fault_count++; }
        info.last_fault = reason;
        latched = 1;
    }
}
void PowerManager_Init(void)
{
    initialized = 0; memset(&info, 0, sizeof(info));
    pending = hold_off = latched = 0;
    PowerPort_Init(); PowerPort_Enable(0);
    last_task = sample_started = PowerPort_Now();
    UsbCDetect_Init(&info.cc, last_task); CurrentMonitor_Init(&info.current);
    info.state = GS75_BOARD_REV2 ? POWER_OFF : POWER_UNAVAILABLE;
    initialized = 1;
}
static PowerReason Conditions(void)
{
    if (!GS75_BOARD_REV2) { return PWR_BOARD_UNAVAILABLE; }
    if (!PowerPort_Healthy()) { return PWR_PORT_INVALID; }
    if (info.fault) { return PWR_HW_FAULT; }
    if (!info.cc.valid) { return PWR_CC_UNSTABLE; }
    if (info.cc.stable == 3) { return PWR_UNATTACHED; }
    if (info.suspended) { return PWR_USB_SUSPEND; }
    if (!info.configured) { return PWR_USB_WAIT; }
    if (!info.current.valid) { return PWR_ADC_INVALID; }
    if (!info.budget_ma || info.current.ma >= info.budget_ma) { return PWR_OVERCURRENT; }
    return PWR_OK;
}
void PowerManager_Task(uint32_t now)
{
    if (!initialized) { return; }
    uint8_t gap = (uint32_t)(now - last_task) > POWER_SERVICE_GAP_MS;
    last_task = now;
    if (!GS75_BOARD_REV2)
    { info.state = POWER_UNAVAILABLE; info.reason = PWR_BOARD_UNAVAILABLE; return; }
    uint8_t configured, suspended; uint32_t declared;
    PowerPort_Usb(&configured, &suspended, &declared);
    info.configured = configured; info.suspended = suspended;
    info.fault = PowerPort_Fault();
    UsbCDetect_Update(&info.cc, PowerPort_Cc(), now, gap);
    if (pending)
    {
        uint32_t raw = 0; int result = PowerPort_AdcPoll(&raw);
        if (result || (uint32_t)(now - sample_started) >= POWER_SAMPLE_MS)
        {
            CurrentMonitor_Sample(&info.current, raw, result > 0 && !gap, now);
            if (result <= 0 || gap) { PowerPort_AdcAbort(); sample_started = now; }
            pending = 0;
            if (info.current.valid && !info.rgb_on)
            { info.baseline_ma = info.current.ma; info.baseline_valid = 1; }
        }
    }
    if (!pending && (uint32_t)(now - sample_started) >= POWER_SAMPLE_MS)
    { PowerPort_AdcStart(); sample_started = now; pending = 1; }
    CurrentMonitor_Age(&info.current, now);
    info.policy_ma = info.cc.stable == 2 ? POWER_DEFAULT_LIMIT_MA : POWER_TYPEC_LIMIT_MA;
    info.budget_ma = info.cc.valid && info.cc.stable != 3 && configured && !suspended ? info.policy_ma : 0;
    /* The active bus-powered configuration declares total USB current. CC
     * advertisement cannot silently raise bMaxPower for this configuration. */
    if (info.budget_ma > declared) { info.budget_ma = declared; }
    PowerReason r = Conditions();
    if (info.fault) { Stop(PWR_HW_FAULT, 1); }
    else if (info.rgb_on && gap) { Stop(PWR_SERVICE_GAP, 1); }
    else if (info.rgb_on && r != PWR_OK)
    { Stop(r, r == PWR_OVERCURRENT || r == PWR_ADC_INVALID || r == PWR_PORT_INVALID); }
    if (latched) { info.state = POWER_FAULT_LATCHED; info.reason = PWR_LATCHED; }
    else if (info.rgb_on) { info.state = POWER_ON; info.reason = PWR_OK; }
    else if (r == PWR_USB_WAIT || r == PWR_USB_SUSPEND || r == PWR_UNATTACHED || r == PWR_CC_UNSTABLE)
    { info.state = POWER_WAIT_USB; info.reason = r; }
    else if (r != PWR_OK) { info.state = POWER_UNAVAILABLE; info.reason = r; }
    else if (!GS75_RGB_POWER_BENCH_TEST) { info.state = POWER_OFF; info.reason = PWR_BENCH_DISABLED; }
    else if (!GS75_POWER_HARDWARE_VERIFIED) { info.state = POWER_UNAVAILABLE; info.reason = PWR_HARDWARE_UNVERIFIED; }
    else { info.state = hold_off ? POWER_OFF : POWER_READY; info.reason = hold_off ? PWR_USER_OFF : PWR_OK; }
}
PowerReason PowerManager_On(void)
{
    PowerManager_Task(PowerPort_Now());
    PowerReason r = !initialized || !GS75_BOARD_REV2 ? PWR_BOARD_UNAVAILABLE :
                    !GS75_RGB_POWER_BENCH_TEST ? PWR_BENCH_DISABLED :
                    !GS75_POWER_HARDWARE_VERIFIED ? PWR_HARDWARE_UNVERIFIED :
                    latched ? PWR_LATCHED : Conditions();
    if (r == PWR_OK)
    { PowerPort_Enable(1); info.rgb_on = 1; hold_off = 0; info.state = POWER_ON; info.reason = PWR_OK; }
    return r;
}
void PowerManager_Off(void)
{ if (initialized) { Stop(PWR_USER_OFF, 0); info.state = latched ? POWER_FAULT_LATCHED : POWER_OFF; } }
PowerReason PowerManager_Reset(void)
{
    PowerManager_Off(); PowerManager_Task(PowerPort_Now());
    PowerReason r = Conditions();
    if (r != PWR_OK) { return r; }
    latched = 0; hold_off = 1; info.state = POWER_OFF; info.reason = PWR_USER_OFF;
    return PWR_OK;
}
const PowerInfo *PowerManager_Info(void) { return &info; }
const char *PowerManager_Reason(uint32_t r)
{
    static const char *const names[] = {"OK", "USER_OFF", "BOARD_UNAVAILABLE", "BENCH_DISABLED", "HARDWARE_UNVERIFIED",
        "CC_UNSTABLE", "UNATTACHED", "USB_NOT_CONFIGURED", "USB_SUSPENDED", "ADC_INVALID", "HW_FAULT",
        "OVERCURRENT", "SERVICE_GAP", "FAULT_LATCHED", "PORT_INVALID"};
    return r < sizeof(names)/sizeof(names[0]) ? names[r] : "UNKNOWN";
}
const char *PowerManager_State(uint32_t s)
{
    static const char *const names[] = {"OFF", "WAIT_USB", "READY", "ON", "FAULT_LATCHED", "UNAVAILABLE"};
    return s < 6 ? names[s] : "UNKNOWN";
}

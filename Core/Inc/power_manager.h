#ifndef POWER_MANAGER_H
#define POWER_MANAGER_H
#include "usb_c_detect.h"
#include "current_monitor.h"
typedef enum { POWER_OFF, POWER_WAIT_USB, POWER_READY, POWER_ON, POWER_FAULT_LATCHED, POWER_UNAVAILABLE } PowerState;
typedef enum {
    PWR_OK, PWR_USER_OFF, PWR_BOARD_UNAVAILABLE, PWR_BENCH_DISABLED,
    PWR_HARDWARE_UNVERIFIED, PWR_CC_UNSTABLE, PWR_UNATTACHED,
    PWR_USB_WAIT, PWR_USB_SUSPEND, PWR_ADC_INVALID, PWR_HW_FAULT,
    PWR_OVERCURRENT, PWR_SERVICE_GAP, PWR_LATCHED, PWR_PORT_INVALID
} PowerReason;
typedef struct {
    UsbCDetect cc;
    CurrentMonitor current;
    uint32_t state, reason, last_off, fault_count, budget_ma, policy_ma;
    uint32_t rgb_on, fault, configured, suspended, baseline_ma, baseline_valid, last_fault;
} PowerInfo;
void PowerManager_Init(void);
void PowerManager_Task(uint32_t now);
PowerReason PowerManager_On(void);
void PowerManager_Off(void);
PowerReason PowerManager_Reset(void);
const PowerInfo *PowerManager_Info(void); /* main context only */
const char *PowerManager_Reason(uint32_t reason);
const char *PowerManager_State(uint32_t state);
uint8_t PowerDebug_Command(const char *command, void (*send)(const char *));
#endif

#ifndef CURRENT_MONITOR_H
#define CURRENT_MONITOR_H
#include <stdint.h>
typedef struct {
    uint32_t raw, mv, ma, filtered_ma, mean_ma, min_ma, max_ma;
    uint32_t valid, saturated, samples, last_ms, vref_mv, gain_ppm;
    int32_t zero_mv;
    uint64_t sum_ma;
} CurrentMonitor;
void CurrentMonitor_Init(CurrentMonitor *s);
void CurrentMonitor_Sample(CurrentMonitor *s, uint32_t raw, uint8_t ok, uint32_t now);
void CurrentMonitor_Age(CurrentMonitor *s, uint32_t now);
uint8_t CurrentMonitor_Calibrate(CurrentMonitor *s, uint32_t vref, int32_t zero, uint32_t gain);
#endif

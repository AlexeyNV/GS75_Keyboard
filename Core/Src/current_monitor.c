#include "current_monitor.h"
#include "board_power_config.h"
#include <string.h>
void CurrentMonitor_Init(CurrentMonitor *s)
{
    memset(s, 0, sizeof(*s)); s->vref_mv = POWER_VREF_MV;
    s->zero_mv = POWER_ZERO_MV; s->gain_ppm = POWER_GAIN_PPM;
}
uint8_t CurrentMonitor_Calibrate(CurrentMonitor *s, uint32_t vref, int32_t zero, uint32_t gain)
{
    if (vref < 2700 || vref > 3600 || zero < -100 || zero > 100 || gain < 500000 || gain > 1500000) { return 0; }
    CurrentMonitor_Init(s); s->vref_mv = vref; s->zero_mv = zero; s->gain_ppm = gain;
    return 1;
}
void CurrentMonitor_Sample(CurrentMonitor *s, uint32_t raw, uint8_t ok, uint32_t now)
{
    s->raw = raw; s->last_ms = now; s->valid = 0; s->saturated = raw >= 4090;
    if (!ok || raw > 4095 || s->saturated) { return; }
    s->mv = (raw * s->vref_mv + 2047U) / 4095U;
    int32_t corrected = (int32_t)s->mv - s->zero_mv;
    /* A1: 50 milliohm * 20 V/V = 1 mV/mA. No absolute-value trick. */
    if (corrected < -20) { return; }
    s->ma = corrected <= 0 ? 0 : (uint32_t)(((uint64_t)(uint32_t)corrected * s->gain_ppm + 500000U) / 1000000U);
    if (s->ma > 3200) { return; }
    if (!s->samples) { s->filtered_ma = s->min_ma = s->max_ma = s->ma; }
    else { s->filtered_ma = (7U * s->filtered_ma + s->ma + 4U) / 8U; }
    if (s->ma < s->min_ma) { s->min_ma = s->ma; }
    if (s->ma > s->max_ma) { s->max_ma = s->ma; }
    /* Saturate diagnostics after 2^32-1 samples (~497 days at 100 Hz).
     * Protection still uses the latest unfiltered sample. */
    if (s->samples != UINT32_MAX)
    {
        s->sum_ma += s->ma;
        s->samples++;
        s->mean_ma = (uint32_t)(s->sum_ma / s->samples);
    }
    s->valid = 1;
}
void CurrentMonitor_Age(CurrentMonitor *s, uint32_t now)
{ if (!s->samples || (uint32_t)(now - s->last_ms) > POWER_STALE_MS) { s->valid = 0; } }

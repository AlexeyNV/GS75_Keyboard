#include "usb_c_detect.h"
#include "board_power_config.h"
#include <string.h>
void UsbCDetect_Init(UsbCDetect *s, uint32_t now)
{ memset(s, 0, sizeof(*s)); s->raw = s->stable = 3; s->raw_since_ms = now; }
void UsbCDetect_Update(UsbCDetect *s, uint32_t raw, uint32_t now, uint8_t gap)
{
    if (raw > 3) { s->valid = 0; s->raw_since_ms = now; return; }
    if (gap || raw != s->raw)
    { s->raw = raw; s->raw_since_ms = now; s->valid = 0; }
    if (!s->valid && (uint32_t)(now - s->raw_since_ms) >= POWER_CC_FILTER_MS)
    {
        if (s->stable != raw)
        {
            if (s->stable == 3 && raw != 3) { if (s->attaches != UINT32_MAX) { s->attaches++; } }
            if (s->stable != 3 && raw == 3) { if (s->detaches != UINT32_MAX) { s->detaches++; } }
            s->stable = raw; s->changed_ms = now;
        }
        s->valid = 1;
    }
}
const char *UsbCDetect_Name(uint32_t code)
{
    static const char *const names[] = {"ATTACHED_3A", "ATTACHED_1P5A", "ATTACHED_DEFAULT", "UNATTACHED"};
    return code < 4 ? names[code] : "INVALID";
}

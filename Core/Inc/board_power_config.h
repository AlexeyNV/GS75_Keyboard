#ifndef BOARD_POWER_CONFIG_H
#define BOARD_POWER_CONFIG_H
#ifndef GS75_BOARD_REV2
#define GS75_BOARD_REV2 0
#endif
#ifndef GS75_RGB_POWER_BENCH_TEST
#define GS75_RGB_POWER_BENCH_TEST 0
#endif
/* Explicit bench acknowledgement of populated A1, shunt, wiring and dummy load.
 * Never infer this from GPIO/ADC readings: neither detects all wiring faults. */
#ifndef GS75_POWER_HARDWARE_VERIFIED
#define GS75_POWER_HARDWARE_VERIFIED 0
#endif
#define POWER_CC_FILTER_MS 20U
#define POWER_SAMPLE_MS 10U
#define POWER_STALE_MS 50U
#define POWER_SERVICE_GAP_MS 25U
#ifndef POWER_DEFAULT_LIMIT_MA
#define POWER_DEFAULT_LIMIT_MA 450U
#endif
#ifndef POWER_TYPEC_LIMIT_MA
#define POWER_TYPEC_LIMIT_MA 700U
#endif
#define POWER_VREF_MV 3300U
#define POWER_ZERO_MV 0
#define POWER_GAIN_PPM 1000000U
/* Reserved, NOT configured: PB12 CS_LAT, PB13 SCK, PB14 MISO, PB15 MOSI,
 * PD2 RGB_EN, PA8 PWM_CLK, PC8 RGB_INT. No SPI/PWM/EXTI ownership. */
#endif

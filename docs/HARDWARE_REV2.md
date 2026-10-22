# Board Rev.2: hardware review (2026-10-22)

Status: **Implemented** firmware adaptation; **Host-tested** logic; **Hardware-validated**:
none of the new power functions. **Not yet tested**: assembled board, ADC accuracy,
CC detection, switch protection and latency. **Future work**: verified RGB controller.
The earlier keyboard hardware results remain historical evidence, not Rev.2 acceptance.

## Schematic evidence and ownership

The requested dated filename is absent. The available EasyEDA schematic is
`hardware/schematics/SCH_keyboard.json`, title `PCB_keyboard`, editor 6.5.60.
SHA256: `4881fa7c010ed004da6da8c714435dfafc01892cf61b9cc45de29dbc7fbc1f13`.
It was supplied in the working tree; firmware work does not alter it. Wires, pin
endpoints and same-name net labels were traced for U12/U13/U15/U4 and the MCU
connections below. This is a schematic review, not PCB continuity verification.

| MCU | Net / connection | Firmware ownership |
|---|---|---|
| PC6 | U12.7 OUT1, R24 10k to VDD | Input, no internal pull |
| PC7 | U12.8 OUT2, R19 10k to VDD | Input, no internal pull |
| PA3 | R29.2 / C6.1 / INA_CUR | Analog ADC1_IN3, 12-bit |
| PC9 | U15.3 EN, R32 100k to GND / BUS_EN | LOW latch before push-pull output |
| PB4 | U15.4 FAULT#, R34 10k to VDD / BUS_FAULT | Input, active low, no EXTI |
| PB12 | R9 22 ohm -> U4.9 RGB_CS_LAT | Reserved, not initialized |
| PB13 | R10 22 ohm -> U4.6 RGB_SCK | Reserved, no SPI |
| PB14 | R11 22 ohm -> U4.8 RGB_MISO | Reserved, no SPI |
| PB15 | R12 22 ohm -> U4.7 RGB_MOSI | Reserved, no SPI |
| PD2 | R7 22 ohm -> U4.10 RGB_EN; R8 10k pulldown | Reserved, not driven |
| PA8 | R14 22 ohm -> U4.11 RGB_PWM_CLK | Reserved, no timer/PWM |
| PC8 | R13 22 ohm -> U4.12 RGB_INT | Reserved, no interrupt |

These pins are unused by the existing `.ioc`, matrix and runtime peripherals.
PB4 has the reset debug alternate role NJTRST; selecting GPIO input relinquishes
that JTAG pin, while PA13/PA14 SWD remains available. USB PA11/PA12, USART1 PA9/PA10,
HSE, clock tree and every matrix pin are unchanged. ADC1 was previously unused.
GPIO/ADC ownership lives in custom `power_port_stm32.c`; no CubeMX regeneration,
`.ioc` change, or generated peripheral edit is needed. Main hooks are USER CODE.
Do not add a competing generated ADC1 driver without migrating this ownership.

U4 contacts 1/2 receive U15.6 +5V_RGB; 3/4/13/14 are GND; 5 is VDD 3.3V.
Contacts 13/14 are mounting contacts of the 12-position connector. Signal contacts
6..12 are listed above. VDD at contact 5 remains powered when +5V_RGB is OFF:
OFF does not mean galvanic isolation of an attached controller. R17/R21 on CS_LAT
and R18/R22 on RGB_INT are marked DNP; confirm assembly rather than assuming pulls.

## Power path and component checks

- U12 TUSB320LAIRWBR: CC1/CC2 reach USB-C A5/B5; PORT and EN_N are GND,
  ADDR is unconnected, VDD is 3.3V. OUT3 and ID are unused. R31 220k and R35
  680k form a series VBUS_DET feed (900k total), **not a divider to GND**.
- U13 IN+ and IN- span R30 0.05 ohm after the input protection. The downstream
  side supplies both the 3.3V regulator and U15 IN, so this is total board current.
  OUT -> R29 1k -> PA3, C6 100n to GND. U13 V+ is VDD.
- **BOM discrepancy**: U13 display/symbol says INA180A1IDBVR, Manufacturer Part
  says INA180A3IDBVR. Correct the BOM and inspect the populated part. Firmware
  targets A1 gain 20, not A3 gain 100. No software gain workaround is selected.
- U15 TPS2553DBVR: EN active high, FAULT active low, ILIM R33 30k to GND,
  OUT to +5V_RGB. The requested datasheet estimates are 796/863/939 mA min/nom/max
  for the RGB branch, before resistor tolerance. This hardware threshold is not
  programmable through PC9 and is not the software total-current limit.
- TPS2553 is a current limiter with hardware recovery, not TPS2553-1 latch-off.
  Firmware adds a persistent fault latch; it is not a substitute for fast hardware
  protection or an externally current-limited bench supply.

No additional power-pin contradiction was found in this JSON. The A1/A3 mismatch
prevents assuming the assembled monitor is correct: PWRON also requires an explicit
bench hardware-verification build flag. This acknowledges an actual operator check;
it does not certify the board or enable automatic RGB power.

## Engineering choices and limits

TUSB320 provides Type-C attach/current advertisement decoding without implementing
USB Power Delivery. GPIO mode matches the fixed sink/ADDR-NC wiring and needs no
I2C driver or controller state writes. All four OUT codes are legal: firmware
cannot distinguish a disconnected pull-up wire, stuck pin or wrong device mode
from every valid advertisement. USB configured state supplies an additional check,
but cannot prove CC wiring integrity. The active USB descriptor still declares
100mA total average bus draw; a 1.5A/3A source indication does not change that
allocation. See [USB-IF compliance guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical).

INA180 provides current visibility for the whole adapter, including an RGB-OFF
baseline. A stuck-low/shorted ADC input can read as a valid zero and pass the
software PWRON gate. Its output cannot prove sensor continuity or sense polarity;
two known-current measurements against an external meter are mandatory before
bench enable. VREF and gain errors are calibration matters. TPS2553 independently limits
only the RGB branch; keeping both protections separates fast hardware limiting
from the source-dependent total-board budget.

See [power policy](POWER_MANAGEMENT.md), [bench procedure](POWER_TEST_PLAN.md)
and the blank [hardware test record](POWER_TEST_RECORD.md). Rev.2 power
hardware validation has not started.
Component sources checked: [TI TUSB320LAI](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf),
[TI INA180](https://www.ti.com/product/INA180),
[TI TPS2553](https://www.ti.com/lit/ds/symlink/tps2553.pdf).
The new firmware does not implement PD, RGB SPI, LED drivers, PWM or RGB_INT.


A fatal exception handler now makes a best-effort LOW write to PC9. The 100k
external EN pulldown holds OFF during reset, but it cannot overcome a healthy MCU
pin driven HIGH during a full firmware hang. No external watchdog or fault-to-EN
hardware path is shown in the available schematic; independently current-limit
the source during bench tests. No RGB signal pin is configured or driven by
firmware, but U4 VDD 3.3V remains live and an attached external circuit can set
its own pin levels. Scope these pins before connecting a controller.

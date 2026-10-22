# Power management

## Status (2026-10-22)

**Implemented:** CC filter, ADC monitor, main-context switch policy, UART commands,
normal/bench/legacy build selection. **Host-tested:** production logic with GPIO,
ADC and USB state stubs, plus real RX/parser/TX/HID integration. **Hardware-validated:**
no new power feature. **Not yet tested:** ADC electrical behavior, board timing,
assembled BOM, USB-C source compatibility and load switching. **Future work:**
calibrated VREF acquisition, full board qualification and separate RGB controller.

## Build selection and default OFF

| Preset | Rev.2 GPIO/ADC | Bench enable | Hardware acknowledgement |
|---|---|---|---|
| LegacyDebug | OFF | OFF | OFF |
| Rev2Debug | ON | OFF | OFF |
| Rev2Bench | ON | ON | OFF |

Fresh generic Debug/Release configurations default to the legacy board.
`GS75_BOARD_REV2` selects the new physical port; the old board does not touch the
five power pins or ADC1. `GS75_RGB_POWER_BENCH_TEST=OFF` prevents PWRON, including
when all input conditions are good. Diagnostics work in Rev2Debug.

Even Rev2Bench requires `GS75_POWER_HARDWARE_VERIFIED=ON` before PWRON can succeed.
This explicit bench acknowledgement is necessary because U13's A1/A3 BOM conflict
and wiring cannot be resolved by software. See the checks in POWER_TEST_PLAN.md.
The low-level output port enforces both flags independently of the manager.
No build enables power automatically. Reset/USB configuration do not call PWRON.
Generic Debug and Release presets explicitly reset all three power flags to OFF,
even when reusing a cache. CMake rejects BENCH_TEST without Rev.2 and VERIFIED
without BENCH_TEST. The acknowledgement is a build assertion by the operator,
not software proof of populated parts or a safe load.

## Modules and data flow

- `board_power_config.h`: build gates, periods, limits and calibration defaults.
- `usb_c_detect.c/.h`: OUT1 bit1, OUT2 bit0; 3=UNATTACHED, 2=ATTACHED_DEFAULT,
  1=ATTACHED_1P5A, 0=ATTACHED_3A. Raw and stable codes, validity, raw/stable change
  times and stable attach/detach counters. A code must remain unchanged for 20ms.
- `current_monitor.c/.h`: portable conversion, validity, EMA (1/8), lifetime
  min/max/arithmetic mean, sample count, timestamp and calibration parameters.
- `power_port_stm32.c` / `power_port.h`: five GPIOs, ADC1 and read-only USB state.
  PC9 is latched LOW before becoming an output. External 100k holds it OFF during
  reset. No RGB signal pins, IRQ, timer or DMA is configured.
- `power_manager.c/.h`: owns the switch, fault latch and decisions. It replaces a
  separate rgb_power_switch module because the only physical output is PC9.
- `power_debug.c`: framed diagnostics and explicit commands; never called by ISR.

The main loop services power before UART parsing. ADC conversion starts every
10ms when main is serviced normally (nominal 100Hz, not a timer guarantee), and
EOC is polled without waiting. ADC1 uses 12-bit, right-aligned single conversion,
channel 3, 480-cycle acquisition and PCLK2/4 = 24MHz. CMSIS register access avoids
adding an ADC HAL/IRQ subsystem to the generated project. Missing EOC after 10ms,
overrun or invalid sample invalidates the monitor; a conversion restart gets a
fresh 10ms interval so ADC startup time is respected. No polling loop blocks HID.

UART TX remains blocking, now split into at most 64-byte portions with a 10ms HAL
timeout. Power is serviced before and after each portion, also for old DUMP/RAW
and GHOST_EVENT replies. Normal response bytes remain unchanged. A TX error stops
the reply and forces OFF. RX, SysTick, USB IRQ priorities and scan timing are
unchanged. No protection code is added to ISR. A main service gap over 25ms latches
OFF if power was ON and restarts CC qualification. Software cannot act while main
is stalled. Fault/Error exception hooks make a best-effort direct LOW GPIO write
before looping. A frozen core, disabled GPIO clock, lost bus or invalid stack can
leave EN high indefinitely: there is no independent watchdog-driven EN disable.
TPS2553 and an external current-limited source are the independent fast protections. Actual
worst-case response latency must be measured; the 25ms check is not a watchdog.

## Current conversion and validity

`ADC_mV = round(raw * VREF_mV / 4095)`.
For A1 and 0.05 ohm, `1 mV = 1 mA`; subtract zero offset in mV, clamp small negative
residuals to zero, then apply `gain_ppm / 1000000`. Defaults: 3300mV, zero 0mV,
gain 1000000ppm. VREF is an **assumption**, not a measured supply. The STM32 factory
VREFINT calibration alone cannot correct voltage without sampling VREFINT; that
extra channel is deferred. No undocumented calibration value is substituted.

`CurrentMonitor_Calibrate` validates VREF 2700..3600mV, zero -100..100mV and gain
500000..1500000ppm, then resets statistics. It is an API for controlled future
calibration; there is no live UART calibration command or persistent calibration.
Build defaults can be edited after external measurements; do not calibrate while
power is ON. All diagnostics explicitly say MEASUREMENT=PRELIMINARY.

Raw >=4090 is saturated; raw >4095, corrected current above 3200mA, correction
below -20mV, conversion errors and samples older than 50ms are invalid. Zero is a
valid numeric sample and currently passes the PWRON conditions. A stuck-low ADC
can therefore falsely look like zero load, defeating software overcurrent protection.
A nonzero value alone also cannot prove correct gain or shunt polarity. This is
a hardware validation gate: compare PA3 voltage/current against an external meter
at two known load points before producing an acknowledged bench build. Invalid readings retain
last numeric values for diagnosis: callers must honor VALID. Protection uses the
unfiltered valid current so the EMA does not hide an overload. BASELINE_MA is the
last valid sample obtained while RGB was OFF; it is diagnostic, not a reservation
of future load budget, and requires settling/verification before interpretation.

## State policy

| State | Meaning |
|---|---|
| OFF | Output off, explicit stop/reset or normal-build inhibition |
| WAIT_USB | CC unstable/unattached, USB not configured or suspended |
| READY | Bench gates and all inputs valid; still OFF until PWRON |
| ON | Explicit PWRON succeeded; conditions checked on every service |
| FAULT_LATCHED | Fault captured; no re-enable without successful PWRRESET |
| UNAVAILABLE | Legacy board, invalid port/ADC or unverified hardware |

Transitions never automatically turn ON. Any raw CC change invalidates stability
and switches an ON output OFF immediately on the next service, without waiting
20ms; a higher advertised current also requires a fresh explicit command.
USB disconnect/suspend switches OFF and does not restart on reconnect/resume.
FAULT# LOW always forces OFF and latches (the TPS2553 already deglitches its FAULT).
Overcurrent, invalid current, invalid port configuration and an excessive service
gap while ON also latch. FAULT_COUNT increments once per latch episode and saturates at UINT32_MAX; LAST_FAULT
survives PWROFF/PWRRESET. LAST_OFF records the latest shutdown command/cause.
PWRRESET first forces OFF, then checks current safety conditions; a successful
reset clears only the latch, retains counters and remains OFF. PWRON is separate.

## Budget and protection thresholds

| Source and USB state | Engineering total-current threshold | Effective budget |
|---|---|---|
| Not configured / suspended / CC uncertain | No enable | 0mA |
| Stable Default + configured | 450mA | min(450, configured bMaxPower), currently **100mA** |
| Stable 1.5A or 3A + configured | 700mA | min(700, configured bMaxPower), currently **100mA** |

USB descriptor bMaxPower is read from the active configuration (50 * 2mA).
**The cap applies to every CC mode.** [USB-IF's compliance guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical)
requires the average current drawn from the bus to remain within declared
bMaxPower. TUSB320 detects source current advertisement; it cannot silently
increase this device's USB configuration allocation. Thus the 450/700mA values
remain engineering policy ceilings, but neither permits >100mA while this
configuration is active. USB-A-to-C legacy cables normally report Default; an
incorrect or stuck CC output cannot authorize above the descriptor cap.

No USB descriptor or endpoint was changed by this audit. A future genuinely
higher-power configuration needs a separate descriptor/compliance decision, source
validation, revised policy and board tests. No PD negotiation or 9/12/20V operation
exists. The present clamp is a measured-total-current trip threshold, not a
hardware current regulator or a guarantee about short transients.

At or above the effective budget, new enabling is denied and an ON output latches
OFF. The requested 450/700 limits are configurable in board_power_config.h.
They apply to **total board current**. TPS2553's resistor sets roughly 796..939mA
for the **RGB branch only** and is unaffected by firmware. There is no software
current regulator and no way to eliminate fast transients by polling ADC.

## UART commands

115200 8N1, uppercase, CR/LF, same RX overflow/recovery policy. No periodic output.

| Command | Response |
|---|---|
| PWRINFO | BEGIN=PWRINFO / END=PWRINFO; state, inhibits, CC, USB, budget, switch, faults, baseline |
| CURRENT | BEGIN=CURRENT / END=CURRENT; raw/mV/mA, EMA/mean/min/max, valid/saturation, calibration |
| PWRTEST | Both groups, read-only GPIO/ADC/config sanity; never asserts EN |
| PWROFF | PWROFF=OK; immediate explicit OFF, latch retained |
| PWRON | PWRON=OK or exact rejection reason |
| PWRRESET | PWRRESET=OK or condition preventing latch reset; always OFF |

Responses use key=value CRLF lines. CC_RAW/CC_STABLE encode OUT1 in bit1 and OUT2
in bit0. CC_VALID qualifies the stable state; do not use the previous stable value
alone during a transition. PORT_CONFIG_OK checks key GPIO/ADC register assignments,
not wiring continuity. CC_WIRING_DIAG explicitly reports limited coverage.
Diagnostics snapshot their fields before TX; power continues being serviced during
printing, so the response is a snapshot rather than a live guarantee of ON.

## Reproducible validation

```powershell
cmake -S tests -B build/host-tests -G "Visual Studio 17 2022" -A x64
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
cmake --preset Rev2Debug
cmake --build --preset Rev2Debug --clean-first
cmake --preset LegacyDebug
cmake --build --preset LegacyDebug --clean-first
cmake --preset Rev2Bench
cmake --build --preset Rev2Bench --clean-first
```

151 Python unittest cases in seven CTest suites: previous 104 unchanged cases,
40 power policy/measurement cases, 5 real UART/HID integration cases and
2 CMake flag rejection cases. Host C uses
/W4 /WX. The power suite compiles normal, bench, unverified and legacy variants;
peripheral stubs cannot validate ADC electrical behavior or STM32 register timing.
Audit against Git HEAD: existing matrix, USB, clock, UART RX, .ioc and generated
CMake sources remain untouched. Rev.2 integration changes in generated files are
limited to preserved USER CODE blocks in main.c and stm32f4xx_it.c.

ARM clean builds: 52 steps each, no warnings/errors. Memory (includes reserved
stack/heap, not a stack watermark): LegacyDebug FLASH 48,996 / RAM 7,080 bytes;
Rev2Debug and Rev2Bench FLASH 51,236 / RAM 7,080 bytes; acknowledged
Rev2BenchVerified FLASH 51,300 / RAM 7,080 bytes. The build variants and
evidence are recorded in [POWER_AUDIT.md](POWER_AUDIT.md).
Compiling an acknowledged bench branch is not hardware verification.
Detailed hardware acceptance is in [POWER_TEST_PLAN.md](POWER_TEST_PLAN.md).
Record real measurements in the blank [POWER_TEST_RECORD.md](POWER_TEST_RECORD.md).
Future work includes measured USB suspend current and a separate USB 2.0
High-Power 500mA feasibility review; the current descriptor remains 100mA.


## Files changed in this stage

New firmware: `Core/Inc/board_power_config.h`, `usb_c_detect.h`,
`current_monitor.h`, `power_manager.h`, `power_port.h`; matching source modules
`Core/Src/usb_c_detect.c`, `current_monitor.c`, `power_manager.c`, plus
`power_port_stm32.c` and `power_debug.c`.

Integration: `Core/Src/main.c`, `Core/Src/matrix_debug.c`, root `CMakeLists.txt`
and `CMakePresets.json`. Tests: `tests/CMakeLists.txt`, `tests/debug_harness.c`,
new `tests/power_harness.c`, `tests/test_power.py`, `tests/test_power_uart.py`,
`tests/test_build_flags.py`. Audit fixes also touch `Core/Src/stm32f4xx_it.c`
within USER CODE and add `PowerPort_EmergencyOff()` to the port.
Documentation: `CODEX_CONTEXT.md`, `README.md`, new `docs/HARDWARE_REV2.md`,
`docs/POWER_MANAGEMENT.md`, `docs/POWER_TEST_PLAN.md`.
The supplied schematic is unchanged; no build products, logs or caches are staged.


## Audit outcome

See [independent audit](POWER_AUDIT.md) for severity, evidence, remaining limits,
updated latency assumptions and acceptance decision. USB Boot/NKRO/Consumer
protocols and descriptors are unchanged.

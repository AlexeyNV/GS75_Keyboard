# Board Rev.2 power: independent audit

2026-10-22. Scope: source, schematic, host tests and clean ARM builds. **At audit time, no Rev.2 board had been flashed or tested.** The available schematic is `hardware/schematics/SCH_keyboard.json`; the requested dated export was absent.

## Findings

| Severity | Evidence and action |
|---|---|
| Critical, fixed | `Core/Src/power_manager.c` previously capped `BUDGET_MA` by USB `bMaxPower` only for Default CC. With 1.5A/3A advertisement it allowed the 700mA policy even though `Core/Src/usb_descriptors.c` declares 100mA total. `test_typec_budget_both_advertisements` reproduces this. Every CC mode now uses `min(policy_ma, declared_ma)`; tests require 100mA for Default/1.5A/3A. Descriptors are unchanged. |
| Major, mitigated | Fatal exception loops and `Error_Handler` left PC9 HIGH. `Core/Src/stm32f4xx_it.c` and `Core/Src/main.c` now call `PowerPort_EmergencyOff()` in USER CODE; the port writes PC9 LOW directly through BSRR without HAL. This works only if an exception handler executes and the GPIO bus/clock remain functional. |
| Major, open hardware gate | `Core/Src/current_monitor.c` accepts stable ADC zero as valid. `test_stuck_zero_adc_can_appear_valid_but_needs_physical_proof` demonstrates that an acknowledged bench build can return `PWRON=OK` with stuck-low PA3. The schematic's U13 symbol says INA180A1 but its Manufacturer Part says INA180A3. Inspect the populated part and verify sensor/shunt polarity, PA3 voltage and external current at two known loads before bench enable. Firmware cannot prove continuity. |
| Major, fixed | Generic Debug/Release CMake presets could inherit stale bench flags from a cache. They now explicitly set all three power flags OFF; CMake rejects VERIFIED without BENCH_TEST and BENCH_TEST without BOARD_REV2. Two regression cases exercise rejected combinations. |
| Minor, fixed | Attach/detach, fault and ADC sample counters could wrap. They now saturate at `UINT32_MAX`. Latest current and unfiltered protection still update after sample-count saturation; the cumulative mean freezes. |

[USB-IF guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical) requires bus average current within the active configuration's `bMaxPower`. [TUSB320LAI](https://www.ti.com/lit/ds/symlink/tusb320lai.pdf) reports source advertisement but does not change the device's USB configuration. The 100mA cap is **total board current**, not an RGB-branch current regulator or a fast transient limit. The prior bug is a compliance risk, not evidence that this board drew 700mA.

## Safety and implementation review

`Core/Src/power_port_stm32.c` latches PC9 LOW before making it push-pull output; the schematic's R32 100k pull-down holds EN LOW during reset. LegacyDebug and Rev2Debug prohibit ON in both manager and port. Rev2Bench prohibits ON unless the operator explicitly builds with HARDWARE_VERIFIED. No automatic ON path exists. PB4 FAULT# LOW, unstable/detached CC, USB suspend/deconfiguration, invalid/stale ADC, overcurrent, invalid port or a detected main-service gap switch an ON output OFF. Faults latch. `PWRRESET` forces OFF, never auto-restarts. Host tests cover these transitions, repeated commands and tick wrap.

**Hardware:** reset EN pull-down and TPS2553 RGB-branch limiter/FAULT (nominal 30k ILIM, approximately 0.8–0.94A before tolerances). **Software:** 100mA current-budget trip, fault latch, CC/USB/ADC checks, exception hooks and service-gap detection. **Assumptions:** correct U13 A1 assembly, 0.05-ohm R30, 3.3V VDDA, ADC/CC wiring, working SysTick/main. The schematic shows no watchdog-to-EN or fault-to-EN circuit. A completely hung MCU can keep PC9 HIGH; use an externally current-limited source for board testing. TPS2553 limits only the RGB branch and cannot enforce a 100mA total USB budget.

ADC1 is custom-owned, with GPIOA/B/C and ADC1 clocks enabled, PA3 analog, channel 3, 12-bit right-aligned single conversion, 480-cycle sampling, PCLK2/4 = 24MHz from configured 96MHz. ADON precedes the first scheduled conversion by at least 10ms. Polling checks OVR before EOC, reads DR on EOC, times out missing EOC, and invalidates readings older than 50ms. The unfiltered sample controls protection. INA180A1 gain 20 times R30 0.05 ohm gives 1mV/mA. VDDA, gain, offset, polarity and populated RC have **not** been measured. Register review follows the [STM32F411 reference manual](https://www.st.com/resource/en/reference_manual/rm0383-stm32f411xce-advanced-armbased-32bit-mcus-stmicroelectronics.pdf); it cannot validate analog hardware.

TUSB320 OUT1/PC6 is bit 1 and OUT2/PC7 bit 0: 11 unattached, 10 Default, 01 1.5A, 00 3A. A raw change invalidates the stable mode immediately; 20ms unchanged input qualifies it. A valid code does not prove pull-up or wire integrity. USB configured/suspended state is checked separately. A USB-A-to-C cable or false high-current CC indication cannot raise the effective budget beyond 100mA.

PB12–PB15, PD2, PA8 and PC8 have no active SPI/PWM/EXTI configuration in `.ioc`, generated GPIO or power modules. U4 pin 5 has unswitched 3.3V: an attached external controller can still drive its own signals with +5V_RGB OFF. Matrix, HID Boot/NKRO/Consumer, USB descriptors, UART RX, clocks, `.ioc` and generated CMake sources remain unchanged versus pre-Rev.2 Git HEAD; generated edits are confined to preserved USER CODE hooks. No RGB driver was added.

## Shutdown latency

`matrix_debug.c:Send()` services power before and after every blocking UART chunk of at most 64 bytes. At 115200 8N1 a full chunk takes about 5.56ms on the wire; `HAL_UART_Transmit` uses a 10ms tick-based timeout. Under a running 1ms SysTick and bounded interrupt/main work, a FAULT# transition is seen on the next power service, nominally within one UART chunk plus scheduling overhead. ADC overcurrent also waits up to its nominal 10ms next sample. CC and suspend shut down at the next service without waiting for 20ms CC requalification. The 25ms service-gap check triggers **after** a paused main loop resumes; it is not a watchdog. TPS2553 fault deglitch precedes FAULT# assertion.

**No absolute software upper bound is provable** if SysTick stops, interrupts are unbounded or MCU/main hangs. The 10ms UART timeout is conditional. On hardware, scope FAULT#, EN and input current together during idle, long DUMP/RAW/PWRINFO and HID traffic; capture maximum observed FAULT-low-to-EN-low delay and separately overload onset-to-FAULT-low. Use a limited supply and dummy load.

## Verification and decision

`ctest --test-dir build/host-tests -C Debug --output-on-failure`: **7/7 suites, 151 Python cases** (19 matrix, 13 keyboard, 49 HID, 23 Consumer, 40 power, 5 power/UART, 2 build flags). Host C uses `/W4 /WX`. These tests do not validate STM32 electrical behavior.

Clean ARM configure/build passed without warnings/errors, 52 steps per variant:

| Variant | FLASH | RAM | Build gates |
|---|---:|---:|---|
| LegacyDebug | 48,996 B | 7,080 B | Rev2 OFF, bench OFF, verified OFF |
| Rev2Debug | 51,236 B | 7,080 B | Rev2 ON, bench OFF, verified OFF |
| Rev2Bench | 51,236 B | 7,080 B | Rev2 ON, bench ON, verified OFF |
| Rev2BenchVerified | 51,300 B | 7,080 B | All three ON |

Cache gates were inspected. No flashing, hardware measurement, commit or push occurred during that software-only audit.

**Pre-bring-up audit decision: GO only for controlled diagnostic Rev2Debug first power-up**, after unpowered PCB/BOM inspection, with RGB controller disconnected and PC9/+5V_RGB scoped through reset and enumeration. PWRON must report BENCH_DISABLED and EN remain LOW. Run CC/USB/ADC and keyboard/HID diagnostics per [POWER_TEST_PLAN.md](POWER_TEST_PLAN.md).

**NO-GO for +5V_RGB enable yet.** First resolve U13 A1/A3, check R30/sense polarity, PC9 pull-down and TPS2553, measure VDDA/PA3 against an external meter at no load and two known currents, evaluate stuck-zero/open-wire behavior, and confirm headroom below 100mA total USB draw. Only then consider the acknowledged bench build with a low-current dummy load and externally limited source. Record board identity, firmware hash, meter and scope traces in the
[hardware test record](POWER_TEST_RECORD.md) before claiming protection latency
or connecting RGB hardware. Future measurements include USB suspend current; a
USB 2.0 High-Power 500mA option requires a separate feasibility and compliance
review, with no change to this baseline descriptor or firmware.

## Subsequent user-run bring-up

After the software-only audit, the user built and ran Board Rev.2 with
Rev2Debug. The steady OFF state, USB-C Default attach, keyboard and initial ADC
readback are documented in [POWER_TEST_RECORD.md](POWER_TEST_RECORD.md). The A1
part is physically confirmed; external-current calibration, switch-on, fault
response, USB suspend and timing still have no hardware test. The audit's
conditional NO-GO for +5V_RGB enable remains in force.

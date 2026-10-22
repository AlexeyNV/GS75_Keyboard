# Board Rev.2 power hardware test record

**Blank template - no measurements or PASS results have been recorded.**
Make a copy for each board, firmware image and test session. Follow
[POWER_TEST_PLAN.md](POWER_TEST_PLAN.md) before asserting EN. A diagnostic
Rev2Debug session is separate from an acknowledged dummy-load bench session.

## Identity and traceability

| Field | Recorded value |
|---|---|
| Session ID / date / operator | |
| PCB revision, assembly ID and board serial | |
| Schematic revision / U13 populated marking / R30 measured value | |
| Firmware Git commit or SHA256, preset, CMake power flags | |
| USB host/hub, port, OS and cable orientation | |
| Type-C source/fixture, advertised CC mode and current limit setting | |
| Ambient temperature, load type and wiring diagram reference | |
| UART log, photo and scope trace filenames | |

## Measurement equipment

| Instrument | Manufacturer/model | Serial or asset ID | Calibration due / verified | Range, probe, burden or accuracy notes |
|---|---|---|---|---|
| USB VBUS current meter | | | | |
| DMM for VDDA, PA3 and shunt | | | | |
| Oscilloscope / logic analyzer | | | | |
| Current probe or shunt monitor | | | | |
| Current-limited supply / Type-C fixture | | | | |
| Electronic dummy load | | | | |

Record meter insertion point and whether USB current includes the entire adapter.
For the oscilloscope, record probe attenuation, bandwidth limit, trigger threshold
and a shared timebase for FAULT# and EN. Record uncertainty or instrument accuracy
for every current and timing comparison.

## Measurements

Use **NOT RUN / PASS / FAIL** in the result column. Keep raw data and trace
references even after a failure. Limits must be set from the applicable USB
specification and the selected test topology before judging a result; a host
test passing does not populate this table.

| ID / condition | USB input current mA (average / peak) | VDDA V / VBUS V / PA3 mV / shunt mV | EN V / +5V_RGB V / FAULT# V | Reaction time ms (trigger to EN LOW) | Expected observation / evidence | Result / trace |
|---|---|---|---|---|---|---|
| R01 Reset and pre-init, RGB controller absent | | | | | EN LOW, +5V_RGB OFF | |
| R02 Rev2Debug configured, idle keyboard, Default CC | | | | | PWRON denied; record baseline | |
| R03 Rev2Debug configured, 1.5A CC | | | | | BUDGET_MA=100; EN LOW | |
| R04 Rev2Debug configured, 3A CC | | | | | BUDGET_MA=100; EN LOW | |
| R05 USB-A-to-C, both connector orientations where applicable | | | | | CC code and budget recorded | |
| R06 USB unconfigured / disconnected | | | | | EN LOW; log USB/CC state | |
| R07 USB suspend, after settling | | | | | Measure **total USB bus suspend current** and EN/rail state; compare with applicable limit | |
| R08 USB resume / reconnect | | | | | No automatic power-on; current recovers to measured baseline | |
| R09 Sensor no-load and two known currents, externally metered | | | | | Confirm INA180A1 slope/offset and PA3 continuity | |
| R10 Stuck-low/open sensor fault injection, controlled fixture | | | | | Record diagnostic limitation and external-meter discrepancy | |
| R11 Verified bench: small dummy load below 100mA total | | | | | PWRON only after hardware gates; log total current | |
| R12 PWROFF while ON | | | | | EN LOW; measure rail discharge separately | |
| R13 FAULT# LOW while idle | | | | | EN LOW, fault latched | |
| R14 FAULT# LOW during DUMP / RAW / PWRINFO | | | | | EN LOW; capture worst observed delay across repeated trials | |
| R15 Software overcurrent during ON | | | | | EN LOW, fault latched; compare ADC to meter | |
| R16 ADC timeout/saturation during ON | | | | | EN LOW, ADC_INVALID latch | |
| R17 USB suspend or CC loss during ON | | | | | EN LOW, no auto-restart | |
| R18 Controlled HardFault / Error_Handler bench test | | | | | Best-effort EN LOW; note no full-hang guarantee | |
| R19 Keyboard/HID plus UART overlap | | | | | Preserve scanning and no stuck keys; record MAX_SCAN_US | |

For R07, record suspend entry and measurement settling time, remote-wakeup
configuration, CC advertisement, cable, host/hub and whether the source itself
remains active. Repeat with the RGB branch OFF and, only after separate safe bench
qualification, with ON-to-suspend shutdown. Measure the applicable USB 2.0 suspend
limit for this topology from the current specification/ECN; do not infer
compliance from the firmware's EN=LOW alone.

## Future USB 2.0 High-Power 500mA feasibility - research only

**No 500mA operation is authorized by this firmware or this record.** The active
configuration currently declares 100mA. USB 2.0 *High-Power* is a power category,
not a claim that the STM32 USB link operates at High-Speed. Investigate a separate
future descriptor/policy revision only after the following data are recorded.

| Question / calculation | Measured data, source or analysis | Decision / evidence |
|---|---|---|
| Total board baseline and peak current in configured, unconfigured and suspend states | | |
| Additional load current available after baseline, margins and measurement uncertainty | | |
| 3.3V regulator, TPS2553, connector, PCB traces and shunt thermal headroom | | |
| Host/hub and USB-A-to-C / Type-C source compatibility and advertised current | | |
| Applicable USB 2.0 High-Power descriptor, configuration, suspend and compliance requirements | | |
| Required firmware, descriptor and regression-test changes for a future separate stage | | |
| Go/no-go for proposing a 500mA-capable revision (not permission to energize) | | |

Relevant primary references: [USB-IF USB 2.0 specification and suspend-current
ECN](https://www.usb.org/documents?search=usb+2.0) and
[USB-IF bus-current compliance guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical).
The applicable limits and revision must be recorded in the test session.

## Session conclusion

| Gate | PASS / FAIL / NOT RUN | Evidence, restrictions and next action |
|---|---|---|
| Diagnostic Rev2Debug safe to run | | |
| INA180A1 BOM and current measurement validated | | |
| +5V_RGB dummy-load test safe to attempt | | |
| Shutdown latency characterized on this board | | |
| USB suspend current assessed | | |
| High-Power 500mA feasibility study complete | | |

Reviewed by: ____________________    Date: ____________________

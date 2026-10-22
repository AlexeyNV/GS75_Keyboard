# Board Rev.2 hardware bring-up record

**Initial user-reported Rev2Debug bring-up.** Board Rev.2 was built, flashed and
started by the project owner. The session date, board serial, programmed image
hash and instrument model were not supplied. This record distinguishes direct
multimeter voltages from firmware UART readings. No agent operated the board.
The previous [power audit](POWER_AUDIT.md) preceded this bring-up.

Result vocabulary: **PASS** = the stated condition was exercised and observed;
**PARTIAL** = only some parts were checked; **NOT RUN** = no hardware test;
**FAIL** = a malfunction was observed. No FAIL was reported. An unperformed
measurement must not be inferred from a passing software test.

## Identity and equipment

| Field | Reported information |
|---|---|
| Hardware | Board Rev.2, built and running; board serial and assembly date not provided |
| Firmware | Diagnostic Rev2Debug reported; exact flashed binary hash not provided |
| Project baseline before this record | Git commit `8595d1488ad72be23b59687978f0454f88ef9497`; not independently matched to the flashed binary |
| U13 | Populated INA180A1IDBVR confirmed by the user; schematic A1/A3 conflict resolved for the reported board |
| Keyboard | MSI GS75, 26-pin FPC, 8 x 18 matrix and 102 mapped keys reported |
| USB host, cable, supply | PC enumeration and both USB-C plug orientations reported; host/cable model and source current limit not provided |
| Multimeter | Used for the five voltages below; manufacturer, model, serial and calibration status not provided |
| External USB ammeter, oscilloscope, current probe, dummy load | Not used/reported |
| UART logs, traces, photos | Individual files not supplied; values below are from the user's report |

The multimeter was **not** used as an external series ammeter. The firmware's
`CURRENT_MA` is an INA180/ADC-derived estimate, not a separately measured USB
input current. Do not use it as proof of current-monitor calibration or USB
power compliance.

## Confirmed observations

| ID | Observation | Source | Result and limit |
|---|---|---|---|
| B01 | STM32F411RET6 runs the configured 96 MHz SYSCLK; USB enumerates on the PC | User report | PASS for boot/enumeration; no independent clock-frequency measurement |
| B02 | `USB_STATE=CONFIGURED`, `USB_PROTOCOL=REPORT`, `USB_LAST_REPORT_SIZE=22` | Firmware UART reported by user | PASS for observed Report/NKRO configuration; Boot mode was not separately exercised in this session |
| B03 | USB Consumer Control works; Fn+Left/Right volume, Fn+Numpad 0 Mute, Fn+PageUp Home and Fn+PageDown End work | User-observed keyboard behavior | PASS for listed combinations; other Fn paths are not implied |
| B04 | All keys checked by the user via key-test.ru; 8 x 18 matrix, 102 keys, `SCAN_HZ=1000`, `DEBOUNCE_MS=5` | User report and firmware diagnostics | PASS for reported key behavior and diagnostic values; no independent scan-period trace |
| B05 | Released baseline `RAW=0`, `DUMP=0`, `KEY_COUNT=0`; Left Ctrl+X+U produces expected ghost event, which clears after release | Firmware UART and user operation | PASS for this baseline and ghost combination |
| B06 | `MAX_SCAN_US=501`; `MAX_RUNTIME_US=721` and `717` in separate checks | Firmware UART reported by user | PASS for diagnostic readback; these are observed maxima, not certified worst-case bounds |
| B07 | Both USB-C plug orientations work; `ATTACHED_DEFAULT`, `CC_RAW=2`, `CC_STABLE=2`, `CC_VALID=1`, `USB_CONFIGURED=1`, `USB_SUSPENDED=0` | User operation and firmware UART | PASS for Default-source attach in both orientations; 1.5A/3A and USB-A-to-C not tested |
| B08 | VDD STM32 = 3.29 V; PA3/INA_CUR = 29.6 mV; PC9/BUS_EN = 0 V; PB4/FAULT# = 3.285 V; +5V_RGB = 15 mV | **Multimeter**, user report | PASS for steady Rev2Debug voltages only; reset transients and switch-on not measured |
| B09 | `ADC_RAW=36`, `ADC_MV=29`, `CURRENT_MA=29`, `MIN_MA=26–27`, `MAX_MA=32`, `VALID=1`, `BASELINE_MA=29` | **Firmware UART**, user report | PARTIAL: ADC readback agrees approximately with PA3 voltage; gain/offset accuracy not validated |
| B10 | `BUDGET_MA=100`, `PORT_CONFIG_OK=1`, `FAULT_ACTIVE=0`, `RGB_POWER=0`; `PWRON=BENCH_DISABLED` | **Firmware UART**, user report | PASS for observed diagnostic interlock; no powered RGB-branch protection test |

## Power test plan tracking

The original test IDs from [POWER_TEST_PLAN.md](POWER_TEST_PLAN.md) remain below.
Blank measurements are marked **not measured**, not silently filled from ADC
estimates. Reported steady-state values do not prove reset or fault latency.

| ID / condition | External USB current | Relevant measured voltage / UART evidence | Timing | Result |
|---|---|---|---|---|
| R01 Reset and pre-init EN LOW | Not measured | Steady PC9=0 V and +5V_RGB=15 mV only (multimeter) | Reset waveform not captured | **PARTIAL** |
| R02 Rev2Debug configured, idle keyboard, Default CC | Not measured | PC9=0 V, PB4=3.285 V, PA3=29.6 mV (multimeter); CURRENT_MA=29, BUDGET_MA=100, PWRON=BENCH_DISABLED (UART) | Not measured | **PASS** for diagnostic behavior; current accuracy remains open |
| R03 Rev2Debug with 1.5A CC | Not measured | Not measured | Not measured | **NOT RUN** |
| R04 Rev2Debug with 3A CC | Not measured | Not measured | Not measured | **NOT RUN** |
| R05 USB-C both connector orientations | Not measured | Default CC code 2 and USB operation in both orientations (UART/user report) | Not measured | **PASS** for USB-C orientation; USB-A-to-C **NOT RUN** |
| R06 USB unconfigured / disconnected state | Not measured | Not measured | Not measured | **NOT RUN** |
| R07 USB suspend current | Not measured | `USB_SUSPENDED=0` during active checks (UART) | Not measured | **NOT RUN** |
| R08 USB resume / reconnect | Not measured | Not measured | Not measured | **NOT RUN** |
| R09 INA180 no-load plus two known external-current points | External current not measured | U13 A1 confirmed; PA3=29.6 mV (multimeter); ADC_MV=29 and CURRENT_MA=29 (UART) | Not measured | **PARTIAL**; two-point slope/offset unverified |
| R10 Stuck-low/open sensor fault injection | Not measured | Not measured | Not measured | **NOT RUN** |
| R11 Verified bench +5V_RGB with dummy load | Not measured | RGB_POWER=0 and +5V_RGB=15 mV while OFF | Not measured | **NOT RUN** for ON test |
| R12 PWROFF while ON / rail discharge | Not measured | Not measured | Not measured | **NOT RUN** |
| R13 FAULT# LOW while ON | Not measured | Idle PB4=3.285 V (multimeter), FAULT_ACTIVE=0 (UART) only | Not measured | **NOT RUN** for fault injection |
| R14 FAULT# during DUMP/RAW/PWRINFO | Not measured | Not measured | Not measured | **NOT RUN** |
| R15 Software overcurrent while ON | Not measured | Not measured | Not measured | **NOT RUN** |
| R16 ADC timeout/saturation while ON | Not measured | Not measured | Not measured | **NOT RUN** |
| R17 USB suspend or CC loss while ON | Not measured | Not measured | Not measured | **NOT RUN** |
| R18 Controlled HardFault / Error_Handler | Not measured | Not measured | Not measured | **NOT RUN** |
| R19 Keyboard/HID plus UART overlap | Not measured | Keyboard, listed Fn functions and ghost behavior work; no simultaneous UART-overlap run reported | MAX_SCAN_US=501; MAX_RUNTIME_US=721/717 (UART) | **PARTIAL** |

## Future USB 2.0 High-Power 500 mA feasibility

**NOT RUN.** The active USB configuration still declares 100 mA. No external
USB-current or suspend-current measurement has been made. A future 500 mA
descriptor/policy change requires a separate hardware and compliance review;
this record does not authorize higher current or RGB power.

| Question | Measured result | Status |
|---|---|---|
| Configured, unconfigured and suspend USB input current | Not measured | **NOT RUN** |
| Load headroom after baseline and uncertainty | Not measured | **NOT RUN** |
| Regulator, TPS2553, shunt, connector and PCB thermal headroom | Not measured | **NOT RUN** |
| Host/hub, USB-A-to-C and Type-C source compatibility | Default USB-C orientations only | **PARTIAL** |
| Descriptor, suspend and compliance requirements for a future revision | Not assessed in hardware | **NOT RUN** |

Primary references remain the [USB-IF USB 2.0 specification and
suspend-current ECN](https://www.usb.org/documents?search=usb+2.0) and
[USB-IF bus-current guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical).

## Session conclusion

| Gate | Status | Evidence / next action |
|---|---|---|
| Initial Rev2Debug USB/keyboard/CC/steady-OFF bring-up | **PASS** | User-reported boot, key checks, both USB-C orientations, multimeter and UART observations above |
| Full reset/pre-init EN behavior | **PARTIAL** | Steady LOW only; oscilloscope capture still needed |
| INA180A1 identity | **PASS** | User confirmed populated A1 |
| INA180 current accuracy and continuity | **PARTIAL** | One voltage/readback point only; external ammeter and two known loads still needed |
| TPS2553 powered switching, fault/overcurrent protection and shutdown delay | **NOT RUN** | Keep +5V_RGB OFF; use controlled dummy-load fixture later |
| USB suspend input current and 500 mA feasibility | **NOT RUN** | Measure and review in a separate stage |
| RGB controller and backlight | **NOT RUN** | Not connected; firmware driver not implemented |

Equipment identity, calibration certificates, raw UART logs, photos, scope
traces and individual test timestamps were not provided; no placeholders above
should be read as measurements.

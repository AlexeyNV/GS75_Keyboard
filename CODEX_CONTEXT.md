# GS75 Keyboard USB Adapter — Codex Context

## Goal
Convert the keyboard from an MSI GS75 laptop into a standalone USB keyboard using a custom STM32F411RET6 adapter board.

Current stage: the user built and ran Board Rev.2 with Rev2Debug. Core keyboard, all 102 keys, Consumer media shortcuts, ghost diagnostics, USB-C Default attach in both orientations and steady OFF-state power readings have initial hardware evidence. The 151 host tests and independent firmware power audit passed. U13 is confirmed populated as INA180A1. External current, sensor accuracy, TPS2553 switching/protection, suspend current and shutdown latency remain untested. RGB driver/signals are unused. See docs/POWER_TEST_RECORD.md for the exact user-reported measurements and limits; earlier board results remain historical.

## Hardware

MCU:
- STM32F411RET6, LQFP64
- External HSE crystal: 8 MHz
- SYSCLK: 96 MHz
- APB1: 48 MHz
- APB2: 96 MHz
- PLL48 clock: 48 MHz

USB (custom module, .ioc unchanged):
- PA11 = USB FS DM, PA12 = USB FS DP, AF10.
- Interface 0, Boot-subclass keyboard: Boot protocol 0 = 8-byte 6KRO;
  Report protocol 1 = 22-byte NKRO (reset default); no Report ID.
- IN endpoint 0x81, max packet 32 bytes, interval 1 ms.
- Interface 1 Consumer Control: subclass/protocol 0, EP82 8 bytes/1 ms;
  one-byte input bits Mute=1, Volume Up=2, Volume Down=4, no Report ID.
- Fn+PrintScreen -> Page 07 SysReq 0x9A; Fn+Left/Right/Numpad0 -> Consumer.
  Previous Home/End/Ctrl+Pause Fn mappings remain. Page 07 volume is removed.
- VBUS sensing disabled to preserve PA9/PA10 USART1. USB IRQ priority 6;
  USART1 RX priority 5 and SysTick priority 15 are unchanged.
- Development-only ST HID example VID/PID 0483:5710; not an allocated GS75 identity.
  Distributed hardware needs authorized IDs (GS75_USB_VID / GS75_USB_PID CMake settings).
- Product GS75 Keyboard Adapter, manufacturer GS75 Adapter, 96-bit UID serial.

Debug:
- ST-LINK/V2 over SWD
- SWDIO = PA13
- SWCLK = PA14
- NRST connected
- Target voltage approximately 3.2–3.3 V

UART:
- USART1
- PA9 = TX
- PA10 = RX
- 115200 baud, 8N1, no flow control
- External FT232RL-compatible USB-UART adapter at 3.3 V logic
- UART TX/RX verified in both directions
- Series resistors on UART lines are 22 ohm

Keyboard FPC, 26 pins:
- FPC01 -> PB10
- FPC02 -> PB2
- FPC03 -> PB1
- FPC04 -> PB0
- FPC05 -> PC5
- FPC06 -> PC4
- FPC07 -> PA7
- FPC08 -> PA6
- FPC09 -> PA5
- FPC10 -> PA4
- FPC11 -> PA2
- FPC12 -> PA1
- FPC13 -> PA0
- FPC14 -> PC3
- FPC15 -> PC2
- FPC16 -> PC1
- FPC17 -> PC0
- FPC18 -> PC13
- FPC19 -> PB9
- FPC20 -> PB8
- FPC21 -> PB7
- FPC22 -> PB6
- FPC23 -> PB5
- FPC24 -> PC12
- FPC25 -> PC11
- FPC26 -> PC10

CubeMX initial configuration for all 26 keyboard lines:
- GPIO input
- internal pull-up
- user labels FPC01...FPC26

Do not use the 40-pin RGB/backlight FPC yet.

## Project / build

Project created with STM32CubeMX.
Toolchain/IDE = CMake.
Workspace root: repository checkout (.)

Build is performed from VS Code using CMake Tools and STM32CubeCLT.
Known-good build:
- ELF: build/Debug/GS75_Keyboard.elf
- CMake/Ninja/arm-none-eabi-gcc confirmed working.

The project is intended to be kept in Git.

## Captured matrix evidence

- Full map: `tools/matrix_mapper/gs75_matrix_20260921_172228.json`.
- Ghosting test: `tools/matrix_mapper/ghosting_results/gs75_ghosting_20260921_174434.json`.
- 102 unique keys, all single-key captures bidirectional, all 5/5 samples matching and releases confirmed.
- Both baselines are empty (10/10 matching samples).
- 11 captured combinations match the discovery tool's electrical connectivity model.
- All 20 captured three-corner ghosting tests show the omitted fourth key as electrically possible.
- Treat the matrix as susceptible to ghosting. Software cannot infer the real pressed subset from a rectangle alone.

## Current firmware design

- Drive rows, in order: FPC17 through FPC24.
- Sense columns, in order: FPC01 through FPC16, FPC25, FPC26.
- `matrix_config.h`: 8 rows, 18 columns, nominal 1000 Hz, debounce 5 ms.
- `matrix.c/.h`: GPIO scanner and atomic `MatrixSnapshot`.
- `matrix_debounce.c/.h`: independent, symmetric 5 ms debounce for all 144 coordinates.
- `anti_ghosting.c/.h`: rectangle detection only; never modifies the input.
- `keymap.c/.h`: static row/column -> KeyId map of exactly the 102 measured keys.
  KEY_NONE marks the 42 unmapped positions. IDs 1..102 follow the JSON singles order;
  they are logical IDs, not USB HID usages. Keep the measured labels (including Fn).
- `keyboard.c/.h`: logical pressed/suppressed/ambiguous KeyId bitsets.
  Accept mapped debounced keys outside ambiguity. Inside ambiguity, retain only
  previously accepted keys whose debounced coordinate is still active.
  Suppress all new ambiguous keys; never choose a phantom corner.
  Once ambiguity disappears, logical state exactly matches mapped debounced keys.
  An electrically hidden release can remain logically held until it becomes observable.
- `keyboard_runtime.c/.h`: consume every completed scan after Matrix_Tick in SysTick,
  including while main is blocked in UART TX. Atomic snapshot API for diagnostics.
  This is a separate post-scan hook: scan/debounce code and timing constants are unchanged.
  MAX_SCAN_US still measures only Matrix_Tick, not the complete SysTick plus logical hook.
  It now calls UsbKeyboard_Capture with accepted state, without any USB API from SysTick.
- `hid_keymap.c/.h`: stable KeyId -> Page 07 HID usage; all 102 entries and FPC
  coordinates are reviewed in docs/HID_KEYMAP.csv. Fn has no direct usage.
  User-confirmed: BACKSLASH=0x31 (above Enter), PIPE_BACKSLASH=0x64 (between
  Space and Right AltGr), WIN=Right GUI 0xE7 (between Right AltGr and Right Ctrl).
- `hid_fn.c/.h`: immutable accepted logical -> effective HID translation only:
  Fn+PageUp=Home, Fn+PageDown=End, Fn+Pause=Left Ctrl+Pause,
  Fn+PrintScreen=SysReq 0x9A; Fn+Left/Right=Consumer Down/Up 0xEA/0xE9;
  Fn+Numpad0=Consumer Mute 0xE2. Keyboard and Consumer outputs are separate.
  Replace the base usage, never emit Fn itself, preserve real modifiers.
  The failed Page 07 volume experiment and GS75_FN_VOLUME option are removed.
- `usb_consumer.c/.h`: independent EP82 busy state, padded stable one-byte report
  buffer and completion/resync counters; no periodic reports or Output report.
- `hid_report.c/.h`: portable 8-byte Boot and 22-byte NKRO builders.
  Boot >6 non-modifiers emits ErrorRollOver in all six slots, retaining modifiers.
- `usb_keyboard.c/.h`: 64-entry accepted-state FIFO from post-scan to main.
  Busy endpoint retains pending states. Emergency overflow coalesces to latest
  full state and resynchronizes, including releases; counters expose overflow/resync.
  Protocol/configuration changes discard obsolete history and schedule full sync.
  Report construction runs outside critical sections; generation checks reject
  stale candidates. No USB API is called from SysTick or USART ISR.
- `usbd_keyboard.c`, `usb_descriptors.c/.h`, `usb_device.c/.h`, `usbd_conf.h`:
  custom class/descriptors and FS PCD port on vendored unmodified ST Device core.
  GET/SET_PROTOCOL, GET/SET_IDLE, input/output GET_REPORT and control LED
  SET_REPORT are implemented; five LED bits are stored only, never driven.
  Unchanged input is sent only for initial sync/protocol recovery/nonzero idle.
  UART TX can delay main-loop USB/idle servicing, but accepted transitions queue.
- `runtime_timing.c/.h`: MAX_RUNTIME_US measures scan + logical update + enqueue;
  retains MAX_SCAN_US meaning and does not change DWT setup or scan period.
- `matrix_debug.c/.h`: main-loop UART command parsing, KEYS and ghost diagnostics.
- `uart_rx.c/.h`: USART1 interrupt RX, 256-byte ring (255 usable bytes), stream-loss reporting.
  The ISR only reads/enqueues bytes and records errors; it never parses commands or transmits.
  The module owns RXNE/error interrupts directly; do not combine it with HAL receive APIs.
  USART1 priority 5 preempts SysTick priority 15, since a byte takes ~87 us at 115200 baud.
  Only the short ring pop/reset operation masks interrupts, preserving the caller's mask.
- RX starts in `MatrixDebug_Init()` before the READY banner. The USART1 IRQ wrapper
  and prototype are inside CubeMX USER CODE blocks. GPIO/UART settings and .ioc are unchanged.
- `Matrix_Init()` then `KeyboardRuntime_Init()` run after GPIO/UART initialization.
- `Matrix_Tick()` runs after `HAL_IncTick()` in the SysTick USER CODE block.
  It does nothing until initialization completes. Do not also call it from main.
- Each 1 ms tick scans all eight rows, updates debounce and the debounced ghost state.
- For each row: latch ODR LOW, select open-drain, enable output, wait 8 us,
  read 18 sense inputs, restore input, wait 1 us. Other rows remain inputs.
- Input pull-ups configured by CubeMX remain enabled throughout.
- `Matrix_GetSnapshot()` copies raw/debounced rows, ghost masks, counters and timing
  under a short interrupt mask; UART formatting/transmission happens with interrupts enabled.
- Raw/debounced data and ghost diagnostics include unmapped coordinates and remain
  unsuppressed. Only the logical layer filters unmapped coordinates and applies
  suppression, AFTER full-matrix ghost detection. HID consumes accepted keys only; no RGB.
- The old 26x26 `Matrix_Scan()/Matrix_GetRaw()` API has been replaced.

## UART protocol at current stage

115200 baud, 8N1. Commands are uppercase and terminated by CR or LF.

- `PING`: `PONG`.
- `INFO`: dimensions, drive/sense order, scan rate, debounce, scan counter,
  sample timestamp and maximum measured scan-handler duration in microseconds.
- `DUMP`: one consistent debounced 8x18 snapshot.
- `RAW`: one consistent raw 8x18 snapshot.
- `GHOST`: raw and debounced ambiguity diagnostics from the same snapshot,
  framed by `GHOST_BEGIN/GHOST_END`.
- `KEYS`: atomic logical snapshot framed by KEYS_BEGIN/KEYS_END. Lists KEY,
  SUPPRESSED and AMBIGUOUS symbolic KeyIds with counts and scan/timestamp metadata.
  AMBIGUOUS includes both retained and suppressed keys; no unmapped KeyId is emitted.
- `USBINFO`: USB state/configuration/protocol/idle/LED mask, completed TX count,
  queue depth/high-water/overflow, resync count, last report size and MAX_RUNTIME_US.
  Existing INFO/KEYS/electrical response fields retain their meanings.
- INFO keeps its existing fields and adds LOGICAL_GHOST=CONSERVATIVE and KEYMAP_KEYS=102.
  ANTI_GHOST=DETECT_ONLY still describes the unchanged electrical detector.
- `DUMP/RAW` retain `MATRIX_BEGIN`, `EDGE,FPCxx,FPCyy`, `EDGE_COUNT,n`,
  `MATRIX_END`, but each edge is now only drive -> sense (one bit per coordinate).
  They also include `MATRIX_STATE` and `SCAN` metadata.
- Automatic `GHOST_EVENT` reports changes in the debounced ambiguity state,
  at most once per 100 ms, including CLEAR transitions. This is a coalesced
  diagnostic stream, not a lossless key-event stream.
- Commands longer than 63 characters retain `ERR: BUFFER OVERFLOW` and are discarded through CR/LF.
- RX ring overflow reports `ERR: RX OVERFLOW`; hardware RX errors report `ERR: UART RX ERROR`.
  These reports come from main, never ISR. A stream loss flushes queued input, resets the
  partial command and discards through a fresh CR/LF. While loss is latched, incoming bytes
  (including delimiters) are dropped; after the error, send CR/LF before retrying a command.
- TX remains blocking; SysTick scanning and interrupt RX continue during transmission.
  Commands received during DUMP/GHOST_EVENT are queued and parsed afterwards, with
  serialized replies. Normal command/reply formats are unchanged.
  Prefer one outstanding command for automated use: the queue is bounded and has no flow control.

The old discovery mapper/ghosting scripts expect 26x26 connectivity; do not use
their capture mode unchanged with this firmware. Saved JSON files remain evidence/test fixtures.
Protocol details, host regression commands and board acceptance checks:
[docs/MATRIX_RUNTIME.md](docs/MATRIX_RUNTIME.md).
Logical policy, KEYS format and board checks:
[docs/KEYBOARD_STATE.md](docs/KEYBOARD_STATE.md).
USB formats, queue/Fn policy, prototype identity, build and board checks:
[docs/USB_HID.md](docs/USB_HID.md).

## Current validation stage

The user confirmed the 8x18 runtime on the board before the interrupt-RX change:
- SCAN_COUNT increased by 3001 over SCAN_TIME_MS delta 3001 ms: 1000 Hz.
- MAX_SCAN_US = 233 us.
- Without keyboard, RAW/DUMP remain at EDGE_COUNT=0 and raw/debounced GHOST are CLEAR.
- With keyboard, baseline is zero and A, W, Space, Enter, Fn match the saved map.
- Scanning continues during blocking DUMP.
- X + Left Ctrl + U gives the four expected coordinates, GHOST ACTIVE,
  ROW_PAIRS=1, RECTANGLES=1; release produces GHOST CLEAR.

Interrupt RX hardware validation was subsequently confirmed on 2026-09-22:
- RX works during blocking TX and GHOST_EVENT; overlap no longer causes UNKNOWN COMMAND.
- Scan frequency remains exactly 1000 Hz; MAX_SCAN_US is 236-237 us with RX IRQ.
- RAW/DUMP baseline remains zero, and the known ghost rectangle still works.
- Long command -> ERR: BUFFER OVERFLOW; the next PING -> PONG.

The logical keyboard layer passes 32 host tests across two CTest suites,
including all 102 mapped keys, 11 ordinary combinations, 20 ghosting fixtures,
4284 rectangle positions, temporal press/release sequences, unmapped corners,
and logical history during blocking UART TX. The production C modules are
tested with stubbed hardware. ARM configure/clean build pass without warnings/errors.
Scanner, debounce, electrical detector, UART RX, GPIO, clock and .ioc remain unchanged.
Those 32 tests remain unchanged and passing in the USB stage. No automatic flashing is performed.

USB hardware validation of the preceding stage was confirmed by the user:
Boot/NKRO, scanner/debounce/suppression, accepted-state queue, protocol, idle and
LED control worked. Page 07 usages 0x80/0x81 did not control Windows volume.

Consumer extension validation (2026-09-22): 104 host tests in four suites
(19 + 13 + 49 + 23), zero failures. Previous 81 scenarios remain, with requested
mapping/configuration expectations updated. New tests cover SysReq, Consumer
mappings, independent endpoint delays, release ordering, UART-blocked media taps,
reset/reconfigure/suspend/halt, protocol isolation, overflow and final recovery.
Frozen descriptor fixtures prove the interface 0 descriptor bytes are unchanged.
An overflow regression found and fixed stale partial-output progress when the
latest resync snapshot changes while the other endpoint remains blocked.
ARM configure and 47-step clean build passed without warnings/errors:
FLASH 45,656 bytes (8.71%); RAM 6,920 bytes (5.28%, includes stack/heap reservations).
Protected modules, clock, GPIO, RX, .ioc and generated CMake remain unchanged.
At that earlier stage, the new Consumer implementation had not yet been tested on hardware. The later Rev.2 bring-up confirmed user-observed Consumer volume/mute behavior; see docs/POWER_TEST_RECORD.md.

## Pending hardware validation

- Rev.2 initial Rev2Debug bring-up is recorded in docs/POWER_TEST_RECORD.md.
  Complete external-current, suspend, protection and latency tests before enabling RGB power.
- Recheck the earlier USB additions on the new board per docs/USB_HID.md: two interfaces,
  Fn+PrintScreen SysReq, Windows Consumer volume/mute, Fn/base transition ordering,
  UART overlap and USB recovery with no stuck keys.
- Recheck interface 0 Boot/NKRO, idle, LEDs and ghost suppression; record scan and
  runtime maxima under USB/UART load. Previous timings are not new measurements.
- RGB 40-pin FPC remains a later independent stage.

## Important workflow rules for Codex
- Preserve CubeMX-generated USER CODE blocks.
- Prefer custom logic in separate custom source/header files, not inside generated peripheral files.
- Do not edit clock/pin assignments unless explicitly required.
- Do not use PA9/PA10 for anything other than USART1.
- Do not use PA13/PA14 for anything other than SWD.
- Keep the 40-pin RGB interface out of scope until matrix discovery is complete.
- Before modifying CMake, inspect the CubeMX-generated CMakeLists.txt and cmake/stm32cubemx/CMakeLists.txt so custom files are added in the correct target/source list.
- USB is custom-owned in root CMake and separate modules; .ioc and generated CMake
  remain unchanged. Do not enable a duplicate Cube-generated USB stack alongside it.
  After future CubeMX regeneration, check USER CODE hooks and retained custom/vendor
  files and run all host tests plus an ARM clean build; regeneration was not run for USB.

## Board Rev.2 power adaptation (2026-10-22)

- Available schematic: hardware/schematics/SCH_keyboard.json (dated requested filename absent).
- Custom power modules own PC6/PC7 TUSB GPIO inputs, PA3 ADC1_IN3, PC9 EN LOW,
  PB4 active-low FAULT. No pin conflicts; ADC1 previously unused. RGB U4 signals
  PB12..15/PD2/PA8/PC8 reserved but not configured. .ioc and generated GPIO unchanged.
- Main-loop CC filter 20ms, nonblocking ADC ~100Hz, total-current diagnostics,
  fault latch, explicit OFF/reset/bench enable. No new ISR work.
- Existing blocking UART replies are sent in <=64-byte portions (10ms timeout),
  servicing protection between portions. RX/USART/SysTick/HID logic unchanged.
- Default USB policy 450mA is capped to descriptor allocation (currently 100mA);
  stable Type-C 1.5A/3A policy ceiling 700mA. The existing USB descriptor caps
  the effective budget to 100mA in every CC mode. No enable before configuration
  or during suspend. TPS2553 hardware branch limit is separate from board current.
- GS75_BOARD_REV2 defaults OFF (legacy). Rev2Debug selects diagnostics; bench gate
  defaults OFF. Rev2Bench still needs explicit GS75_POWER_HARDWARE_VERIFIED=ON
  after assembled A1/wiring/dummy-load checks. No automatic startup/retry enabling.
- Commands: PWRINFO, CURRENT, PWRTEST (read-only), PWROFF, PWRON, PWRRESET.
  Measurement values are PRELIMINARY (3300mV assumed VREF, A1 gain20).
- Host-tested: 151 cases in 7 suites, previous 104 retained, 40 power,
  5 UART/HID integration and 2 invalid CMake configurations. ARM clean
  configurations LegacyDebug, Rev2Debug, Rev2Bench and Rev2BenchVerified pass
  without warnings/errors. Rev2Debug FLASH 51,236 bytes / RAM 7,080 bytes.
- Hardware-validated (initial user report only): Rev2Debug boot, USB-C Default
  attach in both plug orientations, populated A1, steady PC9 LOW, FAULT# HIGH,
  +5V_RGB near zero and ADC voltage readback. External total current, calibrated
  gain/offset, 1.5A/3A sources, suspend, TPS2553 ON/protection and shutdown
  latency are NOT RUN. See docs/POWER_TEST_RECORD.md for values and provenance.
- Existing generated-source edits are limited to main.c and stm32f4xx_it.c
  USER CODE hooks. matrix_debug.c is custom-owned; .ioc and generated CMake are unchanged.

## Independent Rev.2 audit (2026-10-22)

- See [docs/POWER_AUDIT.md](docs/POWER_AUDIT.md) for severity and exact code evidence.
- Corrected effective budget: all CC modes clamp to active bMaxPower (100mA),
  while 450/700mA remain source-dependent engineering policy ceilings. USB
  descriptors are unchanged.
- Fatal exception and Error_Handler USER CODE hooks attempt direct LOW on PC9.
  This is best effort; there is no independent MCU-hang watchdog on EN.
- Generic Debug/Release presets explicitly clear power gates; invalid verified/
  bench/board combinations are rejected. Attach/detach/fault/sample counters
  saturate instead of wrapping where applicable.
- A stable zero ADC sample is numerically valid and may allow PWRON in an
  acknowledged bench build. No reliable in-firmware proof of sensor continuity
  exists. Require external meter and two known-current points before acknowledgement.
- First board action is diagnostic Rev2Debug only, with RGB controller disconnected
  and PC9 verified LOW. Any power-switch test uses externally limited dummy load.
- Future hardware work: external USB current and suspend measurements,
  two-load INA180 calibration and controlled protection/latency tests. A separate
  USB 2.0 High-Power 500mA revision is only a feasibility study; the current
  descriptor and firmware remain at the audited 100mA budget. Planned RGB
  shortcuts are documented in docs/RGB_BACKLIGHT_PLAN.md, not implemented.

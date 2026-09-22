# GS75 USB HID keyboard

## Status and scope

The preceding Boot/NKRO USB stage was hardware-validated by the user.
Page 07 volume usages 0x80/0x81 did not control Windows volume.
The new Consumer Control interface and Fn+PrintScreen implementation pass host
tests and ARM clean build; their hardware validation is still pending.
The agent did not flash the board. RGB remains absent.

```text
1 kHz scan -> 5 ms debounce -> full matrix ghost detection
           -> 102-key map -> conservative logical KeyboardState
           -> accepted-state queue (64 entries)
           -> confirmed Fn translation -> keyboard state + Consumer bits
           -> Boot/NKRO EP81 + Consumer EP82, main-loop nonblocking transport
```

Only `KeyboardState.pressed` enters the queue. No raw/debounced coordinate,
suppressed bit or phantom is converted directly into a USB key. The original
matrix, debounce, electrical detector and temporal suppression are unchanged.
Fn translation works on a copy and never changes the logical state or KEYS.

## Two interfaces, keyboard with two protocols

USB Full Speed interface 0 remains subclass 1 (Boot), protocol 1 (Keyboard).
Its report descriptor and interface/HID/EP81 descriptor block are byte-identical
to the validated version. Interface 1 is Consumer Control, subclass 0, protocol 0.
Neither interface uses Report IDs.
The interface protocol descriptor value is distinct from the runtime
GET_PROTOCOL/SET_PROTOCOL value.

| Runtime protocol | Input format | Size |
|---|---|---|
| 0, Boot | modifiers, reserved zero, six key usage bytes | 8 bytes |
| 1, Report (reset default) | modifiers, Page 07 bitmap | 22 bytes |

The modifier byte is E0..E7: Left Ctrl/Shift/Alt/GUI, Right Ctrl/Shift/Alt/GUI.
The NKRO bitmap covers usages 0x04..0xA4 inclusive: 161 bits in 21 bytes.
Usage U is bit `(U - 4) % 8` in byte `1 + (U - 4) / 8` of the input report.
The final bitmap byte contains one data bit and seven zero constant bits.
The descriptor uses Variable input fields. Modifiers are outside this bitmap.
Static assertions enforce the 8-byte and 22-byte sizes and endpoint capacity.

Endpoint **0x81**, interrupt IN, **32-byte max packet**, **1 ms interval**.
EP0 is 64 bytes. The configuration descriptor is 59 bytes, two interfaces in one configuration,
bus powered, 100 mA declared for the adapter electronics; RGB is not enabled.
The device is FS-only; no high-speed/other-speed descriptor is advertised.

Boot reports list unique normal usages in ascending usage order. Modifiers
do not consume slots. More than six normal usages produces **0x01
ErrorRollOver in all six slots**, retaining the modifier byte. Going back to
six or fewer restores the full representable state, including an empty release.
There is no arbitrary selection of six keys.

GET/SET_PROTOCOL, GET/SET_IDLE, GET_REPORT (Input and Output), and
SET_REPORT(Output) are handled by the custom HID class on the ST Device core.
Requests validate direction, interface, report ID, lengths and supported values.
Invalid class requests stall EP0. Boot/Report changes cancel a pending old-format
IN transaction, discard obsolete queued history and schedule a complete snapshot
in the new format. The STM32 abort path retains the endpoint DATA toggle, flushes
its FIFO and removes a cancelled completion; it does not close/reopen the endpoint
and reset the DATA toggle. A failed abort rejects SET_PROTOCOL.

Bus reset restores Report protocol, idle zero and LED mask zero. Configuration
schedules an initial full snapshot; changes after that snapshot stay queued.
Offline history is not replayed when the host configures the device again.
Endpoint CLEAR_FEATURE(HALT) similarly schedules a full resynchronization.

Interface 1 uses endpoint **0x82**, interrupt IN, **8-byte max packet**, **1 ms
interval**. Its input report is exactly one byte on Usage Page 0x0C:
bit 0 Mute (0xE2), bit 1 Volume Increment (0xE9), bit 2 Volume Decrement (0xEA),
bits 3..7 constant zero. It has no Output report and no periodic transmissions.
GET_REPORT(Input) returns the current accepted Consumer state. GET_IDLE returns
zero; SET_IDLE zero is accepted, nonzero idle and GET/SET_PROTOCOL stall on this
interface. Keyboard protocol/idle/LED requests remain exclusive to interface 0.
One custom ST class dispatches both interfaces and endpoints explicitly; vendor
core sources are unchanged. EP82 has a separate 16-word TX FIFO; total allocated
FIFO storage is 240 of the F411 FS controller's 320 words.

## Confirmed keymap and Fn layer

All **102 stable KeyIds** are listed with their measured labels, electrical
coordinates, base HID usages and classes in [HID_KEYMAP.csv](HID_KEYMAP.csv).
This is also a host regression fixture checked against the measured JSON.
No internal KeyId was renumbered. The 42 unmapped coordinates produce no KeyId.

Physical mappings explicitly confirmed by the user:

| KeyId | Coordinate | Location / base usage |
|---|---|---|
| KEY_BACKSLASH | FPC18 x FPC02 | Above Enter; Keyboard backslash/bar, 0x31 |
| KEY_PIPE_BACKSLASH | FPC20 x FPC16 | Between Space and Right AltGr; Non-US backslash/bar, 0x64 |
| KEY_WIN | FPC19 x FPC25 | Between Right AltGr and Right Ctrl; Right GUI, 0xE7, modifier bit 7 |
| KEY_PAGE_UP_HOME | FPC19 x FPC16 | Base PageUp, 0x4B |
| KEY_PAGE_DOWN_END | FPC19 x FPC15 | Base PageDown, 0x4E |
| KEY_PAUSE_BREAK | From saved map | Base Pause, 0x48 |

The two backslash keys remain different IDs and different HID usages. Actual
characters depend on the Windows layout and must be checked separately on board.
There are no outstanding mapping ambiguities.

`hid_fn.c` implements only the following confirmed translations:

| Accepted combination | Effective USB state |
|---|---|
| Fn + PageUp | Home, 0x4A |
| Fn + PageDown | End, 0x4D |
| Fn + Pause | Left Ctrl modifier plus Pause, 0x48 |
| Fn + PrintScreen | Keyboard SysReq/Attention, Page 07 usage 0x9A; no synthetic Alt/Ctrl |
| Fn + Left Arrow | Consumer Volume Decrement, Page 0C usage 0xEA |
| Fn + Right Arrow | Consumer Volume Increment, Page 0C usage 0xE9 |
| Fn + Numpad 0 | Consumer Mute, Page 0C usage 0xE2 |

The translated base usage is omitted. Fn itself is always internal. Real
modifiers are preserved; Break adds Left Ctrl by OR, so an independently held
Right Ctrl can coexist with it. Releasing Pause removes only the synthetic
Ctrl contribution. Pressing/releasing Fn while holding a base key produces the
corresponding complete replacement state. A suppressed Fn never enables a layer.
Other Fn combinations retain the base usage; none are invented.

The failed Page 07 volume experiment and GS75_FN_VOLUME option are removed.
Volume and mute do not enter either keyboard report. SysReq 0x9A works in both
Boot and NKRO builders; its OS action remains host-dependent. No additional Fn
mappings or RGB functions are introduced.

## State queue, timing and transmission

`KeyboardRuntime_Tick` calls `UsbKeyboard_Capture` after the unchanged logical
update. Capture compares/copies only four 32-bit accepted-key words. It enqueues
only changes, in scan order, into 64 entries (1024 bytes of entry storage).
It never translates reports, calls a USB API, parses commands or transmits UART.
Short press/release pairs occurring during blocking UART TX therefore survive.

Main calls `UsbKeyboard_Task` before the existing `MatrixDebug_Process`.
Translation and report construction run with interrupts enabled. Brief critical
sections protect snapshots/queue publication and arm the nonblocking endpoint.
A generation counter invalidates a prepared report if USB control or an overflow
changes the queue while main is preparing it. This race is exercised on the host.
Each endpoint owns an independent busy flag and stable transfer buffer. One
accepted snapshot stays at the queue head until both effective outputs have been
staged (or already match). Unchanged outputs need no transfer. Before activating
Consumer functions, any keyboard replacement/release finishes first; before
restoring a base key, the Consumer release finishes first. This also handles Fn
pressed or released while the base key stays down. Endpoint backpressure retains
that shared queue snapshot and preserves later transitions; the main loop never
waits synchronously for USB. An unchanged busy endpoint permits the other output
to advance where no release-before-activation dependency is required.

On queue overflow, the overflow counter increments once for that episode, old
queued intermediate states are discarded, and subsequent captures update a
dedicated latest full snapshot until main can arm both resynchronization outputs.
Every update invalidates partially staged output flags as well as prepared reports.
An already in-flight report may complete first. Afterwards the full current state
is delivered, including releases, and normal queuing resumes. This prevents a
permanent stuck key once the host resumes polling. A short intermediate tap can
be lost during this emergency overflow. Host random/busy/wrap tests check the final
report against the latest accepted state. Counters are monotonic for MCU uptime;
USB reset/configuration does not erase them.

Unchanged effective reports are normally suppressed, including Fn-only changes
and changes between equivalent Boot rollover states. For interface 0, SET_IDLE zero disables
periodic duplicates. Nonzero idle is in 4 ms units (1..255); the main task repeats
the report when the interval since the last IN completion has elapsed. Changed
reports are not delayed by idle. A control interrupt can change idle during report
construction, so the deadline is rechecked before arming the endpoint.
Blocking UART can delay main-loop packet arming and idle servicing beyond the
nominal deadline; 1 ms endpoint polling is not a guarantee of 1 ms end-to-end
latency during UART debug output. Accepted transitions are retained by the queue.

SysTick still runs at 1000 Hz, priority 15. USART1 RX stays at priority 5;
USB OTG FS uses priority 6. Only the USB IRQ runs ST USB control/completion
callbacks. Neither SysTick nor USART IRQ calls the USB stack. GPIO scan timing,
8 us / 1 us settle delays, debounce and UART configuration are unchanged.

`MAX_SCAN_US` retains its original meaning. New `MAX_RUNTIME_US` measures
Matrix_Tick + KeyboardRuntime_Tick + enqueue through DWT without resetting the
scanner's cycle counter. It excludes main-loop report construction, UART TX and
USB bus transfer time, but naturally includes time spent in higher-priority IRQs
that preempt this interval. The old 236-237 us hardware scan maximum predates USB;
it must not be presented as a measurement of this build.

## LED output and UART

The one-byte Output report is accepted over control SET_REPORT, ID zero:
bits 0..4 are Num Lock, Caps Lock, Scroll Lock, Compose and Kana; upper bits are
ignored. Storage updates only when the EP0 data stage completes. No physical
LED or RGB output is driven, and no interrupt OUT endpoint is needed.

PING/INFO/DUMP/RAW/GHOST/KEYS remain available with their existing response fields.
`USBINFO` adds these diagnostic lines (no automatic periodic USB print):

```text
USB_STATE=CONFIGURED
USB_CONFIGURED=1
USB_PROTOCOL=REPORT
USB_IDLE=0
USB_LED_MASK=0x00
USB_TX_COUNT=1
USB_QUEUE_DEPTH=0
USB_QUEUE_MAX=0
USB_QUEUE_OVERFLOW=0
USB_RESYNC_COUNT=1
USB_LAST_REPORT_SIZE=22
MAX_RUNTIME_US=...
USB_CONSUMER_BITS=0x00
USB_CONSUMER_TX_COUNT=1
USB_CONSUMER_LAST_REPORT=0x00
USB_CONSUMER_BUSY=0
USB_CONSUMER_RESYNC_COUNT=1
```

STATE is OFF/DEFAULT/ADDRESSED/CONFIGURED/SUSPENDED. CONFIGURED tracks class
configuration and remains one during suspend; endpoint arming additionally checks
the actual USB state. TX_COUNT counts completed IN reports. QUEUE_DEPTH excludes the dedicated resync slot, but can include the head
snapshot while one of its outputs is already in flight; QUEUE_MAX is the queue high-water
mark, not its fixed capacity. RESYNC_COUNT counts successfully armed full resync
reports, including configuration/protocol/overflow recovery. LAST_REPORT_SIZE is
8 or 22 after arming a report, and zero before the first report or immediately
after reset/reconfiguration/protocol change. MAX_RUNTIME_US is an uptime maximum.

Consumer BITS is translated from the latest accepted state. LAST_REPORT is the
last armed one-byte report, BUSY indicates an outstanding transfer, TX_COUNT counts
completions, and RESYNC_COUNT counts forced Consumer reports successfully armed.
Both interfaces are synchronized on configuration and queue recovery. Suspend
retains outstanding buffers; resume completes them and drains retained transitions.
Reset/deconfiguration cancel endpoint transfers and discard offline history.

## STM32 integration and CubeMX ownership

- HSE 8 MHz, PLLM=8, PLLN=192, PLLP=2, PLLQ=4 already give SYSCLK 96 MHz and
  USB clock 48 MHz. PLL and clock tree are unchanged.
- `usb_device.c` owns PA11 DM and PA12 DP (AF10), USB clock and OTG FS IRQ.
  VBUS sensing is disabled because PA9 is USART1 TX. PA10 remains USART1 RX;
  USB ID mode is unused. SWD PA13/PA14 and all matrix pins remain unchanged.
- Added unmodified ST USB Device core and PCD/PCDEx/LL_USB from CubeF4 V1.28.3,
  with licenses. The stock mouse HID class is not compiled. No heap allocation
  is used by the custom class or scheduler.
- All sources/includes and HAL_PCD_MODULE_ENABLED are added in the user-owned
  root CMakeLists.txt. The CubeMX-generated CMake file and HAL config are untouched.
- **No `.ioc` parameters were changed and CubeMX regeneration was not run.**
  USB is deliberately custom-owned, while `.ioc` continues to own the previously
  validated peripherals. Do not enable a second generated USB stack alongside it.
  Main/SysTick/IRQ integration is inside preserved USER CODE blocks. After any
  future regeneration, inspect those hooks and retained custom/vendored files,
  then run all host tests and the ARM clean build again.

Manufacturer: `GS75 Adapter`; product: `GS75 Keyboard Adapter`; serial: the
STM32's complete 96-bit UID encoded as 24 uppercase hex digits.

Default VID/PID **0483:5710** is copied from the ST CubeF4 HID standalone
example, **for this local development prototype only**. It is not a GS75-owned
identity or permission to distribute products under ST's VID. A distributed
product needs an authorized VID/PID. CMake cache settings `GS75_USB_VID` and
`GS75_USB_PID` make the identity explicit and replaceable without editing the
descriptor source. The serial and neutral product strings do not establish VID
ownership.

## Validation and build

```powershell
cmake -S tests -B build/host-tests -G "Visual Studio 17 2022" -A x64
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
cmake --preset Debug
cmake --build --preset Debug --clean-first
```

Result on 2026-09-22: **104 tests passed in four CTest suites**:
19 matrix/UART + 13 logical keyboard + 49 HID/USB + 23 Consumer tests.
All 81 previous scenarios remain; the old Page 07/configuration expectations now
reflect the requested Consumer design, including the obsolete compile-flag check.
Host C compiles with /W4 /WX. Firmware configure and all 47 clean-build steps
completed without warnings/errors. Debug memory usage: **FLASH 45,656 bytes
(8.71% of 512 KiB), RAM 6,920 bytes (5.28% of 128 KiB)**. RAM includes linker
stack/heap reservations; this is not a measured worst-case stack watermark.

HID tests execute the real C mapper, Fn translator, builders, scheduler, HID
class and ST Device core. Only peripheral access is modeled. They cover all
102 base mappings, special keys, Consumer mappings and removal of Page 07 volume, Boot rollover,
protocol switches with held/in-flight keys, idle/tick wrap, LED control requests,
invalid requests, descriptor parsing, UID serial, all 20 saved ghost fixtures,
short taps during the real blocking UART TX harness, busy buffer ownership,
64-entry queue wrap/overflow/random final-state recovery, reset/reconfiguration,
suspend/resume and endpoint halt recovery. Injected control/overflow interrupts
also verify invalidation of reports prepared in main.

The independent descriptor parser checks input/output bit counts, usage ranges,
padding, absence of Report ID, both interfaces and endpoint capacity; frozen byte fixtures additionally
prove the original keyboard descriptors are unchanged. Host tests
do not validate USB electrical signaling, hardware FIFO aborts, Windows key
semantics or scanner timing. Those are board acceptance checks.

## Board acceptance (manual flashing only)

1. After the user flashes the new build, reconnect data USB. Inspect descriptors:
   configuration length 59, interfaces 0 and 1, EP81 32 bytes/1 ms and EP82
   8 bytes/1 ms, no Report IDs. Identity/UID must stay unchanged. USBINFO must
   show CONFIGURED, REPORT, Consumer bits/last report 0, busy 0 once polled.
2. Verify ordinary arrows, Numpad 0 (with Num Lock enabled), PrintScreen and
   existing Fn+PageUp/Home, Fn+PageDown/End, Fn+Pause/Ctrl+Pause. Fn alone emits
   no key. Check both physical backslash keys with the actual Windows layout.
3. Capture USB input: Fn+PrintScreen must send only keyboard usage 0x9A, no
   0x46 and no synthesized Alt/Ctrl. Release Fn while holding PrintScreen:
   0x9A clears and 0x46 returns. Repeat in Boot and Report protocol; SysReq's
   visible Windows behavior alone is not a reliable report-content check.
4. In Windows, Fn+Left decreases volume (EP82 04), Fn+Right increases it (02),
   Fn+Numpad0 toggles mute (01). Each release sends 00. No base arrow/Numpad0
   enters EP81 while translated. Check Fn-first and base-first pressing; also
   release Fn first while holding the base key. Capture traces to verify the
   old function releases before the replacement starts and no key stays held.
5. Request DUMP/RAW over UART and press/release a media combination during TX;
   each electrical phase must exceed the 5 ms debounce. Afterwards both Consumer
   press and zero release must arrive. Repeat with PING overlapping TX, ordinary
   keys and X+Left Ctrl -> add U; ghost suppression and UART responses must remain
   correct. USBINFO Consumer bits/last report should settle at 0 and busy at 0.
6. Send SET_PROTOCOL 0/1 to interface 0 with held keys/media: keyboard remains
   8/22 bytes, Consumer remains one byte. Check Boot rollover/recovery, LED
   SET_REPORT and keyboard idle; Consumer must not repeat at keyboard idle rate.
   Interface 1 rejects protocol and Output requests.
7. Exercise reset, reconfiguration, unplug/replug and suspend/resume with both
   held and released media keys. Resume polling and check the final state and
   releases. Under deliberate queue overload, USB_QUEUE_OVERFLOW increases and
   both interfaces recover to the current accepted state; intermediate taps may
   be lost only on overflow. Verify no persistent volume/mute/key activation.
8. Recheck zero RAW/DUMP baseline and GHOST CLEAR. Measure delta SCAN_COUNT /
   delta SCAN_TIME_MS = 1 scan/ms during USB plus UART load. Record MAX_SCAN_US
   and MAX_RUNTIME_US for this build; host tests cannot supply hardware timings.

## Files introduced or extended in this USB stage

This Consumer extension changes `hid_fn`, `usb_keyboard`, `usb_descriptors`,
`usb_device`, `usbd_keyboard`, `usbd_conf`, `matrix_debug` and root CMake. It adds
`usb_consumer.h/.c`, `test_consumer.py` and frozen keyboard descriptor fixtures;
updates the host harness, tests/CMake and test_hid expectations.

Earlier USB-stage files (retained):

New custom headers/sources: `hid_keymap.h/.c`, `hid_fn.h/.c`, `hid_report.h/.c`,
`usb_keyboard.h/.c`, `usb_descriptors.h/.c`, `usb_device.h/.c`,
`runtime_timing.h/.c`, `usbd_conf.h`, `usbd_keyboard.c` under Core/Inc and Core/Src.

Extended: root `CMakeLists.txt`, `Core/Src/main.c`, `Core/Src/stm32f4xx_it.c`,
`Core/Inc/stm32f4xx_it.h`, `Core/Src/keyboard_runtime.c`, `Core/Src/matrix_debug.c`;
`tests/CMakeLists.txt`, `tests/debug_harness.c`, `tests/stubs/main.h`.
New tests: `tests/usb_harness.c`, `tests/test_hid.py`.

New vendor files: HAL `stm32f4xx_hal_pcd.h/.c`, `stm32f4xx_hal_pcd_ex.h/.c`,
`stm32f4xx_ll_usb.h/.c`; USB core `usbd_core.h/.c`, `usbd_ctlreq.h/.c`,
`usbd_ioreq.h/.c`, `usbd_def.h`, LICENSE.txt and README.GS75.md.

Documentation: updated CODEX_CONTEXT.md, MATRIX_RUNTIME.md, KEYBOARD_STATE.md;
added this document and HID_KEYMAP.csv. Scanner, debounce, detector, suppression,
base keymap, RX, GPIO/USART, clock and .ioc were not modified in this stage.

## Primary references

- [USB-IF HID 1.11](https://www.usb.org/sites/default/files/hid1_11.pdf),
  class requests, Report default and Boot keyboard rollover requirements.
- [USB-IF HID Usage Tables](https://www.usb.org/sites/default/files/hut1_3_0.pdf),
  Keyboard/Keypad SysReq/Attention and Consumer Mute/Volume usages.
- Local STM32Cube FW_F4 V1.28.3 core, HAL sources and
  `Projects/STM324x9I_EVAL/Applications/USB_Device/HID_Standalone/Src/usbd_desc.c`
  for the explicitly documented prototype VID/PID.

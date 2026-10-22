# GS75 matrix runtime (8 x 18)

## Coordinate layout and evidence

Rows 0..7 drive FPC17..FPC24. Columns 0..15 sense FPC01..FPC16;
columns 16 and 17 sense FPC25 and FPC26. Each row is an 18-bit mask.

The full map `gs75_matrix_20260921_172228.json` contains 102 unique keys,
with bidirectional electrical connectivity on every single-key capture.
Both recorded baselines are empty. All 20 cases in
`gs75_ghosting_20260921_174434.json` show a fourth electrically possible
key when three rectangle corners are pressed. These measurements support
the fixed drive/sense assignment and require ambiguity detection.

All 144 coordinates remain observable, including unmapped positions.
A reported coordinate means an electrical LOW, not proof of a physical key press.
A separate [logical keyboard layer](KEYBOARD_STATE.md) maps the 102 measured
positions to KeyId and applies conservative temporal suppression. The other
42 coordinates never create a KeyId, but still participate in detection.
RAW/DUMP and GHOST retain the full electrical matrix. The downstream
[USB HID layer](USB_HID.md) uses only accepted logical keys and does not change
scanner, debounce, electrical detection or conservative suppression.

## Scheduling and debounce

The existing 1 ms HAL SysTick calls `Matrix_Tick()` after incrementing the HAL
millisecond counter. The scanner stays inactive before `Matrix_Init()`.
Do not call it from the main loop as well. The existing scan is followed by
`KeyboardRuntime_Tick()`, which consumes the completed debounced snapshot.
It keeps logical history current even during blocking TX. The scanner,
debounce code, 1 ms period and settling delays are unchanged.
The post-scan logical hook also enqueues changed accepted bits for USB; actual
Fn translation, report construction and packet submission run in main.

Each tick scans all eight drive rows. The active row is open-drain LOW;
the other rows are inputs with pull-ups, and all sense lines remain inputs.
The LOW latch and open-drain selection precede enabling the output.
Settling delays remain 8 us after drive and 1 us after release.

Each coordinate has its own candidate state and change timestamp.
A sampled candidate must remain unchanged for at least 5 ms before it replaces
the debounced state, on both press and release. Another key's bounce does not
restart its timer. Unsigned subtraction handles HAL tick wrap.

The SysTick ISR also runs anti-ghosting on the debounced matrix. Command
parsing, formatting and UART transmission never run in interrupt context.
`Matrix_GetSnapshot()` briefly masks interrupts to copy the full
state, preserving the previous interrupt mask. Formatting and blocking UART
transmission happen after interrupts are restored, so scanning continues
during long dumps. This is a scan assembled row by row, not a simultaneous
electrical measurement of all 144 positions.

## Anti-ghosting semantics

For every pair of rows, find their common active columns. Two or more common
columns form one or more complete rectangles:

- `row_pair_count`: number of row pairs with at least two shared columns.
- `rectangle_count`: sum of C(shared columns, 2) across those row pairs.
- `ambiguous[row]`: union of active corners participating in those rectangles.

This flags potential ambiguity. Four genuinely pressed corners and a three-key
combination with a phantom corner cannot be distinguished. The module neither
labels a particular key as fake nor changes raw/debounced data. It conservatively
includes unmapped coordinates. An incomplete L in a transient sampled matrix
does not trigger until a complete rectangle is observed.

This electrical detector remains diagnostic-only. Conservative suppression
operates in the separate logical layer using its full ambiguity masks,
without changing any scanner data. See [policy and KEYS protocol](KEYBOARD_STATE.md).

## UART debug

Commands: `PING`, `INFO`, `DUMP`, `RAW`, `GHOST`, `KEYS`, `USBINFO`, followed by CR or LF.
Transport remains USART1 115200 8N1. TX remains blocking, but RX now uses
the USART1 interrupt and a 256-byte ring (255 usable queued bytes).
A command typed during a DUMP or an automatic GHOST_EVENT is queued and
parsed in the main loop after the current transmission finishes.
The five existing commands retain their reply formats. INFO adds
`LOGICAL_GHOST=CONSERVATIVE` and `KEYMAP_KEYS=102`; its existing
`ANTI_GHOST=DETECT_ONLY` refers to the electrical detector.
KEYS reports logical, suppressed and ambiguous KeyIds separately.

The RX module owns USART1 RXNE/error interrupts directly; do not mix it with
HAL polling/IT/DMA receive functions. The ISR only reads status/data, enqueues
bytes, or latches a stream-loss condition. It never parses commands or sends
a reply. Initialization and the IRQ wrapper are retained in custom code and
CubeMX USER CODE blocks; GPIO, USART baud/format and .ioc are unchanged.

USART1 has interrupt priority 5, above the existing SysTick priority 15.
At 115200 8N1 a byte arrives every ~87 us, so RX must preempt the measured
233 us scan handler. The scanner code, 1 ms schedule and settling delays
are unchanged; a short RX ISR can add some preemption time to MAX_SCAN_US.

Replies and automatic diagnostic bursts remain serialized. Send commands at
a sustainable rate: this bounded queue does not provide hardware flow control
or unlimited command pipelining. To avoid filling it with queued replies,
automated clients should normally wait for each reply; they no longer need
to avoid overlapping an unsolicited ghost diagnostic.

RX recovery rules:

- A command longer than 63 characters reports the existing
  `ERR: BUFFER OVERFLOW` once and discards through CR/LF. The next command
  can already be queued and is parsed normally.
- A full RX ring reports `ERR: RX OVERFLOW` in the main loop.
- A UART overrun/framing/noise/parity error reports `ERR: UART RX ERROR`.
- On either stream-loss condition, the ISR drops subsequent bytes until
  main acknowledges the loss. Main flushes the queued bytes, resets the
  partial command and discards input through a subsequently received CR/LF.
  Complete commands still waiting in that damaged queue may be lost too.
  No truncated suffix is executed. A delimiter received while the loss was
  latched may itself have been dropped: after an RX error, send a fresh
  CR/LF, then retry the command. Receive interrupts remain enabled.

`DUMP` returns a debounced snapshot; `RAW` returns raw data. Example:

```text
MATRIX_BEGIN
MATRIX_STATE,DEBOUNCED,8,18
SCAN,1234,TIME_MS,1250
EDGE,FPC20,FPC13
EDGE_COUNT,1
MATRIX_END
```

Each EDGE is drive -> sense only. There are no synthetic reverse edges or
sense-to-sense/drive-to-drive discovery edges. This is a semantic change from
the old 26x26 protocol: existing discovery capture scripts must be adapted
before reuse. The stored JSON files are unchanged.

`INFO` includes `SCAN_HZ=1000`, `DEBOUNCE_MS=5`, `SCAN_COUNT`,
`SCAN_TIME_MS`, and `MAX_SCAN_US`. The rate is the configured target;
the counter/timestamp and maximum handler duration help verify it on hardware.
The maximum includes scan, debounce and electrical detection, but excludes
the new post-scan logical hook and UART formatting. It is not the total
SysTick duration; total CPU/IRQ budget must also allow for the logical hook.
`USBINFO` now reports MAX_RUNTIME_US for Matrix_Tick + KeyboardRuntime_Tick +
minimal USB queue capture. It excludes main-loop UART TX/report building and
USB bus transfer time, while including any IRQ preemption during the measured
interval. Existing MAX_SCAN_US/INFO fields are unchanged.

`GHOST` prints an atomic snapshot with `GHOST_BEGIN/GHOST_END`, scan metadata,
and diagnostics for both RAW and DEBOUNCED states. Example diagnostic lines:

```text
GHOST,DEBOUNCED,ACTIVE,ROW_PAIRS,1,RECTANGLES,1
AMBIGUOUS,DEBOUNCED,FPC17,0x00003
AMBIGUOUS,DEBOUNCED,FPC18,0x00003
```

An AMBIGUOUS mask uses the column order above, not FPC pin numbers as bit indices.
On release the status becomes `GHOST,DEBOUNCED,CLEAR,ROW_PAIRS,0,RECTANGLES,0`.

Automatic reports begin with `GHOST_EVENT,<change counter>,TIME_MS,<snapshot time>`
and the current debounced ghost diagnostics. They run at most every 100 ms,
only when the ambiguity state changes. Multiple transitions can be coalesced;
a jump in the change counter reveals this. The timestamp describes the latest
snapshot, not the exact transition time. DUMP/GHOST replies are never interleaved
with automatic reports. This is not a lossless DOWN/UP event log.

## Host regression tests

The test DLL/shared library compiles the production debounce, anti-ghosting
and UART modules, including the production RX ISR and ring buffer. Only
registers, interrupt primitives, blocking TX and snapshot access are stubbed;
Python uses ctypes to exercise the actual C code with recorded fixtures.
The TX stub injects byte arrivals into the RX ISR during transmission,
without running the command parser until TX returns.

Windows with Visual Studio C tools and Python:

```powershell
cmake -S tests -B build/host-tests -G "Visual Studio 17 2022" -A x64
cmake --build build/host-tests --config Debug
ctest --test-dir build/host-tests -C Debug --output-on-failure
```

The separate host CMake project also supports a native GCC/Clang generator.
It is not part of the ARM firmware target. It checks all 102 single keys,
20 captured ghosting cases, all 4284 possible rectangle positions, empty/full
matrices, independent debounce, release bounce, tick wrap, command overflow,
diagnostic rate limiting and consistent snapshots during blocking output.
RX regression tests cover commands arriving during a 144-coordinate DUMP and
an automatic GHOST_EVENT, a command split across TX, ISR-only enqueue,
ring capacity/wrap/overflow, long commands during TX, UART error recovery,
interrupt priority and preservation of the caller's interrupt mask.
The new keyboard regression suite checks all 102 IDs and labels against JSON,
all 11 recorded ordinary combinations, all 20 ghosting fixtures with temporal
sequences, all rectangle locations, unmapped corners, releases, recovery,
and logical history/snapshot consistency during TX. There are 32 tests in
two CTest suites (19 matrix/RX tests and 13 logical keyboard tests).
The USB stage retains those 32 tests and adds 49 HID/USB tests, plus 23 Consumer tests, for **104 tests
in four suites**. See [USB validation](USB_HID.md#validation-and-build).

Firmware:

```powershell
cmake --preset Debug
cmake --build --preset Debug --clean-first
```

## Hardware validation reported by the user

The 8x18 runtime was successfully tested on the board before this RX change:

- SCAN_COUNT delta = 3001 for SCAN_TIME_MS delta = 3001 ms: 1000 Hz.
- MAX_SCAN_US = 233 us.
- Without keyboard: RAW/DUMP consistently report EDGE_COUNT=0; raw and
  debounced GHOST states are CLEAR.
- With keyboard: released baseline is zero; A, W, Space, Enter and Fn match
  the recorded map.
- Scanning continues during blocking DUMP output.
- X + Left Ctrl + U gives the four expected coordinates (FPC20/FPC24 by
  FPC10/FPC16), GHOST ACTIVE, ROW_PAIRS=1, RECTANGLES=1.
- Releasing the keys produces GHOST CLEAR.

The user subsequently validated interrupt RX on hardware on 2026-09-22:

- RX works during blocking TX/GHOST_EVENT, with no overlap-related
  ERR: UNKNOWN COMMAND.
- SCAN_COUNT/SCAN_TIME_MS remains exactly 1000 Hz.
- MAX_SCAN_US with RX IRQ is 236-237 us, versus the earlier 233 us.
- RAW/DUMP baseline remains zero and X + Left Ctrl + U detection works.
- A long command produces ERR: BUFFER OVERFLOW; the next PING returns PONG.

These measurements precede the logical keyboard layer. At that stage,
keymap/suppression had passed host tests and an ARM clean build but had not
been checked on the board. Later Rev.2 observations are recorded below.

## RX checks to retain during logical-layer validation

1. While generating GHOST_EVENT transitions with the known rectangle, type
   PING, INFO, DUMP, RAW and GHOST. Each complete command should receive its
   normal reply without ERR: UNKNOWN COMMAND caused by overlap.
2. Send PING while a DUMP reply is being transmitted. PONG should follow the
   complete MATRIX_END frame, with no response interleaving.
3. Send more than 63 command characters, CR/LF, then PING. Expect one
   ERR: BUFFER OVERFLOW followed by PONG.
4. If deliberately overrunning the 255-byte RX queue, expect ERR: RX OVERFLOW.
   Once it is reported, send a fresh CR/LF then PING to confirm recovery.
5. Recheck scan-count progression and MAX_SCAN_US under simultaneous RX/TX.
   The scan schedule remains 1 kHz; the validated RX-IRQ baseline was
   236-237 us before the logical layer was added. MAX_SCAN_US excludes
   the logical hook; check scan progression under load as well.

Logical-layer checks are described in [KEYBOARD_STATE.md](KEYBOARD_STATE.md).
USB HID with the confirmed Fn translations is now implemented downstream.
The preceding USB keyboard stage was hardware-validated. On Rev.2 the user later confirmed Consumer volume/mute behavior; SysReq
report content still awaits dedicated board validation. RGB remains out of scope.
The USB IRQ is priority 6, below UART RX priority 5 and above SysTick priority 15.
Neither UART RX nor the scanner configuration was changed. Recheck 1000 Hz and
record both maximum durations under USB/UART load; prior hardware timings are
not measurements of the new USB build.

## Subsequent Board Rev.2 observation

The user reports a running 8 x 18 matrix with all 102 keys working through
key-test.ru, RAW/DUMP/KEY_COUNT zero at rest, 1000 Hz scanning, 5 ms debounce,
and the expected Left Ctrl+X+U ghost event followed by CLEAR on release. UART
reported MAX_SCAN_US=501 and MAX_RUNTIME_US=721/717 in separate checks. These
are observed maxima, not a worst-case timing proof. The detailed user-run
record is [POWER_TEST_RECORD.md](POWER_TEST_RECORD.md).

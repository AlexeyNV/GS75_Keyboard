# Logical keyboard state and conservative ghost suppression

## Keymap

`Core/Src/keymap.c` statically maps all 102 measured coordinates from
`tools/matrix_mapper/gs75_matrix_20260921_172228.json`.
Rows 0..7 are FPC17..24; columns 0..15 are FPC01..16, then FPC25/FPC26.
The remaining 42 coordinates return KEY_NONE and never create logical keys.

`KeyId` values 1..102 are explicitly enumerated in `keymap.h` in the JSON
singles order. They are not USB HID usages. Keep existing IDs stable.
`Keymap_At` maps coordinates to IDs; `Keymap_Name` returns symbolic names
such as KEY_A/KEY_LEFT_CTRL/KEY_FN; `Keymap_Label` preserves measured JSON
labels. The two separately measured backslash positions are KEY_BACKSLASH
and KEY_PIPE_BACKSLASH. USB mapping and the confirmed Fn translations now live
in separate downstream modules; neither changes this map or logical state.
See [USB_HID.md](USB_HID.md) and the [102-entry HID fixture](HID_KEYMAP.csv).

## Processing and temporal policy

The pipeline is:

```text
unchanged 1 kHz scan -> unchanged per-coordinate debounce
                   -> full 8x18 electrical ambiguity detection
                   -> keymap + conservative temporal logical state
```

`KeyboardRuntime_Tick()` runs after `Matrix_Tick()` in the SysTick USER CODE
block. It consumes every completed sample, including during blocking UART TX.
Computing logical state only when KEYS is requested, or only in main, would
miss releases/confirmation transitions during long replies and is not valid.
Initialization and the post-scan call are inside CubeMX USER CODE blocks.
After each logical update, UsbKeyboard_Capture queues changed accepted bits.
This additional hook has no USB API or Fn translation; it preserves short
press/release transitions while main is blocked in UART TX. Main consumes the
queue through Fn translation and the Boot/NKRO builders. Suppression is unchanged.

The electrical detector includes ALL coordinates. Filtering unmapped positions
before detecting rectangles would hide real electrical ambiguity.
Its existing raw/debounced matrices and GHOST diagnostics are unchanged.

At each sample, let E be the mapped active debounced keys, A the active keys
whose coordinates belong to full-matrix ambiguous rectangles, and Lprev the
previous accepted logical keys:

```text
L          = (Lprev intersect E) union (E minus A)
suppressed = E minus L
ambiguous  = E intersect A
```

Consequences:

- Without ambiguity, logical state is exactly the mapped debounced state.
- Previously confirmed keys remain accepted while their electrical bits stay
  active, even if they become part of a rectangle.
- New ambiguous keys are all suppressed, including both real new keys and
  possible phantom keys. No corner is arbitrarily selected as the phantom.
- Unambiguous new keys outside the affected rectangle can still be accepted.
- A key whose debounced bit becomes inactive is released even if ambiguity
  remains elsewhere. Its previous confirmation is forgotten.
- Suppressed keys do not become accepted merely by waiting longer.
- When ambiguity clears, every currently active mapped key is accepted and
  absent keys are released.
- An unmapped fourth corner still makes a rectangle ambiguous. It never
  emits a KeyId; the mapped new corners remain conservatively suppressed.

This policy cannot determine a physically hidden release. If a previously
confirmed key is released but remains electrically LOW through other held
keys, it stays logical DOWN until that ambiguity resolves or its bit clears.
This is deliberate electrical compatibility, not proof that it is still held.

The policy trusts the debounced snapshots and the existing rectangle detector.
It does not solve arbitrary noisy/transient electrical states or infer the
real subset of simultaneous presses from an ambiguous snapshot.

`KeyboardState` contains pressed, suppressed and ambiguous bitsets indexed
by (KeyId - 1). `KeyboardRuntime_GetSnapshot()` atomically copies this state
and the source scan count/timestamp. No UART runs in the logical ISR hook.
RAW, DUMP and GHOST remain unsuppressed electrical diagnostics.

The scan period and its 8 us / 1 us GPIO settling delays are unchanged.
The logical hook adds CPU work after the scan. The existing MAX_SCAN_US covers
Matrix_Tick only, so it excludes the logical hook; it is not a total SysTick
budget measurement. Board validation must confirm the 1 kHz cadence under load.
USBINFO now exposes a separate MAX_RUNTIME_US covering scan, logical update
and enqueue; MAX_SCAN_US retains its original meaning.

## KEYS protocol

Send `KEYS\r\n`. A reply is one coherent logical snapshot:

```text
KEYS_BEGIN
SCAN,1234,TIME_MS,1234
POLICY,CONSERVATIVE
KEY,KEY_X
KEY,KEY_LEFT_CTRL
KEY_COUNT,2
SUPPRESSED,KEY_U
SUPPRESSED,KEY_PIPE_BACKSLASH
SUPPRESSED_COUNT,2
AMBIGUOUS,KEY_U
AMBIGUOUS,KEY_X
AMBIGUOUS,KEY_LEFT_CTRL
AMBIGUOUS,KEY_PIPE_BACKSLASH
AMBIGUOUS_COUNT,4
KEYS_END
```

This example follows confirming X + Left Ctrl, then adding U. Both the real
new U and the phantom KEY_PIPE_BACKSLASH are suppressed. Accepted keys can
also appear in AMBIGUOUS; SUPPRESSED is disjoint from KEY. Lists follow KeyId
order and zero counts are printed for empty lists.

The existing PING/INFO/DUMP/RAW/GHOST reply formats remain available.
INFO adds LOGICAL_GHOST=CONSERVATIVE and KEYMAP_KEYS=102. Its historical
ANTI_GHOST=DETECT_ONLY field refers to the separate electrical detector,
which still only detects. The startup command list now includes KEYS.

TX remains blocking and RX keeps using the validated interrupt ring buffer.
Commands received during KEYS are queued normally. The captured reply does
not change halfway through if the logical state changes during its TX.

## Host validation

Run the same [host build and CTest commands](MATRIX_RUNTIME.md#host-regression-tests).
Both suites execute actual C firmware logic. The original 19 tests remain,
and 13 logical tests cover:

- Exact map/IDs/labels, all 102 singles and all 11 recorded ordinary combinations.
- All 20 ghosting fixtures, from zero and from every pair of the three real
  keys already confirmed; the omitted phantom never becomes a new logical key.
- All 4284 rectangle positions, including unmapped corners.
- Simultaneous ambiguous presses, visible/hidden releases, restoration after
  ambiguity, and independent new keys outside the rectangle.
- Release history during blocking DUMP, RX of KEYS during TX, and snapshot
  consistency if logical state changes while a KEYS reply is being sent.

No hardware timing is inferred from host execution.

## Board checks for this stage

The preceding runtime/RX version passed hardware tests: 1000 Hz, scan maximum
236-237 us with RX IRQ, empty baselines, correct recorded single keys/rectangle,
RX during TX/GHOST_EVENT and long-command recovery. The new logical layer
has not yet been flashed or tested by the agent.

1. Released keyboard: RAW/DUMP baseline zero; KEYS counts all zero.
2. Test singles (including Fn and both backslash positions) and W+A+Left Shift,
   Left Ctrl+C. KEYS should contain exactly their mapped IDs.
3. Confirm X + Left Ctrl with KEYS first. Add U: DUMP retains all four electrical
   coordinates; KEYS retains X/Left Ctrl, suppresses U/KEY_PIPE_BACKSLASH,
   and lists all four as ambiguous.
4. Release X while Left Ctrl+U remain: ambiguity disappears and KEYS contains
   Left Ctrl+U. Release all: every KEYS count returns to zero.
5. For a simultaneous ambiguous onset, no corner may be guessed. If hardware
   debounce resolves one key earlier, it may be retained as prior confirmation;
   the strictly simultaneous case is reproduced deterministically in host tests.
6. Repeat transitions while DUMP/GHOST/KEYS replies are being sent and verify
   scan-count progression remains 1000 Hz under load.

USB HID now consumes this accepted logical state; RGB is not implemented.
The original 13 logical regression tests remain passing. There are 104 total
host tests across four suites, including 49 HID/USB and 23 Consumer tests.
Only accepted pressed bits feed the shared keyboard/Consumer queue; suppression
and the source logical state remain unchanged by either output.
The USB suite rechecks all 20 ghost fixtures and the X+Left Ctrl -> add U ->
release X -> release all sequence through actual reports. Fn itself stays in
KEYS, while the downstream effective HID state contains only translated usages.
See [USB board acceptance](USB_HID.md#board-acceptance-manual-flashing-only).

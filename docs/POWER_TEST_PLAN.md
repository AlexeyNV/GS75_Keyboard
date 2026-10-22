# Rev.2 power bench acceptance

**Not yet tested on hardware.** This is a procedure, not a record of results.
Firmware and host tests are implemented; no automatic flashing or physical test
was performed. Keep the keyboard RGB controller disconnected throughout.

## 1. Unpowered inspection and BOM

1. Match the assembled PCB revision to HARDWARE_REV2.md and the actual schematic.
   Resolve U13 Manufacturer Part A3 versus A1; verify the physical part is A1,
   R30 is 0.05 ohm, R29 1k, C6 100n, and sense polarity follows the input current.
2. Continuity-check PC6/PC7 to U12 OUT1/OUT2 and both 10k pull-ups. Check PORT and
   EN_N grounded, ADDR NC, CC1/CC2 routing. Verify R31+R35 are the intended series
   900k VBUS_DET path, not a ground divider or a short to MCU pins.
3. Check U15 identity TPS2553 (not -1), R33 30k, EN pulldown 100k, FAULT pull-up
   10k, PC9/PB4 mapping. Inspect input protection and check power rails for shorts.
4. Confirm U4 pinout and that 3.3V remains present at pin5 independently of EN.
   Do not connect unverified RGB electronics. Prepare an electronic dummy load,
   current-limited supply/Type-C source fixture, DMM, USB current measurement,
   oscilloscope or logic analyzer, and a data connection with known CC behavior.
   Avoid backfeeding a PC through a second supply; use a suitable powered fixture.

## 2. Diagnostic-only firmware

1. Configure/build Rev2Debug as documented; user flashes manually if desired.
   Verify PC9 LOW and +5V_RGB disabled through reset, startup and USB enumeration.
   Observe EN with a scope; external pull-down must hold OFF before firmware.
2. Send PING, INFO, RAW, DUMP, GHOST, KEYS, USBINFO. Recheck keyboard baseline,
   Fn, Boot/NKRO and Consumer behavior; no new RGB traffic should appear on U4.
3. Send PWRINFO, CURRENT and PWRTEST. Check framing, BOARD_REV2=1, BENCH_TEST=0,
   PRELIMINARY marking, PORT_CONFIG_OK=1 and fresh valid ADC samples.
4. Send PWRON repeatedly: BENCH_DISABLED, EN remains LOW. PWROFF/PWRRESET must
   never raise EN. Reboot and repeat, including USB suspend and reconnect.

## 3. CC and current calibration, still OFF

1. With a suitable source fixture test unattached, Default, 1.5A and 3A in both
   cable orientations. Compare physical OUT1/OUT2 with codes 3/2/1/0 and the names.
   Verify 20ms stability qualification, attach/detach counts, last-change times.
   Avoid driving against active outputs; emulate only with appropriate open-drain
   fixture isolation if replacing the TUSB outputs for logic tests.
2. Check effective configured budget is **100mA in all three CC modes** with
   the current descriptor; POLICY_LIMIT_MA remains 450/700 for information only.
   Unconfigured/suspended budget is zero. A USB-A-to-C cable must not produce
   an effective budget above 100mA.
3. Measure VDDA and PA3 voltage with a DMM. Compare raw ADC to raw*VDDA/4095.
   Record RGB-OFF total-board baseline. Apply known controlled total currents
   using safe fixture loads and compare INA output/current to an external meter.
   No-load sensor offset must be measured separately; do not mistake the MCU's
   real baseline consumption for a zero-offset error.
4. Record offset and slope; measure at two known load points and deliberately
   verify that shorting PA3 LOW/stuck-zero is detectable by the external meter,
   even though firmware VALID remains 1. Adjust explicit calibration defaults
   or use the calibration API in a controlled build, then rerun tests. Current reports remain
   preliminary until a documented accuracy/error assessment exists. Saturation,
   open wires and shorted sensor inputs require separate fault-injection analysis;
   zero ADC alone is not proof of a working sensor.

## 4. Explicit dummy-load bench enable

Only after sections 1-3 are satisfied, configure a separate acknowledged build:

```powershell
cmake --preset Rev2Bench -B build/Rev2BenchVerified -DGS75_POWER_HARDWARE_VERIFIED=ON
cmake --build build/Rev2BenchVerified --clean-first
```

The operator acknowledgement does not cause power-on. Flash manually, verify EN
still LOW, then connect a dummy load to +5V_RGB/GND (U4 1/2 and 3/4), leaving the
RGB signal pins and controller disconnected. Use a verified Type-C source and
start with a small load safely below **100mA total USB draw**; measure the board's
RGB-OFF baseline first and leave margin. If the baseline is too high to leave a
useful dummy-load margin, do not enable in this USB configuration. Do not raise
budget flags to bypass bMaxPower. PWRINFO must show READY;
PWRON should return OK and EN rise. CURRENT must track total input current.
PWROFF must lower EN; measure the rail's discharge separately because disabling
TPS2553 is not an active output discharge circuit.

## 5. Shutdown and recovery

1. While ON, detach or alter the CC advertisement. EN must fall at the next
   main service, even before the new 20ms qualification completes. After a stable
   source returns, output stays OFF until another PWRON.
2. Suspend, deconfigure and disconnect USB while ON; check OFF and no restart.
3. Slowly raise dummy load through the effective software budget. Check OFF,
   FAULT_LATCHED, incremented FAULT_COUNT and LAST_FAULT=OVERCURRENT. Repeat during
   long DUMP/RAW/PWRINFO replies and capture current, FAULT and EN together.
4. Inject FAULT# LOW safely with an open-drain fixture. Verify OFF and latched
   HW_FAULT even during TX. PWRON must be denied. PWRRESET while fault remains
   must fail and stay OFF. Remove fault: reset succeeds but stays OFF; a new
   PWRON is required. Counters and LAST_FAULT survive reset.
5. Exercise ADC failure using a controlled test build/fixture: missing conversion,
   invalid/saturated input within MCU electrical limits, or loss of sampling.
   Verify no new enable and shutdown while ON. Do not exceed PA3 absolute ratings.
6. Hardware TPS2553 current-limit characterization at ~0.8-0.94A cannot
   be reached with the 100mA descriptor cap. Characterize separately
   with an appropriately isolated hardware fixture and externally limited supply;
   do not silently raise firmware limits to bypass the software shutdown.
7. Recheck reset/startup with a connected load and that no infinite retries occur.
   In a controlled dummy-load bench build, induce a software HardFault and an
   Error_Handler path separately; check that EN falls. Do not fault an unprotected
   USB host or attached RGB controller. A halted core without a running exception
   handler can still leave EN HIGH indefinitely: no independent watchdog is wired
   to EN. Validate the hardware current limit and external supply first.
8. Scope FAULT# and EN on the same timebase during idle, DUMP, RAW, PWRINFO and
   simultaneous HID traffic. Record many triggers, max observed FAULT-low-to-EN-low
   delay, UART timeouts, scan maxima and source current. The code's 10ms UART
   timeout depends on a running 1ms SysTick; no absolute software bound applies
   if ticks stop or main/interrupt execution halts.

## 6. Keyboard regression and evidence

Run normal key combinations, >6 accepted NKRO keys, Boot rollover, ghost fixture
X+Left Ctrl -> add U, media press/release during DUMP, RX overlap and long-command
recovery. Measure delta SCAN_COUNT/delta SCAN_TIME_MS = 1 scan/ms; record MAX_SCAN_US
and MAX_RUNTIME_US. Confirm no SPI/PWM/EXTI activity was enabled on reserved pins.

Save board identity, firmware hash/build flags, BOM corrections, supply/cable,
ambient conditions, raw UART logs, scope traces, meter readings, thresholds and
observed recovery. Mark each check PASS/FAIL/NOT RUN. Only then update the
Hardware-validated sections. LED/RGB controller reverse engineering, production
power compliance, automatic brightness budgets and suspend power policy remain
future work in separate scope.


## 7. USB suspend consumption and future 500mA feasibility

Before claiming suspend compliance, measure **total USB input current** after
suspend settles, with EN and +5V_RGB observed simultaneously. Record host/hub,
cable, CC mode, remote-wakeup setting, voltage, meter burden and repeatability.
Repeat through resume and reconnect, checking that EN remains LOW. Use the
applicable USB 2.0 specification and suspend-current ECN for this topology;
do not infer a PASS from firmware state alone. Enter the data in the blank
[POWER_TEST_RECORD.md](POWER_TEST_RECORD.md).

Separately study whether a future USB 2.0 High-Power configuration up to 500mA
is viable: measure configured and suspend baselines, load headroom and peaks;
check regulator, TPS2553, shunt, connector and PCB thermal ratings; verify
source/host compatibility and the required USB descriptor, firmware policy and
compliance tests. High-Power refers to the USB power category, not High-Speed
link operation. **Do not draw 500mA or alter the current 100mA descriptor in
this stage.** See the [USB-IF USB 2.0 specification library](https://www.usb.org/documents?search=usb+2.0)
and [bus-current guidance](https://compliance.usb.org/index.asp?UpdateFile=Electrical).

Audit note: [POWER_AUDIT.md](POWER_AUDIT.md) records confirmed defects and
remaining restrictions. First power-up must use diagnostic Rev2Debug; bench
control is a later step after A1 BOM, shunt, meter and stuck-zero tests pass.

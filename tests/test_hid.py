"""Actual report/Fn/scheduler/class/ST-core regressions; no USB hardware needed."""
import csv
import ctypes as C
import itertools
import json
from pathlib import Path
import random
import re
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
DLL = C.CDLL(str(Path(sys.argv.pop(1)).resolve()))
OFF = C.CDLL(str(Path(sys.argv.pop(1)).resolve()))
FIXTURE = list(csv.DictReader((ROOT / 'docs/HID_KEYMAP.csv').open(encoding='utf-8', newline='')))
IDS = {r['name']: int(r['KeyId']) for r in FIXTURE}
USAGES = {r['name']: int(r['HID_usage'], 16) for r in FIXTURE}
LABEL_IDS = {r['label']: r['name'] for r in FIXTURE}
DATA = json.loads((ROOT / 'tools/matrix_mapper/gs75_matrix_20260921_172228.json').read_text())
GHOSTS = json.loads((ROOT / 'tools/matrix_mapper/ghosting_results/gs75_ghosting_20260921_174434.json').read_text())
Keys = C.c_uint32 * 4
Rows = C.c_uint32 * 8
Bytes = C.c_uint8 * 32

class State(C.Structure):
    _fields_ = [('pressed', Keys), ('suppressed', Keys), ('ambiguous', Keys)]

class Effective(C.Structure):
    _fields_ = [('modifiers', C.c_uint8), ('bitmap', C.c_uint8 * 21)]

class Boot(C.Structure):
    _fields_ = [('modifiers', C.c_uint8), ('reserved', C.c_uint8), ('keys', C.c_uint8 * 6)]

class Info(C.Structure):
    _fields_ = [(n, C.c_uint32) for n in ('state', 'configured', 'protocol', 'idle', 'led_mask',
               'tx_count', 'queue_depth', 'queue_max', 'queue_overflow', 'resync_count', 'last_report_size')]

for lib in (DLL, OFF):
    lib.HidFn_Translate.argtypes = [C.POINTER(State), C.POINTER(Effective)]
    lib.HidReport_BuildNkro.argtypes = [C.POINTER(Effective), C.POINTER(Effective)]
    lib.HidReport_BuildBoot.argtypes = [C.POINTER(Effective), C.POINTER(Boot)]
    lib.HidKeymap_Usage.argtypes = [C.c_int]
    lib.HidKeymap_Usage.restype = C.c_uint8
    lib.HidFn_TranslateOutputs.argtypes = [C.POINTER(State), C.POINTER(Effective), C.POINTER(C.c_uint8)]
DLL.UsbKeyboard_Capture.argtypes = [C.POINTER(State)]
DLL.UsbKeyboard_GetInfo.argtypes = [C.POINTER(Info)]
DLL.UsbKeyboard_Task.argtypes = [C.c_uint32]
DLL.Test_UsbSetup.argtypes = [C.c_uint8, C.c_uint8, C.c_uint16, C.c_uint16, C.c_uint16]
DLL.Test_UsbOutput.argtypes = [C.c_uint8, C.c_uint32]
DLL.Test_UsbComplete.argtypes = [C.c_uint32]
DLL.Test_UsbReport.argtypes = [C.c_uint32]
DLL.Test_UsbReport.restype = C.POINTER(C.c_uint8)
DLL.Test_ControlData.restype = C.POINTER(C.c_uint8)
DLL.Test_ControlStalled.restype = C.c_uint8
DLL.Test_UsbPending.restype = C.c_uint8
DLL.Test_UsbBlock.argtypes = [C.c_uint8]
DLL.Test_PlanUsbInterleave.argtypes = [C.c_uint8]
DLL.Test_UsbSize.argtypes = [C.c_uint32]
for name in ('Test_UsbErrors', 'Test_UsbCount', 'Test_UsbSize', 'Test_ControlSize', 'Test_HarnessErrors'):
    getattr(DLL, name).restype = C.c_uint32
DLL.Test_Output.restype = C.c_char_p
DLL.Test_Feed.argtypes = [C.c_char_p]
DLL.Test_QueueDuringTx.argtypes = [C.c_char_p]
DLL.Test_PlanLogicalDuringTx.argtypes = [C.POINTER(C.c_uint32), C.c_uint32]
DLL.Test_LogicalSample.argtypes = [C.POINTER(C.c_uint32)]
DLL.UsbDescriptors_Config.argtypes = [C.POINTER(C.c_uint16)]
DLL.UsbDescriptors_Config.restype = C.POINTER(C.c_uint8)

def state(*names):
    s = State()
    for name in names:
        bit = IDS[name] - 1
        s.pressed[bit // 32] |= 1 << (bit % 32)
    return s

def effective(s, lib=DLL):
    e = Effective()
    lib.HidFn_Translate(C.byref(s), C.byref(e))
    return e

def nkro(s, lib=DLL):
    e, r = effective(s, lib), Effective()
    lib.HidReport_BuildNkro(C.byref(e), C.byref(r))
    return bytes(r)

def boot(s, lib=DLL):
    e, r = effective(s, lib), Boot()
    lib.HidReport_BuildBoot(C.byref(e), C.byref(r))
    return bytes(r)

def decode(report):
    if len(report) == 8:
        return report[0], set(report[2:]) - {0}
    assert len(report) == 22
    return report[0], {u for u in range(4, 0xa5) if report[1 + (u-4)//8] & (1 << ((u-4) % 8))}

def info():
    i = Info()
    DLL.UsbKeyboard_GetInfo(C.byref(i))
    return i

def project(edges):
    rows = Rows()
    for edge in edges:
        a, b = map(int, re.findall(r'FPC(\d+)', edge))
        if 17 <= a <= 24 and b in list(range(1, 17)) + [25, 26]:
            col = b - 1 if b <= 16 else b - 9
            rows[a - 17] |= 1 << col
    return rows

def singles(*labels):
    return project([e for label in labels for e in DATA['singles'][label]['active_edges']])

class HidTests(unittest.TestCase):
    def setUp(self):
        DLL.Test_Reset()
        DLL.Test_UsbInit()

    def tearDown(self):
        self.assertEqual(DLL.Test_HarnessErrors(), 0)
        self.assertEqual(DLL.Test_UsbErrors(), 0)

    def setup(self, bm, request, value=0, length=0, index=0):
        DLL.Test_UsbSetup(bm, request, value, index, length)
        self.assertFalse(DLL.Test_ControlStalled())
        return bytes(DLL.Test_ControlData()[:DLL.Test_ControlSize()])

    def protocol(self, value):
        self.setup(0x21, 0x0b, value)

    def capture(self, *names):
        s = state(*names)
        DLL.UsbKeyboard_Capture(C.byref(s))

    def pump(self, now=1):
        DLL.UsbKeyboard_Task(now)
        DLL.Test_UsbComplete(now)

    def reports(self):
        return [bytes(DLL.Test_UsbReport(i)[:DLL.Test_UsbSize(i)]) for i in range(DLL.Test_UsbCount())]

    def test_all_102_mapping_fixture_and_measured_coordinates(self):
        self.assertEqual(len(FIXTURE), 102)
        self.assertEqual([r['label'] for r in FIXTURE], list(DATA['singles']))
        for record in FIXTURE:
            name, key, usage = record['name'], int(record['KeyId']), int(record['HID_usage'], 16)
            with self.subTest(key=name):
                self.assertEqual(DLL.HidKeymap_Usage(key), usage)
                pair = record['FPC_row'] + '->' + record['FPC_column']
                self.assertIn(pair, DATA['singles'][record['label']]['active_edges'])
                want = (1 << (usage-0xe0), set()) if usage >= 0xe0 else (0, {usage} if usage else set())
                self.assertEqual(decode(nkro(state(name))), want)
                self.assertEqual(decode(boot(state(name))), want)

    def test_confirmed_special_usages_and_right_gui(self):
        expected = {'KEY_BACKSLASH':0x31, 'KEY_PIPE_BACKSLASH':0x64, 'KEY_WIN':0xe7,
                    'KEY_PAGE_UP_HOME':0x4b, 'KEY_PAGE_DOWN_END':0x4e, 'KEY_PAUSE_BREAK':0x48,
                    'KEY_ENTER':0x28, 'KEY_NUMPAD_ENTER':0x58, 'KEY_NUM_LOCK':0x53,
                    'KEY_A':4, 'KEY_W':0x1a, 'KEY_SPACE':0x2c, 'KEY_ESC':0x29}
        for key, usage in expected.items():
            self.assertEqual(DLL.HidKeymap_Usage(IDS[key]), usage)
        self.assertEqual(decode(nkro(state('KEY_WIN'))), (0x80, set()))
        self.assertEqual(decode(nkro(state('KEY_BACKSLASH', 'KEY_PIPE_BACKSLASH'))), (0, {0x31,0x64}))

    def test_modifier_sides(self):
        for key, bit in [('KEY_LEFT_CTRL',0),('KEY_LEFT_SHIFT',1),('KEY_LEFT_ALT',2),
                         ('KEY_RIGHT_CTRL',4),('KEY_RIGHT_SHIFT',5),('KEY_RIGHT_ALTGR',6),('KEY_WIN',7)]:
            self.assertEqual(boot(state(key))[0], 1 << bit)

    def test_fkeys_arrows_navigation_numpad(self):
        for n in range(1,13): self.assertEqual(DLL.HidKeymap_Usage(IDS[f'KEY_F{n}']), 0x39+n)
        for key in IDS:
            if key.startswith('KEY_NUMPAD_') or key.startswith('KEY_ARROW_'):
                self.assertEqual(decode(nkro(state(key)))[1], {USAGES[key]})

    def test_ordinary_combinations(self):
        for keys, mods, usages in [(['KEY_LEFT_SHIFT','KEY_A'],2,{4}),
                                  (['KEY_LEFT_CTRL','KEY_C'],1,{6}),
                                  (['KEY_LEFT_CTRL','KEY_LEFT_SHIFT','KEY_ESC'],3,{0x29}),
                                  (['KEY_W','KEY_A','KEY_LEFT_SHIFT'],2,{0x1a,4})]:
            for builder in (nkro,boot): self.assertEqual(decode(builder(state(*keys))), (mods,usages))

    def test_only_pressed_is_used_and_input_is_immutable(self):
        s = state('KEY_A')
        s.suppressed[:] = [0xffffffff]*4
        s.ambiguous[:] = [0xffffffff]*4
        before = bytes(s)
        self.assertEqual(decode(nkro(s)), (0,{4}))
        self.assertEqual(bytes(s), before)

    def test_fn_alone_never_reaches_host(self):
        self.assertTrue(any(state('KEY_FN').pressed))
        self.assertEqual(nkro(state('KEY_FN')), bytes(22))
        self.assertEqual(boot(state('KEY_FN')), bytes(8))

    def test_fn_home_end_replace_base(self):
        for key, usage in [('KEY_PAGE_UP_HOME',0x4a),('KEY_PAGE_DOWN_END',0x4d)]:
            for builder in (nkro,boot): self.assertEqual(decode(builder(state('KEY_FN',key))), (0,{usage}))

    def test_fn_break_and_real_modifiers(self):
        for builder in (nkro,boot):
            self.assertEqual(decode(builder(state('KEY_FN','KEY_PAUSE_BREAK'))), (1,{0x48}))
            self.assertEqual(decode(builder(state('KEY_FN','KEY_PAUSE_BREAK','KEY_RIGHT_CTRL'))), (0x11,{0x48}))

    def test_fn_volume_removed_from_keyboard_report(self):
        for key in ('KEY_ARROW_LEFT','KEY_ARROW_RIGHT'):
            for builder in (nkro,boot): self.assertEqual(decode(builder(state('KEY_FN',key))), (0,set()))

    def test_legacy_volume_flag_does_not_restore_page07(self):
        for key in ('KEY_ARROW_LEFT','KEY_ARROW_RIGHT'):
            for builder in (nkro,boot): self.assertEqual(decode(builder(state('KEY_FN',key),OFF)), (0,set()))
            e=Effective(); c=C.c_uint8(); s=state('KEY_FN',key)
            OFF.HidFn_TranslateOutputs(C.byref(s),C.byref(e),C.byref(c))
            self.assertEqual(c.value,4 if key=='KEY_ARROW_LEFT' else 2)
        self.assertEqual(decode(nkro(state('KEY_FN','KEY_PAGE_UP_HOME'),OFF)), (0,{0x4a}))

    def test_no_unconfirmed_fn_translation(self):
        self.assertEqual(nkro(state('KEY_FN','KEY_F1','KEY_A')), nkro(state('KEY_F1','KEY_A')))

    def test_fn_changes_while_base_keys_remain_held(self):
        self.pump()
        for keys, expected in [(('KEY_PAGE_UP_HOME',),{0x4b}),
                               (('KEY_PAGE_UP_HOME','KEY_FN'),{0x4a}),
                               (('KEY_PAGE_UP_HOME',),{0x4b}), ((),set())]:
            self.capture(*keys); self.pump()
            self.assertEqual(decode(self.reports()[-1]), (0,expected))

    def test_nkro_over_six_and_release(self):
        keys = ['KEY_'+c for c in 'ABCDEFGH']
        self.assertEqual(decode(nkro(state(*keys,'KEY_LEFT_SHIFT'))), (2,set(range(4,12))))
        self.assertEqual(decode(nkro(state(*keys[1:]))), (0,set(range(5,12))))
        self.assertEqual(nkro(state()),bytes(22))

    def test_boot_one_through_six_plus_modifiers(self):
        for count in range(1,7):
            r=boot(state(*['KEY_'+c for c in 'ABCDEF'[:count]],'KEY_WIN','KEY_LEFT_CTRL'))
            self.assertEqual(decode(r), (0x81,set(range(4,4+count))))
            self.assertEqual(r[1],0)

    def test_boot_rollover_all_six_slots_and_recovery(self):
        self.assertEqual(boot(state(*['KEY_'+c for c in 'ABCDEFG'],'KEY_LEFT_SHIFT')), bytes([2,0]+[1]*6))
        self.assertEqual(decode(boot(state('KEY_A','KEY_B'))), (0,{4,5}))
        self.assertEqual(boot(state()),bytes(8))

    def test_bitmap_upper_bound_and_padding(self):
        e=Effective(); e.bitmap[20]=255
        r=Effective(); DLL.HidReport_BuildNkro(C.byref(e),C.byref(r))
        self.assertEqual(bytes(r)[-1],1)
        self.assertEqual(decode(bytes(r)),(0,{0xa4}))

    def test_default_report_and_get_set_protocol(self):
        self.assertEqual(self.setup(0xa1,3,length=1),b'\x01')
        self.protocol(0)
        self.assertEqual(self.setup(0xa1,3,length=1),b'\x00')
        self.pump(); self.assertEqual(len(self.reports()[-1]),8)
        self.protocol(1); self.pump(); self.assertEqual(len(self.reports()[-1]),22)

    def test_protocol_switch_held_keys_and_pending_transfer(self):
        self.pump(); self.capture(*['KEY_'+c for c in 'ABCDEFG'],'KEY_WIN')
        DLL.UsbKeyboard_Task(1) # Old NKRO packet has not completed.
        self.protocol(0); self.pump()
        self.assertEqual(self.reports()[-1],bytes([128,0]+[1]*6))
        self.protocol(1); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(128,set(range(4,11))))
        self.capture(); self.pump(); self.assertEqual(self.reports()[-1],bytes(22))

    def test_protocol_switch_drops_obsolete_queued_history(self):
        self.pump(); self.capture('KEY_A'); self.capture('KEY_B')
        self.protocol(0); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0,{5}))
        self.assertEqual(info().queue_depth,0)

    def test_get_report_tracks_latest_accepted_state_and_protocol(self):
        self.capture('KEY_A','KEY_WIN')
        self.assertEqual(decode(self.setup(0xa1,1,0x100,22)),(128,{4}))
        self.protocol(0)
        self.assertEqual(decode(self.setup(0xa1,1,0x100,8)),(128,{4}))

    def test_led_control_output_and_get_report(self):
        for mask in (0,1,2,4,8,16,31,255):
            self.setup(0x21,9,0x200,1); DLL.Test_UsbOutput(mask,1)
            self.assertEqual(self.setup(0xa1,1,0x200,1),bytes([mask&31]))
            self.assertEqual(info().led_mask,mask&31)

    def test_new_setup_aborts_incomplete_led_output(self):
        self.setup(0x21,9,0x200,1)
        self.setup(0xa1,3,length=1)
        self.assertEqual(info().led_mask,0)

    def test_invalid_requests_stall_without_changing_state(self):
        for args in [(0x21,11,2,0,0),(0x21,11,0,0,1),(0xa1,3,0,1,1),
                     (0x21,9,0x201,0,1),(0x21,9,0x200,0,22),(0x21,10,1,0,0),
                     (0xa1,1,0x300,0,1),(0xa1,3,0,0x100,1),(0x21,3,0,0,1)]:
            DLL.Test_UsbSetup(*args)
            self.assertTrue(DLL.Test_ControlStalled(),args)
            self.assertEqual(info().protocol,1)

    def test_idle_zero_no_duplicate_reports(self):
        self.pump()
        for now in range(2,100): self.pump(now)
        self.assertEqual(len(self.reports()),1)
        self.capture('KEY_FN'); self.pump(100)
        self.assertEqual(len(self.reports()),1)

    def test_idle_periodic_only_when_enabled(self):
        self.pump(10)
        self.setup(0x21,10,2<<8)
        self.assertEqual(self.setup(0xa1,2,length=1),b'\x02')
        self.pump(17); self.assertEqual(len(self.reports()),1)
        self.pump(18); self.assertEqual(len(self.reports()),2)
        self.setup(0x21,10,0); self.pump(100)
        self.assertEqual(len(self.reports()),2)

    def test_idle_clock_wrap(self):
        self.pump(0xfffffffc); self.setup(0x21,10,2<<8)
        self.pump(3); self.assertEqual(len(self.reports()),1)
        self.pump(4); self.assertEqual(len(self.reports()),2)

    def test_short_press_release_while_transport_blocked(self):
        self.pump(); DLL.Test_UsbBlock(1)
        self.capture('KEY_A'); self.pump(); self.capture(); self.pump()
        self.assertEqual(len(self.reports()),1)
        self.assertEqual(info().queue_depth,2)
        DLL.Test_UsbBlock(0); self.pump(); self.pump()
        self.assertEqual([decode(r) for r in self.reports()],[(0,set()),(0,{4}),(0,set())])

    def test_short_press_release_during_actual_blocking_uart_dump(self):
        self.pump()
        samples=(Rows*2)(singles('A'),Rows())
        DLL.Test_PlanLogicalDuringTx(C.cast(samples,C.POINTER(C.c_uint32)),2)
        DLL.Test_QueueDuringTx(b'PING\r')
        DLL.Test_Feed(b'DUMP\r'); DLL.Test_Drain()
        self.assertIn(b'MATRIX_END\r\nPONG\r\n',DLL.Test_Output())
        self.assertEqual(info().queue_depth,2)
        self.pump(); self.pump()
        self.assertEqual([decode(r) for r in self.reports()][-2:],[(0,{4}),(0,set())])

    def test_initial_sync_does_not_swallow_subsequent_tap(self):
        self.capture('KEY_A'); self.capture()
        self.pump(); self.pump(); self.pump()
        self.assertEqual([decode(r) for r in self.reports()],[(0,set()),(0,{4}),(0,set())])

    def test_queue_wrap_without_loss(self):
        self.pump()
        for _ in range(200):
            self.capture('KEY_A'); self.capture()
            self.pump(); self.pump()
        self.assertEqual(len(self.reports()),401)
        self.assertEqual(info().queue_overflow,0)

    def test_busy_buffer_immutable_until_completion(self):
        self.pump(); self.capture('KEY_A'); DLL.UsbKeyboard_Task(2)
        for _ in range(5): self.capture('KEY_B'); DLL.UsbKeyboard_Task(3)
        DLL.Test_UsbComplete(4)
        self.assertEqual(decode(self.reports()[-1]),(0,{4}))
        self.pump(5); self.assertEqual(decode(self.reports()[-1]),(0,{5}))

    def test_protocol_irq_during_main_report_preparation(self):
        self.pump(); self.capture('KEY_A')
        DLL.Test_PlanUsbInterleave(1); self.pump()
        self.assertEqual(len(self.reports()),1) # Stale report candidate discarded.
        self.pump(); self.assertEqual(self.reports()[-1],boot(state('KEY_A')))

    def test_overflow_irq_invalidates_prepared_report(self):
        self.pump(); self.capture('KEY_A')
        DLL.Test_PlanUsbInterleave(2); self.pump()
        self.assertEqual(len(self.reports()),1)
        self.pump(); self.assertEqual(self.reports()[-1],bytes(22))
        self.assertEqual(info().queue_overflow,1)

    def test_set_idle_zero_irq_cancels_prepared_duplicate(self):
        self.pump(1); self.setup(0x21,10,1<<8)
        DLL.Test_PlanUsbInterleave(3); self.pump(5)
        self.assertEqual(len(self.reports()),1)

    def test_fn_break_release_preserves_real_control(self):
        self.pump()
        self.capture('KEY_FN','KEY_PAUSE_BREAK','KEY_RIGHT_CTRL'); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0x11,{0x48}))
        self.capture('KEY_FN','KEY_RIGHT_CTRL'); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0x10,set()))
        self.capture(); self.pump(); self.assertEqual(self.reports()[-1],bytes(22))

    def test_suppressed_fn_does_not_translate_accepted_arrow(self):
        s=state('KEY_ARROW_LEFT'); fn=state('KEY_FN')
        s.suppressed[:]=fn.pressed[:]
        self.assertEqual(decode(nkro(s)),(0,{0x50}))

    def test_queue_overflow_resync_releases_host(self):
        self.pump(); self.capture('KEY_A'); self.pump()
        for n in range(80): self.capture(*(['KEY_B'] if n%2==0 else []))
        self.assertEqual(info().queue_overflow,1)
        self.assertEqual(info().queue_max,64)
        self.pump()
        self.assertEqual(self.reports()[-1],bytes(22))
        self.assertEqual(info().resync_count,2)
        self.capture('KEY_C'); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0,{6}))

    def test_overflow_while_old_report_is_in_flight(self):
        self.pump(); self.capture('KEY_A'); DLL.UsbKeyboard_Task(2)
        for n in range(90): self.capture(*(['KEY_B'] if n%2 else ['KEY_C']))
        self.capture('KEY_WIN','KEY_D')
        DLL.Test_UsbComplete(3); self.pump(4)
        self.assertEqual(decode(self.reports()[-1]),(128,{7}))

    def test_random_overflow_final_state_matches_current(self):
        rng=random.Random(7522); self.pump()
        for _ in range(15):
            for _ in range(150):
                names=rng.sample(['KEY_A','KEY_B','KEY_C','KEY_WIN','KEY_FN','KEY_PAGE_UP_HOME'],rng.randrange(7))
                self.capture(*names)
            for _ in range(66): self.pump()
            self.assertEqual(self.reports()[-1],nkro(state(*names)))

    def test_bus_reset_restores_protocol_idle_led_and_resync(self):
        self.pump(); self.capture('KEY_A'); self.pump(); self.protocol(0)
        self.setup(0x21,10,3<<8); self.setup(0x21,9,0x200,1); DLL.Test_UsbOutput(31,1)
        DLL.Test_UsbBusReset()
        self.assertEqual((info().configured,info().protocol,info().idle,info().led_mask),(0,1,0,0))
        self.setup(0,5,1); self.setup(0,9,1); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0,{4}))

    def test_unconfigured_history_is_not_replayed(self):
        self.pump(); self.setup(0,9,0)
        self.capture('KEY_A'); self.capture(); self.capture('KEY_B')
        self.setup(0,9,1); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0,{5}))
        self.assertEqual(info().queue_depth,0)

    def test_suspend_busy_and_resume(self):
        self.pump(); DLL.Test_UsbSuspend(); self.capture('KEY_A'); self.pump()
        self.assertEqual(len(self.reports()),1)
        DLL.Test_UsbResume(); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(0,{4}))

    def test_endpoint_halt_clear_recovers_inflight_packet(self):
        self.pump(); self.capture('KEY_A'); DLL.UsbKeyboard_Task(2)
        self.setup(0x02,3,0,index=0x81)
        self.capture(); self.setup(0x02,1,0,index=0x81); self.pump()
        self.assertEqual(self.reports()[-1],bytes(22))

    def test_ghost_suppression_through_usb_and_recovery(self):
        self.pump()
        DLL.Test_LogicalSample(singles('X','Left Ctrl')); self.pump()
        rectangle=Rows(); rectangle[3]=rectangle[7]=(1<<9)|(1<<15)
        DLL.Test_LogicalSample(rectangle); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(1,{0x1b}))
        DLL.Test_LogicalSample(singles('Left Ctrl','U')); self.pump()
        self.assertEqual(decode(self.reports()[-1]),(1,{0x18}))
        DLL.Test_LogicalSample(Rows()); self.pump()
        self.assertEqual(self.reports()[-1],bytes(22))

    def test_all_20_ghost_fixtures_cannot_add_phantom_to_usb(self):
        for fixture in GHOSTS['results']:
            for held in itertools.combinations(fixture['pressed_keys'],2):
                DLL.Test_Reset(); DLL.Test_UsbInit(); self.pump()
                DLL.Test_LogicalSample(singles(*held)); self.pump()
                expected=self.reports()[-1]
                DLL.Test_LogicalSample(project(fixture['observed_edges'])); self.pump()
                self.assertEqual(self.reports()[-1],expected)

    def test_usbinfo_diagnostics(self):
        self.pump(); DLL.Test_Feed(b'USBINFO\r')
        output=DLL.Test_Output()
        for field in ('USB_STATE=CONFIGURED','USB_CONFIGURED=1','USB_PROTOCOL=REPORT','USB_IDLE=0',
                      'USB_LED_MASK=0x00','USB_TX_COUNT=1','USB_QUEUE_DEPTH=0','USB_QUEUE_MAX=0',
                      'USB_QUEUE_OVERFLOW=0','USB_RESYNC_COUNT=1','USB_LAST_REPORT_SIZE=22','MAX_RUNTIME_US=0'):
            self.assertIn(field.encode()+b'\r\n',output)

    def test_configuration_and_report_descriptor_consistency(self):
        size=C.c_uint16(); p=DLL.UsbDescriptors_Config(C.byref(size)); cfg=bytes(p[:size.value])
        self.assertEqual(cfg[:9],bytes([9,2,59,0,2,1,0,0x80,50]))
        self.assertEqual(cfg[9:18],bytes([9,4,0,0,1,3,1,1,0]))
        self.assertEqual(cfg[27:34],bytes([7,5,0x81,3,32,0,1]))
        desc=self.setup(0x81,6,0x2200,255)
        self.assertEqual(len(desc),int.from_bytes(cfg[25:27],'little'))
        # Independent short-item parser; inspect fields, ranges, constants, sizes.
        globals_={}; local={}; fields=[]; offset=0
        while offset<len(desc):
            prefix=desc[offset]; offset+=1
            self.assertNotEqual(prefix,0xfe)
            width=[0,1,2,4][prefix&3]; value=int.from_bytes(desc[offset:offset+width],'little'); offset+=width
            kind=(prefix>>2)&3; tag=prefix>>4
            if kind==1:
                self.assertNotEqual(tag,8,'No Report ID allowed'); globals_[tag]=value
            elif kind==2: local[tag]=value
            elif kind==0:
                if tag in (8,9): fields.append((tag,value,globals_.copy(),local.copy()))
                local={}
        self.assertEqual(sum(g[7]*g[9] for t,v,g,l in fields if t==8),22*8)
        self.assertEqual(sum(g[7]*g[9] for t,v,g,l in fields if t==9),8)
        useful=[(t,g[0],l.get(1),l.get(2),g[9]) for t,v,g,l in fields if not(v&1)]
        self.assertEqual(useful,[(8,7,0xe0,0xe7,8),(8,7,4,0xa4,161),(9,8,1,5,5)])

    def test_usb_device_identity_and_uid_serial(self):
        desc=self.setup(0x80,6,0x100,18)
        self.assertEqual(len(desc),18); self.assertEqual(desc[4:8],bytes([0,0,0,64]))
        self.assertEqual(desc[-1],1)
        serial=self.setup(0x80,6,0x303,64,index=0x409)
        self.assertEqual(serial[2:].decode('utf-16-le'),'1234567890ABCDEF12345678')

if __name__ == '__main__': unittest.main(verbosity=2)

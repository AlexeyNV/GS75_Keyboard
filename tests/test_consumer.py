"""Two-interface regressions through the real scheduler, class and ST core."""
import ctypes as C
import random
import unittest
import test_hid as B

D = B.DLL
for name in ('Test_ConsumerCount', 'Test_UsbEventCount', 'Test_UsbEventSize'):
    getattr(D, name).restype = C.c_uint32
for name in ('Test_ConsumerReport', 'Test_ConsumerPending', 'Test_UsbEventEp'):
    getattr(D, name).restype = C.c_uint8
D.Test_ConsumerReport.argtypes = [C.c_uint32]
D.Test_ConsumerBlock.argtypes = [C.c_uint8]
D.Test_UsbCompleteEndpoint.argtypes = [C.c_uint8, C.c_uint32]
D.Test_UsbEventData.restype = C.POINTER(C.c_uint8)
MEDIA = [('KEY_ARROW_LEFT', 4), ('KEY_ARROW_RIGHT', 2), ('KEY_NUMPAD_0', 1)]

def outputs(s):
    e, bits = B.Effective(), C.c_uint8()
    D.HidFn_TranslateOutputs(C.byref(s), C.byref(e), C.byref(bits))
    return bytes(e), bits.value

class ConsumerTests(unittest.TestCase):
    setUp = B.HidTests.setUp
    tearDown = B.HidTests.tearDown
    setup = B.HidTests.setup
    capture = B.HidTests.capture
    pump = B.HidTests.pump
    reports = B.HidTests.reports
    protocol = B.HidTests.protocol

    def drain(self, count=150):
        for now in range(1, count + 1):
            self.pump(now)
        self.assertEqual(B.info().queue_depth, 0)

    def consumer(self):
        return [D.Test_ConsumerReport(i) for i in range(D.Test_ConsumerCount())]

    def events(self):
        return [(D.Test_UsbEventEp(i), bytes(D.Test_UsbEventData(i)[:D.Test_UsbEventSize(i)]))
                for i in range(D.Test_UsbEventCount())]

    def test_sysreq_boot_and_nkro_without_synthetic_modifiers(self):
        for builder in (B.boot, B.nkro):
            self.assertEqual(B.decode(builder(B.state('KEY_FN', 'KEY_PRINT_SCREEN'))), (0, {0x9a}))
            self.assertEqual(B.decode(builder(B.state('KEY_PRINT_SCREEN'))), (0, {0x46}))
            self.assertEqual(B.decode(builder(B.state('KEY_FN', 'KEY_PRINT_SCREEN', 'KEY_RIGHT_CTRL'))), (16, {0x9a}))

    def test_media_mapping_and_base_replacement(self):
        for key, bits in MEDIA:
            s = B.state('KEY_FN', key)
            before = bytes(s)
            keyboard, consumer = outputs(s)
            self.assertEqual((B.decode(keyboard), consumer), ((0, set()), bits))
            self.assertEqual(bytes(s), before)
            self.assertEqual(outputs(B.state(key)), (B.nkro(B.state(key)), 0))

    def test_simultaneous_consumer_bits_and_real_modifier(self):
        keyboard, bits = outputs(B.state('KEY_FN', 'KEY_LEFT_SHIFT', *(k for k, _ in MEDIA)))
        self.assertEqual((B.decode(keyboard), bits), ((2, set()), 7))

    def test_suppressed_or_ambiguous_only_keys_never_emit_consumer(self):
        for field in ('suppressed', 'ambiguous'):
            s = B.state('KEY_FN')
            setattr(s, field, B.state(*(k for k, _ in MEDIA)).pressed)
            self.assertEqual(outputs(s), (bytes(22), 0))
            s = B.state('KEY_ARROW_LEFT')
            setattr(s, field, B.state('KEY_FN').pressed)
            self.assertEqual(outputs(s), (B.nkro(B.state('KEY_ARROW_LEFT')), 0))

    def test_fn_first_media_taps_and_release(self):
        self.drain(); self.capture('KEY_FN'); self.drain()
        for key, bits in MEDIA:
            self.capture('KEY_FN', key); self.drain()
            self.capture('KEY_FN'); self.drain()
            self.assertEqual(self.consumer()[-2:], [bits, 0])
        self.capture(); self.drain()
        self.assertEqual(self.reports(), [bytes(22)])

    def test_base_held_fn_press_and_release_completion_order(self):
        for key, bits in MEDIA:
            D.Test_Reset(); D.Test_UsbInit(); self.drain()
            self.capture(key); self.drain()
            start = len(self.events())
            self.capture('KEY_FN', key); D.UsbKeyboard_Task(200)
            self.assertTrue(D.Test_UsbPending()); self.assertFalse(D.Test_ConsumerPending())
            D.Test_UsbCompleteEndpoint(1, 201); D.UsbKeyboard_Task(202)
            self.assertTrue(D.Test_ConsumerPending())
            D.Test_UsbCompleteEndpoint(2, 203); self.drain()
            self.capture(key); D.UsbKeyboard_Task(204)
            self.assertTrue(D.Test_ConsumerPending()); self.assertFalse(D.Test_UsbPending())
            D.Test_UsbCompleteEndpoint(2, 205); self.drain()
            self.assertEqual(self.events()[start:], [(1, bytes(22)), (2, bytes([bits])),
                                                   (2, b'\0'), (1, B.nkro(B.state(key)))])

    def test_sysreq_live_fn_release_restores_printscreen(self):
        self.drain()
        for keys in [('KEY_PRINT_SCREEN',), ('KEY_FN', 'KEY_PRINT_SCREEN'), ('KEY_PRINT_SCREEN',), ()]:
            self.capture(*keys); self.drain()
        self.assertEqual([B.decode(r)[1] for r in self.reports()], [set(), {0x46}, {0x9a}, {0x46}, set()])
        self.assertEqual(self.consumer(), [0])

    def test_consumer_stable_inflight_buffer_and_queued_release(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        self.assertTrue(D.Test_ConsumerPending())
        self.capture()
        for now in range(3, 20): D.UsbKeyboard_Task(now)
        D.Test_UsbCompleteEndpoint(2, 20); self.drain()
        self.assertEqual(self.consumer(), [0, 4, 0])

    def test_keyboard_progress_with_unchanged_busy_consumer(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        self.capture('KEY_FN', 'KEY_ARROW_LEFT', 'KEY_A'); D.UsbKeyboard_Task(3)
        D.Test_UsbCompleteEndpoint(1, 4)
        self.assertEqual(B.decode(self.reports()[-1]), (0, {4}))
        self.assertTrue(D.Test_ConsumerPending())
        D.Test_UsbCompleteEndpoint(2, 5); self.capture(); self.drain()

    def test_consumer_release_progress_with_busy_keyboard(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); self.drain()
        self.capture('KEY_FN', 'KEY_ARROW_LEFT', 'KEY_A'); D.UsbKeyboard_Task(2)
        self.capture('KEY_FN', 'KEY_A'); D.UsbKeyboard_Task(3)
        D.Test_UsbCompleteEndpoint(2, 4)
        self.assertEqual(self.consumer()[-1], 0)
        self.assertTrue(D.Test_UsbPending())
        D.Test_UsbCompleteEndpoint(1, 5); self.drain()

    def test_media_tap_during_real_blocking_uart_reply(self):
        self.drain()
        # Measured coordinates, real logical suppression + runtime capture hook.
        arrow_label = next(r['label'] for r in B.FIXTURE if r['name'] == 'KEY_ARROW_LEFT')
        samples = (B.Rows * 2)(B.singles('Fn', arrow_label), B.Rows())
        D.Test_PlanLogicalDuringTx(C.cast(samples, C.POINTER(C.c_uint32)), 2)
        D.Test_QueueDuringTx(b'PING\r'); D.Test_Feed(b'DUMP\r'); D.Test_Drain()
        self.assertIn(b'MATRIX_END\r\nPONG\r\n', D.Test_Output())
        self.drain(); self.assertEqual(self.consumer(), [0, 4, 0])

    def test_overflow_resync_both_outputs_held_and_released(self):
        for final in ((), ('KEY_FN', 'KEY_ARROW_RIGHT', 'KEY_A')):
            D.Test_Reset(); D.Test_UsbInit(); self.drain()
            D.Test_ConsumerBlock(1)
            for i in range(90):
                self.capture('KEY_FN', MEDIA[i % 3][0]); D.UsbKeyboard_Task(i)
            self.capture(*final)
            self.assertGreater(B.info().queue_overflow, 0)
            D.Test_ConsumerBlock(0); self.drain()
            keyboard, consumer = outputs(B.state(*final))
            self.assertEqual(self.reports()[-1], keyboard)
            self.assertEqual(self.consumer()[-1], consumer)

    def test_overflow_with_consumer_packet_already_inflight(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        for i in range(80): self.capture(*(('KEY_A',) if i % 2 else ('KEY_B',)))
        self.capture(); D.Test_UsbCompleteEndpoint(2, 3); self.drain()
        self.assertEqual(self.consumer()[-2:], [4, 0]); self.assertEqual(self.reports()[-1], bytes(22))

    def test_reset_resync_current_consumer_state(self):
        self.drain(); self.capture('KEY_FN', 'KEY_NUMPAD_0'); self.drain()
        D.Test_UsbBusReset(); self.setup(0, 5, 1); self.setup(0, 9, 1); self.drain()
        self.assertEqual(self.consumer()[-2:], [1, 1])
        self.capture(); self.drain(); self.assertEqual(self.consumer()[-1], 0)

    def test_reconfiguration_discards_offline_history(self):
        self.drain(); self.setup(0, 9, 0)
        self.capture('KEY_FN', 'KEY_ARROW_LEFT'); self.capture(); self.capture('KEY_FN', 'KEY_NUMPAD_0')
        self.setup(0, 9, 1); self.drain()
        self.assertEqual(self.consumer(), [0, 1])

    def test_suspend_inflight_press_then_release(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        D.Test_UsbSuspend(); self.capture(); self.pump(3)
        self.assertEqual(self.consumer(), [0])
        D.Test_UsbResume(); self.drain(); self.assertEqual(self.consumer(), [0, 4, 0])

    def test_protocol_switch_preserves_consumer_release(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_RIGHT'); D.UsbKeyboard_Task(2)
        self.protocol(0); self.capture(); self.drain()
        self.assertEqual(self.consumer()[-1], 0); self.assertEqual(self.reports()[-1], bytes(8))
        self.protocol(1); self.drain(); self.assertEqual(self.reports()[-1], bytes(22))

    def test_consumer_halt_clear_resync(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        self.setup(2, 3, index=0x82); self.capture(); self.setup(2, 1, index=0x82); self.drain()
        self.assertEqual(self.consumer()[-1], 0); self.assertEqual(self.reports()[-1], bytes(22))

    def test_consumer_no_periodic_reports_with_keyboard_idle(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); self.drain()
        before = self.consumer(); self.setup(0x21, 10, 1 << 8)
        for now in range(200, 300, 5): self.pump(now)
        self.assertEqual(self.consumer(), before); self.assertGreater(len(self.reports()), 2)

    def test_control_requests_are_interface_local(self):
        self.capture('KEY_FN', 'KEY_NUMPAD_0')
        self.assertEqual(self.setup(0xa1, 1, 0x100, 1, 1), b'\1')
        self.assertEqual(self.setup(0xa1, 2, 0, 1, 1), b'\0')
        self.setup(0x21, 10, index=1)
        for bm, req, value, length in [(0x21, 11, 0, 0), (0xa1, 3, 0, 1),
                                      (0x21, 10, 0x100, 0), (0x21, 9, 0x200, 1), (0xa1, 1, 0x200, 1)]:
            D.Test_UsbSetup(bm, req, value, 1, length)
            self.assertTrue(D.Test_ControlStalled())
        self.assertEqual(B.info().protocol, 1); self.assertEqual(B.info().idle, 0)

    def test_two_interfaces_and_unchanged_keyboard_descriptor(self):
        size = C.c_uint16(); p = D.UsbDescriptors_Config(C.byref(size)); cfg = bytes(p[:size.value])
        self.assertEqual((len(cfg), cfg[4]), (59, 2))
        fixtures = B.ROOT / 'tests/fixtures'
        self.assertEqual(cfg[9:34], bytes.fromhex((fixtures / 'keyboard_interface_descriptor.hex').read_text()))
        keyboard = self.setup(0x81, 6, 0x2200, 255)
        self.assertEqual(keyboard, bytes.fromhex((fixtures / 'keyboard_report_descriptor.hex').read_text()))
        self.assertEqual(cfg[34:43], bytes([9, 4, 1, 0, 1, 3, 0, 0, 0]))
        self.assertEqual(cfg[52:], bytes([7, 5, 0x82, 3, 8, 0, 1]))
        consumer = self.setup(0x81, 6, 0x2200, 255, 1)
        self.assertEqual(len(consumer), int.from_bytes(cfg[50:52], 'little'))
        self.assertEqual(consumer, bytes.fromhex('05 0c 09 01 a1 01 15 00 25 01 09 e2 09 e9 09 ea 75 01 95 03 81 02 75 01 95 05 81 03 c0'))
        self.assertEqual(self.setup(0x81, 6, 0x2100, 9, 1), cfg[43:52])

    def test_usbinfo_consumer_diagnostics(self):
        self.drain(); self.capture('KEY_FN', 'KEY_ARROW_LEFT'); D.UsbKeyboard_Task(2)
        D.Test_Feed(b'USBINFO\r')
        for line in (b'USB_CONSUMER_BITS=0x04', b'USB_CONSUMER_LAST_REPORT=0x04',
                     b'USB_CONSUMER_BUSY=1', b'USB_CONSUMER_TX_COUNT=1', b'USB_CONSUMER_RESYNC_COUNT=1'):
            self.assertIn(line + b'\r\n', D.Test_Output())

    def test_random_independent_endpoint_delays_recover_final_state(self):
        rng = random.Random(7502); self.drain()
        for now in range(1000):
            keys = tuple(k for k in ('KEY_FN', 'KEY_A', 'KEY_ARROW_LEFT', 'KEY_ARROW_RIGHT', 'KEY_NUMPAD_0') if rng.randrange(2))
            self.capture(*keys); D.UsbKeyboard_Task(now)
            for ep in (1, 2):
                if rng.randrange(4) == 0: D.Test_UsbCompleteEndpoint(ep, now)
        self.drain(300)
        keyboard, consumer = outputs(B.state(*keys))
        self.assertEqual(self.reports()[-1], keyboard); self.assertEqual(self.consumer()[-1], consumer)
        self.capture(); self.drain()
        self.assertEqual(self.reports()[-1], bytes(22)); self.assertEqual(self.consumer()[-1], 0)

if __name__ == '__main__': unittest.main(verbosity=2)

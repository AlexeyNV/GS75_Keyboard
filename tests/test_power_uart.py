"""Power commands through actual RX/parser/TX, with existing keyboard transport."""
import ctypes as C
from pathlib import Path
import sys
import unittest
D=C.CDLL(str(Path(sys.argv.pop(1)).resolve()))
D.Test_Feed.argtypes=[C.c_char_p];D.Test_Output.restype=C.c_char_p
D.PowerTest_Output.restype=C.c_uint32
D.Test_UsbReport.argtypes=[C.c_uint32];D.Test_UsbReport.restype=C.POINTER(C.c_uint8)
D.Test_UsbSize.argtypes=[C.c_uint32];D.Test_UsbSize.restype=C.c_uint32
class State(C.Structure):
    _fields_=[(n,C.c_uint32*4) for n in ('pressed','suppressed','ambiguous')]
D.UsbKeyboard_Capture.argtypes=[C.POINTER(State)]
class IntegrationTests(unittest.TestCase):
    def setUp(self):
        D.Test_Reset();D.Test_UsbInit();D.PowerTest_Init()
        for now in range(36):D.Test_SetTick(now);D.PowerTest_Tick(now)
    def tearDown(self):
        self.assertEqual(D.Test_HarnessErrors(),0);self.assertEqual(D.Test_UsbErrors(),0)
    def test_uart_commands_roundtrip(self):
        D.Test_Feed(b'PWRINFO\rCURRENT\rPWRTEST\rPING\r')
        out=D.Test_Output()
        for text in (b'END=PWRINFO',b'END=CURRENT',b'END=PWRTEST',b'PONG'):
            self.assertIn(text+b'\r\n',out)
        self.assertEqual(D.PowerTest_Output(),0)
    def test_enable_and_off_through_parser(self):
        D.Test_Feed(b'PWRON\r');self.assertEqual(D.PowerTest_Output(),1)
        D.Test_Feed(b'PWROFF\r');self.assertEqual(D.PowerTest_Output(),0)
    def test_fault_during_blocking_dump_is_serviced(self):
        D.Test_Feed(b'PWRON\r');self.assertEqual(D.PowerTest_Output(),1)
        D.Test_PowerFaultDuringTx();D.Test_Feed(b'DUMP\r')
        self.assertEqual(D.PowerTest_Output(),0)
        D.Test_Feed(b'PWRINFO\r');out=D.Test_Output()
        self.assertIn(b'MATRIX_END\r\n',out)
        self.assertIn(b'STATE=FAULT_LATCHED\r\n',out)
        self.assertIn(b'LAST_FAULT=HW_FAULT\r\n',out)
    def test_long_command_still_recovers(self):
        D.Test_Feed(b'X'*100+b'\rPING\rPWRINFO\r')
        self.assertIn(b'ERR: BUFFER OVERFLOW\r\nPONG\r\n',D.Test_Output())
        self.assertIn(b'END=PWRINFO\r\n',D.Test_Output())
    def test_usb_keyboard_queue_still_delivers_press_release(self):
        for now in range(36,38):D.UsbKeyboard_Task(now);D.Test_UsbComplete(now)
        s=State();s.pressed[0]=1 # first measured KeyId, Esc
        D.UsbKeyboard_Capture(C.byref(s));D.UsbKeyboard_Task(38);D.Test_UsbComplete(38)
        idx=D.Test_UsbCount()-1
        self.assertNotEqual(bytes(D.Test_UsbReport(idx)[:D.Test_UsbSize(idx)]),bytes(22))
        D.Test_Feed(b'PWRINFO\r');s=State();D.UsbKeyboard_Capture(C.byref(s))
        D.UsbKeyboard_Task(39);D.Test_UsbComplete(39);idx=D.Test_UsbCount()-1
        self.assertEqual(bytes(D.Test_UsbReport(idx)[:D.Test_UsbSize(idx)]),bytes(22))
if __name__=='__main__':unittest.main(verbosity=2)

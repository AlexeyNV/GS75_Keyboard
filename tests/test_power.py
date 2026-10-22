"""Production power modules, four build policies, no MCU required."""
import ctypes as C
from pathlib import Path
import sys
import unittest
U=C.c_uint32
class CC(C.Structure):
    _fields_=[(n,U) for n in ('raw','stable','valid','raw_since_ms','changed_ms','attaches','detaches')]
class Current(C.Structure):
    _fields_=[(n,U) for n in ('raw','mv','ma','filtered_ma','mean_ma','min_ma','max_ma','valid','saturated','samples','last_ms','vref_mv','gain_ppm')]+[('zero_mv',C.c_int32),('sum_ma',C.c_uint64)]
class Info(C.Structure):
    _fields_=[('cc',CC),('current',Current)]+[(n,U) for n in ('state','reason','last_off','fault_count','budget_ma','policy_ma','rgb_on','fault','configured','suspended','baseline_ma','baseline_valid','last_fault')]
libs=[C.CDLL(str(Path(sys.argv.pop(1)).resolve())) for _ in range(4)]
for d in libs:
    d.PowerTest_Tick.argtypes=[U]
    d.PowerTest_Set.argtypes=[U,U,C.c_int,U]
    d.PowerTest_Get.argtypes=[C.POINTER(Info)]
    d.PowerManager_Info.restype=C.POINTER(Info)
    d.PowerTest_Command.argtypes=[C.c_char_p]; d.PowerTest_Command.restype=C.c_char_p
    d.CurrentMonitor_Init.argtypes=[C.POINTER(Current)]
    d.CurrentMonitor_Sample.argtypes=[C.POINTER(Current),U,C.c_uint8,U]
    d.CurrentMonitor_Calibrate.argtypes=[C.POINTER(Current),U,C.c_int32,U]
    d.CurrentMonitor_Calibrate.restype=C.c_uint8
    d.CurrentMonitor_Age.argtypes=[C.POINTER(Current),U]
    d.UsbCDetect_Init.argtypes=[C.POINTER(CC),U]
    d.UsbCDetect_Update.argtypes=[C.POINTER(CC),U,U,C.c_uint8]
D=libs[0]
class PowerTests(unittest.TestCase):
    def setUp(self):
        for d in libs:d.PowerTest_Init()
        self.now=0
    def step(self, n=1, d=D):
        for _ in range(n):
            self.now+=1; d.PowerTest_Tick(self.now)
    def ready(self,d=D):self.step(35,d)
    def info(self,d=D):
        s=Info(); d.PowerTest_Get(C.byref(s)); return s
    def command(self,text,d=D):return d.PowerTest_Command(text.encode()).decode()
    def on(self):
        self.ready();self.assertIn('PWRON=OK',self.command('PWRON'));self.assertEqual(D.PowerTest_Output(),1)
    def test_all_cc_codes(self):
        for code in range(4):
            D.PowerTest_Init();self.now=0;D.PowerTest_Set(code,62,1,10);self.ready()
            s=self.info();self.assertEqual((s.cc.raw,s.cc.stable,s.cc.valid),(code,code,1))
            self.assertEqual(s.cc.attaches, int(code!=3))
    def test_cc_transition_filter_and_detach_counts(self):
        self.ready();D.PowerTest_Set(3,62,1,10);self.step()
        self.assertEqual((self.info().cc.stable,self.info().cc.valid),(1,0))
        self.step(19);self.assertEqual(self.info().cc.detaches,0)
        self.step();self.assertEqual((self.info().cc.stable,self.info().cc.detaches),(3,1))
    def test_cc_bounce_restarts_timer(self):
        self.ready()
        for code in (0,1,0,1):D.PowerTest_Set(code,62,1,10);self.step(10)
        self.assertFalse(self.info().cc.valid);self.step(11);self.assertTrue(self.info().cc.valid)
    def test_cc_uint32_wrap(self):
        c=CC();D.UsbCDetect_Init(C.byref(c),0xfffffff0)
        D.UsbCDetect_Update(C.byref(c),1,0xfffffff0,0)
        D.UsbCDetect_Update(C.byref(c),1,4,0)
        self.assertEqual((c.valid,c.stable),(1,1))
    def test_no_automatic_enable_after_boot(self):
        self.ready();self.assertEqual(self.info().state,2);self.assertEqual(D.PowerTest_Output(),0)
    def test_no_enable_before_cc_stable_or_adc(self):
        self.assertNotIn('PWRON=OK',self.command('PWRON'));self.assertEqual(D.PowerTest_Output(),0)
    def test_normal_build_diagnostics_but_no_enable(self):
        d=libs[1];self.ready(d)
        self.assertIn('BENCH_DISABLED',self.command('PWRON',d));self.assertEqual(d.PowerTest_Output(),0)
        self.assertIn('VALID=1',self.command('CURRENT',d))
    def test_unverified_bom_blocks_bench_enable(self):
        d=libs[2];self.ready(d);self.assertIn('HARDWARE_UNVERIFIED',self.command('PWRON',d));self.assertEqual(d.PowerTest_Output(),0)
    def test_legacy_board_unavailable(self):
        d=libs[3];self.ready(d);self.assertEqual(self.info(d).state,5)
        self.assertIn('BOARD_UNAVAILABLE',self.command('PWRON',d));self.assertEqual(d.PowerTest_Output(),0)
    def test_default_budget_capped_to_descriptor(self):
        D.PowerTest_Set(2,62,1,10);self.ready()
        self.assertEqual((self.info().policy_ma,self.info().budget_ma),(450,100))
        self.assertIn('PWRON=OK',self.command('PWRON'))
        D.PowerTest_Set(2,200,1,10);self.step(12);self.assertEqual(self.info().state,4)
    def test_typec_budget_both_advertisements(self):
        for code in (0,1):
            D.PowerTest_Init();self.now=0;D.PowerTest_Set(code,62,1,10);self.ready();self.assertEqual((self.info().policy_ma,self.info().budget_ma),(700,100))
    def test_cc_change_shuts_down_before_filter_completes(self):
        self.on();D.PowerTest_Set(0,62,1,10);self.step()
        self.assertEqual(D.PowerTest_Output(),0);self.assertEqual(self.info().last_off,5)
        self.step(30);self.assertEqual(D.PowerTest_Output(),0)
    def test_detach_shuts_down_without_auto_restart(self):
        self.on();D.PowerTest_Set(3,62,1,10);self.step()
        self.assertEqual(D.PowerTest_Output(),0)
        D.PowerTest_Set(1,62,1,10);self.step(30);self.assertEqual(D.PowerTest_Output(),0)
    def test_usb_not_configured(self):
        D.PowerTest_Set(1,62,1,8);self.ready();self.assertIn('USB_NOT_CONFIGURED',self.command('PWRON'))
    def test_usb_disconnect_on(self):
        self.on();D.PowerTest_Set(1,62,1,8);self.step()
        self.assertEqual((D.PowerTest_Output(),self.info().last_off),(0,7))
    def test_usb_suspend_on(self):
        self.on();D.PowerTest_Set(1,62,1,14);self.step()
        self.assertEqual((D.PowerTest_Output(),self.info().last_off),(0,8))
        D.PowerTest_Set(1,62,1,10);self.step(30);self.assertEqual(D.PowerTest_Output(),0)
    def test_fault_latch_count_reset_and_explicit_restart(self):
        self.on();D.PowerTest_Set(1,62,1,11);self.step(20)
        self.assertEqual((self.info().state,self.info().fault_count,D.PowerTest_Output()),(4,1,0))
        self.assertIn('FAULT_LATCHED',self.command('PWRON'))
        self.assertIn('HW_FAULT',self.command('PWRRESET'))
        D.PowerTest_Set(1,62,1,10);self.step(20)
        self.assertIn('PWRRESET=OK',self.command('PWRRESET'));self.assertEqual(self.info().state,0)
        self.step(20);self.assertEqual((self.info().state,D.PowerTest_Output()),(0,0))
        self.assertIn('PWRON=OK',self.command('PWRON'))
    def test_fault_active_while_off_latches(self):
        D.PowerTest_Set(1,62,1,11);self.ready();self.assertEqual(self.info().state,4)
        self.assertEqual(D.PowerTest_Output(),0)
    def test_software_overcurrent_uses_unfiltered_measurement(self):
        self.on();D.PowerTest_Set(1,185,1,10);self.step(12)
        s=self.info();self.assertEqual((s.state,s.last_off),(4,11));self.assertLess(s.current.filtered_ma,100)
    def test_invalid_adc_shuts_down_and_latches(self):
        self.on();D.PowerTest_Set(1,62,-1,10);self.step(12)
        self.assertEqual((self.info().state,self.info().last_off),(4,9))
    def test_adc_timeout_shuts_down(self):
        self.on();D.PowerTest_Set(1,62,0,10);self.step(25)
        self.assertEqual(D.PowerTest_Output(),0);self.assertFalse(self.info().current.valid)
    def test_saturation_shuts_down(self):
        self.on();D.PowerTest_Set(1,4095,1,10);self.step(12)
        self.assertEqual(D.PowerTest_Output(),0);self.assertTrue(self.info().current.saturated)
    def test_service_gap_shuts_down_latched(self):
        self.on();self.now+=100;D.PowerTest_Tick(self.now)
        self.assertEqual((self.info().state,self.info().last_off),(4,12))
    def test_gpio_configuration_failure(self):
        self.on();D.PowerTest_Set(1,62,1,2);self.step();self.assertEqual((self.info().state,self.info().last_off),(4,14))
    def test_pwroff_and_reset_never_turn_on(self):
        self.on();self.command('PWROFF');self.step(30)
        self.assertEqual(D.PowerTest_Output(),0);self.command('PWRRESET');self.step(30)
        self.assertEqual(D.PowerTest_Output(),0)
    def test_pwrtest_is_nondestructive(self):
        self.ready();self.assertIn('END=PWRTEST',self.command('PWRTEST'));self.assertEqual(D.PowerTest_Output(),0)
    def test_diagnostic_frames_and_calibration_fields(self):
        self.ready()
        for cmd in ('PWRINFO','CURRENT','PWRTEST'):
            text=self.command(cmd);self.assertTrue(text.startswith('BEGIN='+cmd));self.assertTrue(text.endswith('END='+cmd+'\r\n'))
            self.assertIn('MEASUREMENT=PRELIMINARY',text)
        self.assertIn('VREF_MV=3300',self.command('CURRENT'));self.assertIn('GAIN_PPM=1000000',self.command('CURRENT'))
    def test_off_baseline_not_updated_while_on(self):
        self.on();baseline=self.info().baseline_ma;D.PowerTest_Set(1,500,1,10);self.step(12)
        self.assertEqual(self.info().baseline_ma,baseline)
    def sample(self,raw):
        c=Current();D.CurrentMonitor_Init(C.byref(c));D.CurrentMonitor_Sample(C.byref(c),raw,1,10);return c
    def test_stuck_zero_adc_can_appear_valid_but_needs_physical_proof(self):
        D.PowerTest_Set(1,0,1,10);self.ready()
        self.assertEqual((self.info().current.valid,self.info().current.ma),(1,0))
        self.assertIn('PWRON=OK',self.command('PWRON'))
        # This deliberately documents the sensor's undetectable failure mode.
        # A verified-hardware gate and real ammeter/PA3 test remain mandatory.
    def test_repeated_on_off_and_reset_do_not_auto_restart(self):
        self.ready()
        for _ in range(20):
            self.assertIn('PWRON=OK',self.command('PWRON'))
            self.assertIn('PWRON=OK',self.command('PWRON'))
            self.assertIn('PWROFF=OK',self.command('PWROFF'))
            self.assertIn('PWROFF=OK',self.command('PWROFF'))
            self.assertIn('PWRRESET=OK',self.command('PWRRESET'))
            self.assertIn('PWRRESET=OK',self.command('PWRRESET'))
            self.step(2)
            self.assertEqual(D.PowerTest_Output(),0)
    def test_cc_counters_saturate(self):
        c=CC();D.UsbCDetect_Init(C.byref(c),0)
        c.attaches=c.detaches=0xffffffff
        D.UsbCDetect_Update(C.byref(c),1,0,0)
        D.UsbCDetect_Update(C.byref(c),1,20,0)
        D.UsbCDetect_Update(C.byref(c),3,21,0)
        D.UsbCDetect_Update(C.byref(c),3,41,0)
        self.assertEqual((c.attaches,c.detaches),(0xffffffff,0xffffffff))
    def test_fault_counter_saturates_without_clearing_latch(self):
        self.ready();D.PowerManager_Info().contents.fault_count=0xffffffff
        D.PowerTest_Set(1,62,1,11);self.step()
        self.assertEqual((self.info().fault_count,self.info().state),(0xffffffff,4))
        D.PowerTest_Set(1,62,1,10);self.step()
        self.assertIn('PWRRESET=OK',self.command('PWRRESET'))
        self.assertEqual(D.PowerTest_Output(),0)
    def test_current_sample_counter_saturates_without_affecting_protection(self):
        c=self.sample(62)
        c.samples=0xffffffff
        D.CurrentMonitor_Sample(C.byref(c),185,1,20)
        self.assertEqual(c.samples,0xffffffff)
        self.assertTrue(c.valid)
        self.assertGreater(c.ma,100)
    def test_main_tick_wrap_with_qualification(self):
        D.PowerTest_Init();self.now=0xfffffff0
        D.PowerTest_Set(1,62,1,10)
        self.step(35)
        self.assertEqual((self.info().cc.valid,self.info().budget_ma),(1,100))
        self.assertIn('PWRON=OK',self.command('PWRON'))
        D.PowerTest_Set(3,62,1,10);self.step()
        self.assertEqual(D.PowerTest_Output(),0)
    def test_adc_zero_is_valid_without_claiming_wire_integrity(self):
        c=self.sample(0);self.assertEqual((c.valid,c.ma,c.mv),(1,0,0))
    def test_adc_nominal_transfer_and_statistics(self):
        c=self.sample(1241);self.assertEqual((c.ma,c.mv),(1000,1000))
        D.CurrentMonitor_Sample(C.byref(c),0,1,20)
        self.assertEqual((c.min_ma,c.max_ma,c.mean_ma,c.filtered_ma),(0,1000,500,875))
    def test_adc_bad_range_and_saturation(self):
        for raw in (4090,4095,5000):self.assertFalse(self.sample(raw).valid)
    def test_calibration_range_and_zero_correction(self):
        c=Current();D.CurrentMonitor_Init(C.byref(c))
        self.assertFalse(D.CurrentMonitor_Calibrate(C.byref(c),5000,0,1000000))
        self.assertTrue(D.CurrentMonitor_Calibrate(C.byref(c),3300,10,1100000))
        D.CurrentMonitor_Sample(C.byref(c),1241,1,10);self.assertEqual(c.ma,1089)
    def test_adc_implausible_negative_correction(self):
        c=Current();D.CurrentMonitor_Init(C.byref(c));D.CurrentMonitor_Calibrate(C.byref(c),3300,100,1000000)
        D.CurrentMonitor_Sample(C.byref(c),0,1,10);self.assertFalse(c.valid)
    def test_adc_age_timeout(self):
        c=self.sample(62);D.CurrentMonitor_Age(C.byref(c),61);self.assertFalse(c.valid)
if __name__=='__main__':unittest.main(verbosity=2)

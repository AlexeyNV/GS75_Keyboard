"""Reject unsafe build flag combinations before creating firmware artifacts."""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
CMAKE=sys.argv.pop(1)
class BuildFlagTests(unittest.TestCase):
    def check_rejected(self,flags,message):
        with tempfile.TemporaryDirectory(dir=ROOT/'build',prefix='invalid-power-') as output:
            command=[CMAKE,'-S',str(ROOT),'-B',output,'-G','Ninja',
                     '-DCMAKE_TOOLCHAIN_FILE='+str(ROOT/'cmake/gcc-arm-none-eabi.cmake')]
            command += ['-D'+k+'='+v for k,v in flags.items()]
            result=subprocess.run(command,capture_output=True,text=True,timeout=90)
            self.assertNotEqual(result.returncode,0,result.stdout)
            self.assertIn(message,result.stderr+result.stdout)
    def test_bench_without_rev2_is_rejected(self):
        self.check_rejected({'GS75_BOARD_REV2':'OFF','GS75_RGB_POWER_BENCH_TEST':'ON'},
                            'Bench power testing requires GS75_BOARD_REV2=ON')
    def test_verified_without_bench_is_rejected(self):
        self.check_rejected({'GS75_BOARD_REV2':'ON','GS75_RGB_POWER_BENCH_TEST':'OFF',
                             'GS75_POWER_HARDWARE_VERIFIED':'ON'},
                            'Hardware acknowledgement requires GS75_RGB_POWER_BENCH_TEST=ON')
if __name__=='__main__':unittest.main(verbosity=2)

import importlib.util
from pathlib import Path
import tempfile
import unittest

SPEC=importlib.util.spec_from_file_location('analyze_presentmon',
    Path(__file__).resolve().parents[1]/'tools/analyze_presentmon.py')
MODULE=importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class PresentMonTests(unittest.TestCase):
    def test_complete_ordered_api_capture_is_not_scanout_proof(self):
        self.check('1000.00005\n1000.01675\n',True)

    def test_missing_extra_and_reversed_submissions_fail(self):
        for values in ('1000.00005\n', '1000.00005\n1000.00006\n1000.01675\n',
                       '1000.01675\n1000.00005\n'):
            self.check(values,False)

    def check(self,values,expected):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'trace.csv'
            path.write_text('ProcessID,Runtime,QPCTime,SwapChainAddress\n'+
                ''.join('42,DXGI,'+v+',0x1\n' for v in values.splitlines()))
            frames=[{'submit_qpc_ms':1000000+x,'present_end_qpc_ms':1000000+x+.2}
                    for x in (0,16.7)]
            result=MODULE.analyze(path,42,frames)
            self.assertEqual(result['passed'],expected,result)
            self.assertFalse(result['display_verified'])

    def test_missing_csv_and_clock_do_not_pass(self):
        self.assertFalse(MODULE.analyze('no-such-trace.csv',42,[])['passed'])

import sys
from pathlib import Path
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from presentmon_report import summarize_presentmon

class PresentMonTests(unittest.TestCase):
    def parse(self,body):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'pm.csv'
            p.write_text('ProcessID,SwapChainAddress,Dropped,MsBetweenPresents,MsBetweenDisplayChange\n'+body)
            return summarize_presentmon(p,10)
    def test_dropped_frames_do_not_become_displayed_frames(self):
        r=self.parse('10,0x1,0,8,16\n10,0x1,1,8,0\n10,0x1,0,8,16\n')
        s=r['streams'][0]
        self.assertEqual(s['dropped'],1); self.assertEqual(s['displayed_fps'],62.5)
        self.assertEqual(s['submitted_interval']['count'],3)
        self.assertEqual(s['displayed_interval']['count'],2)
        self.assertIsNone(r['input_to_photon_ms'])
    def test_streams_separated_and_missing_display_is_not_zero(self):
        r=self.parse('10,0x1,0,8,NA\n10,0x2,0,10,20\n99,0x3,0,1,1\n')
        self.assertEqual(len(r['streams']),2)
        self.assertIsNone(r['streams'][0]['displayed_fps'])
    def test_nonfinite_rejected(self):
        with self.assertRaises(ValueError): self.parse('10,0x1,0,NaN,16\n')
    def test_real_v1_header_and_bom(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'pm.csv'
            p.write_text('Application,ProcessID,SwapChainAddress,Dropped,msBetweenPresents,msBetweenDisplayChange\ngame.exe,10,0x1,0,8,16\n',encoding='utf-8-sig')
            r=summarize_presentmon(p,10)
            self.assertTrue(r['display_tracking_available'])
            self.assertEqual(r['streams'][0]['displayed_fps'],62.5)
    def test_minimal_capture_has_no_display_measurement(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'pm.csv'
            p.write_text('ProcessID,SwapChainAddress,Dropped,msBetweenPresents\n10,0x1,0,8\n',encoding='utf-8-sig')
            r=summarize_presentmon(p,10)
            self.assertFalse(r['display_tracking_available'])
            self.assertIsNone(r['streams'][0]['displayed_fps'])
            self.assertEqual(r['streams'][0]['submitted_interval']['count'],1)

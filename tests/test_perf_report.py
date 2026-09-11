import importlib.util
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('perf_report',Path(__file__).resolve().parents[1]/'tools/perf_report.py')
report=importlib.util.module_from_spec(spec); spec.loader.exec_module(report)

class PerfReportTests(unittest.TestCase):
    def analyze(self,rows,**kwargs):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'trace.csv'
            p.write_text('event,start_qpc,end_qpc,frequency,thread,a,b\n'+rows,encoding='utf-8')
            return report.summarize(p,**kwargs)
    def test_durations_are_not_frame_intervals_or_display_latency(self):
        r=self.analyze('gpu_present,100,102,1000,1,0,0\ngpu_present,120,123,1000,1,0,0\ntrace_dropped,0,0,1000,0,0,0\n')
        self.assertEqual(r['durations']['gpu_present']['mean_ms'],2.5)
        self.assertEqual(r['calls']['gpu_present']['call_rate_hz'],50)
        self.assertIsNone(r['displayed_fps']); self.assertIsNone(r['input_to_photon_ms'])
        self.assertTrue(r['valid'])
    def test_loss_invalidates_trace(self):
        r=self.analyze('gdi_blit,100,104,1000,1,0,0\ntrace_dropped,0,0,1000,0,8,0\n')
        self.assertFalse(r['valid']); self.assertEqual(r['dropped'],8)
    def test_warmup_and_shutdown_are_excluded(self):
        r=self.analyze('gdi_blit,0,99,1000,1,0,0\ngdi_blit,100,104,1000,1,0,0\ngdi_blit,190,201,1000,1,0,0\n',start_qpc=100,end_qpc=200)
        self.assertEqual(r['durations']['gdi_blit']['count'],1)
    def test_invalid_clock_rejected(self):
        with self.assertRaises(ValueError): self.analyze('gdi_blit,10,9,1000,1,0,0\n')
        with self.assertRaises(ValueError): self.analyze('gdi_blit,0,1,0,1,0,0\n')
    def test_empty_is_not_zero_latency_success(self):
        self.assertFalse(self.analyze('')['valid'])
    def test_missing_footer_is_incomplete(self):
        self.assertFalse(self.analyze('gdi_blit,0,1,1000,1,0,0\n')['valid'])
    def test_request_footprints_use_each_surface_size_and_keep_unknowns(self):
        r=self.analyze('request_blt,1,2,1000,1,25,100\nrequest_blt,2,4,1000,1,400,400\nrequest_blt,4,5,1000,1,0,100\nrequest_flip,5,5,1000,1,0,0\ntrace_dropped,0,0,1000,0,0,0\n')
        blt=r['output_requests']['request_blt']
        self.assertEqual(blt['at_most_quarter_area_count'],1)
        self.assertEqual(blt['full_area_count'],1)
        self.assertEqual(blt['zero_area_count'],1)
        self.assertAlmostEqual(blt['mean_requested_percent'],125/3)
        self.assertEqual(blt['total_requested_pixels'],425)
        self.assertEqual(blt['cpu_duration']['max_ms'],2)
        old=r['output_requests']['request_flip']
        self.assertEqual(old['unknown_area_count'],1)
        self.assertIsNone(old['mean_requested_percent'])
    def test_invalid_footprint_rejected(self):
        with self.assertRaises(ValueError): self.analyze('request_unlock,1,2,1000,1,101,100\n')

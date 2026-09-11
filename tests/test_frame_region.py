import sys
from pathlib import Path
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from perf_report import region_summary

class RegionTests(unittest.TestCase):
    def test_repeat_then_change(self):
        rows=[(0,'frame_region_change',1,10),(50,'frame_region_change',0,10),
              (100,'frame_region_change',0,10),(150,'frame_region_change',4,10)]
        r=region_summary(rows,1000)
        self.assertEqual(r['max_observed_unchanged_span_ms'],100)
        self.assertEqual(r['change_interval_ms']['max_ms'],150)
        self.assertEqual(r['mean_changed_sample_percent'],12.5)

    def test_exclusion_breaks_gap(self):
        rows=[(0,'frame_region_change',1,10),(50,'frame_region_unavailable',1,0),
              (500,'frame_region_baseline',0,0),(550,'frame_region_change',1,10)]
        r=region_summary(rows,1000)
        self.assertIsNone(r['change_interval_ms'])
        self.assertEqual(r['unavailable_samples'],1)

    def test_absent_and_invalid(self):
        self.assertFalse(region_summary([],None)['available'])
        with self.assertRaises(ValueError):
            region_summary([(1,'frame_region_change',11,10)],1000)

if __name__=='__main__': unittest.main()

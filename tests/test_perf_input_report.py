import json
import sys
import tempfile
import unittest
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from perf_input_report import summarize_input


class InputReportTests(unittest.TestCase):
    def capture(self, root, events, rows, footer=True):
        trace=root/'trace.csv'; injections=root/'injections.json'
        trace.write_text('event,start_qpc,end_qpc,frequency,thread,a,b\n'+rows+
            ('trace_dropped,0,0,1000,0,0,0\n' if footer else ''))
        injections.write_text(json.dumps(dict(columns=['before_qpc','after_qpc','screen_x','screen_y','sequence'],events=events)))
        return summarize_input(trace,injections,0,1000)

    def test_bounds_include_delivery_during_send_and_coalescing(self):
        with tempfile.TemporaryDirectory() as d:
            r=self.capture(Path(d),[[100,110,0,0,1],[120,130,0,0,2],[150,160,0,0,3]],
                'mouse_injected_entry,105,105,1000,1,1,0\nmouse_injected_entry,170,170,1000,1,3,0\n')
            self.assertEqual(r['matched'],2); self.assertEqual(r['unobserved_sequences'],1)
            self.assertEqual(r['delivery_lower_bound_ms']['max_ms'],10)
            self.assertEqual(r['delivery_upper_bound_ms']['max_ms'],20)
            self.assertEqual(r['delivery_lower_bound_ms']['p50_ms'],0)
            self.assertIsNone(r['input_to_photon_ms'])

    def test_duplicates_and_impossible_order_are_excluded(self):
        with tempfile.TemporaryDirectory() as d:
            r=self.capture(Path(d),[[100,110,0,0,1],[200,210,0,0,2]],
                'mouse_injected_entry,115,115,1000,1,1,0\nmouse_injected_entry,120,120,1000,1,1,0\n'
                'mouse_injected_entry,190,190,1000,1,2,0\nmouse_injected_entry,300,300,1000,1,9,0\n')
            self.assertFalse(r['available']); self.assertEqual(r['ambiguous_sequences'],1)
            self.assertEqual(r['invalid_order_sequences'],1); self.assertEqual(r['unmatched_entries'],1)

    def test_incomplete_trace_and_duplicate_injections_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            rows='mouse_injected_entry,120,120,1000,1,1,0\n'
            with self.assertRaisesRegex(ValueError,'complete'):
                self.capture(Path(d),[[100,110,0,0,1]],rows,False)
            with self.assertRaisesRegex(ValueError,'duplicate'):
                self.capture(Path(d),[[100,110,0,0,1],[110,120,0,0,1]],rows)
            with self.assertRaisesRegex(ValueError,'duplicate'):
                self.capture(Path(d),[[-20,-10,0,0,1],[110,120,0,0,1]],rows)

    def test_no_tags_is_unavailable_not_zero_latency(self):
        with tempfile.TemporaryDirectory() as d:
            r=self.capture(Path(d),[[100,110,0,0,1]],'gpu_present,120,125,1000,1,0,0\n')
            self.assertFalse(r['available']); self.assertEqual(r['unobserved_sequences'],1)
            self.assertIsNone(r['delivery_upper_bound_ms'])

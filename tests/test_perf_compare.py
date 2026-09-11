import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('perf_compare',Path(__file__).resolve().parents[1]/'tools/perf_compare.py')
report=importlib.util.module_from_spec(spec); spec.loader.exec_module(report)

class CompareTests(unittest.TestCase):
    def test_invalid_runs_are_excluded_and_repeats_not_pooled(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'manifest.json').write_text(json.dumps({'jobs':[{}, {}, {}, {}]}))
            for i,v in enumerate((2,8,100)):
                run=root/f'run-{i:02d}'; run.mkdir()
                data=dict(valid=i<2,renderer='auto',scaling='nearest',vsync=0,fullscreen=0,
                    summary=dict(valid=True,durations={'output_attempt':{'p95_ms':v}},calls={'gpu_present':{'call_rate_hz':60}}))
                if i==2: data['foreground_lost']=True
                (run/'result.json').write_text(json.dumps(data))
            r=report.compare(root)
            self.assertEqual(r['conditions'][0]['median_run_p95_cpu_ms'],5)
            self.assertEqual(r['valid_runs'],2); self.assertTrue(r['incomplete'])
            self.assertEqual(r['invalid_runs'][0]['reasons'],['foreground_lost'])
            self.assertIsNone(r['displayed_fps']); self.assertIn('Not pooled p95',report.render(r))
    def test_empty_report_renders(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'manifest.json').write_text('{"jobs":[{}]}')
            r=report.compare(root)
            self.assertEqual(r['conditions'],[]); self.assertTrue(r['incomplete'])
            self.assertIn('Completed 0/1',report.render(r))
    def test_cursor_sweep_is_not_pooled_or_paired_with_unscripted_runs(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'manifest.json').write_text('{"jobs":[{},{},{}]}')
            for i,(sweep,trace,cpu) in enumerate(((True,True,30),(False,True,20),(False,False,18))):
                run=root/f'run-{i:02d}'; run.mkdir()
                (run/'result.json').write_text(json.dumps(dict(valid=True,renderer='auto',scaling='nearest',
                    vsync=0,fullscreen=0,cursor_sweep=sweep,trace_enabled=trace,cpu_percent_one_core=cpu)))
            r=report.compare(root)
            self.assertEqual(len(r['conditions']),3)
            self.assertEqual(len(r['trace_comparisons']),1)
            self.assertFalse(r['trace_comparisons'][0]['cursor_sweep'])
            self.assertEqual(r['trace_comparisons'][0]['cpu_delta_percentage_points'],2)
    def test_trace_off_is_separate_and_has_no_internal_timings(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'manifest.json').write_text('{"jobs":[{}]}')
            run=root/'run-00'; run.mkdir()
            (run/'result.json').write_text(json.dumps(dict(valid=True,renderer='auto',scaling='sharp-bilinear',vsync=0,
                fullscreen=1,trace_enabled=False,cpu_percent_one_core=24)))
            r=report.compare(root); condition=r['conditions'][0]
            self.assertFalse(condition['trace_enabled']); self.assertIsNone(condition['median_run_p95_cpu_ms'])
            self.assertEqual(condition['median_cpu_percent_one_core'],24)
    def test_trace_comparison_matches_condition_and_excludes_geometry_change(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); (root/'manifest.json').write_text('{"jobs":[{},{},{},{}]}')
            for i,(enabled,cpu,scaling,valid) in enumerate(((True,26,'nearest',True),
                    (False,24,'nearest',True),(False,80,'bilinear',True),(True,99,'nearest',False))):
                run=root/f'run-{i:02d}'; run.mkdir()
                data=dict(valid=valid,renderer='auto',scaling=scaling,vsync=0,fullscreen=1,
                    trace_enabled=enabled,cpu_percent_one_core=cpu,summary={'valid':True})
                if not valid: data['geometry_changed']=True
                (run/'result.json').write_text(json.dumps(data))
            r=report.compare(root)
            self.assertEqual(len(r['trace_comparisons']),1)
            self.assertEqual(r['trace_comparisons'][0]['cpu_delta_percentage_points'],2)
            self.assertEqual(r['trace_comparisons'][0]['on_runs'],1)
            self.assertEqual(r['invalid_runs'][0]['reasons'],['geometry_changed'])

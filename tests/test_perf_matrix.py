import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from perf_matrix import select_jobs, save_cursor_capture

RUNNER=Path(__file__).resolve().parents[1]/'tools/perf_matrix.py'

class PerfMatrixTests(unittest.TestCase):
    def test_cursor_save_failure_preserves_capture_error_and_restores(self):
        sweep=Mock(events=[(1,2,3,4,1)]); result={'error':'Game lost foreground'}
        with patch.object(Path,'write_text',side_effect=OSError('disk full')):
            save_cursor_capture(sweep,Path('unused'),result)
        sweep.restore.assert_called_once()
        self.assertEqual(result['cursor_injections'],1)
        self.assertIn('Game lost foreground',result['error'])
        self.assertIn('disk full',result['error'])
    def test_cursor_restore_failure_does_not_escape_cleanup(self):
        sweep=Mock(events=[]); sweep.restore.side_effect=RuntimeError('cursor unavailable')
        with tempfile.TemporaryDirectory() as d:
            result={}; save_cursor_capture(sweep,Path(d),result)
            self.assertEqual(result['cursor_restore_error'],'cursor unavailable')
            self.assertEqual(json.loads((Path(d)/'cursor-injections.json').read_text())['events'],[])
    def test_trace_pairs_and_selected_conditions(self):
        jobs=select_jobs(3,['auto:sharp-bilinear:0:1'],False,'both')
        self.assertEqual(len(jobs),6)
        self.assertEqual(sum(j['trace_enabled'] for j in jobs),3)
        self.assertEqual({j['repeat'] for j in jobs},{0,1,2})
        with self.assertRaises(ValueError): select_jobs(1,['auto:typo:0:1'],False,'on')
        with self.assertRaises(ValueError): select_jobs(1,['auto:sharp-bilinear:0:1'],True,'on')
    def test_dry_run_covers_matrix_without_writes(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); game=root/'game'; game.mkdir()
            exe=game/'client.exe'; exe.write_bytes(b'fixture')
            dll=root/'hqcdd.dll'; dll.write_bytes(b'fixture dll')
            out=root/'results'
            command=[sys.executable,str(RUNNER),'--exe',str(exe),'--dll',str(dll),'--output',str(out),'--repeats','1']
            result=subprocess.run(command,capture_output=True,text=True,check=True)
            plan=json.loads(result.stdout)
            self.assertEqual(len(plan['jobs']),10)
            self.assertEqual(set(plan['tools_sha256']),{'perf_matrix.py','perf_report.py','perf_compare.py',
                'presentmon_report.py','perf_cursor.py','perf_input_report.py'})
            self.assertEqual(plan['tools_sha256']['perf_matrix.py'],plan['runner_sha256'])
            self.assertTrue(all(len(value)==64 for value in plan['tools_sha256'].values()))
            self.assertEqual(len({(j['renderer'],j['scaling'],j['vsync'],j['fullscreen']) for j in plan['jobs']}),10)
            self.assertFalse(out.exists()); self.assertEqual(list(game.iterdir()),[exe])
            self.assertEqual(result.stdout,subprocess.run(command,capture_output=True,text=True,check=True).stdout)
            invalid=subprocess.run(command+['--presentmon-api-only'],capture_output=True,text=True)
            self.assertNotEqual(invalid.returncode,0)
            self.assertIn('requires --presentmon',invalid.stderr)
            api_only=subprocess.run(command+['--presentmon',str(root/'PresentMon.exe'),'--presentmon-api-only'],capture_output=True,text=True,check=True)
            self.assertEqual(json.loads(api_only.stdout)['presentmon_tracking'],'api-only')
            self.assertFalse(out.exists())
    def test_existing_or_nested_output_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d); exe=root/'client.exe'; exe.write_bytes(b'fixture')
            dll=root/'hqcdd.dll'; dll.write_bytes(b'fixture dll')
            for out in (root,root/'nested'):
                r=subprocess.run([sys.executable,str(RUNNER),'--exe',str(exe),'--dll',str(dll),'--output',str(out)],capture_output=True,text=True)
                self.assertNotEqual(r.returncode,0)
                self.assertIn('Output must be new',r.stderr)

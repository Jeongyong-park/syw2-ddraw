import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from perf_matrix import select_jobs

RUNNER=Path(__file__).resolve().parents[1]/'tools/perf_matrix.py'

class PerfMatrixTests(unittest.TestCase):
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

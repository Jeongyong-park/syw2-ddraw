"""Interactive Windows smoke test for opt-in diagnostics; not a performance benchmark."""
import json
import csv
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from perf_report import summarize

def main():
    with tempfile.TemporaryDirectory(prefix='hqcdd-perf-') as temporary:
        root=Path(temporary)
        dll=root/'hqcdd.dll'; shutil.copy2(ROOT/'build/Release/hqcdd.dll',dll)
        exe=ROOT/'build/Release/wrapper_test.exe'
        env=os.environ.copy(); env.pop('HQCDD_PERF_FILE',None)
        subprocess.run([str(exe),str(dll)],env=env,check=True,timeout=30)
        assert not list(root.glob('*.csv'))
        trace=root/'trace.csv'; env['HQCDD_PERF_FILE']=str(trace)
        subprocess.run([str(exe),str(dll)],env=env,check=True,timeout=30)
        summary=summarize(trace)
        assert summary['valid'],summary
        assert summary['trace_finished_qpc']>0,summary
        for name in ('gpu_present','gdi_blit','output_attempt','mouse_mapped'):
            assert name in summary['durations'],(name,summary)
        assert summary['displayed_fps'] is None and summary['input_to_photon_ms'] is None
        with trace.open(encoding='utf-8',newline='') as f: rows=list(csv.DictReader(f))
        def footprint(name,area,total):
            return sum(r['event']==name and int(r['a'])==area and int(r['b'])==total for r in rows)
        assert footprint('request_unlock',12,64*48)==1 # readonly lock must not add a request
        assert footprint('request_blt_fast',4,64*48)>=1
        assert footprint('request_blt',1,64*48)>=1 # requested rectangle clipped to primary bounds
        assert summary['output_requests']['request_flip']['full_area_count']>0
        out=ROOT/'output/perf-smoke'; out.mkdir(parents=True,exist_ok=True)
        shutil.copy2(trace,out/'trace.csv')
        (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        print('PASS: disabled produces no CSV; enabled captures GPU/GDI/input and normal teardown; no display-latency claims')

if __name__=='__main__': main()

"""Parse PresentMon --v1_metrics CSV, keeping PID/swap-chain streams separate."""
import csv
import math
from pathlib import Path
from .perf_report import stats


def positive(value):
    if value is None or value.strip().upper() in ('','NA','N/A'): return None
    number=float(value)
    if not math.isfinite(number): raise ValueError('Non-finite PresentMon metric')
    return number if number>0 else None


def summarize_presentmon(path,pid):
    streams={}
    with Path(path).open(encoding='utf-8-sig',newline='') as f:
        reader=csv.DictReader(f)
        fields=set(reader.fieldnames or [])
        required={'ProcessID','SwapChainAddress','Dropped'}
        # Actual v1 CSV uses lowercase "ms"; accept older fixture capitalization too.
        submitted_column=next((k for k in ('msBetweenPresents','MsBetweenPresents') if k in fields),None)
        display_column=next((k for k in ('msBetweenDisplayChange','MsBetweenDisplayChange') if k in fields),None)
        if not required.issubset(fields) or submitted_column is None:
            raise ValueError('Expected PresentMon v1 metric columns')
        for row in reader:
            if int(row['ProcessID'])!=pid: continue
            key=row['SwapChainAddress']
            stream=streams.setdefault(key,dict(rows=0,dropped=0,submitted=[],displayed=[],modes=set()))
            stream['rows']+=1; stream['modes'].add(row.get('PresentMode','unknown'))
            dropped=row['Dropped'].strip()
            if dropped not in ('0','1'): raise ValueError('Invalid Dropped flag')
            stream['dropped']+=int(dropped)
            interval=positive(row[submitted_column])
            if interval is not None: stream['submitted'].append(interval)
            interval=positive(row.get(display_column)) if display_column else None
            if dropped=='0' and interval is not None: stream['displayed'].append(interval)
    result=[]
    for key,s in sorted(streams.items()):
        displayed=s['displayed']; submitted=s['submitted']
        result.append(dict(swap_chain=key,rows=s['rows'],dropped=s['dropped'],present_modes=sorted(s['modes']),
            submitted_interval=stats(submitted),displayed_interval=stats(displayed),
            displayed_fps=len(displayed)*1000/sum(displayed) if displayed else None))
    return dict(pid=pid,streams=result,display_tracking_available=display_column is not None,input_to_photon_ms=None,
        note='ETW display-change estimate per swap chain, not camera-measured latency. Capture window may differ from internal QPC window.')

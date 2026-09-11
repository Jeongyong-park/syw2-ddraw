"""Summarize HQCDD CPU trace durations. Never infer display FPS or photon latency."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def stats(values):
    values = sorted(values)
    if not values:
        return None
    def percentile(p):
        return values[max(0, math.ceil(len(values)*p)-1)]
    return dict(count=len(values), mean_ms=sum(values)/len(values), p50_ms=percentile(.5),
                p95_ms=percentile(.95), p99_ms=percentile(.99), max_ms=values[-1])


def region_summary(events, frequency):
    previous=None; last_change=None; unchanged=0.0; longest=0.0
    gaps=[]; fractions=[]; changed_count=0; unavailable=0; baselines=0
    for t,name,changed,total in sorted(events):
        if name!='frame_region_change':
            previous=last_change=None; unchanged=0.0
            unavailable+=name=='frame_region_unavailable'
            baselines+=name=='frame_region_baseline'
            continue
        if total<=0 or changed<0 or changed>total:
            raise ValueError('Invalid region change count')
        fractions.append(changed*100/total)
        if changed:
            changed_count+=1
            if last_change is not None: gaps.append((t-last_change)*1000/frequency)
            last_change=t; unchanged=0.0
        elif previous is not None:
            unchanged+=(t-previous)*1000/frequency; longest=max(longest,unchanged)
        previous=t
    return dict(available=bool(fractions),compared_samples=len(fractions),changed_samples=changed_count,
        unchanged_samples=len(fractions)-changed_count,unavailable_samples=unavailable,baselines=baselines,
        max_observed_unchanged_span_ms=longest if fractions else None,change_interval_ms=stats(gaps),
        mean_changed_sample_percent=sum(fractions)/len(fractions) if fractions else None,
        max_changed_sample_percent=max(fractions) if fractions else None,
        note='Source RGB on a two-pixel grid before output; not displayed frames, unit motion or simulation ticks. Gaps reset at unavailable/baseline records. Static terrain and camera/palette changes affect this metric.')


def summarize(path, start_qpc=None, end_qpc=None):
    groups=defaultdict(list); starts=defaultdict(list); dropped=0; frequency=None
    gpu_settings=set(); output_settings=set(); footer=False; failures=0; finished=None
    requests=defaultdict(list); region=[]
    with Path(path).open(encoding='utf-8',newline='') as f:
        for row in csv.DictReader(f):
            hz=int(row['frequency'])
            if hz<=0 or (frequency is not None and hz!=frequency):
                raise ValueError('Invalid or inconsistent QPC frequency')
            frequency=hz
            if row['event']=='trace_dropped':
                if footer: raise ValueError('Duplicate trace footer')
                stamp=int(row['start_qpc'])
                if stamp<0 or int(row['end_qpc'])!=stamp: raise ValueError('Invalid trace finish timestamp')
                finished=stamp or None # Legacy traces have no finish timestamp.
                dropped+=int(row['a']); footer=True; continue
            a,b=int(row['start_qpc']),int(row['end_qpc'])
            if b<a: raise ValueError('Negative duration')
            if start_qpc is not None and a<start_qpc: continue
            if end_qpc is not None and b>end_qpc: continue
            groups[row['event']].append((b-a)*1000/hz)
            starts[row['event']].append(a)
            if row['event'] in ('frame_region_change','frame_region_baseline','frame_region_unavailable'):
                region.append((a,row['event'],int(row['a']),int(row['b'])))
            if row['event']=='gpu_output': gpu_settings.add((int(row['a']),int(row['b'])))
            if row['event']=='output_attempt': output_settings.add((int(row['a']),int(row['b'])))
            if row['event']=='gpu_present_result' and int(row['a'])<0: failures+=1
            if row['event'].startswith('request_'):
                area,total=int(row['a']),int(row['b'])
                if area<0 or total<0 or area>total: raise ValueError('Invalid request footprint')
                requests[row['event']].append((area,total))
    intervals={}
    for name in ('gpu_present','gdi_blit'):
        times=sorted(starts[name])
        gaps=[(b-a)*1000/frequency for a,b in zip(times,times[1:])]
        intervals[name]=dict(interval=stats(gaps),
            call_rate_hz=1000/(sum(gaps)/len(gaps)) if gaps and sum(gaps)>0 else None,
            over_50ms=sum(x>50 for x in gaps))
    request_summary={}
    for name,footprints in sorted(requests.items()):
        known=[(area,total) for area,total in footprints if total>0]
        request_summary[name]=dict(count=len(footprints),known_area_count=len(known),
            unknown_area_count=len(footprints)-len(known),
            zero_area_count=sum(area==0 for area,total in known),
            full_area_count=sum(area==total for area,total in known),
            at_most_quarter_area_count=sum(0<area*4<=total for area,total in known),
            mean_requested_percent=sum(area*100/total for area,total in known)/len(known) if known else None,
            total_requested_pixels=sum(area for area,total in known),
            cpu_duration=stats(groups[name]))
    covers_end=None if finished is None or end_qpc is None else finished>=end_qpc
    return dict(valid=bool(groups) and dropped==0 and footer and failures==0 and covers_end is not False,dropped=dropped,
        trace_finished_qpc=finished,covers_capture_end=covers_end,
        gpu_present_failures=failures,gpu_settings=sorted(gpu_settings),output_settings=sorted(output_settings),
        durations={k:stats(v) for k,v in sorted(groups.items())},calls=intervals,
        output_requests=request_summary,
        frame_region=region_summary(region,frequency),
        request_note='Primary output request footprints, including skipped attempts; not changed pixels, upload bytes, or cursor identification. CPU durations overlap output_attempt.',
        displayed_fps=None, input_to_photon_ms=None,
        note='CPU API timings and call intervals only; no frame/input association. Dropped traces are invalid.')


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('csv',type=Path); p.add_argument('--output',type=Path)
    a=p.parse_args(); result=json.dumps(summarize(a.csv),indent=2)
    if a.output: a.output.write_text(result,encoding='utf-8')
    else: print(result)

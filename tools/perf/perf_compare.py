"""Compare recorded runs without treating API call rates as displayed FPS."""
import argparse
import html
import json
from pathlib import Path
import statistics


def compare(root):
    manifest=json.loads((root/'manifest.json').read_text(encoding='utf-8'))
    groups={}; invalid=[]; completed=0
    for path in sorted(root.glob('run-*/result.json')):
        run=json.loads(path.read_text(encoding='utf-8')); completed+=1
        if not run.get('valid'):
            reasons=[k for k in ('error','foreground_lost','forced_exit','settings_mismatch','backend_mismatch','geometry_changed','runtime_settings_changed') if run.get(k)]
            if run.get('trace_enabled',True) and not run.get('summary',{}).get('valid'): reasons.append('invalid_or_missing_trace')
            if run.get('presentmon_exit',0)!=0: reasons.append('presentmon_failed')
            if run.get('presentmon_error'): reasons.append('presentmon_schema_error')
            invalid.append(dict(run=path.parent.name,reasons=reasons or ['invalid_capture']))
            continue
        key=(run['renderer'],run['scaling'],run['vsync'],run['fullscreen'],run.get('trace_enabled',True),run.get('cursor_sweep',False))
        groups.setdefault(key,[]).append(run)
    rows=[]
    for key,runs in sorted(groups.items()):
        event='gdi_blit' if key[0]=='gdi' else 'gpu_present'
        durations=[r.get('summary',{}).get('durations',{}).get('output_attempt') for r in runs]
        durations=[d for d in durations if d]
        rates=[r.get('summary',{}).get('calls',{}).get(event,{}).get('call_rate_hz') for r in runs]
        rates=[v for v in rates if v is not None]
        display_rates=[]
        cpu_rates=[r['cpu_percent_one_core'] for r in runs if 'cpu_percent_one_core' in r]
        for run in runs:
            streams=run.get('presentmon',{}).get('streams',[])
            # Multiple streams require explicit window attribution; never sum them.
            if len(streams)==1 and streams[0].get('displayed_fps') is not None:
                display_rates.append(streams[0]['displayed_fps'])
        rows.append(dict(renderer=key[0],scaling=key[1],vsync=key[2],fullscreen=key[3],trace_enabled=key[4],cursor_sweep=key[5],
            repetitions=len(runs),
            median_run_p95_cpu_ms=statistics.median(d['p95_ms'] for d in durations) if durations else None,
            min_run_p95_cpu_ms=min(d['p95_ms'] for d in durations) if durations else None,
            max_run_p95_cpu_ms=max(d['p95_ms'] for d in durations) if durations else None,
            median_api_calls_hz=statistics.median(rates) if rates else None,
            median_etw_displayed_fps=statistics.median(display_rates) if display_rates else None,
            display_samples=len(display_rates)))
        rows[-1]['median_cpu_percent_one_core']=statistics.median(cpu_rates) if cpu_rates else None
        rows[-1]['min_cpu_percent_one_core']=min(cpu_rates) if cpu_rates else None
        rows[-1]['max_cpu_percent_one_core']=max(cpu_rates) if cpu_rates else None
        delivery=[r['input_delivery'] for r in runs if r.get('input_delivery',{}).get('available')]
        rows[-1]['input_delivery_runs']=len(delivery)
        rows[-1]['input_delivery_missing_runs']=sum(r.get('cursor_sweep',False) and not r.get('input_delivery',{}).get('available',False) for r in runs)
        rows[-1]['input_injected_count']=sum(r.get('input_delivery',{}).get('injected',r.get('cursor_injections',0)) for r in runs)
        rows[-1]['input_matched_count']=sum(r.get('input_delivery',{}).get('matched',0) for r in runs)
        for bound in ('lower','upper'):
            values=[r[f'delivery_{bound}_bound_ms']['p95_ms'] for r in delivery]
            rows[-1][f'median_run_p95_delivery_{bound}_bound_ms']=statistics.median(values) if values else None
    pairs=[]
    for on in rows:
        if not on['trace_enabled']: continue
        off=next((row for row in rows if not row['trace_enabled'] and all(row[k]==on[k] for k in ('renderer','scaling','vsync','fullscreen','cursor_sweep'))),None)
        if off and on['median_cpu_percent_one_core'] is not None and off['median_cpu_percent_one_core'] is not None:
            pairs.append(dict(renderer=on['renderer'],scaling=on['scaling'],vsync=on['vsync'],fullscreen=on['fullscreen'],cursor_sweep=on['cursor_sweep'],
                on_runs=on['repetitions'],off_runs=off['repetitions'],
                cpu_delta_percentage_points=on['median_cpu_percent_one_core']-off['median_cpu_percent_one_core'],
                note='Observed median difference, includes run-to-run noise; not proof of exact instrumentation overhead.'))
    return dict(planned=len(manifest['jobs']),completed=completed,incomplete=completed!=len(manifest['jobs']),
        valid_runs=sum(r['repetitions'] for r in rows),invalid_runs=invalid,conditions=rows,
        trace_comparisons=pairs,
        scope='Median of per-run CPU p95 and API call rates. Not pooled p95, displayed FPS, or input latency.',
        displayed_fps=None,input_to_photon_ms=None)


def render(report):
    rows=report['conditions']; maximum=max((r['max_run_p95_cpu_ms'] or 0 for r in rows),default=1) or 1
    body=[]
    for r in rows:
        label=f"{r['renderer']} / {r['scaling']} / VSync {r['vsync']} / {'fullscreen' if r['fullscreen'] else 'window'} / trace {'on' if r['trace_enabled'] else 'off'}"
        label+=f" / cursor {'sweep' if r.get('cursor_sweep') else 'unscripted'}"
        value=r['median_run_p95_cpu_ms']; width=0 if value is None else value/maximum*100
        rate=r['median_api_calls_hz']
        displayed=r.get('median_etw_displayed_fps')
        cpu=r.get('median_cpu_percent_one_core')
        body.append(f'<tr><td>{html.escape(label)}</td><td>{r["repetitions"]}</td><td>{value if value is not None else "N/A"}</td>'
                    f'<td>{rate if rate is not None else "N/A"}</td><td>{displayed if displayed is not None else "N/A"}</td><td>{cpu if cpu is not None else "N/A"}</td><td><div style="background:#4e8bc6;height:12px;width:{width:.2f}%"></div></td></tr>')
    invalid=''.join(f'<li>{html.escape(r["run"])}: {html.escape(", ".join(r["reasons"]))}</li>' for r in report['invalid_runs'])
    delivery=''.join('<tr><td>'+html.escape(f"{r['renderer']} / {r['scaling']} / VSync {r['vsync']} / fullscreen {r['fullscreen']}")+
        f" / trace {'on' if r['trace_enabled'] else 'off'}</td><td>{r.get('input_delivery_runs',0)}</td><td>{r.get('input_delivery_missing_runs',0)}</td><td>{r.get('input_matched_count',0)}/{r.get('input_injected_count',0)}</td><td>{r.get('median_run_p95_delivery_lower_bound_ms') if r.get('input_delivery_runs') else 'N/A'}</td><td>{r.get('median_run_p95_delivery_upper_bound_ms') if r.get('input_delivery_runs') else 'N/A'}</td></tr>"
        for r in rows if r.get('cursor_sweep') or r.get('input_delivery_runs'))
    return '<!doctype html><meta charset="utf-8"><title>HQCDD performance comparison</title>' \
        '<style>body{font:16px system-ui;max-width:1200px;margin:40px auto;padding:20px}table{border-collapse:collapse;width:100%}td,th{padding:12px;border-bottom:1px solid #ccc;text-align:left}td:last-child{width:20%}</style>' \
        f'<h1>HQCDD CPU timing comparison</h1><p>{html.escape(report["scope"])}</p>' \
        f'<p>Completed {report["completed"]}/{report["planned"]}; valid {report["valid_runs"]}. Incomplete: {report["incomplete"]}.</p>' \
        '<table><tr><th>Condition</th><th>Runs</th><th>Median run p95 CPU ms</th><th>API calls/s</th><th>ETW displayed FPS</th><th>CPU % of one core</th><th>CPU p95 comparison</th></tr>' \
        +''.join(body)+'</table><h2>Synthetic input delivery to wrapper entry</h2><p>Median of per-run p95 bounds in ms; includes OS delivery and scheduling. Not photon latency. Matched tags only; coalesced inputs are unobserved.</p>' \
        '<table><tr><th>Condition</th><th>Measured runs</th><th>Unavailable runs</th><th>Matched/injected</th><th>Lower bound p95</th><th>Upper bound p95</th></tr>'+delivery+'</table><h2>Trace ON/OFF observations</h2><pre>'+html.escape(json.dumps(report.get('trace_comparisons',[]),indent=2))+'</pre><h2>Excluded runs</h2><ul>'+invalid+'</ul>'


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__); p.add_argument('directory',type=Path)
    root=p.parse_args().directory
    report=compare(root)
    (root/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (root/'comparison.html').write_text(render(report),encoding='utf-8')
    print(json.dumps(report,indent=2))

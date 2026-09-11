"""Tagged synthetic input delivery to wrapper entry; never input-to-photon latency."""
import csv
import json
from collections import defaultdict
from pathlib import Path
from .perf_report import stats, summarize


def summarize_input(trace, injections, start_qpc, end_qpc):
    if not summarize(trace,start_qpc,end_qpc)['valid']:
        raise ValueError('Input delivery requires a complete valid trace')
    data=json.loads(Path(injections).read_text(encoding='utf-8'))
    if data['columns']!=['before_qpc','after_qpc','screen_x','screen_y','sequence']:
        raise ValueError('Tagged injection schema required')
    sent={}; seen=set()
    for before,after,x,y,sequence in data['events']:
        if not 1<=sequence<=65535 or sequence in seen or after<before:
            raise ValueError('Invalid or duplicate injection sequence')
        seen.add(sequence)
        if start_qpc<=before<=after<=end_qpc: sent[sequence]=(before,after)
    received=defaultdict(list); frequency=None
    with Path(trace).open(encoding='utf-8',newline='') as f:
        for row in csv.DictReader(f):
            if row['event']!='mouse_injected_entry': continue
            timestamp=int(row['start_qpc']); frequency=int(row['frequency'])
            if start_qpc<=timestamp<=end_qpc:
                received[int(row['a'])].append(timestamp)
    lower=[]; upper=[]; unmatched=0; ambiguous=0; invalid=0
    for sequence,times in received.items():
        if sequence not in sent: unmatched+=len(times); continue
        if len(times)!=1: ambiguous+=1; continue
        before,after=sent[sequence]; timestamp=times[0]
        if timestamp<before: invalid+=1; continue
        # Delivery may occur while SendInput is still running on the producer thread.
        lower.append(max(0,timestamp-after)*1000/frequency)
        upper.append((timestamp-before)*1000/frequency)
    return dict(available=bool(upper),injected=len(sent),matched=len(upper),
        unobserved_sequences=len(set(sent)-set(received)),ambiguous_sequences=ambiguous,
        unmatched_entries=unmatched,invalid_order_sequences=invalid,
        delivery_lower_bound_ms=stats(lower),delivery_upper_bound_ms=stats(upper),
        scope='Synthetic injection to wrapper entry before its lock. Includes OS delivery and game scheduling; not pure queue wait or photon latency. Unobserved inputs may be coalesced; not a hardware loss rate.',
        input_to_photon_ms=None)

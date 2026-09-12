"""Windows main-menu comparison runner. Copies the game; never edits the source install.

Dry-run is the default. --run opens game windows; leave them foreground during capture.
Optional cursor sweep moves without clicking. No login, saved-game loading or photon latency measurement.
"""
import argparse
import configparser
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import random
import shutil
import subprocess
import sys
import time
from .perf_report import summarize
from .perf_compare import compare, render
from .presentmon_report import summarize_presentmon
from .perf_cursor import CursorSweep
from .perf_input_report import summarize_input

MODES=[('gdi','nearest',0),('auto','nearest',0),('auto','bilinear',0),
       ('auto','sharp-bilinear',0),('auto','sharp-bilinear',1)]

def presentmon_tracking_args(api_only):
    # Display tracking cannot be disabled independently of GPU/input tracking.
    return ['--no_track_gpu','--no_track_input','--no_track_display'] if api_only else []

def plan(repeats):
    jobs=[dict(renderer=r,scaling=s,vsync=v,fullscreen=f,repeat=i)
          for i in range(repeats) for f in (0,1) for r,s,v in MODES]
    random.Random(600).shuffle(jobs)
    return jobs

def digest(p): return hashlib.sha256(p.read_bytes()).hexdigest()


def backend_mismatch(renderer, run_log):
    if run_log is None: return True
    return renderer=='auto' and ('D3D11 hardware presentation active' not in run_log or 'switching to GDI' in run_log)


def tool_hashes():
    root=Path(__file__).resolve().parent
    return {name:digest(root/name) for name in ('perf_matrix.py','perf_report.py','perf_compare.py',
        'presentmon_report.py','perf_cursor.py','perf_input_report.py')}


def wait_for_scene(run, process, scene, timeout=600):
    (run/'awaiting-ready.json').write_text(json.dumps(dict(pid=process.pid,scene=scene)),encoding='utf-8')
    deadline=time.monotonic()+timeout
    while not (run/'ready.json').exists():
        if process.poll() is not None: raise RuntimeError('Game exited while waiting for scene')
        if time.monotonic()>=deadline: raise RuntimeError('Scene readiness timed out')
        time.sleep(.2)
    ready=json.loads((run/'ready.json').read_text(encoding='utf-8'))
    if ready.get('scene_verified') is not True: raise RuntimeError('Scene was not verified')
    return ready


def save_cursor_capture(sweep, run, result):
    if sweep is None: return
    result['cursor_injections']=len(sweep.events)
    try:
        (run/'cursor-injections.json').write_text(json.dumps(dict(
            columns=['before_qpc','after_qpc','screen_x','screen_y','sequence'],events=sweep.events,
            note='SendInput acceptance timestamps; not game processing or display timestamps'),indent=2),encoding='utf-8')
    except Exception as e:
        result['cursor_capture_error']=str(e)
        result['error']='; '.join(filter(None,(result.get('error'),f'Cursor capture save failed: {e}')))
    finally:
        try: sweep.restore()
        except Exception as e: result['cursor_restore_error']=str(e)

def select_jobs(repeats,cases,window_only,trace_mode):
    jobs=plan(repeats)
    if cases:
        allowed={tuple(case.split(':')) for case in cases}
        jobs=[j for j in jobs if (j['renderer'],j['scaling'],str(j['vsync']),str(j['fullscreen'])) in allowed]
        if len({(j['renderer'],j['scaling'],str(j['vsync']),str(j['fullscreen'])) for j in jobs})!=len(allowed):
            raise ValueError('Unknown --case; use renderer:scaling:vsync:fullscreen')
    if window_only: jobs=[j for j in jobs if not j['fullscreen']]
    traces={'on':[True],'off':[False],'both':[True,False]}[trace_mode]
    jobs=[dict(job,trace_enabled=enabled) for job in jobs for enabled in traces]
    random.Random(601).shuffle(jobs)
    if not jobs: raise ValueError('No selected conditions')
    return jobs

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--exe',type=Path,required=True)
    p.add_argument('--dll',type=Path,default=Path('build/Release/hqcdd.dll'))
    p.add_argument('--output',type=Path,required=True,help='New, nonexistent directory outside the game installation')
    p.add_argument('--repeats',type=int,default=3)
    p.add_argument('--warmup',type=float,default=10)
    p.add_argument('--seconds',type=float,default=60)
    p.add_argument('--presentmon',type=Path)
    p.add_argument('--presentmon-api-only',action='store_true',
                   help='Collect Present API events only; displayed FPS and input latency remain unavailable')
    p.add_argument('--window-only',action='store_true',help='Restrict the matrix to windowed runs')
    p.add_argument('--case',action='append',help='Select renderer:scaling:vsync:fullscreen; repeat to select several')
    p.add_argument('--trace-mode',choices=['on','off','both'],default='on')
    p.add_argument('--cursor-sweep',action='store_true',help='Move cursor horizontally, hold, reverse and hold; abort on focus loss or user movement')
    p.add_argument('--scene',default='main-menu; scene must be verified separately',help='Describe the actual scene and workload')
    p.add_argument('--wait-for-ready',action='store_true',help='Wait up to 600 seconds for each run/ready.json before warmup; JSON must contain scene_verified=true')
    p.add_argument('--run',action='store_true')
    a=p.parse_args()
    if a.presentmon_api_only and not a.presentmon: p.error('--presentmon-api-only requires --presentmon')
    if not 1<=a.repeats<=10 or not 1<=a.seconds<=300 or not 0<=a.warmup<=120:
        p.error('Invalid repeat/duration bounds')
    source=a.exe.resolve(strict=True); dll=a.dll.resolve(strict=True); target=a.output.resolve()
    if not source.is_file() or not dll.is_file(): p.error('EXE/DLL must be files')
    if target.exists() or target.is_relative_to(source.parent): p.error('Output must be new and outside the source game folder')
    try: jobs=select_jobs(a.repeats,a.case,a.window_only,a.trace_mode)
    except ValueError as e: p.error(str(e))
    manifest=dict(scene=a.scene,wait_for_ready=a.wait_for_ready,cursor_sweep=a.cursor_sweep,seed=600,exe_sha256=digest(source),
                  dll_sha256=digest(dll),warmup=a.warmup,seconds=a.seconds,jobs=jobs,
                  runner_sha256=digest(Path(__file__)),
                  tools_sha256=tool_hashes(),python_version=sys.version,
                  presentmon_tracking='api-only' if a.presentmon_api_only else 'full',
                  limits='No 1:1 window sizing, deterministic battle, or photon measurement; synthetic cursor sweep is not hardware latency')
    if not a.run:
        print(json.dumps(manifest,indent=2)); return
    if os.name!='nt': p.error('--run requires Windows')
    # Reject junctions/symlinks before copying so a source link cannot escape the install.
    for root,dirs,files in os.walk(source.parent,followlinks=False):
        for name in dirs+files:
            entry=Path(root)/name
            if entry.lstat().st_file_attributes & 0x400: p.error(f'Reparse point in game folder: {entry}')
    target.mkdir(parents=True)
    game=target/'game'; shutil.copytree(source.parent,game)
    shutil.copy2(dll,game/'ddraw.dll')
    original_ini=(game/'hqcdd.ini').read_bytes() if (game/'hqcdd.ini').exists() else b''
    manifest['source_ini_sha256']=hashlib.sha256(original_ini).hexdigest()
    manifest['source_ini_present']=(game/'hqcdd.ini').exists()
    manifest['platform']=os.sys.getwindowsversion()[:]
    manifest['frame_region_requested']=os.environ.get('HQCDD_PERF_REGION')
    manifest['presentmon']=str(a.presentmon.resolve()) if a.presentmon else None
    if a.presentmon: manifest['presentmon_sha256']=digest(a.presentmon.resolve(strict=True))
    (target/'manifest.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
    u=c.WinDLL('user32',use_last_error=True); k=c.WinDLL('kernel32',use_last_error=True)
    u.GetForegroundWindow.restype=w.HWND
    u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
    u.PostMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM]
    u.GetWindowRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    u.GetClientRect.argtypes=[w.HWND,c.POINTER(w.RECT)]
    u.IsWindowVisible.argtypes=[w.HWND]
    u.GetDpiForWindow.argtypes=[w.HWND]; u.GetDpiForWindow.restype=w.UINT
    # Query window geometry in a per-monitor-aware observer, without changing game DPI awareness.
    u.SetThreadDpiAwarenessContext.argtypes=[c.c_void_p]; u.SetThreadDpiAwarenessContext.restype=c.c_void_p
    observer_context=u.SetThreadDpiAwarenessContext(c.c_void_p(-4))
    geometry_context='per-monitor-v2 observer' if observer_context else 'default observer; potentially virtualized'
    k.QueryPerformanceCounter.argtypes=[c.POINTER(c.c_longlong)]
    k.GetProcessTimes.argtypes=[w.HANDLE,c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME),c.POINTER(w.FILETIME)]
    def cpu_ms(process):
        created,exited,kernel,user=w.FILETIME(),w.FILETIME(),w.FILETIME(),w.FILETIME()
        if not k.GetProcessTimes(int(process._handle),c.byref(created),c.byref(exited),c.byref(kernel),c.byref(user)):
            raise c.WinError(c.get_last_error())
        return sum((f.dwHighDateTime<<32)|f.dwLowDateTime for f in (kernel,user))/10000
    def qpc():
        t=c.c_longlong(); k.QueryPerformanceCounter(c.byref(t)); return t.value
    def windows(pid):
        result=[]
        callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM)
        @callback
        def visit(hwnd,_):
            owner=w.DWORD(); u.GetWindowThreadProcessId(hwnd,c.byref(owner))
            if owner.value==pid: result.append(hwnd)
            return True
        u.EnumWindows(visit,0); return result
    for index,job in enumerate(jobs):
        run=target/f'run-{index:02d}'; run.mkdir()
        ini=configparser.ConfigParser(); ini.read_string(original_ini.decode('utf-8-sig') or '[Display]\n')
        if not ini.has_section('Display'): ini.add_section('Display')
        for key,value in [('Renderer',job['renderer']),('Scaling',job['scaling']),('VSync',job['vsync']),('Fullscreen',job['fullscreen'])]:
            ini.set('Display',key,str(value))
        with (game/'hqcdd.ini').open('w',encoding='utf-8') as f: ini.write(f)
        shutil.copy2(game/'hqcdd.ini',run/'hqcdd.ini')
        settings_hash=digest(run/'hqcdd.ini')
        trace=run/'internal.csv'; env=os.environ.copy(); env.pop('HQCDD_PERF_FILE',None)
        if job['trace_enabled']: env['HQCDD_PERF_FILE']=str(trace)
        log_path=game/'hqcdd.log'; log_start=log_path.stat().st_size if log_path.exists() else 0
        process=subprocess.Popen([str(game/source.name)],cwd=game,env=env)
        monitor=None; monitor_log=None; sweep=None; result=dict(job,pid=process.pid,foreground_lost=False,cursor_sweep=a.cursor_sweep,
            presentmon_tracking=manifest['presentmon_tracking'],settings_sha256=settings_hash,scene=a.scene)
        print(f'Run {index+1}/{len(jobs)}: {job}',flush=True)
        try:
            if a.wait_for_ready:
                result['scene_verification']=wait_for_scene(run,process,a.scene)
            time.sleep(a.warmup)
            if process.poll() is not None: raise RuntimeError('Game exited during warmup')
            visible=[hwnd for hwnd in windows(process.pid) if u.IsWindowVisible(hwnd)]
            if not visible: raise RuntimeError('No visible game window after warmup')
            result['windows']=[]
            for hwnd in visible:
                rect=w.RECT(); client=w.RECT(); u.GetWindowRect(hwnd,c.byref(rect)); u.GetClientRect(hwnd,c.byref(client))
                result['windows'].append(dict(hwnd=hwnd,dpi=u.GetDpiForWindow(hwnd),
                    window_rect=[rect.left,rect.top,rect.right,rect.bottom],
                    client_size=[client.right,client.bottom],
                    coordinate_context=geometry_context))
            capture_log_start=log_path.stat().st_size if log_path.exists() else 0
            if a.presentmon and job['renderer']=='auto':
                monitor_log=(run/'presentmon.log').open('w',encoding='utf-8')
                monitor=subprocess.Popen([str(a.presentmon.resolve()),'--process_id',str(process.pid),
                    '--timed',str(a.seconds),'--terminate_after_timed','--v1_metrics','--no_console_stats',
                    '--session_name',f'HQCDD-{process.pid}-{index}', '--output_file',str(run/'presentmon.csv')]
                    +presentmon_tracking_args(a.presentmon_api_only),
                    creationflags=subprocess.CREATE_NO_WINDOW,stdout=monitor_log,stderr=subprocess.STDOUT)
            if a.cursor_sweep:
                if not observer_context: raise RuntimeError('Cursor sweep requires physical per-monitor coordinates')
                if len(visible)!=1: raise RuntimeError('Cursor sweep requires exactly one visible game window')
                sweep=CursorSweep(u,visible[0],process.pid,qpc)
            start=qpc(); cpu_start=cpu_ms(process); wall_start=time.perf_counter(); until=wall_start+a.seconds
            while time.perf_counter()<until:
                if process.poll() is not None: raise RuntimeError('Game exited during capture')
                if monitor and monitor.poll() is not None and monitor.returncode!=0:
                    raise RuntimeError(f'PresentMon failed with exit {monitor.returncode}; see presentmon.log')
                pid=w.DWORD(); u.GetWindowThreadProcessId(u.GetForegroundWindow(),c.byref(pid))
                if pid.value!=process.pid: result['foreground_lost']=True
                for initial in result['windows']:
                    rect=w.RECT(); u.GetWindowRect(initial['hwnd'],c.byref(rect))
                    if [rect.left,rect.top,rect.right,rect.bottom]!=initial['window_rect']:
                        result['geometry_changed']=True
                if sweep: sweep.step(time.perf_counter()-wall_start)
                time.sleep(.008 if sweep else .1)
            end=qpc(); wall_seconds=time.perf_counter()-wall_start
            result.update(start_qpc=start,end_qpc=end,cpu_ms=cpu_ms(process)-cpu_start,wall_seconds=wall_seconds)
            result['cpu_percent_one_core']=result['cpu_ms']/(wall_seconds*10)
            if log_path.exists():
                with log_path.open('rb') as log: log.seek(capture_log_start); capture_log=log.read().decode('utf-8',errors='replace')
                result['runtime_settings_changed']=any(text in capture_log for text in ('Display settings applied:','Presentation mode:','Display overlay opened'))
        except Exception as e:
            result['error']=str(e)
        finally:
            save_cursor_capture(sweep,run,result)
            for hwnd in windows(process.pid): u.PostMessageW(hwnd,0x10,0,0) # WM_CLOSE, own child only
            try: process.wait(timeout=15)
            except subprocess.TimeoutExpired:
                process.terminate(); process.wait(timeout=10); result['forced_exit']=True
            if monitor:
                try: result['presentmon_exit']=monitor.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    monitor.terminate(); monitor.wait(); result['presentmon_exit']='timeout'
            if monitor_log: monitor_log.close()
        run_log=None
        if log_path.exists():
            with log_path.open('rb') as log: log.seek(log_start); run_log=log.read().decode('utf-8',errors='replace')
            (run/'hqcdd.log').write_text(run_log,encoding='utf-8')
            if not job['trace_enabled']:
                result['settings_mismatch']='Display settings applied:' in run_log or 'switching to GDI' in run_log
        elif not job['trace_enabled']: result['settings_mismatch']=True
        result['backend_mismatch']=backend_mismatch(job['renderer'],run_log)
        if trace.exists() and 'start_qpc' in result:
            try:
                result['summary']=summarize(trace,result['start_qpc'],result['end_qpc'])
                if sweep:
                    result['input_delivery']=summarize_input(trace,run/'cursor-injections.json',result['start_qpc'],result['end_qpc'])
            except (ValueError,KeyError,OSError) as e: result['error']=f'Trace analysis failed: {e}'
        if result.get('summary'):
            summary=result['summary']
            expected_scaling={'nearest':0,'bilinear':1,'sharp-bilinear':2}[job['scaling']]
            expected_gpu=int(job['renderer']=='auto')
            result['settings_mismatch']=summary['output_settings']!=[(expected_gpu,expected_scaling)]
            if expected_gpu:
                result['settings_mismatch'] |= summary['gpu_settings']!=[(job['vsync'],expected_scaling)]
        result['valid']=(bool(result.get('summary',{}).get('valid')) if job['trace_enabled'] else True) and not any(result.get(x) for x in ('error','foreground_lost','forced_exit','geometry_changed','runtime_settings_changed'))
        result['valid'] &= not result.get('settings_mismatch',False) and not result['backend_mismatch']
        if monitor: result['valid'] &= result.get('presentmon_exit')==0 and (run/'presentmon.csv').exists()
        if monitor and result.get('presentmon_exit')==0 and (run/'presentmon.csv').exists():
            try:
                result['presentmon']=summarize_presentmon(run/'presentmon.csv',process.pid)
                if not result['presentmon']['streams']: result['valid']=False
                if not a.presentmon_api_only and not result['presentmon']['display_tracking_available']:
                    result['presentmon_error']='Display tracking columns missing from full capture'
                    result['valid']=False
            except (ValueError,KeyError,OSError) as e:
                result['presentmon_error']=str(e); result['valid']=False
        (run/'result.json').write_text(json.dumps(result,indent=2),encoding='utf-8')
        if result.get('error') or result.get('forced_exit'): break
    report=compare(target)
    (target/'comparison.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    (target/'comparison.html').write_text(render(report),encoding='utf-8')

if __name__=='__main__': main()

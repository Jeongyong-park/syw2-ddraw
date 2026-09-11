"""Read-only timing sampler. Observed counter changes are not exact tick timestamps.

No writes, suspension, input injection or game clock changes. Keep the same active
unpaused battle throughout; --scene-verified is the operator's scene assertion.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import time

SUPPORTED = {
    '6c0597be236fc5c803b130bed6ae6ce19698b1b4b58af8b790c41b9560e5cea8',
    '716dde6a1cd837c74c83c426fba634491143d624c0c284aad4dc4d5d5ea2417f',
}
FIELDS = {'state': (0x4ed818, '<H'), 'speed_option': (0x6695a4, '<I'),
          'base_ms': (0x4ed80c, '<I'), 'requested_interval_ms': (0x61d9ec, '<I'),
          'ticks': (0x8924b8, '<I'), 'game_clock_ms': (0x9b5210, '<I'),
          'width': (0xe5bf1c, '<I'), 'height': (0xe5bf20, '<I')}


def image_digest(raw):
    data = bytearray(raw)
    if data[0xebb7e:0xebb88] == b'hqcdd.dll\0':
        data[0xebb7e:0xebb87] = b'DDRAW.dll'
    digest = hashlib.sha256(data).hexdigest()
    if digest not in SUPPORTED:
        raise ValueError('Unsupported executable; no process memory was read')
    return digest


def summarize(samples, poll_ms, scene_verified=False):
    if len(samples) < 2:
        raise ValueError('At least two samples required')
    first, last = samples[0], samples[-1]
    elapsed = last['time_s'] - first['time_s']
    if elapsed <= 0:
        raise ValueError('Nonpositive observation duration')
    reasons, observations, gaps = [], [], []
    stable = ('state', 'speed_option', 'base_ms', 'requested_interval_ms', 'width', 'height')
    if not scene_verified:
        reasons.append('Battle scene not verified by operator')
    if any(any(s[k] != first[k] for k in stable) for s in samples):
        reasons.append('State, speed, requested interval or dimensions changed')
    if not all(s['foreground'] for s in samples):
        reasons.append('Game was not foreground at every sample')
    total_ticks, flat_start, longest_flat = 0, first['time_s'], 0.0
    for previous, current in zip(samples, samples[1:]):
        gap = current['time_s'] - previous['time_s']
        if gap <= 0:
            raise ValueError('Sample times must increase')
        gaps.append(gap * 1000)
        delta = (current['ticks'] - previous['ticks']) & 0xffffffff
        if delta > 0x7fffffff:
            reasons.append('Tick counter moved backwards or reset')
            continue
        total_ticks += delta
        if delta:
            observations.append({'time_s': current['time_s'], 'ticks': delta,
                                 'observation_window_ms': gap * 1000})
            # Count only intervals where equal values were actually sampled;
            # the transition may occur anywhere in the following poll window.
            longest_flat = max(longest_flat, previous['time_s'] - flat_start)
            flat_start = current['time_s']
        else:
            longest_flat = max(longest_flat, current['time_s'] - flat_start)
    if total_ticks == 0:
        reasons.append('No tick progress observed')
    if max(gaps) > max(100, poll_ms * 5):
        reasons.append('Sampler scheduling gap exceeds observation tolerance')
    if max(s.get('read_ms', 0) for s in samples) > max(10, poll_ms):
        reasons.append('Snapshot read duration exceeds observation tolerance')
    return {'valid_for_comparison': not reasons, 'invalid_reasons': sorted(set(reasons)),
            'seconds': elapsed, 'ticks_per_second': total_ticks / elapsed if not any('reset' in r for r in reasons) else None,
            'max_observed_unchanged_ticks_ms': longest_flat * 1000,
            'multi_tick_observations': sum(o['ticks'] > 1 for o in observations),
            'max_ticks_per_observation': max((o['ticks'] for o in observations), default=0),
            'max_poll_gap_ms': max(gaps), 'tick_observations': observations,
            'limitations': ['External polling is not exact tick timing or completed-simulation timing',
                            'Multiple ticks per poll do not alone prove catch-up or lag',
                            'Foreground/state sampling cannot detect every pause or scene change',
                            'No displayed FPS, photon latency, terrain duration or multiplayer checksum']}


def collect(pid, seconds, poll_ms):
    import ctypes as c
    from ctypes import wintypes as w
    k, u = c.WinDLL('kernel32', use_last_error=True), c.WinDLL('user32', use_last_error=True)
    k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
    k.CloseHandle.argtypes = [w.HANDLE]
    k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
    k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
    u.GetForegroundWindow.restype = w.HWND
    u.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
    handle = k.OpenProcess(0x1000 | 0x10, False, pid)
    if not handle:
        raise c.WinError(c.get_last_error())
    try:
        name = c.create_unicode_buffer(32768); length = w.DWORD(len(name))
        if not k.QueryFullProcessImageNameW(handle, 0, name, c.byref(length)):
            raise c.WinError(c.get_last_error())
        raw = Path(name.value).read_bytes()
        digest = image_digest(raw)

        def read(address, fmt):
            size = struct.calcsize(fmt); buffer = c.create_string_buffer(size); count = c.c_size_t()
            if not k.ReadProcessMemory(handle, address, buffer, size, c.byref(count)) or count.value != size:
                raise c.WinError(c.get_last_error())
            return struct.unpack(fmt, buffer.raw)[0]

        if read(0x400000, '<H') != 0x5a4d:
            raise ValueError('Unexpected image base')
        start, samples = time.perf_counter(), []
        while True:
            before = time.perf_counter()
            row = {key: read(*field) for key, field in FIELDS.items()}
            foreground_pid = w.DWORD()
            u.GetWindowThreadProcessId(u.GetForegroundWindow(), c.byref(foreground_pid))
            after = time.perf_counter()
            row.update(time_s=(before + after) / 2 - start, read_ms=(after - before) * 1000,
                       foreground=foreground_pid.value == pid)
            samples.append(row)
            if after - start >= seconds:
                break
            time.sleep(min(poll_ms / 1000, max(0, start + seconds - after)))
        return {'pid': pid, 'exe': name.value, 'exe_sha256': hashlib.sha256(raw).hexdigest(),
                'normalized_sha256': digest, 'poll_ms': poll_ms, 'samples': samples}
    finally:
        k.CloseHandle(handle)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('pid', type=int)
    p.add_argument('--seconds', type=float, default=5)
    p.add_argument('--poll-ms', type=float, default=10)
    p.add_argument('--scene-verified', action='store_true')
    p.add_argument('--output', type=Path)
    a = p.parse_args()
    if not 1 <= a.seconds <= 30 or not 5 <= a.poll_ms <= 1000:
        p.error('Use 1..30 seconds and 5..1000 poll-ms')
    result = collect(a.pid, a.seconds, a.poll_ms)
    result['summary'] = summarize(result['samples'], a.poll_ms, a.scene_verified)
    if a.output:
        a.output.parent.mkdir(parents=True, exist_ok=True)
        a.output.write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in result['summary'].items() if k != 'tick_observations'}, indent=2))


if __name__ == '__main__':
    main()

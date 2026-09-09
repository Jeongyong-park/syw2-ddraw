"""Read only confirmed SYW2Plus timing fields; never writes or suspends the game.

Usage: python tools/sample_syw2_timing.py PID --seconds 10
Keep the game in an active, unpaused single-player battle while sampling.
"""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
from pathlib import Path
import struct
import time

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('pid', type=int)
p.add_argument('--seconds', type=float, default=5)
a = p.parse_args()
if not 1 <= a.seconds <= 30:
    p.error('--seconds must be between 1 and 30')
k = c.WinDLL('kernel32', use_last_error=True)
k.OpenProcess.argtypes = [w.DWORD, w.BOOL, w.DWORD]; k.OpenProcess.restype = w.HANDLE
k.CloseHandle.argtypes = [w.HANDLE]
k.QueryFullProcessImageNameW.argtypes = [w.HANDLE, w.DWORD, w.LPWSTR, c.POINTER(w.DWORD)]
k.ReadProcessMemory.argtypes = [w.HANDLE, c.c_void_p, c.c_void_p, c.c_size_t, c.POINTER(c.c_size_t)]
handle = k.OpenProcess(0x1000 | 0x10, False, a.pid)  # QUERY_LIMITED_INFORMATION | VM_READ
if not handle:
    raise c.WinError(c.get_last_error())
try:
    name = c.create_unicode_buffer(32768); length = w.DWORD(len(name))
    if not k.QueryFullProcessImageNameW(handle, 0, name, c.byref(length)):
        raise c.WinError(c.get_last_error())
    raw = bytearray(Path(name.value).read_bytes())
    # Accept only the known original or its import-only HQCDD copy.
    if raw[0xebb7e:0xebb88] == b'hqcdd.dll\0':
        raw[0xebb7e:0xebb87] = b'DDRAW.dll'
    if hashlib.sha256(raw).hexdigest() != '6c0597be236fc5c803b130bed6ae6ce19698b1b4b58af8b790c41b9560e5cea8':
        raise ValueError('Unsupported executable; no process memory was read')
    def read(address, fmt='<I'):
        size = struct.calcsize(fmt); buffer = c.create_string_buffer(size); count = c.c_size_t()
        if not k.ReadProcessMemory(handle, address, buffer, size, c.byref(count)) or count.value != size:
            raise c.WinError(c.get_last_error())
        return struct.unpack(fmt, buffer.raw)[0]
    if read(0x400000, '<H') != 0x5a4d:
        raise ValueError('Unexpected image base')
    def snapshot():
        return dict(state=read(0x4ed818,'<H'), speed_option=read(0x6695a4),
                    base_ms=read(0x4ed80c), requested_interval_ms=read(0x61d9ec),
                    ticks=read(0x8924b8), game_clock_ms=read(0x9b5210))
    start = time.perf_counter(); first = snapshot()
    time.sleep(a.seconds)
    last = snapshot(); elapsed = time.perf_counter()-start
    print(json.dumps(dict(seconds=elapsed, start=first, end=last,
        ticks_per_second=((last['ticks']-first['ticks'])&0xffffffff)/elapsed,
        game_clock_ms_per_second=((last['game_clock_ms']-first['game_clock_ms'])&0xffffffff)/elapsed,
        note='State/settings changes, pause, inactive window or a new battle invalidate the comparison.'), indent=2))
finally:
    k.CloseHandle(handle)

"""Read-only ESL widescreen probe; executes selected original x86 functions in Unicorn.

Never modifies or launches the supplied EXE. Patched bytes exist only in emulated memory.
Requires pefile and unicorn. Optional Ghidra snapshots use the running local HTTP plugin.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import urllib.request

SUPPORTED_SHA256 = '716dde6a1cd837c74c83c426fba634491143d624c0c284aad4dc4d5d5ea2417f'
BASE = 0x400000
MODE_FUNCTION = 0x4644a0
MODE_CALL = 0x46457a
ENGINE = 0xe5bf18
CAMERA = 0xb3dda8
BOUNDS = 0xb3ac80
CACHE = 0xba3d30
CACHE_END = 0xc0bd30
FUNCTIONS = {
    'display_mode': MODE_FUNCTION,
    'clip_rect': 0x465130,
    'screen_to_world': 0x4a4860,
    'terrain_cache_update': 0x4324e0,
    'terrain_cache_build': 0x431ab0,
    'battle_surfaces': 0x4922d0,
    'hud_layout': 0x4b5960,
    'cache_copy': 0x466280,
}


def validate_image(raw):
    digest = hashlib.sha256(raw).hexdigest()
    if digest != SUPPORTED_SHA256:
        raise ValueError('Unsupported EXE SHA-256; refusing address-based execution: ' + digest)
    return digest


def inspect(exe, width=1067):
    import pefile
    import unicorn as uc
    from unicorn import x86_const as x
    if width not in (1067, 1068):
        raise ValueError('Only the planned 1067/1068 width candidates are supported')
    raw = Path(exe).read_bytes()
    digest = validate_image(raw)
    pe = pefile.PE(data=raw)
    if pe.OPTIONAL_HEADER.ImageBase != BASE or pe.FILE_HEADER.Machine != 0x14c:
        raise ValueError('Expected fixed-base x86 image')
    mapped = pe.get_memory_mapped_image()
    stack, obj, vtable = 0x2000000, 0x2100000, 0x2101000

    def machine():
        m = uc.Uc(uc.UC_ARCH_X86, uc.UC_MODE_32)
        m.mem_map(BASE, (pe.OPTIONAL_HEADER.SizeOfImage + 0xfff) & ~0xfff)
        m.mem_write(BASE, mapped)
        m.mem_map(stack, 0x20000)
        m.mem_map(obj, 0x2000)
        return m

    def read32(m, addr, count=1):
        return struct.unpack('<' + 'I' * count, m.mem_read(addr, 4 * count))

    def mode(number, patched):
        m = machine()
        if patched:
            for addr, expected, value in ((0x46452c, 1024, width), (0x464533, 768, 600)):
                if read32(m, addr)[0] != expected:
                    raise ValueError('Mode patch preimage mismatch')
                m.mem_write(addr, struct.pack('<I', value))
        sp = stack + 0x10000
        m.mem_write(sp, struct.pack('<II', stack, number))
        m.mem_write(ENGINE + 0x1518, struct.pack('<I', obj))
        m.mem_write(obj, struct.pack('<I', vtable))
        m.reg_write(x.UC_X86_REG_ESP, sp)
        m.reg_write(x.UC_X86_REG_ECX, ENGINE)
        # Stop before the external DirectDraw call; require that exact boundary.
        m.emu_start(MODE_FUNCTION, MODE_CALL, count=200)
        if m.reg_read(x.UC_X86_REG_EIP) != MODE_CALL:
            raise RuntimeError('Display setup did not reach the expected DirectDraw call')
        args = read32(m, m.reg_read(x.UC_X86_REG_ESP), 6)
        expected = (width, 600, 8) if patched and number == 4 else (800, 600, 8)
        if args[1:4] != expected:
            raise RuntimeError('Unexpected original engine SetDisplayMode arguments')
        return dict(mode=number, width=args[1], height=args[2], bpp=args[3])

    def world(screen_width, px, py):
        m = machine()
        m.mem_write(BOUNDS, struct.pack('<4I', 0, 0, screen_width - 1, 511))
        m.mem_write(CAMERA + 0x4fd4, struct.pack('<2I', 64, 64))
        sp = stack + 0x10000
        m.mem_write(sp, struct.pack('<5I', stack, px, py, obj, obj + 2))
        m.reg_write(x.UC_X86_REG_ESP, sp)
        m.reg_write(x.UC_X86_REG_ECX, CAMERA)
        m.emu_start(0x4a4860, stack, count=200)
        if m.reg_read(x.UC_X86_REG_EIP) != stack:
            raise RuntimeError('Coordinate function did not return')
        return struct.unpack('<hh', m.mem_read(obj, 4))

    baseline_center = world(800, 400, 256)
    wide_center = world(width, width // 2, 256)
    left, right = world(width, 0, 256), world(width, width - 1, 256)
    if baseline_center != wide_center or right[0] - left[0] != width - 1:
        raise RuntimeError('Camera center/span regression')
    cache_bytes = CACHE_END - CACHE
    if cache_bytes != 832 * 512:
        raise RuntimeError('Unexpected fixed cache boundary')
    def copy_probe(copy_width):
        m = machine()
        source, dest = 0x2200000, 0x2300000
        m.mem_map(source, 0x10000)
        m.mem_map(dest, 0x10000)
        height = 2
        pattern = bytes((i % 251) + 1 for i in range(copy_width * height))
        m.mem_write(source, pattern)
        m.mem_write(dest, bytes([0xcd]) * (copy_width * height + 16))
        m.mem_write(ENGINE + 0xc, struct.pack('<II', 8, copy_width))
        m.mem_write(ENGINE + 0x1514, struct.pack('<I', dest))
        sp = stack + 0x10000
        m.mem_write(sp, struct.pack('<6I', stack, 0, 0, copy_width, height, source))
        m.reg_write(x.UC_X86_REG_ESP, sp)
        m.reg_write(x.UC_X86_REG_ECX, ENGINE)
        m.emu_start(0x466280, stack, count=20000)
        if m.reg_read(x.UC_X86_REG_EIP) != stack:
            raise RuntimeError('Cache copy did not return')
        actual = bytes(m.mem_read(dest, len(pattern)))
        return {'width': copy_width, 'rows': height, 'exact_copy': actual == pattern,
                'mismatched_bytes': sum(a != b for a, b in zip(actual, pattern)),
                'trailing_guard_intact': bytes(m.mem_read(dest + len(pattern), 16)) == bytes([0xcd]) * 16}

    copy_cases = [copy_probe(1067), copy_probe(1068), copy_probe(1088)]
    if copy_cases[0]['exact_copy'] or not all(c['exact_copy'] and c['trailing_guard_intact'] for c in copy_cases[1:]):
        raise RuntimeError('Unexpected original dword-copy alignment behavior')
    return {
        'exe_sha256': digest,
        'status': 'emulated_components_only_not_playable',
        'baseline': mode(3, False),
        'prototype': mode(4, True),
        'menu_mode_after_prototype_patch': mode(3, True),
        'camera': {'baseline_center': baseline_center, 'wide_center': wide_center,
                   'wide_left': left, 'wide_right': right},
        'terrain_cache': {'address': hex(CACHE), 'next_object': hex(CACHE_END),
                          'bytes': cache_bytes, 'width': 832, 'height': 512,
                          'candidate_minimum_bytes': width * 512,
                          'additional_bytes_required': width * 512 - cache_bytes},
        'original_cache_copy': copy_cases,
        'recommended_visible_width': 1068,
        'proposed_cache_width': ((width + 63) // 64) * 64,
        'blockers': ['Relocate terrain cache and audit every reference',
                     'Replace fixed scrolling copies and dirty-tile bounds',
                     'Center HUD rendering and hit testing',
                     'Validate full battle rendering, inputs and performance'],
    }


def ghidra_snapshots(url, output):
    from urllib.parse import urlparse
    if urlparse(url).hostname not in ('127.0.0.1', 'localhost', '::1'):
        raise ValueError('Ghidra endpoint must be local')
    metadata = urllib.request.urlopen(url.rstrip('/') + '/get_metadata', timeout=10).read().decode()
    if 'Program Name: [ESL]Syw2plus 2606.exe' not in metadata:
        raise ValueError('Ghidra has a different current program')
    (output / 'ghidra-metadata.txt').write_text(metadata, encoding='utf-8')
    for name, address in FUNCTIONS.items():
        endpoint = url.rstrip('/') + '/decompile_function?address=%08x' % address
        data = urllib.request.urlopen(endpoint, timeout=30).read().decode()
        (output / ('ghidra-' + name + '.c.txt')).write_text(data, encoding='utf-8')


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('--width', type=int, default=1067, choices=(1067, 1068))
    ap.add_argument('--output', type=Path, required=True)
    ap.add_argument('--ghidra-url', help='Optional running Ghidra plugin, e.g. http://127.0.0.1:8089')
    args = ap.parse_args()
    report = inspect(args.exe, args.width)
    args.output.mkdir(parents=True, exist_ok=True)
    if args.ghidra_url:
        ghidra_snapshots(args.ghidra_url, args.output)
    (args.output / 'report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

"""Create a separate, import-retargeted SYW2Plus test EXE; never edit the source.

Only the DDRAW.dll import descriptor's name changes, to hqcdd.dll (same size).
The legacy ddraw.dll/dxwrapper/DxWnd files remain untouched.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct


def retarget(raw: bytes) -> tuple[bytes, int]:
    """Validate PE32 and the observed SYW2Plus DD7 import before changing 9 bytes."""
    if len(raw) < 0x40 or raw[:2] != b'MZ':
        raise ValueError('Not a PE executable')
    pe = struct.unpack_from('<I', raw, 0x3C)[0]
    if pe + 24 > len(raw) or raw[pe:pe+4] != b'PE\0\0':
        raise ValueError('Invalid PE header')
    machine, nsec = struct.unpack_from('<HH', raw, pe+4)
    opt_size = struct.unpack_from('<H', raw, pe+20)[0]
    opt = pe+24
    if machine != 0x14C or opt_size < 112 or opt+opt_size+nsec*40 > len(raw):
        raise ValueError('Expected a complete x86 PE32 image')
    if struct.unpack_from('<H', raw, opt)[0] != 0x10B:
        raise ValueError('Expected PE32')
    sections = [struct.unpack_from('<IIII', raw, opt+opt_size+i*40+8) for i in range(nsec)]

    def offset(rva: int, size: int = 1) -> int:
        for _virtual_size, va, raw_size, raw_offset in sections:
            if va <= rva and rva+size <= va+raw_size:
                pos = raw_offset+rva-va
                if pos+size <= len(raw):
                    return pos
        raise ValueError(f'RVA is not file-backed: 0x{rva:x}')

    def string(rva: int) -> bytes:
        pos = offset(rva)
        end = raw.find(b'\0', pos, min(pos+256, len(raw)))
        if end < 0:
            raise ValueError('Unterminated import name')
        offset(rva, end-pos+1)
        return raw[pos:end]

    imports, length = struct.unpack_from('<II', raw, opt+104)
    matches = []
    for index in range(min(length//20, 4096)):
        desc = offset(imports+index*20, 20)
        original, stamp, chain, name, first = struct.unpack_from('<IIIII', raw, desc)
        if not any((original, stamp, chain, name, first)):
            break
        if string(name).lower() != b'ddraw.dll':
            continue
        names = []
        for n in range(4096):
            thunk = struct.unpack_from('<I', raw, offset((original or first)+n*4, 4))[0]
            if thunk == 0:
                break
            if thunk & 0x80000000:
                raise ValueError('Ordinal DirectDraw imports are not supported')
            names.append(string(thunk+2))
        else:
            raise ValueError('Unterminated import thunk')
        if names != [b'DirectDrawCreateEx']:
            raise ValueError(f'Unsupported DirectDraw import set: {names!r}')
        matches.append(offset(name, 10))
    if len(matches) != 1:
        raise ValueError('Expected exactly one DDRAW.dll import')
    # The confirmed DirectDraw7 IID in this SYW2Plus build. Do not silently patch other games.
    iid7 = bytes.fromhex('c05ee6159c3bd211b92f00609797ea5b')
    if iid7 not in raw:
        raise ValueError('DirectDraw7 IID not found; this build needs analysis')
    pos = matches[0]
    result = bytearray(raw)
    result[pos:pos+9] = b'hqcdd.dll'
    return bytes(result), pos


def prepare(source: Path, dll: Path, output_dir: Path) -> Path:
    source = source.resolve(strict=True)
    dll = dll.resolve(strict=True)
    output_dir = output_dir.resolve()
    target = output_dir / (source.stem + ' HQ그래픽.exe')
    target_dll = output_dir / 'hqcdd.dll'
    launcher = output_dir / 'launch.ps1'
    manifest = output_dir / 'hqcdd-install.json'
    config = output_dir / 'hqcdd.ini'
    # Refuse to replace anything, including previous test versions and manifests.
    for path in (target, target_dll, launcher, manifest, config):
        if path.exists():
            raise ValueError(f'Already exists (nothing replaced): {path}')
    raw = source.read_bytes()
    launcher_bytes = Path(__file__).with_name('launch.ps1').read_bytes()
    config_bytes = Path(__file__).with_name('hqcdd.ini').read_bytes()
    patched, pos = retarget(raw)
    dll_raw = dll.read_bytes()
    if len(dll_raw) < 64 or dll_raw[:2] != b'MZ':
        raise ValueError('Invalid wrapper DLL')
    dll_pe = struct.unpack_from('<I', dll_raw, 0x3C)[0]
    if dll_pe+24 > len(dll_raw) or dll_raw[dll_pe:dll_pe+4] != b'PE\0\0' or struct.unpack_from('<H',dll_raw,dll_pe+4)[0] != 0x14c:
        raise ValueError('Wrapper DLL must be x86')
    output_dir.mkdir(parents=True, exist_ok=True)
    created = []
    try:
        for path, data in ((target_dll,dll_raw),(target,patched),(launcher,launcher_bytes),(config,config_bytes)):
            with path.open('xb') as f:
                created.append(path)
                f.write(data)
        record = {
            'source': str(source), 'source_sha256': hashlib.sha256(raw).hexdigest(),
            'import_name_offset': pos,
            'files': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in created if p != config},
            'editable_files': [config.name],
            'note': 'Remove the listed generated files and this manifest to uninstall. Original files were not changed.',
        }
        with manifest.open('x', encoding='utf-8') as f:
            created.append(manifest)
            json.dump(record, f, ensure_ascii=False, indent=2)
    except Exception:
        for path in reversed(created):
            path.unlink(missing_ok=True)
        raise
    return target


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('exe', type=Path)
    ap.add_argument('--dll', type=Path, default=Path(__file__).parent/'build/Release/hqcdd.dll')
    ap.add_argument('--output-dir', type=Path, help='Default: alongside source game EXE')
    args = ap.parse_args()
    try:
        target = prepare(args.exe, args.dll, args.output_dir or args.exe.parent)
    except (ValueError, OSError, struct.error) as e:
        ap.exit(1, f'{e}\n')
    print(target)


if __name__ == '__main__':
    main()

"""Build end-user or developer archives without game assets."""
from pathlib import Path
import argparse
import hashlib
import re
import subprocess
import tempfile
import zipfile

ROOT_FILES = (
    '.gitignore', 'LICENSE', 'README.md', 'INSTALL.txt', 'ASI-INSTALL.txt', 'CMakeLists.txt', 'build.ps1',
    'VERSION', 'CHANGELOG.md', 'exports.def', 'exports-asi.def', 'hqcdd.ini', 'launch.ps1', 'prepare.py', 'package.py',
)
USER_FILES = ('LICENSE', 'README.md', 'INSTALL.txt', 'VERSION', 'CHANGELOG.md', 'hqcdd.ini')
SOURCE_DIRS = ('.github', 'src', 'tests', 'docs', 'tools')
SOURCE_SUFFIXES = {'.cpp', '.h', '.rc', '.py', '.md', '.yml', '.yaml', '.in', '.ps1'}
EXCLUDED_DIRS = {'.git', 'build', 'output', '__pycache__'}


def atomic_write(path: Path, data: bytes) -> None:
    with tempfile.NamedTemporaryFile(dir=path.parent, suffix='.tmp', delete=False) as f:
        temporary = Path(f.name)
    try:
        temporary.write_bytes(data)
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)


def package(root: Path, developer: bool = False, asi: bool = False) -> Path:
    root = root.resolve()
    if developer and asi: raise ValueError('Choose either developer or ASI distribution')
    dll = root / ('build/Release/hqcdd.asi' if asi else 'build/Release/hqcdd.dll')
    if not dll.is_file():
        raise FileNotFoundError(f'Build the Release wrapper first: {dll}')
    version = (root / 'VERSION').read_text(encoding='utf-8').strip()
    if (not re.fullmatch(r'(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)', version)
            or any(int(part) > 65535 for part in version.split('.'))):
        raise ValueError('VERSION must be MAJOR.MINOR.PATCH with components 0..65535')
    sources = [root / name for name in (ROOT_FILES if developer else USER_FILES)]
    for folder in (SOURCE_DIRS if developer else ('docs',)):
        for path in sorted((root / folder).rglob('*')):
            relative = path.relative_to(root)
            image = relative.parts[:2] == ('docs', 'images') and path.suffix.lower() in {'.png', '.jpg', '.jpeg', '.webp'}
            suffixes = SOURCE_SUFFIXES if developer else {'.md'}
            if (path.is_file() and not path.is_symlink()
                    and not EXCLUDED_DIRS.intersection(relative.parts)
                    and (path.suffix in suffixes or image)):
                sources.append(path)
    # Read all inputs first: missing required documentation must not replace a good ZIP.
    prefix = 'syw2-ddraw/' if developer else ''
    entries = {prefix + p.relative_to(root).as_posix(): p.read_bytes() for p in sources}
    if asi: entries['INSTALL.txt'] = (root / 'ASI-INSTALL.txt').read_bytes()
    entries[prefix + 'build/Release/hqcdd.dll' if developer else 'plugins/hqcdd.asi' if asi else 'ddraw.dll'] = dll.read_bytes()
    entries[prefix + 'SHA256SUMS.txt'] = ''.join(
        f'{hashlib.sha256(data).hexdigest()}  {name.removeprefix(prefix)}\n'
        for name, data in sorted(entries.items())
    ).encode('utf-8')
    output = root / 'output'
    output.mkdir(exist_ok=True)
    kind = '-developer' if developer else '-asi' if asi else ''
    target = output / f'syw2-ddraw-v{version}{kind}.zip'
    with tempfile.NamedTemporaryFile(dir=output, suffix='.tmp', delete=False) as f:
        temporary = Path(f.name)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, data in sorted(entries.items()):
                archive.writestr(name, data)
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    digest = hashlib.sha256(target.read_bytes()).hexdigest()
    atomic_write(target.with_suffix('.zip.sha256'), f'{digest}  {target.name}\n'.encode('ascii'))
    return target


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--developer', action='store_true', help='Include build sources and test tools')
    ap.add_argument('--asi', action='store_true', help='Build an ASI-only archive for an existing ASI loader')
    args = ap.parse_args()
    root = Path(__file__).resolve().parent
    subprocess.run([
        'powershell.exe', '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
        str(root / 'tests/test_version.ps1'),
        '-DllPath', str(root / ('build/Release/hqcdd.asi' if args.asi else 'build/Release/hqcdd.dll')),
    ], check=True)
    print(package(root, developer=args.developer, asi=args.asi))

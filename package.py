"""Package wrapper sources and the built x86 DLL, without game assets."""
from pathlib import Path
import tempfile
import zipfile

ROOT_FILES = (
    '.gitignore', 'LICENSE', 'README.md', 'CMakeLists.txt', 'build.ps1',
    'exports.def', 'hqcdd.ini', 'launch.ps1', 'prepare.py', 'package.py',
)
SOURCE_DIRS = ('.github', 'src', 'tests', 'docs', 'tools')
SOURCE_SUFFIXES = {'.cpp', '.h', '.rc', '.py', '.md', '.yml', '.yaml'}
EXCLUDED_DIRS = {'.git', 'build', 'output', '__pycache__'}


def package(root: Path) -> Path:
    root = root.resolve()
    dll = root / 'build/Release/hqcdd.dll'
    if not dll.is_file():
        raise FileNotFoundError(f'Build the Release wrapper first: {dll}')
    # Only known source/config files belong in a distribution.
    sources = [root / name for name in ROOT_FILES]
    for folder in SOURCE_DIRS:
        for path in sorted((root / folder).rglob('*')):
            relative = path.relative_to(root)
            if (path.is_file() and not path.is_symlink()
                    and not EXCLUDED_DIRS.intersection(relative.parts)
                    and path.suffix in SOURCE_SUFFIXES):
                sources.append(path)
    output = root / 'output'
    output.mkdir(exist_ok=True)
    target = output / 'syw2-ddraw-v0.5.2.zip'
    with tempfile.NamedTemporaryFile(dir=output, suffix='.tmp', delete=False) as f:
        temporary = Path(f.name)
    try:
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED) as archive:
            for path in sources:
                archive.write(path, Path('syw2-ddraw') / path.relative_to(root))
            archive.write(dll, 'syw2-ddraw/build/Release/hqcdd.dll')
        temporary.replace(target)
    finally:
        temporary.unlink(missing_ok=True)
    return target


if __name__ == '__main__':
    print(package(Path(__file__).resolve().parent))

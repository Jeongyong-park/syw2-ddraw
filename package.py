"""Package wrapper sources and the built x86 DLL, without game assets."""
from pathlib import Path
import shutil
import zipfile

root = Path(__file__).resolve().parent
output = root.parents[1] / 'output'
output.mkdir(exist_ok=True)
with zipfile.ZipFile(output/'syw2-ddraw-v0.5.2.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
    for path in sorted(root.rglob('*')):
        relative = path.relative_to(root)
        if not path.is_file() or any(p in ('build', '__pycache__') for p in relative.parts):
            continue
        if path.suffix == '.log':
            continue
        archive.write(path, Path('syw2-ddraw')/relative)
    archive.write(root/'build/Release/hqcdd.dll', 'syw2-ddraw/build/Release/hqcdd.dll')
test = output/'syw2-graphics-v0.5.2'
if test.is_dir():
    shutil.copyfile(root/'README.md', test/'README.md')
print(output/'syw2-ddraw-v0.5.2.zip')

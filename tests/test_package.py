"""Distribution tests use synthetic files, never game assets."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('package', Path(__file__).parents[1] / 'package.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
    def test_asi_package_preserves_loader_and_uses_asi_instructions(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            for name in packager.USER_FILES: (root/name).write_text('ordinary')
            (root/'VERSION').write_text('1.2.3')
            (root/'ASI-INSTALL.txt').write_text('keep existing ASI loader')
            binary=root/'build/Release/hqcdd.asi'; binary.parent.mkdir(parents=True); binary.write_bytes(b'ASI')
            target=packager.package(root,asi=True)
            self.assertEqual(target.name,'syw2-ddraw-v1.2.3-asi.zip')
            with zipfile.ZipFile(target) as z:
                self.assertEqual(z.read('plugins/hqcdd.asi'),b'ASI')
                self.assertNotIn('ddraw.dll',z.namelist())
                self.assertEqual(z.read('INSTALL.txt'),b'keep existing ASI loader')
            with self.assertRaises(ValueError): packager.package(root,developer=True,asi=True)

    def test_missing_build_does_not_create_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            with self.assertRaises(FileNotFoundError):
                packager.package(root)
            self.assertFalse((root / 'output').exists())


    def test_invalid_versions_preserve_existing_archive(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            dll = root / 'build/Release/hqcdd.dll'
            dll.parent.mkdir(parents=True)
            dll.write_bytes(b'wrapper')
            output = root / 'output'
            output.mkdir()
            existing = output / 'existing.zip'
            existing.write_bytes(b'previous release')
            for version in ('01.2.3', '1.2', '1x2x3', '1.2.3-rc.1', '65536.0.0'):
                with self.subTest(version=version):
                    (root / 'VERSION').write_text(version)
                    with self.assertRaises(ValueError):
                        packager.package(root)
                    self.assertEqual(list(output.iterdir()), [existing])
                    self.assertEqual(existing.read_bytes(), b'previous release')

    def test_archive_excludes_repository_and_game_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in packager.ROOT_FILES:
                (root / name).write_text('source', encoding='utf-8')
            (root / 'VERSION').write_text('1.2.3', encoding='utf-8')
            files = {
                'build/Release/hqcdd.dll': b'wrapper',
                'src/ddraw.cpp': b'source',
                'docs/images/settings.png': b'documentation image',
                'src/game.png': b'not a documentation image',
                '.github/workflows/build.yml': b'workflow',
                '.git/config': b'private',
                'output/game.exe': b'game',
                'game.exe': b'game',
                'src/game.exe': b'game',
                'tests/__pycache__/cache.py': b'cache',
                'src/output/accidental.cpp': b'generated',
            }
            for name, content in files.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
            for developer in (False, True):
                with self.subTest(developer=developer):
                    target = packager.package(root, developer=developer)
                    self.assertTrue(target.parent.samefile(root / 'output'))
                    suffix = '-developer' if developer else ''
                    self.assertEqual(target.name, f'syw2-ddraw-v1.2.3{suffix}.zip')
                    packager.package(root, developer=developer)
                    sidecar = target.with_suffix('.zip.sha256').read_text()
                    self.assertEqual(sidecar, f'{hashlib.sha256(target.read_bytes()).hexdigest()}  {target.name}\n')
                    prefix = 'syw2-ddraw/' if developer else ''
                    with zipfile.ZipFile(target) as archive:
                        self.assertIsNone(archive.testzip())
                        expected = {prefix + name for name in
                                    (packager.ROOT_FILES if developer else packager.USER_FILES)}
                        expected.update({prefix + 'docs/images/settings.png',
                                         prefix + 'SHA256SUMS.txt',
                                         prefix + 'build/Release/hqcdd.dll' if developer else 'ddraw.dll'})
                        if developer:
                            expected.update({prefix + 'src/ddraw.cpp',
                                             prefix + '.github/workflows/build.yml'})
                        self.assertEqual(set(archive.namelist()), expected)
                        sums = archive.read(prefix + 'SHA256SUMS.txt').decode().splitlines()
                        verified = set()
                        for line in sums:
                            digest, name = line.split('  ', 1)
                            self.assertEqual(digest, hashlib.sha256(archive.read(prefix + name)).hexdigest())
                            verified.add(prefix + name)
                        self.assertEqual(verified, expected - {prefix + 'SHA256SUMS.txt'})
                    previous = target.read_bytes()
                    install = root / 'INSTALL.txt'
                    content = install.read_bytes()
                    install.unlink()
                    with self.assertRaises(FileNotFoundError):
                        packager.package(root, developer=developer)
                    self.assertEqual(target.read_bytes(), previous)
                    install.write_bytes(content)



if __name__ == '__main__':
    unittest.main()

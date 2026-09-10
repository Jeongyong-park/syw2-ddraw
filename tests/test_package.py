"""Distribution tests use synthetic files, never game assets."""
import importlib.util
from pathlib import Path
import tempfile
import unittest
import zipfile

spec = importlib.util.spec_from_file_location('package', Path(__file__).parents[1] / 'package.py')
packager = importlib.util.module_from_spec(spec)
spec.loader.exec_module(packager)


class PackageTests(unittest.TestCase):
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
            target = packager.package(root)
            self.assertTrue(target.parent.samefile(root / 'output'))
            self.assertEqual(target.name, 'syw2-ddraw-v1.2.3.zip')
            # Packaging twice must not include the previous archive.
            packager.package(root)
            with zipfile.ZipFile(target) as archive:
                self.assertIsNone(archive.testzip())
                expected = {'syw2-ddraw/' + name for name in packager.ROOT_FILES}
                expected.update({
                    'syw2-ddraw/build/Release/hqcdd.dll',
                    'syw2-ddraw/src/ddraw.cpp',
                    'syw2-ddraw/docs/images/settings.png',
                    'syw2-ddraw/.github/workflows/build.yml',
                })
                self.assertEqual(set(archive.namelist()), expected)


if __name__ == '__main__':
    unittest.main()

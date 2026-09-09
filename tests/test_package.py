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

    def test_archive_excludes_repository_and_game_artifacts(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            for name in packager.ROOT_FILES:
                (root / name).write_text('source', encoding='utf-8')
            files = {
                'build/Release/hqcdd.dll': b'wrapper',
                'src/ddraw.cpp': b'source',
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
            self.assertEqual(target.parent, root / 'output')
            # Packaging twice must not include the previous archive.
            packager.package(root)
            with zipfile.ZipFile(target) as archive:
                self.assertIsNone(archive.testzip())
                expected = {'syw2-ddraw/' + name for name in packager.ROOT_FILES}
                expected.update({
                    'syw2-ddraw/build/Release/hqcdd.dll',
                    'syw2-ddraw/src/ddraw.cpp',
                    'syw2-ddraw/.github/workflows/build.yml',
                })
                self.assertEqual(set(archive.namelist()), expected)


if __name__ == '__main__':
    unittest.main()

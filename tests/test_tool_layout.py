"""Compatibility and path-boundary checks for the developer tool packages."""
import hashlib
import importlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))


class ToolLayoutTests(unittest.TestCase):
    def test_old_and_package_cli_help_from_supported_locations(self):
        modules = {
            'perf': ['perf_report', 'perf_compare', 'perf_matrix', 'sample_syw2_timing'],
            'widescreen': ['widescreen_probe', 'widescreen_cache_probe', 'widescreen_hud_probe',
                          'prepare_widescreen_prototype', 'export_widescreen_patch',
                          'verify_widescreen_alert', 'verify_widescreen_full_height',
                          'verify_widescreen_hud_preview', 'verify_widescreen_messages',
                          'verify_widescreen_visibility'],
        }
        with tempfile.TemporaryDirectory() as directory:
            for group, names in modules.items():
                for name in names:
                    with self.subTest(tool=name):
                        old = subprocess.run([sys.executable, str(ROOT / 'tools' / (name + '.py')), '--help'],
                                             cwd=directory, capture_output=True, text=True)
                        new = subprocess.run([sys.executable, '-m', f'tools.{group}.{name}', '--help'],
                                             cwd=ROOT, capture_output=True, text=True)
                        self.assertEqual(old.returncode, 0, old.stderr)
                        self.assertEqual(new.returncode, 0, new.stderr)
                        self.assertIn('--help', old.stdout)
                        self.assertIn('--help', new.stdout)
            self.assertEqual(list(Path(directory).iterdir()), [])

    def test_provenance_hashes_implementation_not_compatibility_launcher(self):
        from tools.perf.perf_matrix import tool_hashes
        for name, digest in tool_hashes().items():
            self.assertEqual(digest, hashlib.sha256((ROOT / 'tools/perf' / name).read_bytes()).hexdigest())
        from tools.perf import perf_report
        old = importlib.import_module('tools.perf_report')
        self.assertIs(old.summarize, perf_report.summarize)

    def test_prototype_output_boundary_remains_repository_output(self):
        from tools.widescreen.prepare_widescreen_prototype import prepare
        # Only the initial path checks execute; this test needs no game/pefile.
        with patch.dict(sys.modules, {'pefile': object()}):
            with tempfile.TemporaryDirectory() as directory:
                outside = Path(directory)
                with self.assertRaisesRegex(ValueError, 'repository output'):
                    prepare(outside / 'source.exe', outside / 'target.exe')
            (ROOT / 'output').mkdir(exist_ok=True)
            with tempfile.TemporaryDirectory(dir=ROOT / 'output') as directory:
                inside = Path(directory)
                # Passing the path gate must reach the missing-source read.
                with self.assertRaises(FileNotFoundError):
                    prepare(inside / 'source.exe', inside / 'target.exe')
                self.assertEqual(list(inside.iterdir()), [])


if __name__ == '__main__':
    unittest.main()

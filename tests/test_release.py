"""Release gates are tested offline; never create a GitHub release."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("release", Path(__file__).parents[1]/"tools/release.py")
release = importlib.util.module_from_spec(spec)
spec.loader.exec_module(release)


class ReleaseTests(unittest.TestCase):
    def fixture(self, root, version="0.6.0"):
        (root/"VERSION").write_text(version)
        output = root/"output"
        output.mkdir(exist_ok=True)
        archive = output/f"syw2-ddraw-v{version}.zip"
        archive.write_bytes(b"validated artifact")
        archive.with_suffix(".zip.sha256").write_text(
            f"{hashlib.sha256(archive.read_bytes()).hexdigest()}  {archive.name}")
        return archive

    def test_only_user_assets_and_experimental_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.fixture(root)
            assets = release.release_plan(root,"v0.6.0")
            self.assertEqual(assets[:2],(archive,archive.with_suffix(".zip.sha256")))
            self.assertTrue(assets[3])
            self.assertIn(archive.name,assets[2])
            self.fixture(root,"1.0.0")
            self.assertFalse(release.release_plan(root,"v1.0.0")[3])

    def test_mismatched_tag_and_invalid_version_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.fixture(root)
            for tag in ("v0.6.1","0.6.0","v0.6.0-rc.1"):
                with self.subTest(tag=tag), self.assertRaises(ValueError):
                    release.release_plan(root,tag)
            for version in ("00.6.0","65536.0.0","0.6.0-rc.1"):
                (root/"VERSION").write_text(version)
                with self.subTest(version=version), self.assertRaises(ValueError):
                    release.release_plan(root,"v"+version)

    def test_missing_or_modified_asset_rejected(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            archive = self.fixture(root)
            archive.write_bytes(b"modified")
            with self.assertRaises(ValueError):
                release.release_plan(root,"v0.6.0")
            archive.unlink()
            with self.assertRaises(FileNotFoundError):
                release.release_plan(root,"v0.6.0")

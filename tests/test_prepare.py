"""Independent PE fixture tests; no copyrighted game files needed."""
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

spec = importlib.util.spec_from_file_location('prepare', Path(__file__).parents[1]/'prepare.py')
prepare = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare)


def fixture():
    data = bytearray(0x600)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HH', data, 0x84, 0x14c, 1)
    struct.pack_into('<H', data, 0x94, 224)
    struct.pack_into('<H', data, 0x98, 0x10b)
    struct.pack_into('<II', data, 0x98+104, 0x1000, 40)
    struct.pack_into('<IIII', data, 0x98+224+8, 0x400, 0x1000, 0x400, 0x200)
    struct.pack_into('<IIIII', data, 0x200, 0x1080, 0, 0, 0x1060, 0x1080)
    data[0x260:0x26a] = b'DDRAW.dll\0'
    struct.pack_into('<II', data, 0x280, 0x10a0, 0)
    data[0x2a2:0x2b3] = b'DirectDrawCreateEx'
    data[0x300:0x310] = bytes.fromhex('c05ee6159c3bd211b92f00609797ea5b')
    return bytes(data)


class PrepareTests(unittest.TestCase):
    def test_only_import_name_changes(self):
        source = fixture()
        result, pos = prepare.retarget(source)
        self.assertEqual(pos, 0x260)
        self.assertEqual(result[:pos], source[:pos])
        self.assertEqual(result[pos:pos+10], b'hqcdd.dll\0')
        self.assertEqual(result[pos+10:], source[pos+10:])

    def test_rejects_bad_or_already_patched_executables(self):
        good = fixture()
        patched, _ = prepare.retarget(good)
        cases = [b'', good[:100], patched, good.replace(b'DirectDrawCreateEx', b'DirectDrawCreateXX')]
        x64 = bytearray(good); struct.pack_into('<H',x64,0x84,0x8664); cases.append(x64)
        for data in cases:
            with self.subTest(size=len(data)):
                with self.assertRaises(ValueError):
                    prepare.retarget(data)

    def test_creation_does_not_overwrite_source_or_previous_install(self):
        with tempfile.TemporaryDirectory() as root:
            root = Path(root)
            exe = root/'game.exe'; exe.write_bytes(fixture())
            dll = root/'build.dll'; dll.write_bytes(fixture())
            output = root/'output'
            result = prepare.prepare(exe,dll,output)
            self.assertEqual(exe.read_bytes(),fixture())
            self.assertTrue(result.exists())
            record = json.loads((output/'hqcdd-install.json').read_text(encoding='utf-8'))
            self.assertEqual(record['editable_files'], ['hqcdd.ini'])
            self.assertNotIn('hqcdd.ini', record['files'])
            self.assertIn('Fullscreen=0', (output/'hqcdd.ini').read_text())
            with self.assertRaises(ValueError):
                prepare.prepare(exe,dll,output)
            self.assertEqual(exe.read_bytes(),fixture())


if __name__ == '__main__':
    unittest.main()

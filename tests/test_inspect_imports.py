import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_imports import imported_libraries
from inspect_pe import inspect_pe
from test_inspect_pe import fixture


class ImportTest(unittest.TestCase):
    def build(self):
        data = bytearray(fixture())
        opt = 0x98
        struct.pack_into('<II', data, opt + 104, 0x1000, 40)
        struct.pack_into('<IIIII', data, 0x300, 0, 0, 0, 0x1040, 0x1080)
        data[0x314:0x328] = bytes(20)
        data[0x340:0x34d] = b'KERNEL32.dll\0'
        return data

    def test_static_import(self):
        data = self.build()
        self.assertEqual(imported_libraries(data, inspect_pe(data)), ['KERNEL32.dll'])

    def test_invalid_name_rva_is_rejected(self):
        data = self.build()
        struct.pack_into('<I', data, 0x30c, 0x999999)
        with self.assertRaises(ValueError):
            imported_libraries(data, inspect_pe(data))

    def test_missing_terminator_is_rejected(self):
        data = self.build()
        struct.pack_into('<I', data, 0x98 + 108, 20)
        with self.assertRaises(ValueError):
            imported_libraries(data, inspect_pe(data))


if __name__ == '__main__':
    unittest.main()

import struct
import unittest
from scripts.inspect_pe import inspect_pe, inspect_signatures


def fixture():
    data = bytearray(0x400)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x14c, 1, 123, 0, 0, 0xe0, 0)
    opt = 0x98
    struct.pack_into('<H', data, opt, 0x10b)
    struct.pack_into('<I', data, opt + 16, 0x1020)
    struct.pack_into('<I', data, opt + 28, 0x400000)
    struct.pack_into('<H', data, opt + 70, 0x40)
    struct.pack_into('<I', data, opt + 92, 16)
    struct.pack_into('<II', data, opt + 96 + 5 * 8, 0x2000, 16)
    section = opt + 0xe0
    data[section:section + 8] = b'.text\0\0\0'
    struct.pack_into('<IIII', data, section + 8, 0x100, 0x1000, 0x100, 0x300)
    data[0x320:0x324] = b'\x11\x22\x33\x44'
    return bytes(data)


class InspectPeTest(unittest.TestCase):
    def test_header_and_signature_rva(self):
        data = fixture()
        info = inspect_pe(data)
        self.assertEqual(info['machine'], 0x14c)
        self.assertTrue(info['dynamic_base'])
        self.assertEqual((info['relocation_rva'], info['relocation_size']), (0x2000, 16))
        profile = 'constexpr uint8_t exampleSignature[] = {0x11, 0x22, 0x33, 0x44};'
        result = inspect_signatures(data, profile, info['sections'])
        self.assertEqual(result[0]['matches'], [{'file_offset': 0x320, 'rva': 0x1020}])

    def test_rejects_truncated_and_non_pe(self):
        for data in (b'', fixture()[:0x100], b'NO' + fixture()[2:]):
            with self.subTest(size=len(data)), self.assertRaises(ValueError):
                inspect_pe(data)

    def test_reports_unmatched_signature(self):
        info = inspect_pe(fixture())
        result = inspect_signatures(fixture(), 'constexpr uint8_t absentSignature[] = {0xDE, 0xAD};', info['sections'])
        self.assertEqual(result[0]['matches'], [])


if __name__ == '__main__':
    unittest.main()

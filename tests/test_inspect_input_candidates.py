import struct
import sys
import unittest
import hashlib
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_input_candidates import audit, relative_call_target, section_bytes


class InputCandidatesTest(unittest.TestCase):
    def test_signed_relative_calls_and_file_boundaries(self):
        data = bytearray(32)
        section = {'rva': 0x1000, 'raw_offset': 4, 'raw_size': 16}
        data[4:9] = b'\xe8' + struct.pack('<i', -10)
        self.assertEqual(relative_call_target(data, section, 0x1000), 0xffb)
        data[4:9] = b'\xe8' + struct.pack('<i', 0x100)
        self.assertEqual(relative_call_target(data, section, 0x1000), 0x1105)
        data[4] = 0x90
        with self.assertRaises(ValueError):
            relative_call_target(data, section, 0x1000)
        for address, size in [(0xfff, 1), (0x100f, 2), (0x1000, 0)]:
            with self.subTest(address=address), self.assertRaises(ValueError):
                section_bytes(data, section, address, size)
        with self.assertRaises(ValueError):
            section_bytes(data[:5], section, 0x1000, 5)

    def test_rejects_unrecognized_file_before_parsing(self):
        with self.assertRaisesRegex(ValueError, 'fingerprint mismatch'):
            audit(b'not the game')

    def test_audit_selects_unique_section_and_checks_call(self):
        data = bytearray(64)
        data[4:8] = b'abcd'
        data[12:17] = b'\xe8' + struct.pack('<i', -13)
        info = {'machine': 0x14c, 'image_base': 0x400000, 'sections': [
            {'name': '.text', 'rva': 0x1000, 'raw_offset': 4, 'raw_size': 32},
            {'name': '.text', 'rva': 0x3000, 'raw_offset': 36, 'raw_size': 16},
        ]}
        with patch('inspect_input_candidates.EXPECTED_SHA256', hashlib.sha256(data).hexdigest()), \
             patch('inspect_input_candidates.inspect_pe', return_value=info), \
             patch('inspect_input_candidates.PREFIXES', {0x1000: b'abcd'}), \
             patch('inspect_input_candidates.CALLS', {0x1008: 0x1000}):
            self.assertTrue(audit(data)['direct_calls_match']['0x1008'])
            info['sections'].append(dict(info['sections'][0]))
            with self.assertRaisesRegex(ValueError, 'ambiguous code section'):
                audit(data)


if __name__ == '__main__':
    unittest.main()

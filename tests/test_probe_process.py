import os
import struct
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_pe import inspect_pe
from probe_process import disk_bytes_at_rva, parse_candidate_array, probe, sample_plant_array, validate_candidate_vtables
from test_inspect_pe import fixture


class ProbeTest(unittest.TestCase):
    def test_plant_slot_research_requires_array_observation(self):
        with self.assertRaisesRegex(ValueError, 'require array header observation'):
            probe(1, 'unused', observe_plant_slots=True)

    def test_sun_candidate_requires_explicit_candidate_observation(self):
        with self.assertRaisesRegex(ValueError, 'requires candidate observation'):
            probe(1, 'unused', observe_sun_candidate=True)

    def test_card_candidate_requires_explicit_candidate_observation(self):
        with self.assertRaisesRegex(ValueError, 'requires candidate observation'):
            probe(1, 'unused', observe_card_candidate=True)

    def test_candidate_vtables_are_pinned_to_loaded_base(self):
        validate_candidate_vtables(0x667ba0, 0x667d50, 0x400000)
        with self.assertRaisesRegex(ValueError, 'vtable mismatch'):
            validate_candidate_vtables(0x667ba0, 0, 0x400000)
        with self.assertRaisesRegex(ValueError, 'vtable mismatch'):
            validate_candidate_vtables(0x667ba0, 0x667d50, 0x500000)

    def test_candidate_array_header_limits(self):
        valid = struct.pack('<7I', 0x1234, 1, 2048, 0, 1, 0, 0)
        self.assertEqual(parse_candidate_array(valid), {'used': 1, 'capacity': 2048, 'count': 1})
        for invalid in (valid[:-1], struct.pack('<7I', 0, 1, 2048, 0, 1, 0, 0),
                        struct.pack('<7I', 1, 2, 1, 0, 1, 0, 0),
                        struct.pack('<7I', 1, 1, 4097, 0, 1, 0, 0)):
            with self.subTest(header=invalid), self.assertRaises(ValueError):
                parse_candidate_array(invalid)

    def test_bounded_slot_read_and_generation_race(self):
        board, block = 0x100000, 0x200000
        header = struct.pack('<7I', block, 1, 2048, 0, 1, 2, 0)
        memory = {board + 0xac: header[:16], board + 0xbc: header[16:],
                  board + 0x554c: struct.pack('<I', 0),
                  block + 0x148: struct.pack('<I', 0x10000),
                  block + 0x1c: struct.pack('<I', 2),
                  block + 0x24: struct.pack('<I', 1),
                  block + 0x28: struct.pack('<I', 0)}
        def read(address, size):
            self.assertLessEqual(size, 16)
            self.assertEqual(len(memory[address]), size)
            return memory[address]
        result = sample_plant_array(read, board, {'0x7f8': 0}, True)
        self.assertEqual(result['plant_array_candidate']['capacity'], 2048)
        self.assertEqual(result['plant_slot_candidates'][0]['words']['0x1c'], 2)
        calls = 0
        def changing_read(address, size):
            nonlocal calls
            if address == block + 0x148:
                calls += 1
                if calls == 2:
                    return struct.pack('<I', 0x20000)
            return read(address, size)
        with self.assertRaisesRegex(ValueError, 'slot changed'):
            sample_plant_array(changing_read, board, {'0x7f8': 0}, True)
        with self.assertRaisesRegex(ValueError, 'daytime adventure'):
            sample_plant_array(read, board, {'0x7f8': 1}, True)

    def test_array_header_race_is_rejected(self):
        board = 0x100000
        header = struct.pack('<7I', 0x200000, 1, 2048, 0, 1, 2, 0)
        calls = 0
        def read(address, size):
            nonlocal calls
            if address == board + 0xac:
                calls += 1
                return header[:16] if calls == 1 else bytes(16)
            return header[16:]
        with self.assertRaisesRegex(ValueError, 'array changed'):
            sample_plant_array(read, board, {'0x7f8': 0})

    def test_disk_rva_translation_is_bounded(self):
        data = fixture()
        info = inspect_pe(data)
        self.assertEqual(disk_bytes_at_rva(data, info, 0x1020, 4), b'\x11\x22\x33\x44')
        with self.assertRaises(ValueError):
            disk_bytes_at_rva(data, info, 0x10ff, 2)

    @unittest.skipUnless(sys.platform == 'win32' and sys.maxsize > 2**32, '64-bit Windows only')
    def test_rejects_unrecognized_file_before_process_open(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = Path(directory) / 'unrelated.exe'
            executable.write_bytes(b'unrecognized')
            with self.assertRaisesRegex(ValueError, 'identity mismatch'):
                probe(os.getpid(), executable)


if __name__ == '__main__':
    unittest.main()

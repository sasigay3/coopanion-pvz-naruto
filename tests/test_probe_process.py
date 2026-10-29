import os
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from inspect_pe import inspect_pe
from probe_process import disk_bytes_at_rva, probe
from test_inspect_pe import fixture


class ProbeTest(unittest.TestCase):
    def test_sun_candidate_requires_explicit_candidate_observation(self):
        with self.assertRaisesRegex(ValueError, 'requires candidate observation'):
            probe(1, 'unused', observe_sun_candidate=True)

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

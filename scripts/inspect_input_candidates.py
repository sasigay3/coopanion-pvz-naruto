#!/usr/bin/env python3
"""Audit static input candidates in the pinned executable; never runs the game."""
import argparse
import hashlib
import json
from pathlib import Path
import struct

from inspect_pe import inspect_pe

EXPECTED_SHA256 = 'c536aaff6679fd391ee2ddb4711b4f983f4ce0ea9b32ecc66456cead5fdc59a2'
PREFIXES = {
    0x1384d2: bytes.fromhex('c7 06 b4 f8 66 00 89 5e'),
    0x13861b: bytes.fromhex('c7 07 b4 f8 66 00 e8 9a'),
    0x1392e0: bytes.fromhex('55 8b 6c 24 0c 85 ed 56'),
    0x139390: bytes.fromhex('51 85 db 55 8b 6c 24 0c'),
    0x1394a0: bytes.fromhex('51 53 57 8b fa 83 bf e8'),
}
CALLS = {
    0x14ee1f: 0x139390, 0x15075e: 0x139390,
    0x14ee3d: 0x1392e0, 0x150852: 0x1392e0,
    0x14ee7b: 0x1394a0, 0x14eec8: 0x1394a0, 0x1506e8: 0x1394a0,
}


def section_bytes(data, section, rva, size):
    delta = rva - section['rva']
    if size <= 0 or delta < 0 or delta + size > section['raw_size']:
        raise ValueError('candidate is outside file-backed code')
    start = section['raw_offset'] + delta
    if start < 0 or start + size > len(data):
        raise ValueError('truncated code')
    return data[start:start + size]


def relative_call_target(data, section, rva):
    code = section_bytes(data, section, rva, 5)
    if code[0] != 0xe8:
        raise ValueError('candidate is not a direct relative call')
    return (rva + 5 + struct.unpack_from('<i', code, 1)[0]) & 0xffffffff


def audit(data):
    if hashlib.sha256(data).hexdigest() != EXPECTED_SHA256:
        raise ValueError('executable fingerprint mismatch')
    info = inspect_pe(data)
    if info['machine'] != 0x14c or info['image_base'] != 0x400000:
        raise ValueError('executable layout mismatch')
    # The examined file has multiple sections named .text. Select only a
    # uniquely file-backed section containing all candidate instructions.
    positions = [(rva, len(expected)) for rva, expected in PREFIXES.items()]
    positions.extend((rva, 5) for rva in CALLS)
    sections = [section for section in info['sections'] if section['name'] == '.text'
                and all(section['rva'] <= rva and rva + size <= section['rva'] + section['raw_size']
                        for rva, size in positions)]
    if len(sections) != 1:
        raise ValueError('ambiguous code section')
    section = sections[0]
    prefixes = {f'{rva:#x}': section_bytes(data, section, rva, len(expected)) == expected
                for rva, expected in PREFIXES.items()}
    calls = {f'{rva:#x}': relative_call_target(data, section, rva) == target
             for rva, target in CALLS.items()}
    if not all(prefixes.values()) or not all(calls.values()):
        raise ValueError('candidate bytes or call targets differ')
    return {'research_only': True, 'sha256': EXPECTED_SHA256,
            'prefixes_match': prefixes, 'direct_calls_match': calls}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    try:
        result = audit(args.executable.read_bytes())
    except (OSError, ValueError) as error:
        parser.exit(1, f'audit failed: {error}\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()

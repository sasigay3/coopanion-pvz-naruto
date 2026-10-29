#!/usr/bin/env python3
"""Read-only PE inspection. Never executes the target or writes to its process."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct


def inspect_pe(data: bytes) -> dict:
    def unpack(fmt: str, offset: int):
        size = struct.calcsize(fmt)
        if offset < 0 or offset + size > len(data):
            raise ValueError('truncated PE file')
        return struct.unpack_from(fmt, data, offset)

    if data[:2] != b'MZ':
        raise ValueError('missing DOS signature')
    (pe,) = unpack('<I', 0x3c)
    if data[pe:pe + 4] != b'PE\0\0':
        raise ValueError('missing PE signature')
    machine, count, timestamp, _, _, optsize, _ = unpack('<HHIIIHH', pe + 4)
    if not 0 < count <= 96:
        raise ValueError('invalid section count')
    opt = pe + 24
    (magic,) = unpack('<H', opt)
    if magic != 0x10b or optsize < 96:
        raise ValueError('expected PE32 optional header')
    if opt + optsize > len(data):
        raise ValueError('truncated optional header')
    entry = unpack('<I', opt + 16)[0]
    base = unpack('<I', opt + 28)[0]
    image_size = unpack('<I', opt + 56)[0]
    checksum = unpack('<I', opt + 64)[0]
    characteristics = unpack('<H', opt + 70)[0]
    dirs = unpack('<I', opt + 92)[0]
    if dirs > 5 and optsize >= 96 + 6 * 8:
        reloc_rva, reloc_size = unpack('<II', opt + 96 + 5 * 8)
    else:
        reloc_rva, reloc_size = 0, 0
    sections = []
    for i in range(count):
        start = opt + optsize + 40 * i
        if start + 40 > len(data):
            raise ValueError('truncated section table')
        name = data[start:start + 8].split(b'\0', 1)[0].decode('ascii', 'replace')
        virtual_size, rva, raw_size, raw_offset = unpack('<IIII', start + 8)
        if raw_size and raw_offset + raw_size > len(data):
            raise ValueError('section exceeds file')
        sections.append({'name': name, 'rva': rva, 'virtual_size': virtual_size,
                         'raw_size': raw_size, 'raw_offset': raw_offset})
    return {'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest(),
            'machine': machine, 'pe_magic': magic, 'timestamp': timestamp,
            'entry_rva': entry, 'image_base': base, 'image_size': image_size,
            'checksum': checksum, 'dll_characteristics': characteristics,
            'dynamic_base': bool(characteristics & 0x40),
            'relocation_rva': reloc_rva, 'relocation_size': reloc_size,
            'sections': sections}


def inspect_signatures(data: bytes, profile: str, sections: list) -> list:
    results = []
    pattern = r'constexpr\s+uint8_t\s+(\w+Signature)\[\]\s*=\s*\{([^}]+)\};'
    for match in re.finditer(pattern, profile):
        signature = bytes(int(x, 16) for x in re.findall(r'0x([0-9A-Fa-f]{2})\b', match.group(2)))
        if not signature:
            continue
        positions = []
        offset = data.find(signature)
        while offset != -1 and len(positions) < 10:
            rva = next((s['rva'] + offset - s['raw_offset'] for s in sections
                        if s['raw_offset'] <= offset and offset + len(signature) <= s['raw_offset'] + s['raw_size']), None)
            positions.append({'file_offset': offset, 'rva': rva})
            offset = data.find(signature, offset + 1)
        results.append({'name': match.group(1), 'matches': positions, 'truncated': offset != -1})
    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    parser.add_argument('--compare-profile', type=Path, help='Optional C++ header containing byte signatures')
    args = parser.parse_args()
    data = args.executable.read_bytes()
    info = inspect_pe(data)
    if args.compare_profile:
        info['signature_comparison'] = inspect_signatures(data, args.compare_profile.read_text(encoding='utf-8'), info['sections'])
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    main()

"""Inspect PE32 static import names without loading any executable or DLL."""
import argparse
import json
from pathlib import Path
import struct
from inspect_pe import inspect_pe


def imported_libraries(data: bytes, info: dict) -> list:
    def read(fmt, pos):
        size = struct.calcsize(fmt)
        if pos < 0 or pos + size > len(data):
            raise ValueError('truncated PE structure')
        return struct.unpack_from(fmt, data, pos)

    pe = read('<I', 0x3c)[0]
    optional = pe + 24
    optional_size = read('<H', pe + 20)[0]
    count = read('<I', optional + 92)[0]
    if count < 2 or optional_size < 112:
        return []
    import_rva, import_size = read('<II', optional + 104)
    if not import_rva and not import_size:
        return []
    if not import_rva or import_size < 20:
        raise ValueError('invalid import directory')

    def translate(rva, length):
        for s in info['sections']:
            delta = rva - s['rva']
            if delta >= 0 and delta + length <= s['raw_size']:
                return s['raw_offset'] + delta
        raise ValueError('RVA outside section data')

    # Bound both descriptor count and library name length for malformed files.
    limit = min(import_size // 20, 4096)
    libraries = []
    for index in range(limit):
        pos = translate(import_rva + index * 20, 20)
        descriptor = read('<IIIII', pos)
        if not any(descriptor):
            return libraries
        name_rva = descriptor[3]
        chars = bytearray()
        for i in range(260):
            byte = data[translate(name_rva + i, 1)]
            if not byte:
                break
            chars.append(byte)
        else:
            raise ValueError('unterminated import name')
        if not chars:
            raise ValueError('empty import name')
        libraries.append(chars.decode('ascii', 'strict'))
    raise ValueError('unterminated import directory')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('executable', type=Path)
    args = parser.parse_args()
    data = args.executable.read_bytes()
    info = inspect_pe(data)
    print(json.dumps({'sha256': info['sha256'], 'imported_libraries': imported_libraries(data, info)}, indent=2))


if __name__ == '__main__':
    main()

"""Bounded, read-only check of one explicit x86 process; no injection or writes."""
import argparse
import ctypes
from ctypes import wintypes
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
from inspect_pe import inspect_pe

EXPECTED_HASH = 'c536aaff6679fd391ee2ddb4711b4f983f4ce0ea9b32ecc66456cead5fdc59a2'
EXPECTED_SIZE = 7_340_032
# Candidate only. Do not dereference it or treat it as an established game object.
GLOBAL_RVA = 0x2a9ec0
CODE_RVAS = (0x4e8a0, 0x4e8c0, 0xb069)


def disk_bytes_at_rva(data, info, rva, size):
    for section in info['sections']:
        delta = rva - section['rva']
        if 0 <= delta and delta + size <= section['raw_size']:
            start = section['raw_offset'] + delta
            return data[start:start + size]
    raise ValueError('RVA is not backed by file data')


def probe(pid, expected_path, observe_candidate=False, observe_sun_candidate=False):
    if observe_sun_candidate and not observe_candidate:
        raise ValueError('sun candidate requires candidate observation')
    if sys.platform != 'win32':
        raise OSError('Windows is required')
    if ctypes.sizeof(ctypes.c_void_p) != 8:
        raise OSError('64-bit Python is required to enumerate a 32-bit process')
    expected_path = os.path.normcase(os.path.realpath(expected_path))
    data = Path(expected_path).read_bytes()
    if len(data) != EXPECTED_SIZE or hashlib.sha256(data).hexdigest() != EXPECTED_HASH:
        raise ValueError('executable identity mismatch')
    info = inspect_pe(data)
    if info['machine'] != 0x14c:
        raise ValueError('expected an x86 PE')

    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    psapi = ctypes.WinDLL('psapi', use_last_error=True)
    kernel.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    kernel.OpenProcess.restype = wintypes.HANDLE
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    kernel.QueryFullProcessImageNameW.restype = wintypes.BOOL
    kernel.IsWow64Process.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.BOOL)]
    kernel.IsWow64Process.restype = wintypes.BOOL
    kernel.ReadProcessMemory.argtypes = [wintypes.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.POINTER(ctypes.c_size_t)]
    kernel.ReadProcessMemory.restype = wintypes.BOOL
    psapi.EnumProcessModulesEx.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.HMODULE), wintypes.DWORD, ctypes.POINTER(wintypes.DWORD), wintypes.DWORD]
    psapi.EnumProcessModulesEx.restype = wintypes.BOOL
    psapi.GetModuleFileNameExW.argtypes = [wintypes.HANDLE, wintypes.HMODULE, wintypes.LPWSTR, wintypes.DWORD]
    psapi.GetModuleFileNameExW.restype = wintypes.DWORD

    # PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, with no write or thread rights.
    handle = kernel.OpenProcess(0x0400 | 0x0010, False, pid)
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        image = ctypes.create_unicode_buffer(32768)
        length = wintypes.DWORD(len(image))
        if not kernel.QueryFullProcessImageNameW(handle, 0, image, ctypes.byref(length)):
            raise ctypes.WinError(ctypes.get_last_error())
        if os.path.normcase(os.path.realpath(image.value)) != expected_path:
            raise ValueError('process image path mismatch')
        wow = wintypes.BOOL()
        if not kernel.IsWow64Process(handle, ctypes.byref(wow)):
            raise ctypes.WinError(ctypes.get_last_error())
        if not wow.value:
            raise ValueError('target is not an x86 WOW64 process')
        modules = (wintypes.HMODULE * 1024)()
        needed = wintypes.DWORD()
        if not psapi.EnumProcessModulesEx(handle, modules, ctypes.sizeof(modules), ctypes.byref(needed), 1):
            raise ctypes.WinError(ctypes.get_last_error())
        if needed.value > ctypes.sizeof(modules):
            raise ValueError('module list exceeds safe limit')
        base = None
        for module in modules[:needed.value // ctypes.sizeof(wintypes.HMODULE)]:
            name = ctypes.create_unicode_buffer(32768)
            if not psapi.GetModuleFileNameExW(handle, module, name, len(name)):
                raise ctypes.WinError(ctypes.get_last_error())
            if os.path.normcase(os.path.realpath(name.value)) == expected_path:
                base = int(module)
                break
        if base is None:
            raise ValueError('main image missing from module list')

        def read(address, size):
            if size <= 0 or size > 16:
                raise ValueError('read exceeds safety bound')
            result = (ctypes.c_ubyte * size)()
            count = ctypes.c_size_t()
            if not kernel.ReadProcessMemory(handle, address, result, size, ctypes.byref(count)):
                raise ctypes.WinError(ctypes.get_last_error())
            if count.value != size:
                raise ValueError('partial process read')
            return bytes(result)

        checks = [read(base + rva, 8) == disk_bytes_at_rva(data, info, rva, 8) for rva in CODE_RVAS]
        if not all(checks):
            return {'pid': pid, 'image_matches': True, 'code_matches_file': checks,
                    'candidate_checked': False, 'reason': 'runtime code differs from file'}
        pointer = struct.unpack('<I', read(base + GLOBAL_RVA, 4))[0]
        result = {'pid': pid, 'image_matches': True, 'code_matches_file': checks,
                  'loaded_at_preferred_base': base == info['image_base'],
                  'candidate_checked': True, 'candidate_nonzero': pointer != 0}
        if observe_candidate:
            if pointer == 0 or pointer > 0xfffff7bf:
                raise ValueError('candidate pointer is null or outside 32-bit bounds')
            # No pointer obtained here is dereferenced. Names are intentionally neutral:
            # the object type and the meaning of these words remain unverified.
            offsets = (0, 0x768, 0x7f8, 0x7fc, 0x82c)
            words = {hex(offset): struct.unpack('<I', read(pointer + offset, 4))[0]
                     for offset in offsets}
            if struct.unpack('<I', read(base + GLOBAL_RVA, 4))[0] != pointer:
                raise ValueError('candidate changed during observation')
            result['candidate_words'] = words
            if observe_sun_candidate:
                board_candidate = words['0x768']
                if words['0x7fc'] != 3 or board_candidate == 0 or board_candidate > 0xffffaa9b:
                    raise ValueError('candidate board is absent or outside 32-bit bounds')
                sun_candidate = struct.unpack('<I', read(board_candidate + 0x5560, 4))[0]
                # Discard if either pointer changed during the bounded read.
                if (struct.unpack('<I', read(base + GLOBAL_RVA, 4))[0] != pointer or
                    struct.unpack('<I', read(pointer + 0x768, 4))[0] != board_candidate):
                    raise ValueError('candidate pointers changed during observation')
                result['sun_candidate'] = sun_candidate
        return result
    finally:
        kernel.CloseHandle(handle)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pid', required=True, type=int)
    parser.add_argument('--executable', required=True, type=Path)
    parser.add_argument('--observe-candidate', action='store_true', help='Read five unverified words from the candidate object')
    parser.add_argument('--observe-sun-candidate', action='store_true', help='Read one unverified word from the board candidate (requires --observe-candidate)')
    args = parser.parse_args()
    try:
        print(json.dumps(probe(args.pid, args.executable, args.observe_candidate, args.observe_sun_candidate), indent=2))
    except (OSError, ValueError) as exc:
        print(json.dumps({'error': str(exc)}), file=sys.stderr)
        raise SystemExit(1)


if __name__ == '__main__':
    main()

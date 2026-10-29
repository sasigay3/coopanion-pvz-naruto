# Initial compatibility investigation

Status: **static investigation only; the extension does not run the Naruto build yet.** No game executable, asset, save, process memory or local path is included in the repository.

The target executable supplied by the operator was read as a file on 2026-10-30. Its SHA-256 is `c536aaff6679fd391ee2ddb4711b4f983f4ce0ea9b32ecc66456cead5fdc59a2` and its size is 7,340,032 bytes. Its Windows file version is 1.0.0.1051. These facts identify the examined file; they do **not** establish compatibility or guarantee the integrity of accompanying files.

- PE32 / x86 (`0x14c`), preferred image base `0x400000`, entry RVA `0x6ea62c`, image size `0x7b7000`, timestamp `0x49ecf563`, checksum `0x70cdb3`.
- `DYNAMIC_BASE` is unset, but a relocation directory exists (RVA `0x77f000`, size `0x518`). Actual loaded base and relocation behavior must be checked; fixed addresses cannot be assumed.
- The static import table names `GDI42.dll` and `gdi.dll`; both names also occur in the examined game directory. Static imports do not prove which file Windows will load, whether further DLLs are loaded dynamically, or whether any DLL is trustworthy. The dependent files and real load paths require separate verification.
- The locally examined directory contains no `.pak` files. The HD extension's mandatory `main.pak` check cannot be reused without designing and verifying an appropriate target-specific policy.
- Comparing the complete byte arrays named `*Signature` in HD source `profile.h` at commit `f506025` against this executable found one matching array at RVA `0x15a9b5` and 13 with no exact match. An exact match alone does not identify a function or validate a calling convention. This scan does not cover signatures outside that header or runtime-generated code.
- The local HD runtime differs from the public checkout. Its changes to `src/events.ts` add livestream cues; those changes are not suitable for this independent game extension. Other differences need individual review.

Reproduce the static PE and exact-byte scan locally with Python 3:

```powershell
python scripts/inspect_pe.py <path-to-game-exe> --compare-profile <path-to-hd-profile.h>
python scripts/inspect_imports.py <path-to-game-exe>
python -m unittest discover -s tests
```

The tool only reads the provided files; its output may contain hashes and offsets. Review any captured output before publishing it. Later work must verify executable and resource identity, loaded image, object layouts and calling conventions before any injection, process write or game input. Test fixtures can validate parsing but cannot prove game compatibility.

## Compatibility matrix

| Capability | Status |
| --- | --- |
| Static PE identity and exact-signature comparison | Implemented; exercised on the supplied file |
| Static imported-library inventory | Implemented; actual loaded paths not verified |
| Loaded process identity and safe runtime observation | Not implemented |
| Menus, selection, board observation and actions | Not implemented |
| Automated play and special modes | Not implemented |
| Optional assists | Not implemented |
| World identity and fail-closed scaffold | Implemented; not installed or tested alongside other extensions |
| Installation alongside other extensions | Not implemented |

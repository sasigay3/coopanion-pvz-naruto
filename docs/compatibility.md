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
scripts/test-native.cmd
```

The tool only reads the provided files; its output may contain hashes and offsets. Review any captured output before publishing it. Later work must verify executable and resource identity, loaded image, object layouts and calling conventions before any injection, process write or game input. Test fixtures can validate parsing but cannot prove game compatibility.

## Preliminary static pointer candidate

For the examined SHA-256 only, a read-only x86 disassembly found code loading a pointer from preferred-base VA `0x006a9ec0` at RVAs `0x4e8a0` and `0x4e8c0`. The latter checks that pointer, then reads a pointer at offset `0x82c` and checks it before dereferencing it. Another instruction at RVA `0x57e0b` loads the same global and accesses an object at offset `0x768`. The global is located in the virtual range of the first `.data` section; it is not evidence that the pointer is initialized or valid at runtime.

This is an **unverified candidate**, not a game profile. Static constructor/destructor code at RVAs `0x4eacb` and `0x4ee11` writes two vtable constants that also match the sampled object in this run. That strengthens the candidate identity, but the field meanings and behavior across modes are not yet established. No process was started, attached to, modified, or injected to make these observations. The fields at `0x768` and `0x82c` remain unverified. The optional probe can sample these words without dereferencing their values; it does not interpret their meaning. Further work requires independent code-path analysis and bounded read-only verification against visible in-game states.

## First live read-only check

On 2026-10-30, after backing up the two possible local save directories and asking the operator to start the game, a read-only probe opened the identified x86 process with query and memory-read rights only. The image path and file SHA-256 matched the examined executable; its loaded base was the preferred base, both eight-byte candidate code checks matched the file, and the candidate global contained a nonzero pointer. The initial probe did not dereference that pointer, infer its type, operate the game, or write to the process. One observed run does not establish behavior in other scenes or after restart.

A separate process-module query showed local modules named `gdi.dll`, `GDI42.dll`, `bass.dll`, and `UpdateCheck.dll` in that run. This is an observation, not a security assessment or a complete inventory of all files that may load later. The actual save location and any sharing with other builds remain unconfirmed.

The bounded probe takes an explicit PID and executable path: `python scripts/probe_process.py --pid <pid> --executable <path>`. It verifies the file fingerprint, process image path, x86 architecture, loaded image and eight eight-byte on-disk code prefixes before reading the candidate global. Any mismatch stops the candidate read. With the explicit `--observe-candidate` flag, it can additionally check two candidate vtables and sample four unverified 32-bit words and checks that the global pointer did not change during the sample; With a second explicit `--observe-sun-candidate` flag, it follows the candidate at offset `0x768` only when the word at `0x7fc` is 3, reads one word at `0x5560`, then checks both pointers again. A third explicit `--observe-plant-candidate` flag reads and validates the 28-byte candidate array header twice without following its element buffer. An additional research-only `--observe-plant-slots` flag samples a maximum of 16 slots after checking candidate scene, mode and background values and the slot IDs. It must only be used after an operator confirms a visible ordinary daytime board: those candidate values alone cannot guarantee visibility. These values are still unverified and do not authorize game actions.

## First board and sun candidates

In the same live run, the operator reported the main menu; a sample around that time had zero at candidate offset `0x768` and 1 at `0x7fc`. After the operator entered the first level, these values were nonzero and 3 respectively. A statically observed instruction at RVA `0xb069` writes 150 to offset `0x5560` of an object. Reading that word through the `0x768` pointer yielded 150, matching the operator's reported sun. After the operator planted a plant, the same word read 75, again matching the reported display. This is preliminary correlation, not proof of object type, scene semantics, correct behavior in other modes, or safe writes. Further independent samples, transition and restart checks are needed.

## First array candidate

Static code at RVA `0x8771` refers to offset `0xac` in a structure with repeated 28-byte-spaced fields. In the first-level sample, the header at candidate board + `0xac` looked like a DataArray with one used element, one live element, and capacity 2048, after the operator reported planting one plant. Static call sites use this candidate array in conjunction with a function at RVA `0x1de80` that indexes elements using a stride of `0x300` and accesses an ID-like word at offset `0x148`; another initialization path writes candidate row and column fields at offsets `0x1c` and `0x28`. These observations are consistent with a plant array, but the entry semantics and behavior after transitions remain unproven. The HD reader's 1024-element capacity limit would reject this observed header, and its plant stride is `0x14c`; neither can be copied unchanged. On the operator-confirmed first daytime level, a single slot passed the candidate ID check and returned raw words at offsets `0x1c`, `0x24`, and `0x28`; their meanings and the plant type are not yet confirmed.

A separate candidate array at board offset `0x90` is passed to code at RVA `0x1dda0`, where element indexing uses `0x408` and an ID-like word at `0x404`. Its entity type has not been established in a live test. The HD iterator copies an entire stride into a `0x200` buffer and rejects larger strides, so this candidate also calls for a redesigned, bounded reader rather than changing constants alone. No elements from this candidate array have been read. The new standalone native reader samples explicitly requested fields and IDs instead of copying full strides, bounds lengths and addresses, and rejects ID or header changes before publishing results. It has only synthetic fixture coverage and is not yet connected to the game; the field reads are not atomic and visibility filtering remains the caller's responsibility.

The operator's local possible save folders were backed up before the game started. Comparing those folders with the backup after the interaction showed a modified file in one and a missing file in the other. This does not by itself establish causation or which build owns either folder. Do not restore, overwrite, or delete files automatically; establish ownership and discuss restoration before changing saves.

## Operator-provided control console

The operator provided an image of an in-game control console with options labelled automatic collection, free planting, ignore cooldown, health display, and overlapping planting, plus a difficulty setting. Their real behavior, persisted state, implementation and interaction with the extension have not been verified. In particular, free planting is not the same feature as a sun reserve, a one-shot sun increase, or a bonus per verified sun collection. Future assist work should evaluate existing controls before duplicating functionality, and must independently test each requested feature and its off state.

## Compatibility matrix

| Capability | Status |
| --- | --- |
| Static PE identity and exact-signature comparison | Implemented; exercised on the supplied file |
| Static imported-library inventory | Implemented; actual loaded paths not verified |
| Loaded process identity and bounded candidate-pointer check | Implemented; checked in one live run, object type not established |
| Scene, board and sun candidates | One live correlation; not verified for general use |
| Plant array header candidate | One live correlation; element layout and mode coverage not verified |
| Bounded native array reader | Implemented and fixture-tested; not connected to the game |
| Entity observation | Not implemented |
| Menus, selection, board observation and actions | Not implemented |
| Automated play and special modes | Not implemented |
| Optional assists | Not implemented |
| World identity and fail-closed scaffold | Implemented; not installed or tested alongside other extensions |
| Installation alongside other extensions | Not implemented |

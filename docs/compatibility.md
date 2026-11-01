# Initial compatibility investigation

Status: **research only; the extension does not run the Naruto build yet.** No game executable, asset, save, process memory or local path is included in the repository.

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

This is an **unverified candidate**, not a game profile. Static constructor/destructor code at RVAs `0x4eacb` and `0x4ee11` writes two vtable constants that also match the sampled object in this run. That strengthens the candidate identity, but the field meanings and behavior across modes are not yet established. Those static observations did not start or attach to a process, modify it, or inject code. The fields at `0x768` and `0x82c` remain unverified. The optional probe can sample these words without dereferencing their values; it does not interpret their meaning. Further work requires independent code-path analysis and bounded read-only verification against visible in-game states.

## First live read-only check

On 2026-10-30, after backing up the two possible local save directories and asking the operator to start the game, a read-only probe opened the identified x86 process with query and memory-read rights only. The image path and file SHA-256 matched the examined executable; its loaded base was the preferred base, both eight-byte candidate code checks matched the file, and the candidate global contained a nonzero pointer. The initial probe did not dereference that pointer, infer its type, operate the game, or write to the process. One observed run does not establish behavior in other scenes or after restart.

A separate process-module query showed local modules named `gdi.dll`, `GDI42.dll`, `bass.dll`, and `UpdateCheck.dll` in that run. This is an observation, not a security assessment or a complete inventory of all files that may load later. The actual save location and any sharing with other builds remain unconfirmed.

The bounded probe takes an explicit PID and executable path: `python scripts/probe_process.py --pid <pid> --executable <path>`. It verifies the file fingerprint, process image path, x86 architecture, loaded image and ten eight-byte on-disk code prefixes before reading the candidate global. Any mismatch stops the candidate read. With the explicit `--observe-candidate` flag, it can additionally check two candidate vtables and sample four unverified 32-bit words and checks that the global pointer did not change during the sample; With a second explicit `--observe-sun-candidate` flag, it follows the candidate at offset `0x768` only when the word at `0x7fc` is 3, reads one word at `0x5560`, then checks both pointers again. A third explicit `--observe-plant-candidate` flag reads and validates the 28-byte candidate array header twice without following its element buffer. An additional research-only `--observe-plant-slots` flag samples a maximum of 16 slots after checking candidate scene, mode and background values and the slot IDs. It must only be used after an operator confirms a visible ordinary daytime board: those candidate values alone cannot guarantee visibility. A separate `--observe-card-candidate` flag reads only a candidate card-bank count after checking both pointers; it is not card observation. These values are still unverified and do not authorize game actions.

## Standalone native candidate snapshot

A separate C++ research component checks the ten candidate code prefixes, the preferred image base and both candidate vtables before sampling the candidate scene, mode, sun and card count. It repeats pointer, scene, mode and requested-value checks and returns no partial snapshot if they change. The component does not open a process or authenticate its executable and must not be called on a live process until a separate file-and-process identity gate is in place. Checks around sequential reads reduce races; they cannot make the sample atomic or prove any field's game meaning. The World remains disabled. The ten stored prefixes were independently checked against the examined local executable; its implementation has synthetic x86 fixture coverage only and has not been tested against the game.

## Native read-only identity gate

A separate x86 Windows component now opens only an explicitly selected existing PID, with process query and memory-read rights. It verifies the supplied file's size, PE fields and SHA-256, holds its read-only file handle, checks the actual process image and first module against the same file identity, requires an x86 WOW64 process and the examined image layout, and validates the candidate code and vtables before exposing the research snapshot method. The gate cannot start a process, write memory, inject code, create remote threads or send input. Each snapshot repeats the code and object checks. A file's current identity and ten short in-memory prefixes do not prove every loaded byte or associated DLL is trustworthy, and the sequential reads are not atomic.

The native file gate accepted the examined local executable and rejected a synthetic same-size PE with the wrong digest and malformed headers. A standalone research CLI combines this gate with the candidate reader and emits bounded candidate-only JSON. Its argument validation and rejection of an unrelated process have been tested; it is not a World tool and does not launch the game. A separate TypeScript parser rejects malformed, excessive, or unexpected responses instead of treating them as game state. An internal client invokes the helper with an argument array and no shell, limits output and execution time, and checks a caller-supplied helper digest before execution. The caller must obtain the helper and digest from a trusted build; the digest does not authenticate the source or eliminate a file-replacement race. The client is not connected to the World. A process test paired that real file with the test runner's own unrelated PID and confirmed rejection. Separately, an x86 fixture exercised the same OS-level process/file matching and bounded read primitives against its own process, and verified that its query-and-read handle could not write even to that fixture. The fixture did not use or modify the game process. Synthetic tests exercise bad signatures, object identity, changed pointers, scene transitions and failures without a game. **The native gate has not yet been tested against the actual running target**, and it is not connected to the World. The earlier positive live sample used only the Python research probe.

## First board and sun candidates

In the same live run, the operator reported the main menu; a sample around that time had zero at candidate offset `0x768` and 1 at `0x7fc`. After the operator entered the first level, these values were nonzero and 3 respectively. A statically observed instruction at RVA `0xb069` writes 150 to offset `0x5560` of an object. Reading that word through the `0x768` pointer yielded 150, matching the operator's reported sun. After the operator planted a plant, the same word read 75, again matching the reported display. This is preliminary correlation, not proof of object type, scene semantics, correct behavior in other modes, or safe writes. Further independent samples, transition and restart checks are needed.

## First array candidate

Static code at RVA `0x8771` refers to offset `0xac` in a structure with repeated 28-byte-spaced fields. In the first-level sample, the header at candidate board + `0xac` looked like a DataArray with one used element, one live element, and capacity 2048, after the operator reported planting one plant. Static call sites use this candidate array in conjunction with a function at RVA `0x1de80` that indexes elements using a stride of `0x300` and accesses an ID-like word at offset `0x148`; another initialization path writes candidate row and column fields at offsets `0x1c` and `0x28`. These observations are consistent with a plant array, but the entry semantics and behavior after transitions remain unproven. The HD reader's 1024-element capacity limit would reject this observed header, and its plant stride is `0x14c`; neither can be copied unchanged. On the operator-confirmed first daytime level, a single slot passed the candidate ID check and returned raw words at offsets `0x1c`, `0x24`, and `0x28`; their meanings and the plant type are not yet confirmed.

A separate candidate array at board offset `0x90` is passed to code at RVA `0x1dda0`, where element indexing uses `0x408` and an ID-like word at `0x404`. Its entity type has not been established in a live test. The HD iterator copies an entire stride into a `0x200` buffer and rejects larger strides, so this candidate also calls for a redesigned, bounded reader rather than changing constants alone. No elements from this candidate array have been read. The new standalone native reader samples explicitly requested fields and IDs instead of copying full strides, bounds lengths and addresses, and rejects ID or header changes before publishing results. It has only synthetic fixture coverage and is not yet connected to the game; the field reads are not atomic and visibility filtering remains the caller's responsibility.

The operator's local possible save folders were backed up before the game started. Comparing those folders with the backup after the interaction showed a modified file in one and a missing file in the other. Static inspection later found the missing file's directory and filename pattern in the target executable. That strengthens the possibility of shared save access but does not establish causation or ownership. The folders changed further after that backup; do not restore, overwrite, or delete files automatically. Establish ownership and discuss restoration before changing saves.

## First card-bank candidate

Static code associates candidate board offset `0x144` with an allocated object; other code uses its word at offset `0x24` as a loop bound and advances through entries at `0x28` with a stride of `0x50`. One first-level sample produced a candidate count of 5. The field interpretation and correspondence with visible cards still require independent confirmation; no card identity, cost or cooldown has been established. The research probe reads only that count and caps it at 10, without following individual entries.

## Operator-provided control console

The operator provided an image of an in-game control console with options labelled automatic collection, free planting, ignore cooldown, health display, and overlapping planting, plus a difficulty setting. The locally supplied text resources describe the auto-collection option as covering sun and coins and describe a keyboard toggle; they also describe free planting and ignoring card cooldown. These descriptions corroborate the labels but do not verify real behavior, persisted state, implementation, or interaction with the extension. In particular, free planting is not the same feature as a sun reserve, a one-shot sun increase, or a bonus per verified sun collection. Future assist work should evaluate existing controls before duplicating functionality, and must independently test each requested feature and its off state.

## Version-independent action safeguards

A prototype input arbiter, action ticket fence and guarded action flow are covered by simulated tests. They serialize competing callers, reject stale sessions and runs before dispatch, require a correlated terminal result plus a newer same-run observation and a caller-supplied visible-effect check before reporting verification, and close the arbiter when native input release is uncertain. Cancellation of an active request does not automatically release the lease. These are building blocks, not a completed queue or a native bridge: their safety depends on the future driver actually rechecking context, confirming native input release, producing immutable fresh snapshots and implementing correct game-specific evidence. They are not connected to the disabled World and have not sent game input.

## HD baseline reuse checks

In an isolated archive of HD commit `f506025`, five selected test files for protocol, conditions, triggers, executor freshness, and delivery freshness produced 264 passes and one timeout. Instrumenting only the temporary archive showed that the timed-out delivery test received and rendered the intended batch, then hung during cleanup. Starting `world.stop()` while fake timers were active and advancing those timers allowed that single instrumented test to pass. This is evidence about that fixture's cleanup ordering, not a proven fix for production shutdown, other failing tests, or Naruto compatibility. A second isolated selection covering executor, runtime, bridge, lifecycle and automatic collection produced 179 passes and three failures: one expected status object lacks assist fields, one expected receipt text differs from the implementation, and one expects the sun pickup before the coin while the observed call order was reversed. Whether the ordering difference represents a behavioral defect or a stale expectation remains unverified. The original HD checkout was not modified. Review and adapt these components only after target snapshots and action contracts are verified.

## Compatibility matrix

| Capability | Status |
| --- | --- |
| Static PE identity and exact-signature comparison | Implemented; exercised on the supplied file |
| Static imported-library inventory | Implemented; some local modules observed in one run, later loads unverified |
| Loaded process identity and bounded candidate-pointer check | Python probe checked in one live run; native process gate and TypeScript client have no positive live test |
| Scene, board and sun candidates | Python research probe has one live correlation; standalone native snapshot has synthetic tests only |
| Plant array header candidate | One live correlation; element layout and mode coverage not verified |
| Bounded native array reader | Implemented and fixture-tested; not connected to the game |
| Card-bank count candidate | Static evidence and one live sample; not compared with visible cards |
| Entity observation | Not implemented |
| Action arbitration and verification prototype | Synthetic tests only; no native driver or game actions |
| Menus, selection, board observation and actions | Not implemented |
| Automated play and special modes | Not implemented |
| Optional assists | Not implemented |
| World identity and fail-closed scaffold | Implemented; not installed or tested alongside other extensions |
| Installation alongside other extensions | Not implemented |

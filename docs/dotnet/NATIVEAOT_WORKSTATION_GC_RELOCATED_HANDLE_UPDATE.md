# NativeAOT Workstation GC — C011EC35 Relocated Short-Weak Handle Update

## Result

C011EC35 reached Level 3 on three fresh QEMU 11.0.0 boots.

The C34 managed root was relocated by production Workstation GC from
`0x0000000100A01F38` to `0x0000000100901F50`. The same C33 short-weak handle
was structurally matched in the authentic handle-relocation scan while it
still contained the old address. Production `UpdatePointer` dispatched
production `GCHeap::Relocate`, which rewrote the handle slot to the same new
address. Collection 1 completed, `RestartEE` returned, and the C33 managed
workload resumed while the helper frame was still active.

No diagnostic code copied an object or wrote a root/handle slot. The only slot
mutation was the locked production store in `GCHeap::Relocate`.

## Locked identity and repository boundary

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- GC interfaces `5.3 / 2`
- locked runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- QEMU `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`
- branch `v1.1_DOTNET_SUPPORT`
- upstream `origin/v1.1_DOTNET_SUPPORT`
- actual starting HEAD `0d057f6137a09166976a8c586681117043b329d5`
- actual starting divergence `ahead 0, behind 0`
- starting worktree clean; no untracked files

The expected starting “ahead 1” state was not present; actual Git state was
treated as authoritative. No reset, amend, rebase, squash, discard, or push
was used. No runtime version or GC-flavor change was made.

## C34 boundary retained

Every authoritative C35 run recorded:

| C34 evidence | Result |
|---|---:|
| `GCToEEInterface::GcScanRoots` entries / returns | `1 / 1` |
| C34 root rewrite | `1` |
| old managed target | `0x0000000100A01F38` |
| post-C34 managed-root value | `0x0000000100901F50` |
| condemned generation | `0` |
| maximum generation | `2` |
| compacting / relocating | `1 / 1` |

The C34 relocation-root scan returned normally. Relocation stack/root
enumeration was not reopened or changed.

At C35 entry the C33 weak slot still contained the C34 old target. The slot
was not manually repaired.

## Locked source chronology

The exact locked Workstation path is:

1. `src/coreclr/gc/gc.cpp:36859`, `gc_heap::relocate_phase` enters relocation.
2. `gc.cpp:36897`, `GCScan::GcScanRoots(GCHeap::Relocate, ...)` relocates the
   managed stack/root references. The C34 caller now returns normally.
3. In the active non-card-stealing WKS order, `gc.cpp:37000`, after survivor
   and finalization relocation, `GCScan::GcScanHandles(GCHeap::Relocate, ...)`
   begins handle relocation. The card-stealing build branch at `gc.cpp:36926`
   is retained and has the corresponding handle call before the card work.
4. `src/coreclr/gc/gcscan.cpp:160`, `GCScan::GcScanHandles` selects the
   non-promotion branch and calls, in order,
   `Ref_UpdatePointers`, `Ref_UpdatePinnedPointers`,
   `Ref_ScanDependentHandlesForRelocation`, and
   `Ref_ScanWeakInteriorPointersForRelocation`.
5. `src/coreclr/gc/objecthandle.cpp:1571`, `Ref_UpdatePointers` calls
   `HndScanHandlesForGC` with the relocation callback `UpdatePointer`.
6. `src/coreclr/gc/handletablescan.cpp:413-442`, the real table/segment/block
   scanner dispatches the callback for each inspected non-null slot.
7. `src/coreclr/gc/objecthandle.cpp:406`, `UpdatePointer` invokes the
   production callback passed by `Ref_UpdatePointers`.
8. `src/coreclr/gc/gc.cpp:49546`, `GCHeap::Relocate` resolves the old address
   through the brick/relocation metadata and performs the production store at
   `gc.cpp:49614`, `*ppObject = (Object*)pheader`.
9. `gc.cpp:34192-34195` continues to `compact_phase`,
   `fix_generation_bounds`, segment rearrangement, and generation cleanup.
10. `clear_gen1_cards` is reached at `gc.cpp:34370` in the completed path.
11. `gc.cpp:50707`, `do_post_gc`, calls `GCToEEInterface::GcDone`.
12. `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:55`,
   `GCToEEInterface::RestartEE` calls `ResumeAllThreads(true)`, clears
   `GCInProgress`, unlocks the ThreadStore, and returns.
13. The C33 collection API returns through
   `guideXosNativeAotC011EC33GetCompletedCollections`; the C35 resume check
   runs from that managed-facing continuation and stops immediately after
   managed execution has resumed, before helper return or Collection 2.

`compact_phase` is the authentic object movement/copy completion boundary. The
C35 probes record phase completion only; they do not instrument individual
object copies. The run reached relocation, compaction, generation bounds, card
cleanup, `GcDone`, `RestartEE` entry/return, and managed resumption without a
newly blocking subsystem.

## Production handle update proof

The locked `Ref_UpdatePointers` type array contains:

- `HNDTYPE_WEAK_SHORT = 0`
- `HNDTYPE_WEAK_LONG = 1`
- `HNDTYPE_STRONG = 2`
- `HNDTYPE_REFCOUNTED = 5` when enabled
- `HNDTYPE_WEAK_NATIVE_COM = 9` when enabled
- `HNDTYPE_SIZEDREF = 8`

The authoritative run reported `typeCount=5` and `typeMask=0x00000127`,
which is the active set `{0, 1, 2, 5, 8}`. Native COM type 9 was not active.
This is the relocation update scan, not the C31/C32/C33 `CheckPromoted`
liveness callback. The short-weak liveness callback was not reused.

The same C33 handle was first bound from allocation provenance and then
matched in the scanner by structural identity:

| Structural field | Current-run value |
|---|---:|
| handle type | `0` (`HNDTYPE_WEAK_SHORT`) |
| handle table | `0x0000000010242B90` |
| segment | `0x0000000104020000` |
| block / first slot | `0x0000000104021200` |
| block index | `1` |
| slot index | `127` |
| exact weak slot | `0x00000001040213F8` |
| target generation | `0` |
| pre-update slot value | `0x0000000100A01F38` |

The match used the allocation-record slot, handle type, old value, table,
segment, block, and slot index. It did not search the handle table for the old
object address. `C011EC35-PREFLIGHT` was emitted only after the exact slot was
observed in the relocation scanner and the relocation lookup agreed with C34.

### Exact callback and mutation

For the authoritative three-run set, the current-run function addresses were:

- scan callback `UpdatePointer`: `0x0000000010084E30`
- production mutation callback `GCHeap::Relocate`:
  `0x000000001008ECB0`
- callback entry return address: `0x0000000010084E5A`
- callback return address: `0x00000000100C10DC`
- production store return address: `0x0000000010084E65`
- relocation lookup return address: `0x00000000100B62DB`

The function addresses are evidence for this boot layout, not hard-coded
matching keys.

| Handle evidence | Value |
|---|---:|
| handle scan entries / returns | `1 / 1` |
| total slots inspected | `32` |
| total callback entries / returns | `2 / 2` |
| total rewritten / unchanged | `2 / 0` |
| short-weak slots inspected | `16` |
| short-weak callbacks | `1` |
| short-weak rewritten | `1` |
| short-weak unchanged | `0` |
| exact slot observed / callback entered / returned | `1 / 1 / 1` |
| stale exact weak handles remaining | `0` |

The second rewritten handle was the surviving strong handle in category 2.
The active scanner set included categories 0, 1, 2, 5, and 8; the run
observed category 0 and category 2 blocks, each with 16 inspected slots, one
callback, and one rewrite. Categories 1, 5, and 8 had zero non-null slot
events in the reached table topology. Pinned, dependent, and weak-interior
processing remained in their separate locked calls after `Ref_UpdatePointers`;
the C33 short-weak slot was not in those categories.

### Relocation metadata and result

The exact proof-handle relocation lookup recorded:

- lookup input: `0x0000000100A01F38`
- brick table: `0x0000000104008040`
- brick index: `0x0000000000000A00`
- brick entry: `0x0000000000000F21`
- tree node: `0`
- relocation distance: `0` (the destination was resolved by the active
  relocation metadata path)
- lookup entries / returns / successes / failures: `1 / 1 / 1 / 0`
- destination: `0x0000000100901F50`

The production result was:

`0x0000000100A01F38 -> 0x0000000100901F50`

The weak slot changed from the old target to the destination through the
production `GCHeap::Relocate` store. No duplicate or recreated weak handle was
observed. The logical target remained the managed `byte[64]` established by
C33; its identity was retained by the agreement of C34 root relocation,
handle relocation metadata, EEType/size provenance from the C33 workload, and
the final managed-root/weak-slot equality. No arbitrary byte-for-byte heap
scan was used.

## Collection 1 completion and managed resumption

All three authoritative boots reported:

| Completion evidence | Value |
|---|---:|
| relocation / compaction / generation bounds | `1 / 1 / 1` |
| card cleanup / `GcDone` | `1 / 1` |
| `RestartEE` entries / returns | `1 / 1` |
| EE suspended before restart | `1` |
| EE resumed after restart | `1` |
| ThreadStore lock state at restart boundary | valid / `1` |
| managed entry prohibited while suspended | `1` |
| sensitive allocations while suspended | `0` |
| managed re-entry while suspended | `0` |
| managed resume | `1` |
| resumed ControlPC | `0x000000001006DE09` |
| safe-stop reason | `0` |

Before managed resumption, the weak slot was non-null, was not the stale
condemned address, equaled the authentic relocated target, and had not been
cleared. The managed root and weak handle both identified
`0x0000000100901F50`. The C33 helper remained active immediately after the
collection API returned; the helper did not return, Collection 2 did not
start, and dead-weak clearing was not retested in C35.

The markers were emitted in the required order on every run:

`C011EC35-PREFLIGHT -> C011EC35-HANDLE -> C011EC35`

The final marker was `outcome=LEVEL3`. `C011EC35-HANDLE` was intermediate and
did not terminate the proof.

## Three-run QEMU evidence

Evidence root:

`out/dotnet/c011ec35-relocated-handle-update/run-20260823-065526789`

| Run | Result | Serial SHA-256 |
|---|---|---|
| first-run | Level 3 | `706235642DA385E824C9B04BC2F6EAF71EDCBE68BC8DFF138F1AC357F334C271` |
| repeat-1 | Level 3 | `29613AE2FAB654A0CDAE4E57574063CD01422CEA603B6974E0BCAAF817914948` |
| repeat-2 | Level 3 | `A4E96FB3AAE136E91812B375F9842ED941C28FD73EF622E601C3D9BDF2A408DD` |

The serial hashes differ because current-run code addresses and serial
timing/tail content are serialized, but all semantic fields above agreed
across the three runs: old/new target, slot identity, handle type, lookup
metadata, callback/update counts, phase completion, restart/resume state, and
safe-stop state.

Artifact hashes for the authoritative proof build:

| Artifact | SHA-256 |
|---|---|
| specialized proof kernel | `5A79E4EFC7D0C46614C7FF0421A53C0025F8736691F248448880A96D2E2CA67E` |
| PE payload | `1D909C879F2FDAEB8660A6AD1E2310AC38EB034702F0694CFCA3729D417C9976` |
| ELF payload | `9BED3A348CD974B8657FA068F4D1920AC2B63C3221C48BA094F49F1C12E72992` |
| MAP payload | `F9817B3B9C079F7AAA7AE21CF21F5655058563A5D3B084B9123FD064750D457F` |

## Retained guards and regressions

- C19-C34 chronology guards were retained.
- C26 complete stack-walk guard was retained.
- C28 mark-closure guard was retained.
- C31 live short-weak preservation and C32 dead-short-weak clearing guards
  were retained; C35 did not rerun `CheckPromoted` artificially.
- C33 live-root Collection-1 proof and C34 managed-root relocation rewrite
  were observed at their existing boundaries.
- PE-to-ELF conversion passed.
- linker/table symbol validation passed.
- PowerShell source parse passed.
- locked-source injection and required-symbol guards passed.
- `git diff --check` passed.
- ordinary one-boot SuspendEE smoke passed after restoration.

An early disposable C35 attempt exposed two harness-only issues: canonical
slot binding was initially gated by prior startup scans, and a similar
promotion boundary initially received the relocation-entry hook. Both were
corrected before the authoritative three-run set. The final set reports one
relocation scan entry and no C35 runtime or GC-flavor regression.

## Restoration and Git handoff

The proof payload was removed after testing. The ordinary source-state kernel
was restored to both destinations:

- `kernel/build/amd64/bin/kernel.elf`
- `ESP/kernel.elf`

Final ordinary hash for both files:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The repository-owned QEMU process set was empty after testing. An unrelated
QEMU from another workspace was left untouched.

The intended committed files are this document, the C35 harness script, and
the two C35 diagnostic source files. No generated `out` evidence is part of
the commit. Push remains unauthorized and was not performed.

## Next milestone

Resume C011EC33 exactly at the managed continuation now proven: allow
`CreateAndRunLiveCollection1` to return, start Collection 2, and prove that
the same surviving short-weak handle is cleared only after its last managed
root disappears. C35 itself deliberately did not return the helper, start
Collection 2, or retest dead-weak clearing.

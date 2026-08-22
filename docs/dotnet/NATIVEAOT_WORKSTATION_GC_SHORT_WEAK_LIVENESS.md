# NativeAOT Workstation GC — C011EC31 Real Short-Weak Handle Liveness

## Result

C011EC31 is Outcome C, success level 3. The workload created a real NativeAOT short-weak handle through the production managed/runtime path before EE suspension, retained the same target through an independent production strong handle, reached the exact handle slot through `HandleTableMap` traversal, executed the locked `CheckPromoted` / `GCHeap::IsPromoted` decision, and preserved the live weak slot without a clearing store.

The proof does not test dead weak clearing. The next milestone is C011EC32: remove the independent strong handle in a controlled workload and prove production clearing of a genuinely dead short-weak target.

## Locked identity and source path

All proof boots used NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`, and source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The locked allocation path is:

1. `System.WeakReference` / `WeakReference<T>` calls `GCHandle.InternalAlloc(target, GCHandleType.Weak)`.
2. NativeAOT `GCHandle.NativeAot.cs` calls `RuntimeImports.RhHandleAlloc`.
3. NativeAOT `RuntimeImports.cs` imports `RhpHandleAlloc`.
4. `Runtime/HandleTableHelpers.cpp:RhpHandleAlloc` calls `CreateHandleOfType` on the global handle store.
5. `gc/gchandletable.cpp:GCHandleStore::CreateHandleOfType` reaches `HndCreateHandle` and the table/block allocator.
6. `gc/objecthandle.cpp:Ref_CheckAlive` selects `HNDTYPE_WEAK_SHORT` (`0`) and dispatches `CheckPromoted`.
7. Locked `CheckPromoted` calls `g_theGCHeap->IsPromoted(*ppRef)` and clears only the false branch.

The managed proof uses the same production allocator directly as `GCHandle.Alloc(target, GCHandleType.Weak)`. It also allocates `GCHandleType.Normal` for the independent strong root. No table entry, block, or slot is constructed or written by diagnostics.

Locked handle constants: `HNDTYPE_WEAK_SHORT = 0`, `HNDTYPE_WEAK_LONG = 1`, and `HNDTYPE_STRONG = 2`.

## Pre-GC provenance

The target was a real managed `byte[64]`, initialized with the deterministic `0xC0 + index` pattern. The explicit strong root was a production normal handle, not the weak handle and not a diagnostic slot. The final-run values were:

| item | value |
|---|---|
| target object | `0x0000000100A01F38` |
| strong handle slot | `0x00000001040211F0` |
| strong handle value | `0x0000000100A01F38` |
| weak handle slot | `0x00000001040213F8` |
| weak handle value before GC | `0x0000000100A01F38` |
| weak handle type | `0x00000000` (`HNDTYPE_WEAK_SHORT`) |
| production allocator | `RhpHandleAlloc` at `0x000000001001E94A` |

The proof saw three completed production allocations: the NativeAOT startup/internal normal handle, the explicit normal strong handle, and the explicit short-weak handle. The explicit weak allocation callback count was exactly one. The strong-handle promotion callback count was two, including the startup/internal normal handle and the exact explicit strong handle; `strongRootMatched = 1` proves the latter was promoted.

## C011EC31-PREFLIGHT and structural reachability

`C011EC31-PREFLIGHT` was emitted only after the explicit weak allocation callback, strong-root provenance, C26–C30 chronology, and structural handle match were all present.

Final topology:

| field | value |
|---|---|
| HandleTableMap buckets visited | `1` |
| table | `0x0000000010237250` |
| segment | `0x0000000104020000` |
| block | `0x0000000104021200` |
| block first slot | `0x0000000104021200` |
| block type | `0` |
| block index | `1` |
| slot index | `63` |
| structurally matched slot | `0x00000001040213F8` |
| slots inspected | `16` |
| short-weak slots | `1` |
| non-null short-weak handles | `1` |
| null slots | `15` |

The match used the pre-GC allocation slot plus the reached table/segment/block geometry. It did not search the table for the target address. The C30 no-handle topology boundary remains intact: one non-null bucket, one table, one segment; C31 adds the real block and slot reached within that same registered storage.

## Production liveness and preservation

The exact callback path was the locked `objecthandle.cpp:CheckPromoted` callback to `GCHeap::IsPromoted`. Final-run callback evidence:

| field | value |
|---|---|
| callback function | `0x000000001007A5F0` |
| callback entry | `0x000000001007A607` |
| liveness decision address | `0x000000001007A692` |
| condemned generation | `0` |
| target generation | `0` |
| mark-word address | `0x0000000100A01F38` |
| mark word before | `0x0000000010265D81` |
| mark mask | `0x00000001` |
| mark state before | marked/promoted (`1`) |
| liveness callbacks | `1` |
| live decisions | `1` |
| dead decisions | `0` |
| liveness result | live/promoted (`1`) |
| slot before | `0x0000000100A01F38` |
| slot after | `0x0000000100A01F38` |
| mutation attempted | `0` |
| clearing store | `0` |
| preserved handles | `1` |
| cleared handles | `0` |

The short-weak handle was not a normal root. The independent normal handle promoted the target before short-weak processing; the mark queue was already closed, and short-weak processing only read the mark state. `unexpectedWeakRooting = 0`.

## Chronology and sensitive-path invariants

The three final boots retained the complete C26–C30 chronology:

`C011EC26` stack walk completion → `C011EC27` root/mark processing → `C011EC28` mark-queue closure → `C011EC29` `AfterGcScanRoots` return and next phase → `C011EC30` authentic short-weak traversal → `C011EC31` live preservation.

For each boot:

- queue pending work at short-weak entry: `0`;
- mark pending work: `0`;
- EE suspended: `1`;
- ThreadStore lock held: `1`;
- managed entry prohibited: `1`;
- restart/resume: `0 / 0`;
- sensitive allocations after suspension: `0`;
- diagnostic mutations and mark writes: `0`;
- safe-stop reason: `0`.

The success boundary is the production decision completion, which emits `C011EC31` and enters the existing terminal safe-stop loop. No dead weak clearing, long weak handles, dependent handles, finalization, relocation, or sweeping was attempted.

## Three-run QEMU evidence

QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. All three fresh boots produced Outcome C and identical semantic marker fields.

Evidence root:

`out/dotnet/c011ec31-short-weak-live-handle/run-20260822-140813265`

| boot | serial SHA-256 |
|---|---|
| first-run | `F5002DFF55F822453F068F4CE62240E1DC6BE988B9373707BBB68D72589D23B3` |
| repeat-1 | `180BFD77A39C6A3B5780AFA8168929A3A429DECA1369446236FE9DBF2CE7E951` |
| repeat-2 | `089A6F854F2C19EAF0D01C353A1D606F4D0814B190FFCB99CCB8DB4BD6ECB044` |

Proof artifact hashes:

- specialized proof kernel: `0DDD421DAF75F53329C625996436067530358EE2FD9B6DCB30CC94B5D8F22609`;
- PE: `84F5414A42D1607181B82D3C082FA71F9ECDBD88E3035345AFFA7D9B9E4CF282`;
- ELF: `3328D5234F3192BD7FC0DAEFAF0242B352020DE082737F6BB8141D44490A78F5`;
- MAP: `DEB70A3B8597F5BD542F7FCC3C9382A21944345858A46AAEA85036ACC5B70C4A`.

## Regression and restoration status

- C19–C30 chronology: PASS, including native unwind provider, `_start.halt`, stack-walk completion, root enumeration, mark graph, queue closure, `AfterGcScanRoots`, and C30 no-handle topology.
- PE→ELF converter: PASS.
- Linker/table validation and locked-source guards: PASS.
- PowerShell parse: PASS.
- Ordinary build/boot smoke and `git diff --check`: PASS.
- Ordinary kernel/ESP after proof cleanup: both `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- Repository QEMU instances were terminated by the harness; no unrelated QEMU process was targeted.

## Git state

The authoritative starting state was branch `v1.1_DOTNET_SUPPORT`, HEAD `266737304710cac339a1c1b46f1f299227bd2ae`, upstream `origin/v1.1_DOTNET_SUPPORT`, ahead `0`, behind `0`, clean worktree, and zero untracked entries. The final C31 changes are limited to the managed workload, C31 harness, allocation diagnostics, platform diagnostics, and this document. No push was performed.

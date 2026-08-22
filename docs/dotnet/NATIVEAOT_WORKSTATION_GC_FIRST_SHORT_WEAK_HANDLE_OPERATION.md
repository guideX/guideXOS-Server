# NativeAOT Workstation GC First Short-Weak Handle Operation

## C011EC30 outcome

C011EC30 is Outcome A, success level 2. The authentic Workstation short-weak handle-table pass entered after the retained C011EC29/C011EC28 chronology, read `HandleTableMap.pBuckets`, visited a real bucket, handle-table array, and segment, then completed with no eligible block or handle slot. This is the permitted no-handle result: `noHandleCompletion=1`, no liveness callback or decision was reached, and no diagnostic mutation occurred.

This proves the first authentic short-weak handle-table operation after the `pBuckets` root read. It does not claim that a non-null short-weak referent was found, that `GCHeap::IsPromoted` ran, or that the weak handle store was changed.

Locked identity remained NativeAOT `9.0.0`, AMD64, Workstation GC, GC/EE interfaces `5.3 / 2`, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## Locked production path

The locked source is the runtime-pack source snapshot under `out/dotnet/pal-runtime-active-replacement-build/locked-source`.

```text
gc_heap::mark_phase
  -> GCToEEInterface::AfterGcScanRoots
  -> GCScan::GcShortWeakPtrScan
  -> Ref_CheckAlive
  -> g_HandleTableMap.pBuckets read
  -> bucket/table/segment traversal
  -> no eligible block or handle slot
```

The source-backed boundaries are:

* `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:145-155`: retained `AfterGcScanRoots` return.
* `src/coreclr/gc/gc.cpp:30128`: the next collector statement is `GCScan::GcShortWeakPtrScan`.
* `src/coreclr/gc/gcscan.cpp:140-143`: `GcShortWeakPtrScan` calls `Ref_CheckAlive`.
* `src/coreclr/gc/objecthandle.cpp:1523-1566`: `Ref_CheckAlive` reads `g_HandleTableMap`, checks buckets, selects the handle table, and calls `HndScanHandlesForGC`.
* `src/coreclr/gc/handletablescan.cpp:1375-1740`: authentic segment and block scan path. In this run the first short-weak pass did not select an eligible block, so the block and slot callbacks remained zero.
* `src/coreclr/gc/objecthandle.cpp:339-365`: retained production `CheckPromoted` liveness callback source, not reached because there was no eligible short-weak slot.
* `src/coreclr/gc/gc.cpp:49187`: retained `GCHeap::IsPromoted` rule, not reached in the no-handle result.

The C30 source generator keeps the locked production predicate and `*ppRef = NULL` store in place. It adds bounded observers around the authentic map, bucket, table, segment, block, slot, callback, and liveness boundaries; the observers do not write handle-table, object, mark, queue, or heap state.

## First operation and topology

The first final-run boot reported these structural values:

| Item | Value |
|---|---:|
| `HandleTableMap` address | `0x0000000010245F08` |
| `pBuckets` field address | `0x0000000010245F08` |
| first `pBuckets` value | `0x00000000102370D0` |
| `dwMaxIndex` | `0x000000000000000A` |
| buckets visited | `1` |
| handle tables visited | `1` |
| segments visited | `1` |
| blocks visited | `0` |
| slots inspected | `0` |
| first bucket address | `0x0000000010237128` |
| first table array | `0x0000000010237140` |
| first handle table | `0x0000000010237150` |
| first segment | `0x0000000104020000` |
| first operation return address | `0x000000001007A7E7` |

The zero block/slot counts are not missing evidence: they are the observed no-handle classification, closed by `noHandleCompletion=1` after the authentic table/segment walk returned.

## Collection and EE invariants

The operation ran at condemned generation `0`, maximum generation `2`, with `handleScanFlags=0`. At the boundary, EE was suspended, the ThreadStore lock was held with recursion `1`, managed entry was prohibited, and both queue-pending and mark-pending state were `0`. Restart and resume counts were both `0`.

The final operation counters were all zero for candidate handles, liveness checks, liveness decisions, callback dispatches, mutation attempts, cleared handles, preserved handles, and diagnostic mutations. The C30 safe-stop reason was `0`.

## Three fresh QEMU 11.0.0 boots

Evidence root:

`out/dotnet/c011ec30-short-weak-handle-operation/run-20260822-113702846`

All three fresh boots emitted the C011EC29 predecessor preflight, C011EC30 preflight, and C011EC30 completion markers. Semantic fields were identical across boots:

| Boot | Serial SHA-256 | Result | `pBuckets` value | Handle classification |
|---|---|---|---:|---|
| `first-run` | `6F4A4A8DC20BDE31BBEC0E5FEE0562D170A4AA05CC23FF080E23517B818007F5` | Outcome A, level 2 | `0x102370D0` | no eligible block/slot |
| `repeat-1` | `5CA9E091AFE5525F5227930C6FC3D96CC678380984CDD50938C26B992E23D311` | Outcome A, level 2 | `0x102370D0` | no eligible block/slot |
| `repeat-2` | `F1EA0E1F92F46818CAC40F2C2C185C3E82468A1E64D0AF102DD8F4BB08DDE7CD` | Outcome A, level 2 | `0x102370D0` | no eligible block/slot |

QEMU version was `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

## Artifacts and restoration

| Artifact | SHA-256 |
|---|---|
| proof kernel | `D18572B752491C4D1DAA55903D41A93DA23E0B25280085AB3B065B200A19B9A8` |
| managed PE | `3D204D76E5E460BD85FC88AC59BF7381BAC5E0DA0B3ACE46D0264BDA0947BF06` |
| managed ELF | `04D052CD0A7E076787F5BCA5B02DF8E39ACB909D8225E88F0B70AC4B74163AE7` |
| MAP | `F6C5FD3E729D743746F36B8183A54F4F6A0558AEC7774E2AE64F08FE0E4B5DF7` |

The ordinary kernel and ESP were restored after the harness completed. Independent post-run hashes were identical:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

The final run retained source-injection guards, required-symbol audits, serial revalidation, exact kernel restoration checks, and `git diff --check`. C011EC29, C011EC28, and C011EC27 remain predecessor evidence; they were not relabeled or rerun as separate milestones.

## Git state at proof

The proof started on branch `v1.1_DOTNET_SUPPORT` at `0ae24bdecbc629cb0d041013633cac59acbedb21`, tracking `origin/v1.1_DOTNET_SUPPORT` with zero divergence. The only starting worktree changes were the C011EC30 harness, diagnostics, platform, and this document; the final coherent commit is reported by the repository handoff.

## Next smallest milestone

The next bounded step is to arrange or identify a real non-null short-weak handle in the first eligible scanned block, then capture the authentic `CheckPromoted`/`GCHeap::IsPromoted` decision and production store boundary without changing the production predicate or store.

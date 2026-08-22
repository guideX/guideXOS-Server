# NativeAOT Workstation GC Post-Mark Short-Weak Handle Boundary

## C011EC29 outcome

C011EC29 is Outcome A, success level 1: authentic Workstation marking retained its C011EC28 closure, `GCToEEInterface::AfterGcScanRoots` returned normally, the next collector operation entered short-weak handle processing, and the first production `HandleTableMap::pBuckets` read was captured. No post-mark heap, handle, mark, queue, or relocation mutation was performed. This is not a claim that GC completed.

Locked identity remained NativeAOT `9.0.0`, AMD64, Workstation GC, GC/EE interfaces `5.3 / 2`, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## Locked control-flow trace

The locked source is the runtime-pack source snapshot under `out/dotnet/pal-runtime-active-replacement-build/locked-source`.

The production path is:

```text
gc_heap::mark_phase
  -> GCToEEInterface::AfterGcScanRoots
  -> GCScan::GcShortWeakPtrScan
  -> Ref_CheckAlive
  -> first HandleTableMap::pBuckets read
  -> HandleTableBucket traversal (not entered by this bounded stop)
```

The exact locked boundaries are:

* `src/coreclr/gc/gc.cpp:30089`: `gc_heap::mark_phase` calls `GCToEEInterface::AfterGcScanRoots`.
* `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:145-155`: `AfterGcScanRoots` invokes `RestrictedCallouts::InvokeGcCallouts(GCRC_AfterMarkPhase, condemned)` and, when enabled and non-concurrent, the ObjC marshal callback. It has no guideXOS-specific production action and returns normally.
* `src/coreclr/gc/gc.cpp:30128`: the next collector statement is `GCScan::GcShortWeakPtrScan(condemned_gen_number, max_generation, &sc)`.
* `src/coreclr/gc/gcscan.cpp:140-143`: `GcShortWeakPtrScan` calls `Ref_CheckAlive`.
* `src/coreclr/gc/objecthandle.cpp:1523-1566`: `Ref_CheckAlive` begins short-weak handle liveness processing and reads `g_HandleTableMap`.
* `src/coreclr/gc/objecthandle.h:23-30`: `HandleTableMap` owns `pBuckets`, `pNext`, and `dwMaxIndex`; `g_HandleTableMap` is the production global.
* `src/coreclr/gc/gc.cpp:30147-30164`: finalization follows short-weak processing.
* `src/coreclr/gc/gc.cpp:32553`: the later plan phase is not reached by C011EC29.

Therefore the first new phase is short-weak handle processing, not finalization, dependent-handle processing, plan, sweep, relocation, or restart preparation. `AfterGcScanRoots` itself is not a missing guideXOS EE contract in this run.

## First post-mark operation

The first semantically meaningful operation was a production read of `g_HandleTableMap.pBuckets` from `Ref_CheckAlive`.

Current-run structural values from the first proof boot were:

| Item | Value |
|---|---:|
| `HandleTableMap` address | `0x0000000010242DB8` |
| `pBuckets` field address | `0x0000000010242DB8` |
| first `pBuckets` read value | `0x0000000010233F80` |
| `dwMaxIndex` | `0x000000000000000A` |
| first operation return address | `0x00000000100786CE` |
| first mutation attempted | `0` |

These are current-run structural identities, not universal production addresses. The read proves entry into the handle subsystem and a valid initialized handle-map root. The bounded observer stops immediately after this read; it does not broadly implement handle processing.

## Collection and EE state

The first next-phase observer recorded condemned generation `0`, maximum generation `2`, generation count `5`, Workstation heap number `0`, heap count `1`, collection reason raw value `0`, promotion `0`, full collection `0`, and compaction flag `1`. No relocation operation was reached; the reported relocation state is `0` because the proof stops before relocation planning or execution.

At the read, EE was suspended (`1`), the ThreadStore lock was held (`1`) by current-run owner `0x0000000003963CC0`, recursion was `1`, the current thread was cooperative (`1`) and not preemptive (`0`), and managed entry was prohibited (`1`). Managed-entry attempts, sensitive allocations, restart requests, and resumes were all `0`. Stack base and `ScanContext.stack_limit` remained `0`; stack bounds consumed remained `0`.

## Retained C26-C28 chronology

C26 retained normal terminal stack completion: one `GcScanRoots` entry/return, six roots at iterator terminal, seven total roots after ThreadAbort, two native unwinds, no third unwind, `_start.halt` terminal proof, and no stack-bound consumption.

C27 retained genuine mark/child traversal. The current three boots retained the historical semantic graph: ten graph scans, `0xC5` (`197`) child/reference slots, `0x89` null references, `0x3C` non-null references, and genuine mark writes.

C28 retained authentic queue closure. The current proof boots reported:

* successful dequeues `12`, successful enqueues `14`, queue invariant failures `0`;
* mark tests `14`, already marked `2`, newly marked `12`, mark writes `12`;
* graph scans `10`, reference slots `197`, null `137`, non-null `60`, child-derived queue insertions `3`;
* final queue count `0`, final empty test true, final drain empty test true;
* five drain entries and five normal drain returns.

At the C28-to-C29 transition, queue pending work and mark pending work were both `0`.

## Three fresh QEMU boots

Evidence root:

`out/dotnet/c011ec29-post-mark-short-weak-handle/run-20260822-093503418`

All three fresh QEMU 11.0.0 boots emitted `C011EC29-PREFLIGHT` and `C011EC29`, and the serial revalidation found identical semantic fields and current-run structural identities:

| Boot | Serial SHA-256 | C29 result | Map address | First read |
|---|---|---|---:|---:|
| `first-run` | `4D07B94B6A00580BBB974CA4C8E55843D61DFC8E3A4A81F895CAE338E635CD45` | Outcome A | `0x10242DB8` | `0x10233F80` |
| `repeat-1` | `D528682E699673DDA9415D035991F8A1761B60ED19FBB75F30BA1B98CB3EEB65` | Outcome A | `0x10242DB8` | `0x10233F80` |
| `repeat-2` | `132318369E523169A3F354DB683367A4A4E2F30B67D003808B9CA53F05FADE60` | Outcome A | `0x10242DB8` | `0x10233F80` |

The final wrapper invocation reached all three boots and produced the complete marker vector. Its last post-boot assertion list contained a field-name typo, which was corrected in the harness; the captured serials were independently revalidated after that correction.

## Artifact and regression record

For build run `build-run-20260822-093503418`:

| Artifact | SHA-256 |
|---|---|
| proof kernel in `first-run/esp/kernel.elf` | `6723B4FC00F9CEC9E52A006969A98C7BB2BE02E61672936CFF97540FE5388FD2` |
| managed PE | `94140C040A2D1872EB7CE8743A4BC99B0B858533F7F49927E36C256173F76132` |
| managed ELF | `90A6FD2D8F4BFE3C2208FD980A684520EF0CCE04941EC6F38663A1BB5B5AFE1D` |
| MAP | `3DDEACF1E1257FAB6BFA184468D95E0483CDC901A25B582B204E376BA8E8A86E` |

Locked-source tracing, source guards, linker/table validation, PE-to-ELF conversion, normal kernel build, PowerShell parse, and `git diff --check` were retained or passed. C19-C28 chronology, native unwind-provider, alias, kernel-main return-slot, C27 mark/child, and C28 queue-closure evidence were not reopened or relabeled. Ordinary boot-smoke behavior remains retained from C28; this focused proof did not broaden into a separate Navigator scenario suite.

The ordinary source-state kernel and ESP were restored and independently hashed as:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

No QEMU process belonging to this repository remained after the proof. Unrelated QEMU instances were not targeted.

## Next smallest milestone

The next bounded milestone is one authentic short-weak handle-table operation after the `HandleTableMap` root read, with exact handle-table/bucket structure and EE contract captured. It should stop before broad weak/dependent/finalization implementation unless the locked production path makes that operation unavoidable.

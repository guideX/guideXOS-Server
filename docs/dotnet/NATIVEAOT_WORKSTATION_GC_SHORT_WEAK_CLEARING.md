# NativeAOT Workstation GC — C011EC32 Dead Short-Weak Clearing

## Outcome

C011EC32 reached Outcome C, success level 3, on three fresh QEMU 11.0.0 boots. One genuine type-0 short-weak handle was created by the managed NativeAOT path, the target had no independent strong reachability at collection time, production short-weak processing called `CheckPromoted`/`GCHeap::IsPromoted`, classified the target dead, and performed the normal `*ppRef = NULL` clearing store.

This milestone deliberately does not test `GCHandle.Free` or long/dependent/finalization handles. It stops after the first production dead-target clear.

## Locked identity and source trace

- NativeAOT `9.0.0`, AMD64, Workstation GC.
- GC interfaces `5.3 / 2`.
- Locked runtime source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
- Locked source files/functions:
  - `src/coreclr/nativeaot/Runtime/HandleTableHelpers.cpp:RhpHandleAlloc` calls `CreateHandleOfType`.
  - `src/coreclr/gc/gchandletable.cpp:GCHandleStore::CreateHandleOfType` calls `HndCreateHandle`.
  - `src/coreclr/gc/handletable.cpp:HndCreateHandle` allocates/registers the handle-table slot and stores the initial object.
  - `src/coreclr/gc/objecthandle.cpp:Ref_CheckAlive` selects `HNDTYPE_WEAK_SHORT` for the short-weak pass.
  - `src/coreclr/gc/objecthandle.cpp:CheckPromoted` calls `GCHeap::IsPromoted`; the locked dead branch stores `*ppRef = NULL`.
  - `src/coreclr/gc/gc.cpp:GCHeap::IsPromoted` supplies the mark-state decision.
  - If a handle is freed, `HandleTableHelpers.cpp:RhHandleFree` calls `DestroyHandleOfUnknownType`, then `gchandletable.cpp:GCHandleManager::DestroyHandleOfUnknownType` and `handletable.cpp:HndDestroyHandleOfUnknownType`/`HndDestroyHandle`. That free path was traced but not used by this proof.

The managed workload is `GCHandle.Alloc(target, GCHandleType.Weak)`. In this locked configuration `GCHandleType.Weak` reaches `HNDTYPE_WEAK_SHORT = 0`. No handle-table entry or slot was directly constructed or modified by diagnostics.

## Workload and pre-GC provenance

`HostLogProof` uses a `[NoInlining]` helper to allocate and initialize a `byte[64]` target, call the real `GCHandle.Alloc(..., Weak)` path, and record scalar identity metadata. Only scalar values cross the helper boundary. The outer collection workload retains no managed target reference and no temporary strong handle. The helper's `GC.KeepAlive(target)` covers only production allocation and the callback before returning.

The final three-run values were stable:

- target object/type: `byte[64]`, target `0x0000000100A01F38`, method-table/type value `0x0000000010268EB0`;
- production allocation entry: `RhpHandleAlloc`, entry `0x000000001001E9AA`, two total allocation callbacks including an unrelated startup handle;
- proof weak handle: type `0x00000000` (`HNDTYPE_WEAK_SHORT`), slot `0x00000001040213F8`, pre-GC slot value `0x0000000100A01F38`;
- helper-return proof: `helperReturned=1`, helper return address `0x0000000010001A9D`;
- strong-root slot/value: not applicable; the target had no strong root. Root/graph audit was zero in every category.

The target was not observed in stack, register, ordinary, static/thread-static, ThreadAbort, or strong-handle roots. The target also had zero graph-derived promotions, target queue insertions, child discoveries, and target mark writes. Therefore the weak handle did not act as an ordinary strong root.

## Structural traversal and liveness

The C26–C30 chronology remained intact: complete managed/native stack walk, `GcScanRoots` completion, C27/C28 mark-queue closure, `AfterGcScanRoots` return, C29 post-mark entry, then `GCScan::GcShortWeakPtrScan` → `Ref_CheckAlive` → `HndScanHandlesForGC` → `HandleTableMap` traversal.

The proof handle was matched by allocation provenance plus exact structural traversal, not by target-address search:

- bucket/table: table `0x000000001023A380`;
- segment: `0x0000000104020000`;
- block: `0x0000000104021200`, first slot `0x0000000104021200`, block type `0`, block index `1`;
- slot index `63`, slot `0x00000001040213F8`;
- aggregate traversal: 1 bucket, 1 table, 1 segment, 1 block, 16 slots inspected, 1 short-weak slot, 1 non-null short-weak handle, 15 null slots.

`C011EC32-PREFLIGHT` was emitted only after the real allocation, helper-return validation, C29 closure, root/graph audit, and exact structural match. The production callback was `CheckPromoted` (callback address `0x000000001007CE10`, callback entry `0x000000001007CE27`) and the production decision boundary returned at `0x000000001007CEBD`.

Liveness evidence:

- condemned generation: `0`;
- target generation: `0`;
- mark word address: target `0x0000000100A01F38`;
- mark word before: `0x0000000010268EB0`;
- mark mask: `0x00000001`;
- mark state before: unmarked (`0`);
- liveness result: dead/unpromoted (`0`), `liveDecisions=0`, `deadDecisions=1`.

The C29 queue and mark closure were already complete at short-weak entry: pending queue work `0`, pending mark work `0`. No diagnostic mark or queue mutation occurred.

## Production clearing result

The locked dead branch attempted the normal clearing mutation exactly once:

- mutation attempted: `1`;
- clearing store: `1`, store return address `0x000000001007CE9C`;
- slot before: `0x0000000100A01F38`;
- slot after: `0x0000000000000000`;
- cleared handles: `1`;
- preserved handles: `0`.

No diagnostic write-back occurred. EE/thread invariants remained valid: EE suspended, ThreadStore lock held, cooperative mode `1`, preemptive mode `0`, managed entry prohibited, restart `0`, resume `0`, and sensitive-path allocations `0`.

## Three-run evidence

Evidence root:

`out/dotnet/c011ec32-short-weak-dead-handle/run-20260822-150507213`

QEMU: `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

All three runs reported C011EC32, success level 3, and identical semantic fields:

| Run | Serial SHA-256 | Result |
|---|---|---|
| first-run | `E1FDD3B3D15F0F9393E9DA270ECF216D11A943CC115929D8D00D919230462A11` | Outcome C |
| repeat-1 | `9CC159DA2D29683244D2AE8728559A06A23BA95291271C47D6B12006FB669199` | Outcome C |
| repeat-2 | `DF09887A720FD6233555A0FFFDDBF0EE86A7E665C02304A3C2F1669B34B8C6E8` | Outcome C |

Proof artifact hashes for this run:

- proof kernel: `10837C56170B21ABB3D89DCD23AC0438F6F2E55CC931844815196BB609F6E077`;
- PE: `826180B9E08F2D49D63DA6E15CDED6E09976AD039393F1465A9CD2FC97617E2D`;
- ELF: `3CACD7310B0514A89CDDB576F1FCF3B9779CE7CB5BE62601F252AE3E5F1010B7`;
- MAP: `FF3A2850A894C6730F5D114A6F5715EFDC31E2AD9D410227F1CF109F309A919F`.

## Regression and restoration status

- C19–C30 chronology, native unwind provider, `_start.halt` boundary, mark closure, graph traversal, `AfterGcScanRoots`, and authentic HandleTableMap traversal remained active.
- PE→ELF conversion, linker/table validation, PowerShell parse, source guards, ordinary build/boot smoke, and `git diff --check` passed in the harness.
- Ordinary artifacts were restored after proof. Final ordinary kernel and ESP hashes both equal the established ordinary hash `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
- The harness terminated only its repository QEMU instances. An unrelated QEMU process for `D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration` was observed and preserved.

## Git and next milestone

Starting state was branch `v1.1_DOTNET_SUPPORT`, HEAD `9b7f176261693e0bb27cbaea1e5b53e91cf7159d` (`Validate NativeAOT short-weak liveness`), upstream `origin/v1.1_DOTNET_SUPPORT`, actual divergence `0/0`, and a clean worktree before C32 edits. The C32 changes are the managed workload/project define, the diagnostic header/platform implementation, this harness, and this document. No push is authorized.

C011EC33 should be the smallest next milestone: reuse the same production allocation path and remove the independent reachability in a separately controlled workload, then prove the dead short-weak clear with its own lifetime/root evidence. Do not infer that result from C32's helper-boundary proof.

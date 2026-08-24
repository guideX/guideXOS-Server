# NativeAOT Workstation GC post-GC allocator provenance

## C011EC41 result

C011EC41 completed at Level 2, Outcome C:

> The C40 recovered tail is not in the ordinary small-object allocation domain used by the resumed generation-0/ephemeral allocation path.

The proof preserved C40 authentic compaction reclamation, traced the first ordinary managed `byte[64]` allocation through the locked NativeAOT array entry, captured the production Thread allocation context before the first request, and captured all eight bounded allocations. It did not alter allocation pointers, limits, segments, GC flavor, planner policy, or allocator ordering. No Collection 3 was triggered.

Locked identity:

- NativeAOT `9.0.0`
- AMD64
- Workstation GC
- interfaces `5.3 / 2`
- runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

## C40 reclaimed-tail baseline

The three C011EC41 boots retained the C40 facts:

- dead relocated target: `0x100901F50`, EEType `0x10278020`
- payload `0x40`, aligned extent `0x58`, target end `0x100901FA8`
- generation `1`, unmarked, weak slot `0x1040213F8`, cleared to null
- target live-plug membership `false`
- relocation callbacks `0`, copy/move operations `0`
- authentic dead gap `[0x100901F50, 0x100901FC0)`, size `0x70`
- neighboring live plug source `[0x100901FC0, 0x100911FD8)`
- neighboring destination `[0x100801F20, 0x100811F38)`
- shift `0x1000A0`
- old compacted frontier `0x100942068`
- new compacted frontier `0x100900028`
- frontier decrease `0x42040`
- allocator-visible tail `[0x100900028, 0x100943000)`, size `0x42FD8`
- C40 tail segment `0x104010668`, generation `1`

C37 completed both collections and managed resumed after Collection 2 in every run. C39 retained the final `COMPACT` decision. C40 emitted `C011EC40-RECLAIMED` and completed its reclamation record in every run.

## Locked allocation chain

The managed workload is the existing `RunC011EC40AllocationReuse` sequence in `samples/managed/HostLogProof/Program.cs`, bounded at eight ordinary `new byte[64]` allocations. The C41 mode reuses that workload; it does not add allocation pressure.

The locked path is:

1. Managed `RunC011EC40AllocationReuse` calls the existing before-allocation boundary.
2. NativeAOT enters `RhpNewArray`, locked `nativeaot/Runtime/amd64/AllocFast.asm:148-203`.
3. `RhpNewArray` validates the element count, derives the aligned object size, loads the current Thread allocation context, and performs the pointer bump at lines `171-193`.
4. The carry/limit branches at lines `175` and `183` enter locked `RhpNewArrayRare`, lines `205-244`.
5. `RhpNewArrayRare` enters locked `RhpGcAlloc`, `nativeaot/Runtime/GCHelpers.cpp:571-604`.
6. `RhpGcAlloc` calls `GcAllocInternal`, `GCHelpers.cpp:474-563`.
7. `GcAllocInternal` calls `GCHeap::Alloc` through the Workstation heap, `gc/gc.cpp:49905-49995`.
8. The SOH refill/region selection is locked `gc_heap::soh_try_fit` and `gc_heap::allocate_soh`, `gc.cpp:17896-18220`.

C41 uses a diagnostic-only MASM interposition source, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_c011ec41_allocfast.asm`, around the locked `RhpNewArray` entry and fast-return boundary. The production arithmetic and branch sequence remains the locked sequence. The rare return is observed from the diagnostic copy of `GCHelpers.cpp` after the locked `GcAllocInternal` return. No production allocator state is written by either callback.

The authoritative allocation-context APIs remain production structures:

- `gcenv.ee.cpp:135-142`, `GCToEEInterface::GcEnumAllocContexts`
- `gcenv.ee.cpp:190-193`, `GCToEEInterface::GetAllocContext`
- `gc.cpp:7858-7916`, `gc_heap::fix_allocation_context`
- `gc.cpp:7963-7971`, `gc_heap::fix_allocation_contexts`

## Post-Collection 2 context

Captured after authentic `RestartEE` and managed resume, before the first C41 request:

| Field | Value |
|---|---:|
| Thread | `0x000000000397CCC0` |
| allocation-context address | `0x000000000397CCC0` |
| initial allocation pointer | `0x100A10040` |
| initial allocation limit | `0x100A10040` |
| alloc_bytes | `0x4D89E8` |
| home heap | `0x10159500` |
| active segment | `0x104010710` |
| active segment range | `[0x100A00028, 0x100AF1000)` |
| active generation | `0` |
| context valid after resume | yes |

The pointer equaled the limit, so the resumed context was valid but empty. It was not restored into the C40 tail. The active context had no overlap with the tail and was on a different segment: active segment `0x104010710`, C40 tail segment `0x104010668`.

The captured context is established by the production collection-time context-fixing and restart/resume sequence. C41 does not repair or refill it. The first ordinary request naturally enters the rare path because the valid context has no remaining bytes.

## Bounded allocation trace

Each request is payload `0x40`; the structurally derived aligned object size is `0x58`. The trace below is identical across all three boots.

| Ordinal | Object | Size | Context before | Context after | Limit | Path | Segment | Generation |
|---:|---:|---:|---:|---:|---:|---|---:|---:|
| 0 | `0x100A10058` | `0x58` | `0x100A10040` | `0x100A100B0` | `0x100A10040` | rare/refill | `0x104010710` | 0 |
| 1 | `0x100A100B0` | `0x58` | `0x100A100B0` | `0x100A10108` | `0x100A12060` | fast | `0x104010710` | 0 |
| 2 | `0x100A10108` | `0x58` | `0x100A10108` | `0x100A10160` | `0x100A12060` | fast | `0x104010710` | 0 |
| 3 | `0x100A10160` | `0x58` | `0x100A10160` | `0x100A101B8` | `0x100A12060` | fast | `0x104010710` | 0 |
| 4 | `0x100A101B8` | `0x58` | `0x100A101B8` | `0x100A10210` | `0x100A12060` | fast | `0x104010710` | 0 |
| 5 | `0x100A10210` | `0x58` | `0x100A10210` | `0x100A10268` | `0x100A12060` | fast | `0x104010710` | 0 |
| 6 | `0x100A10268` | `0x58` | `0x100A10268` | `0x100A102C0` | `0x100A12060` | fast | `0x104010710` | 0 |
| 7 | `0x100A102C0` | `0x58` | `0x100A102C0` | `0x100A10318` | `0x100A12060` | fast | `0x104010710` | 0 |

The first request proves the empty-context rare branch: the `0x58` request does not fit the initial `[0x100A10040,0x100A10040)` context, `RhpNewArrayRare`/`RhpGcAlloc` runs, and the heap returns an object in the generation-0 region while establishing a new context limit `0x100A12060`. The following seven requests satisfy the locked fast bump equations:

`object == pointer_before`, `pointer_after == pointer_before + 0x58`, and `pointer_after <= limit`.

The context pointer is monotonic, all objects are aligned, the objects do not overlap, heap and segment ownership are valid, and no bounded request caused Collection 3. Invariant failures: `0`.

## Recovered-tail eligibility and preference

The final C41 provenance classification is:

- recovered tail immediately eligible for this ordinary allocation domain: **no**
- eligibility timing code: `3` (`not part of this allocation domain`)
- tail considered by the actual refill: **no**
- tail consumed naturally: **no**
- actual supplying range: `[0x100A00028, 0x100AF1000)`
- actual supplying segment: `0x104010710`
- actual supplying generation: `0`

C40 published the tail in segment `0x104010668`, generation `1`. Ordinary resumed `byte[64]` allocations were supplied by segment `0x104010710`, generation `0`, the ephemeral generation allocation domain. The locked `gc_heap::soh_try_fit`/`allocate_soh` ordering operates on the requested generation’s allocation region; it does not treat the generation-1 compacted tail as a candidate for this generation-0 small-object request. This is a production generation/segment-domain rule, not an allocation-context restoration defect.

The first allocation’s rare path was observed naturally because the restored context was empty. The bounded workload then remained in the new generation-0 context. No artificial exhaustion or forced refill was used. The optional strongest result was not attempted: the C40 tail was not naturally consumed.

## Runtime markers and safety

Every final run emitted, in order:

`C011EC41-PREFLIGHT` → eight `C011EC41-ALLOC` records → `C011EC41-PROVENANCE` → `C011EC41` completion.

The preflight was emitted only after C40 reclamation, Collection 2 completion, managed resume, context capture, and entry into the first authentic `RhpNewArray` helper. Diagnostic allocations while EE was suspended: `0`. Segment/frontier/context mutation by diagnostics: `0`. Collection 3: `0`.

## Three-run evidence

Evidence root:

`out/dotnet/c011ec41-post-gc-allocator-provenance/run-20260824-104244255`

QEMU version: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

Serial SHA-256:

1. `EB3C4B7845E2174F1FEACECA32E38964618564252DE8CADDFCD447849F9EEAFC`
2. `95942D7DD583E4896FF02BC732ABBAB73238193461ECAB18F96D2728DD821F15`
3. `4769576D13AE3137EEC8EB3D7FD5B389B62A8D45214A7515403A8D75F9A7C1FD`

The serial hashes differ because each serial stream has run-specific boot/watchdog text; the semantic C37/C39/C40/C41 fields agree across all three boots.

Proof artifact SHA-256:

- kernel: `E2A2E325BB7A4EEC82805C3E0BC4CF1468BDEF452DBDF6A2A149F46C978807FB`
- PE: `DD0253B7702642930124AE518077D2870980B67E2776EDFEBB2ADA328F3FE213`
- ELF: `1D3FF144F0DE4FABF47CB007E12F7B0F63D18F30A0621B6FECF44DA3DCDB821E`
- MAP: `D7ECBC661EAEDFBD4FCAF30E5B682965247FCDD1C9575A16B61E85E5AF84E238`

Regression gates retained: C19-C40 chronology, C37 repeated-GC completion, C36 live-to-dead weak transition, C39 planner provenance, C40 reclamation, PE-to-ELF conversion, linker/table guards, PowerShell parse, ordinary boot smoke, and `git diff --check`.

## Artifact restoration and next milestone

The proof kernel/ESP payload was removed from the ordinary boot path in the harness cleanup. The ordinary source-state kernel and ESP were restored to:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

Only the repository-owned QEMU instances were terminated; unrelated VMs were preserved.

The next smallest milestone is a natural post-refill observation under a separately bounded experiment, if needed. C011EC41 itself does not justify changing allocator ordering or forcing the generation-1 recovered tail into the generation-0 allocation domain.

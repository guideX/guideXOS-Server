# C188R12R4R5R4R5 — GC Initialization Failure Probes

## Result

**Outcome A — `GC_INIT_PROBES_HOSTED_READY`.** Bounded, allocation-free probes are linked into the isolated diagnostic Workstation GC image. The diagnostic ELF and kernel and the proof-disabled production ELF and kernel built and passed their artifact checks. The candidate and the unchanged source ramdisk are staged under:

`out/dotnet/c188r12r4r5r4r5-gc-init-probes/candidate-20261101T195924998Z/`

This is hosted build qualification only. No guest was booted, no QAC slot was reserved, and the internal cause of the earlier guest `E_FAIL` remains unproven.

The deployable diagnostic ESP, production comparison artifacts, runtime-pack inputs and manifests, analyzer, hosted test evidence, and this report are grouped in `out/dotnet/c188r12r4r5r4r5-gc-init-probes/candidate-20261101T195924998Z/staged/`. Its `qualification-manifest.json` records SHA-256 hashes and sizes for every staged payload file.

## Repository and retained evidence

- Branch: `v1.1_DOTNET_SUPPORT`.
- `HEAD` at the start and at the end of the build/qualification work, before this source commit: `1e27d474aa78d9aecf7b6ac4963a3baa32a193f5`.
- The original source `ESP/ramdisk.img` hash before and after the work is `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The isolated candidate ESP contains a copy with the same hash.
- R3 guest-003 raw serial remains byte-identical: SHA-256 `BAEF3ACE33F44924DA99DEC535EF52401075F125DBF9D05E04138B2812DFD8E9`.
- The R3 guest-003 serial analysis and init-trace analysis remain untouched. The guest-001 recovery manifest and its serial analysis also remain untouched.

## Audited source contract

The exact source is the locked NativeAOT runtime commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, in `src/coreclr/gc/gc.cpp`, compiled into the diagnostic image as `Runtime.WorkstationGC:gcwks.cpp.obj`.

| Source location | Contract recorded by the probe |
| --- | --- |
| `GCHeap::Initialize()`, around line 48665 | Entry is recorded. Every concrete heap return is passed through an ABI-safe result recorder that returns the original `HRESULT` unchanged. |
| `gc_heap::initialize_gc()`, around line 14120 | The result assigned to `hr` is recorded immediately after the `initialize_gc()` call and before later `GCHeap::Initialize()` work. |
| `init_semi_shared()`, call around line 14406; definition around line 14417 | The function returns `int`. Its actual integer is recorded. Zero is the false/failure result; the caller maps it to `E_FAIL` and keeps its existing behavior. No HRESULT is invented for the integer result. |
| `WaitForGCEvent = new (nothrow) GCEvent`, around line 48955 | Allocation entry and its actual null/non-null outcome are recorded. Null keeps the existing `E_OUTOFMEMORY` return. |
| `WaitForGCEvent->CreateManualEventNoThrow(TRUE)`, around line 48962 | The actual boolean outcome is recorded. False keeps the existing log and direct `E_FAIL` return. The call is reached only after `initialize_gc()` succeeded. |

The earlier guest record has only the outer `GCHeap::Initialize()` `E_FAIL`; it cannot choose between `init_semi_shared()` and `WaitForGCEvent` creation. This phase does not claim either is responsible. The local `GCEvent::Impl` allocation performed by `CreateManualEventNoThrow` is included in the actual boolean result. Native/PAL last-error state is not safely available at these points, so its record fields remain the explicit unavailable sentinel and source identifier zero.

## Record and reporting behavior

The record is a 64-byte, 16-word, statically allocated scalar structure. Its source layout and offsets are checked with `static_assert`. The field order is:

`magic, version, invocation, heapState, finalHeapHresult, initializeGcHresult, semiSharedState, semiSharedResult, waitEventAllocationState, waitEventAllocationResult, waitEventCreateState, waitEventCreateResult, firstFailureSite, nativeStatus, nativeStatusSource, flags`.

State identifiers are source-defined: not entered `0`, entered `1`, returned successfully `2`, explicitly failed `3`, did not return `4`, result unavailable `5`, trace incomplete `6`. The raw unavailable value is `0xFFFFFFFF`. Once `RhInitialize` returns, the adapter marks any operation still in the entered state as did-not-return, then reads the record and emits 16 fixed-width `C188R12R4R5R4R5_GCINIT` lines through the existing bounded serial sink. If the whole outer call does not return, the adapter cannot serialize a snapshot; the analyzer treats absent internal data as unresolved or incomplete and does not infer a failing operation.

The first explicit failure-site identifier is set only while it is zero, so later propagation through `GCHeap::Initialize()` cannot overwrite the source site. The record has static lifetime after failed initialization and uses scalar writes/reads only. Its reader and writer execute on the initialization thread, with the adapter reading after `RhInitialize` returns. `volatile` is used for the scalar record; it is not a cross-thread synchronization protocol and the record is not intended for concurrent readers.

The record implementation has no allocator, logger, dynamic strings, PAL or Windows service calls, managed calls, or added locks. It does not alter GC, PAL, event, memory, or thread behavior. The new probe compile define is accepted only with the diagnostic NativeAOT profile and is absent from the production runtime pack and production map.

## Analyzer rules

`scripts/dotnet/analyze-c188r12r4r5r1-init-serial.ps1` accepts only 16 unique fields in order with one invocation and the expected magic/schema. It checks state/result consistency, stage ordering, the source `E_FAIL`/`E_OUTOFMEMORY` contracts, first-failure-site consistency, and agreement with the outer heap result.

- A fully successful record is `gc_heap_initialize_succeeded`.
- A zero `init_semi_shared()` result with the caller's `E_FAIL` is `init_semi_shared_failed`.
- Null `WaitForGCEvent` allocation with `E_OUTOFMEMORY` is `wait_for_gc_event_allocation_failed`.
- False event creation after prior success with `E_FAIL` is `wait_for_gc_event_create_failed`.
- Entered work without a completion is incomplete; partial fields are `trace_incomplete`.
- An outer heap failure without any internal record is `unresolved_internally`.
- Duplicate, out-of-order, contradictory, or outer/internal-conflicting data is `malformed_or_conflicting`.

The parser was exercised with synthetic cases A–H (success, each identified failure, missing completion, missing whole record, conflicting evidence, the retained R3 guest fixture, and result-format compatibility). Synthetic cases are labeled in the hosted manifest and are not guest evidence.

## Build and linkage qualification

The candidate manifest records the full hashes, pinned commit, runtime source hashes, replacement object hash, and production isolation checks. Key artifact hashes are:

| Artifact | SHA-256 |
| --- | --- |
| Diagnostic NativeAOT ELF | `928A3BBC995CEC67E60885C7159FC8EE6B16BD641A3A0B5C23593ABAA3F080C5` |
| Diagnostic kernel ELF | `714A7DBC77EDC32513221F63B6789BC0FED267A3495C5EF54F5E886ADCEB3341` |
| Diagnostic staged ESP ramdisk copy | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Proof-disabled production NativeAOT ELF | `A69CABA99C4EF0BF7E0DA46F9C3BC91F3B30AF22E744F557C97CA84CCB695BF0` |
| Proof-disabled production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Diagnostic replacement `gcwks.cpp.obj` | `C0503979BBB13DCBB51F29A74B6C5A306D303414FFF51E2FCAD55515BDC5387A` |
| Instrumented copied `gc.cpp` | `7344E5129997CD9608D3EE8F0CDC488EA1C10B130D937FF591C88515D26404F0` |

The diagnostic final ELF map contains `GuideXosNativeAotGcInitGetField`, `GuideXosNativeAotGcInitRecordHeapResult`, the concrete `GCHeap::Initialize()` symbol, and the `Runtime.WorkstationGC:gcwks.cpp.obj` archive member. The runtime-pack archive manifest confirms one stock Workstation GC member removed and one replacement member included. The candidate PE import report contains zero occurrences of `FreeLibrary`, `GetProcAddress`, `LoadLibraryExW`, and `SetThreadErrorMode`.

Production isolation passed: the production runtime manifest has `c188r12r4r5r4r5GcInitProbes=false`, the production NativeAOT map has no `GCINIT` marker or probe accessor, and the proof-disabled production kernel builder rejects both the marker and compile define. The accepted production `PersistentCompositeLifecycle` remains enabled.

## Hosted qualification and warnings

The new hosted GC-init runner passed. It compiled and ran the actual record implementation with 19 C++ assertions and passed all 8 analyzer fixtures. Its manifest records identical raw R3 serial hashes before and after analysis and `noQemuLaunched=true`, `noQacReserved=true`.

Eight focused regression scripts also passed: R1 initialization attribution, R3 return contract, R4 ThreadStore readiness, isolated GC ownership, QAC ownership predicates (15 cases), QAC admission (32 cases), QAC stream capture (2 cases), and C181 QMP event capture. Combined with the new runner, 9 hosted test scripts passed. The managed UI baseline remains 40 suites / 969 cases; it was not rerun because this change did not touch UI dependencies or projects.

PowerShell AST parsing passed for all seven changed PowerShell scripts, and `git diff --check` passed. No tests made a guest connection or reserved QAC.

Non-fatal build warnings were recorded in the logs: 65 diagnostic NativeAOT warning lines, mostly missing-PDB `LNK4099` notices, linker/compiler warnings, managed/project warnings, and entry-point converter advisories; 219 diagnostic kernel warning lines, mostly unused parameters, functions, variables, and other GCC diagnostics; 67 production NativeAOT detailed-log warning lines, with the same warning families as the diagnostic NativeAOT build; and 425 production kernel warning lines, mostly unused parameters, functions, variables, and other GCC diagnostics. The production wrapper summaries themselves passed. No build error or failed qualification gate remained.

## Next guest phase

Prepare C188R12R4R5R4R5R1 as a separate, maximum-one-boot attribution run. It must obtain fresh QAC admission, use a fresh one-boot guard, boot this staged candidate, preserve raw serial and diagnostic records, and release its owned guest. Continue through managed execution only if the actual initialization succeeds and the established ThreadStore prerequisites pass. The R3 guard is consumed and must not be reused. This phase did not execute that run.

The smallest next step is the fresh single-guest attribution run. A source correction is not justified until that run identifies the first failed internal operation.

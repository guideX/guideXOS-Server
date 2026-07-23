# NativeAOT GC-Owned Virtual Memory Boundary

Status: the raw-address adapter, bounded ownership registry, hosted probe, and
true bare-metal QEMU probe are complete. Workstation GC remains inactive: this
work does not call `RhInitialize`, allocate through the managed GC, start a
finalizer/helper thread, or trigger collection.

Date: 2026-07-22. This document records the current evidence for the exact
locked .NET 9.0 `win-x64` Workstation GC pack. Historical VM/readiness reports
remain useful background, but the current gate is the result recorded in
section 21.

## 1. Objective and stop rule

The target is a GC-owned PAL boundary in which every reservation, commitment,
decommit, protection transition, reset classification, and release is owned by
the guideXOS virtual-memory layer. The boundary must be usable by a future
Workstation GC startup experiment without adding GC policy to the generic VM
allocator.

The probe is deliberately inactive. It performs native VM operations only and
does not enter `RhInitialize`, `GC_Initialize`, `RhpNewFast`, a collection, a
finalizer, or a managed thread.

## 2. Generic VM baseline

The generic API is in
`runtime/memory/guidexos_virtual_memory_region.h/.cpp`. It separates reserve
from commit, records ownership in `VirtualMemoryRegion`, and delegates true
AMD64 reservation, page-table mapping, TLB invalidation, zero-on-commit, and
frame release to the existing address-space backend.

The generic layer remains collector-neutral. The adapter is the only new
runtime-pack boundary and is linked into the kernel test image solely so the
QEMU probe can execute it.

## 3. Exact GC PAL contract

The matching source declares these `GCToOSInterface` operations in
`out/dotnet/gc-feasibility-baseline/source-extract/src/coreclr/gc/env/gcenv.os.h`:

| Contract | Matching Windows implementation | Adapter status |
| --- | --- | --- |
| `VirtualReserve(size, alignment, flags, node)` | `gc/windows/gcenv.windows.cpp:685` | Implemented; raw base returned |
| `VirtualRelease(address, size)` | `:710`; Windows ignores `size` and calls `VirtualFree(address, 0, MEM_RELEASE)` | Implemented; exact base required; zero or exact size accepted |
| `VirtualCommit(address, size, node)` | `:753` | Implemented for reserved subranges |
| `VirtualDecommit(address, size)` | `:772` | Implemented; decommit is not release |
| `VirtualReset(address, size, unlock)` | `:786` | Explicitly unsupported; never aliases decommit |
| `VirtualReserveAndCommitLargePages` | `:720` | Explicitly unsupported in selected configuration |
| write-watch support | `:798` and `VirtualReserveFlags::WriteWatch` | Explicitly unsupported |
| NUMA node selection | reserve/commit implementations at `:700`, `:762` | Explicitly unsupported; one-node configuration |
| page size | declaration `gcenv.os.h:434` | Implemented from generic VM |
| virtual/physical memory status | `gcenv.windows.cpp:958`, `:972`, `:1006` | Implemented only from bounded native ledger |

The stock source has no general GC-owned protection method; the adapter still
exposes protection for the guideXOS mapping proof and keeps GC commit protection
read/write by default.

The extracted source contains the contract definitions and declarations. The
stock archive audit additionally finds `gcenv.windows.cpp.obj` in
`Runtime.WorkstationGC.lib`; the exact stock symbol binding is the remaining
gate documented in section 19.

## 4. Raw address semantics

The adapter operates on raw addresses, not a C++ region object handed to the
collector:

- reserve returns the actual base and rounded reserved size;
- commit, decommit, protect, and query accept page-aligned subranges fully
  contained in one active reservation;
- release requires the exact reservation base and rejects interior pointers;
- release accepts size zero, matching the Windows implementation, or the exact
  rounded reservation size;
- `reserveVirtualMemoryAt` is an explicit exact-address test helper. The stock
  GC contract has no separate preferred-address hint operation;
- address arithmetic is checked before lookup, offset calculation, or size
  rounding.

## 5. Bounded ownership registry

`guidexos_nativeaot_virtual_memory_adapter.cpp` owns a fixed array of 32
records. Each active record contains the generic `VirtualMemoryRegion`, raw
base, reserved size, adapter instance, generic region generation, reservation
generation, opaque legacy-handle state, purpose, and operation counters.

The registry is the sole source of adapter ownership. It does not duplicate
per-page mapping state and it does not allocate dynamically.

## 6. Capacity and stale identity

The registry fails deterministically with `OutOfMemory` at 32 live
reservations; it never grows silently. Each adapter lifecycle has an instance
identity, and each record has a reservation generation. Queries and explicit
diagnostics cross-check the adapter instance, slot generation, generic region
generation, base, and size.

The probe exhausts all 32 slots, rejects the 33rd reservation, releases every
slot, reuses an address, verifies a new generation, and rejects the stale
identity. Release also rejects interior, wrong-size, double-release, and
post-release use.

## 7. Reservation mapping and frame delta

Reservation creates address-space metadata without committing VM data pages.
In true bare-metal mode the QEMU probe reports:

```text
Adapter reserve data-frame delta: PASS
```

The observed delta is zero. The hosted probe also reports zero because the
hosted provider has no physical frame ledger; it does not fabricate physical
ownership from host process accounting.

## 8. Commit mapping and partial ranges

Commit delegates to the generic region for page mapping and zero-on-commit.
The QEMU probe commits disjoint pages in one reservation, checks that every
committed page is independently writable, verifies zero initialization, and
checks the committed-frame delta. The observed adapter commit delta is two
frames for two sparse pages.

An outside-range or cross-reservation commit is rejected before delegation.
Generic rollback leaves the registry and generic VM statistics unchanged after
an overflow/invalid-range failure.

## 9. Decommit and recommit

Decommit removes mappings and returns owned frames while retaining the
reservation. The QEMU probe decommits one sparse page, verifies the other page
remains live, checks frame recovery, recommits the released page, and verifies
zero initialization again. Hosted decommit/recommit passes with zero physical
ledger deltas.

## 10. Release semantics

Release is a full ownership transition. It unmaps any committed pages, removes
the generic metadata, clears the adapter record, and returns the slot to the
bounded registry. The QEMU probe checks range reuse, registry-generation change,
stale-record rejection, mapping leak count, frame leak count, and registry leak
count.

## 11. Reset/discard classification

NativeAOT `VirtualReset` is a discard hint over still-committed pages. It is not
decommit and it is not release. The generic guideXOS VM has no source-backed
discard primitive, so `resetVirtualMemoryRaw` returns `VmResult::Unsupported`
for an owned range and leaves the mapping/ownership unchanged.

This is an intentional selected-configuration limitation. Reset is not needed
by the first reserve/commit proof, but it must be implemented separately before
any collection path that depends on discard semantics is considered.

## 12. Protection

The adapter exposes read/write, read-only, and no-access transitions through the
generic region. On true AMD64, read-only maps a present non-writable PTE and
no-access maps a non-present PTE with the existing CR0.WP/fault machinery. The
QEMU adapter probe checks the protection mapping and query transitions; the
generic VM probe separately checks the guarded fault behavior.

## 13. Page size and allocation granularity

The adapter reports the actual generic VM values. The hosted and QEMU probes
both validate 4096-byte pages and the selected allocation granularity; no
64-KiB Windows allocation-granularity value is substituted for the guest page
size.

## 14. Memory status

`getMemoryStatus` is callable without host APIs. In true mode it reports limits
from the bounded address-space/frame ledger and the QEMU probe passes the
status check. In hosted mode it reports `NOT REQUIRED` because no physical
ledger is available; claiming host-wide available memory would violate the
ownership contract.

## 15. Failure and rollback behavior

The adapter rejects uninitialized use, duplicate initialization, unsupported
flags, NUMA nodes, invalid alignment, overflow, non-page-aligned subranges,
cross-range operations, invalid handles, stale identities, and registry
capacity exhaustion. Failed operations are traced with result and rollback
state. The hosted and QEMU probes verify that failed reserve/commit paths leave
no live reservation or committed mapping behind.

## 16. Shutdown

Normal shutdown rejects live reservations. A separate failure-cleanup path is
available for teardown diagnostics and records cleanup as failure cleanup. The
probe verifies live-shutdown rejection, explicit release, successful shutdown,
reinitialization with a new adapter instance, and zero live records at the end.

## 17. Hosted probe evidence

Command:

```text
powershell -ExecutionPolicy Bypass -File scripts/smoke-native-virtual-memory.ps1
```

Results on 2026-07-22: hosted generic VM PASS, adapter initialization/PAL
reserve/zero/decommit/recommit/reset classification/PAL release/double release
rejection/shutdown PASS, generic API runtime-neutral coupling PASS, and the
inactive adapter probe PASS. The expected-fault guard is `BLOCKED` because the
hosted probe deliberately has no unsafe fault harness.

The separate managed no-collection proof sweep is not VM-boundary evidence:
artifact/static checks and single-allocation execution passed; the host-log
execution proof reproduced `0xC0000005`, and repeated-allocation execution
stopped at its PE-import assertion because resolver-only imports were present.
Those proof-harness results do not change the raw VM probe result.

## 18. QEMU probe evidence

Command:

```text
powershell -ExecutionPolicy Bypass -File scripts/smoke-native-virtual-memory-qemu.ps1
```

The 2026-07-22 run rebuilt both baseline and true-VM test kernels, booted the
baseline, and emitted `Native VM ALL_PASS: PASS`. All generic and adapter
checks passed, including adapter initialization, reserve, zeroing, sparse
commit, decommit/recovery, recommit zeroing, protection, preferred-address
reuse, registry reuse, stale-record rejection, failure rollback, shutdown,
frame/mapping/registry leak checks, and memory status.

## 19. Stock Workstation GC binding audit

The locked stock archive still contains
`nativeaot/Runtime/Full/CMakeFiles/Runtime.WorkstationGC.dir/__/__/__/gc/windows/gcenv.windows.cpp.obj`.
The extracted object imports Windows VM entry points including
`VirtualAlloc`, `VirtualFree`, `VirtualUnlock`, and
`VirtualAllocExNuma`, and defines the decorated `GCToOSInterface::Virtual*`
methods. The current runtime-pack build removes only the existing
`EHHelpers.cpp.obj`, `thread.cpp.obj`, and optional allocation helper members;
it does not replace the stock GC environment object with the new adapter.

Therefore the adapter is linked and proven in the guideXOS kernel test image,
but exact stock Workstation GC PAL symbol binding and elimination of those
Windows VM imports are not yet proven. This is the one current readiness
blocker. No claim is made that a stock GC startup could already reach the
adapter.

## 20. Deliberate limitations

Large pages, NUMA placement, write-watch, reset/discard, collection-safe thread
suspension, GC heap publication, card tables/write barriers, module/type-manager
registration, handle-manager startup, finalizer/helper startup, and managed GC
teardown remain outside this pass. The registry is bounded and intended for the
first controlled integration experiment, not a production multi-process VM
manager. The legacy `VirtualMemoryHandle` API remains only for compatibility;
new PAL call sites must use raw-address identity and diagnostics.

## 21. Current readiness result

**Outcome B.** The raw VM PAL behavior and bounded ownership registry are
complete and independently proven in hosted and true bare-metal probes. The one
remaining prerequisite is exact binding of the stock Workstation GC
`GCToOSInterface::Virtual*` symbols to this adapter while removing the stock
Windows VM object/imports. No `RhInitialize` or collection experiment is
authorized yet.

This replaces the older statement that generic reserve/commit behavior itself
was the next blocker. The generic VM, adapter registry, and QEMU lifecycle are
now evidence-backed; the remaining work is the stock archive/link boundary.

## 22. Exact next experiment

Create a runtime-pack-local GC environment replacement or equivalent decorated
symbol layer that binds the locked Workstation GC `GCToOSInterface::Virtual*`
calls to these raw-address adapter functions, then audit the adapted archive
for the absence of Windows VM imports from the GC-owned path. Keep that change
inactive, rerun the hosted/QEMU adapter probes and static link audit, and only
then reassess the separate GC event/critical-section/startup prerequisites.

Do not call `RhInitialize`, start a finalizer/helper thread, allocate through
the real GC, or trigger a collection as part of this binding experiment.

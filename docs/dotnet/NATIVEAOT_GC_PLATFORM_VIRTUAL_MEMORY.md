# NativeAOT Workstation GC Platform Virtual Memory

Status: inactive adapter-probe only. Workstation GC is not initialized, no GC
heap is created, no finalizer/helper thread is started, and no collection is
triggered.

Current 2026-07-22 update: the raw-address adapter and bounded ownership
registry now pass hosted and true bare-metal QEMU lifecycle probes. The generic
VM integration is no longer the active blocker. See
[NativeAOT GC-Owned Virtual Memory Boundary](NATIVEAOT_GC_OWNED_VIRTUAL_MEMORY.md)
for the current contract matrix and evidence. The locked stock Workstation GC
archive still contains the Windows `gcenv.windows.cpp.obj` VM implementation,
so exact decorated `GCToOSInterface::Virtual*` binding to the adapter remains
the one readiness blocker; `RhInitialize` remains uncalled.

## 1. Selected collector boundary

The future target remains one-node Workstation GC with background/concurrent GC,
Server GC, NUMA placement, large pages, and write-watch disabled. The only
platform surface advanced in this pass is virtual-memory reserve/commit/
decommit/release/protection. This is not permission to make the collector
reachable.

## 2. Required NativeAOT VM contract

The matching runtime-pack surface separates reservation from commitment and
requires page size, allocation granularity, reserve, commit, decommit, release,
protection, and memory-status queries. Reset/discard remains a distinct
optional operation. The generic guideXOS API intentionally has no reset method;
the adapter returns `VmResult::Unsupported` rather than aliasing reset to
decommit.

## 3. Generic boundary

The adapter at
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_virtual_memory_adapter.*`
owns one `VirtualMemoryRegion` and forwards lifecycle calls to
`runtime/memory/guidexos_virtual_memory_region.*`. It exposes:

```cpp
bool trueReservationSemantics();
const char* backendModeName();
```

`reserveVirtualMemory(..., requireTrueReservation=true)` refuses an
eager-compatibility backend. No collector-specific behavior was added to the
frame allocator, page-table layer, process model, page-fault handler, or
generic VM API.

## 4. True bare-metal mode

The experimental AMD64 kernel compiles `GXOS_TRUE_VIRTUAL_MEMORY`. The UEFI
bootloader supplies a 2 MiB identity-mapped frame pool. The generic address-
space layer owns both VM data frames and page-table frames. Reservation alone
uses zero VM data frames; commit allocates, zeroes, maps, and accounts frames;
decommit unmaps and returns them; release removes mappings and metadata.

The runtime reservation range is bounded to 64 MiB with 32 metadata slots and
4 MiB maximum regions. This is sufficient for the lifecycle proof and is not a
claim of a production process VM or a GC heap policy.

## 5. Adapter mapping

| Adapter operation | Generic guideXOS operation | Status |
| --- | --- | --- |
| page size | `pageSize()` | Implemented |
| allocation granularity | `allocationGranularity()` | Implemented |
| reserve | `reserve()` | Implemented; true mode can be required |
| commit | `commit()` | Implemented; zero-on-commit |
| decommit | `decommit()` | Implemented; unmap and frame release |
| release | `release()` | Implemented; unmap, release, metadata clear |
| protection | `protect()` | Implemented for R/RW/NoAccess in bare AMD64 |
| reset/discard | none | Explicit `Unsupported` |
| memory available | bounded maximum-region check | Probe only |
| large pages / NUMA / write-watch | none | Explicitly unsupported/disabled |

## 6. Protection and fault evidence

Bare-metal protection maps NoAccess to a non-present PTE, ReadOnly to a
present non-writable PTE, and ReadWrite to a present writable PTE. CR0.WP is
enabled. The opt-in QEMU fault hook validates exact fault address, read/write
error bit, protection error bit, and instruction resume address. It tests
read-only writes, no-access reads, reserved-uncommitted pages, decommitted
pages, and released pages. Unexpected faults still use the normal fatal path.

## 7. Inactive adapter probe

Run:

```text
powershell -ExecutionPolicy Bypass -File scripts/smoke-native-virtual-memory.ps1
```

The hosted generic lifecycle and real adapter probe passed on 2026-07-17,
including page size, granularity, true-mode reserve request, commit/zeroing,
decommit/recommit zeroing, reset classification, release, and stale release.
The probe does not initialize GC or create managed objects.

## 8. QEMU evidence

Run:

```text
powershell -ExecutionPolicy Bypass -File scripts/smoke-native-virtual-memory-qemu.ps1
```

The default run rebuilt the kernel, ran the QEMU baseline boot, and ran the
true VM test. The result was `ALL_PASS`. Separate reported fields passed for
frame allocation/release, metadata capacity, virtual-range exhaustion,
protection-fault handling, rollback, teardown, TLB invalidation, no leaks, and
direct read/write behavior.

The hosted thread lifecycle, scheduler/event regressions, bare-metal
native-thread QEMU lifecycle, generic ELF smoke, managed static artifact,
single-allocation live proof, and bounded repeated-allocation/OOM live proof
also passed. These are no-collection execution proofs only; they do not prove
Workstation GC initialization or collection.

## 9. Reset/discard distinction

NativeAOT `VirtualReset` is a discard hint while the reservation remains
committed. It is not decommit and it is not release. The current generic API
has no source-backed discard primitive, so the adapter returns
`VmResult::Unsupported` and preserves the distinction.

## 10. Current initialization blockers

The VM lifecycle blocker addressed by this pass was the absence of generic
frame ownership, page-table mapping/unmapping, zero-on-commit, protection,
TLB invalidation, bounded rollback, and address-space teardown. Those pieces
are now proven by the focused QEMU test.

The remaining blockers for actual Workstation GC initialization are outside
this VM pass: generic critical sections/mutexes, GC event/wait semantics,
FLS/TLS lifetime, ThreadStore attachment, stack/context contracts,
finalizer/helper startup, heap/root/write-barrier initialization, and a
controlled managed teardown policy.

## 11. No GC activation

No code in this pass calls GC initialization, creates an allocation context,
starts managed threads, invokes a finalizer, or triggers collection. The
adapter probe is a contract probe only. It must not be cited as evidence that
NativeAOT managed allocation or GC is ready.

## 12. Historical decision and next experiment

Decision for the VM pass: **Outcome A — true bare-metal reserve/commit
lifecycle complete**. The next experiment is the generic critical-section/
mutex primitive required by the inactive Workstation GC boundary. Keep GC
initialization disabled until that primitive and the remaining managed startup
contracts are independently validated.

The preceding decision is historical. The current 2026-07-22 decision is
Outcome B: VM lifecycle and registry evidence pass, while exact stock
Workstation GC PAL symbol binding/import elimination remains outstanding.

## 13. Current exact binding result (2026-07-22)

The VM adapter is now bound through the exact Workstation GC symbols in a
replacement `gcenv` object. See [NativeAOT Workstation GC Platform Object
Replacement](NATIVEAOT_WORKSTATION_GC_PLATFORM_OBJECT.md). The adapted archive
has zero missing or duplicate exact definitions and the inactive hosted
exact-symbol VM lifecycle passes. Remaining Windows imports are contributed by
the separate PAL/runtime object family, so this document's historical VM
primitive result does not authorize collector startup or `RhInitialize`.

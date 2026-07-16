# NativeAOT Workstation GC Platform Virtual Memory

Status: inactive adapter-probe only. Workstation GC is not initialized, no GC
heap is created, no finalizer/helper thread is started, and no collection is
triggered.

The locked matching source is the NativeAOT/runtime-pack baseline at commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. The source extract is under
`out/dotnet/gc-feasibility-baseline/source-extract`.

## 1. GC VM requirements

The collector separates virtual-address reservation from commitment and
release. It also exposes reset/discard, page-size and granularity queries,
memory-status discovery, optional write-watch, large-page, and NUMA paths, and
platform protection through the NativeAOT PAL. The selected target is:

- Workstation GC
- one heap/node
- background/concurrent GC disabled
- one managed application thread during the future first probe
- finalizer/helper thread required later
- no Server GC, NUMA placement, large pages, or write-watch mode

This pass implements only an inactive VM adapter probe. It does not make the
collector reachable.

## 2. Matching source symbols

The collector declarations are in
`src/coreclr/gc/env/gcenv.os.h`, with the Windows behavior in
`src/coreclr/gc/windows/gcenv.windows.cpp`. The relevant declarations are at
approximately lines 253-319 and 431-434 in the locked extract; the Windows
implementations are approximately lines 685-834 and 1006.

The platform-facing protection entry is
`src/coreclr/nativeaot/Runtime/windows/PalRedhawkMinWin.cpp`:
`PalVirtualProtect` at approximately line 998. The matching source uses
`VirtualAlloc` reserve/commit, `VirtualFree` decommit/release, `MEM_RESET` for
reset, and native protection calls.

## 3. Mandatory/optional classification

Classification is for the selected one-node, Workstation, no-background,
no-large-page, no-NUMA configuration:

| Collector surface | Matching source symbol | First initialization | First collection | Class | Adapter status |
| --- | --- | --- | --- | --- | --- |
| Page size | `GCToOSInterface::GetPageSize` | Required | Required | M | Maps to `pageSize()` |
| Allocation granularity | `GCToOSInterface::Initialize` / system-info query | Required | Required | M | Maps to `allocationGranularity()` |
| Reserve | `GCToOSInterface::VirtualReserve` | Required | Required | M | Maps to owned region reserve |
| Commit | `GCToOSInterface::VirtualCommit` | Required | Required | M | Maps to region commit |
| Decommit | `GCToOSInterface::VirtualDecommit` | Setup/lifecycle | Segment lifecycle | M | Maps to region decommit |
| Release | `GCToOSInterface::VirtualRelease` | Setup/lifecycle | Segment lifecycle | M | Maps to region release |
| Reset/discard | `GCToOSInterface::VirtualReset` | Not reached in selected startup proof | Not proven reachable in selected first collection | N | Precise `Unsupported`; not aliased |
| Protection | `PalVirtualProtect` | PAL/runtime setup only | Mode-dependent | C | Generic hosted support; bare enforcement unavailable |
| Memory availability | `GCToOSInterface::GetMemoryStatus` | Required policy input | Low-memory policy | M | Bounded `memoryAvailable` probe only |
| Large pages | `VirtualReserveAndCommitLargePages` | Disabled | Disabled | N | `supportsLargePages() == false` |
| NUMA-aware reserve/commit | node parameter / `VirtualAllocExNuma` | One node selected | Disabled | N | `supportsNumaPlacement() == false` |
| Write-watch capability | `SupportsWriteWatch` | Disabled | Disabled with selected mode | N | Not implemented |
| Write-watch query/reset | `GetWriteWatch` | Disabled | Disabled with selected mode | N | Not implemented |
| Unknown extra mode surface | future configuration-dependent entry points | Unknown | Unknown | U | Not exposed |

`M` does not mean GC initialization is safe today; it means the symbol is
required once that initialization is intentionally attempted. The generic
bare-metal implementation is currently an eager-backed compatibility backend,
so it cannot satisfy the collector's true reserve/commit contract.

## 4. Mapping to generic guideXOS VM

The inactive adapter is under
`tools/dotnet/runtime-pack/src/platform/` and owns an opaque
`VirtualMemoryHandle` containing one generic `VirtualMemoryRegion`:

| Adapter operation | Generic operation |
| --- | --- |
| `reserveVirtualMemory` | `reserve` |
| `commitVirtualMemory` | `commit` |
| `decommitVirtualMemory` | `decommit` |
| `releaseVirtualMemory` | `release` |
| `baseAddress` | region base |
| `getPageSize` | `pageSize` |
| `getAllocationGranularity` | `allocationGranularity` |
| `memoryAvailable` | bounded maximum-region query |
| large pages/NUMA | explicit false/unsupported capability |

The adapter does not duplicate test-only operations. The probe calls these
same adapter functions and verifies reserve, commit, zeroing, decommit,
recommit, release, stale release, page size, granularity, and preferred-base
behavior.

## 5. Page size and granularity

The current AMD64 bare-metal page size is 4096 bytes. Hosted builds query the
host page size. The generic API requires page-aligned commit/decommit ranges.
Windows hosted allocation granularity is reported by the host implementation;
the bare-metal compatibility backend reports 4096 bytes and does not claim a
Windows-style 64 KiB granularity.

## 6. Preferred-address requirements

The generic API treats a non-null preferred base as exact placement. The
address must be absolutely aligned and the entire rounded reservation must be
available. Hosted failure to honor it returns `AddressUnavailable` rather than
silently relocating the region. The inactive adapter forwards the request.

The bare-metal compatibility arena accepts exact addresses only inside its
bounded arena. It is not a replacement for collector segment placement and no
collector proof base is hardcoded.

## 7. Decommit/reset/release distinctions

The matching source explicitly documents `VirtualReset` as a discard hint for
contents that are no longer of interest while the range remains committed; it
is not decommit. The Windows implementation calls `MEM_RESET` and optionally
`VirtualUnlock`. `VirtualDecommit` instead uses `MEM_DECOMMIT`, and
`VirtualRelease` uses `MEM_RELEASE` for the complete reservation.

The generic API currently has no reset/discard operation. The adapter returns
`VmResult::Unsupported` for reset, with the source distinction preserved. It
does not alias reset to decommit, because that would change commitment and
addressing semantics without source evidence for the generic implementation.
Reset should be added only if it becomes mandatory for the selected
initialization or first-collection path.

## 8. Inactive adapter probe

The independent hosted command is:

```text
scripts/smoke-native-virtual-memory.ps1
```

It reports the generic lifecycle and then runs
`guidexos_nativeaot_virtual_memory_adapter_probe`. The probe passed on
2026-07-16 for page size, allocation granularity, reserve, commit/zeroing,
decommit/recommit, reset classification, release, and stale release. The
probe does not initialize GC, create a heap, start a helper thread, collect, or
modify managed proof execution.

The QEMU VM test independently passed the compatibility lifecycle, but records
protection, physical-page accounting, and expected-fault guard validation as
blocked. Consequently the adapter probe is a hosted contract probe, not GC
readiness evidence.

## 9. Remaining GC initialization blockers

The exact next blocker is true generic bare-metal reservation/commit ownership:
an address-space-owned interval index, physical-frame allocator, page-table
mapping/unmapping, zero-on-commit, permission updates, and TLB invalidation.
That is the smallest correction required before claiming the collector's VM
contract honestly.

After that, the previously identified GC blockers remain: critical sections,
GC event/wait semantics, FLS/TLS lifetime, ThreadStore attachment, stack
bounds/context contracts, finalizer/helper startup, heap/root/write-barrier
initialization, and controlled teardown. None are activated by this pass.

## 10. Exact next primitive

Because the current bare-metal address-space model provides only eager backing,
the decision is Outcome C for this VM pass. The next experiment is to add the
smallest address-space-owned reservation metadata and physical-frame/page-table
mapping invariant, including decommit/recommit zeroing and TLB updates. Keep it
generic, bounded, and independent of NativeAOT; do not initialize GC yet.

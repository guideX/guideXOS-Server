# guideXOS Virtual-Memory Regions

## 1. Purpose

This document describes the runtime-neutral virtual-memory region primitive used
by future native subsystems, executable loaders, allocators, language runtimes,
and isolated platform adapters. The public abstraction is in
`runtime/memory/guidexos_virtual_memory_region.h`; it does not expose host
handles or name a particular operating system or runtime.

The first implementation deliberately stops at the smallest lifecycle:

```text
reserve -> commit -> decommit -> recommit -> release
                         \-> query/protect
```

The managed proof heap is not routed through this API. Workstation GC remains
uninitialized, no finalizer/helper thread is started, and no collection is
triggered.

## 2. Existing memory infrastructure

The audit found a useful hosted executable-memory wrapper and a hosted malloc
page-accounting wrapper, but neither is a general reserve/commit abstraction.
The kernel has a fixed heap and a special huge-page facility. AMD64 boot creates
identity mappings and the kernel can read CR3, but there is no general kernel
physical-frame allocator, page-table mapper, address-space reservation index,
or recoverable page-fault service.

### Capability inventory

| Capability | Source path | Hosted | Bare metal | Suitable as-is? | Required adaptation |
| --- | --- | ---: | ---: | ---: | --- |
| Physical-frame allocation | `kernel/core/hugepages.cpp`; bootloader page allocations | Partial | No general 4 KiB allocator | No | Add an address-space-owned frame allocator before true commitment accounting |
| Physical-frame release | `kernel/core/hugepages.cpp` | Partial | No general release path | No | Pair frame ownership with page-table unmap and release |
| Virtual-address selection | `runtime/memory/guidexos_virtual_memory_region.cpp`; host APIs | Yes | Bounded static arena only | Partial | Replace arena selection with address-space metadata |
| Fixed-address mapping | `executable_memory.cpp`; bootloader identity maps | Yes | Bootloader-only mappings | No | Add generic kernel mapping primitive |
| Anonymous mapping | `executable_memory.cpp` via `VirtualAlloc`/`mmap` | Yes | No | No | Map anonymous frames through the kernel address-space layer |
| Zero-filled pages | `executable_memory.cpp`; host OS commitment | Yes | Compatibility arena is zeroed explicitly | Partial | Zero every newly mapped frame at commitment |
| Mapping removal | `executable_memory.cpp` release | Yes | No generic unmap | No | Remove PTEs, invalidate TLB, release frames |
| Page protection | `executable_memory.cpp` (`VirtualProtect`/`mprotect`) | Yes | No page-table protection API | No | Add per-page permission updates and TLB invalidation |
| Read/write/execute flags | `executable_memory.cpp` | Yes | No | No | Define page-table permission translation; first collector surface needs only R/RW/NA |
| Guard/no-access mappings | Host `PAGE_NOACCESS`/`PROT_NONE` | Yes | No recoverable fault test | No | Add no-access PTEs and an isolated expected-fault harness |
| Address-space queries | Host query/list metadata | Yes | No generic query index | No | Query reservation and page metadata owned by the address space |
| Reserve without backing | `VirtualAlloc(MEM_RESERVE)`/`mmap(PROT_NONE)` | Yes | No | No | Implement unbacked interval reservation |
| Commit with backing | Host `MEM_COMMIT`/`mprotect` | Yes | No physical-frame mapping | No | Allocate, zero, map, and record frames |
| Decommit retaining reservation | Host `MEM_DECOMMIT`/`madvise` | Yes | Metadata-only compatibility behavior | No | Unmap and release frames while retaining interval metadata |
| Full release | `VirtualFree(MEM_RELEASE)`/`munmap` | Yes | Static arena metadata release only | Partial | Release mappings, frames, and interval metadata |
| Page size | host query; bare boot convention | Yes | 4096 bytes in current AMD64 path | Yes for first primitive | Make the architecture page-size source authoritative |
| Allocation granularity | Windows host reports 64 KiB; POSIX host page size | Yes | 4096-byte compatibility granularity | Partial | Do not claim 64 KiB on bare metal; define future policy when true reservation exists |
| Address alignment | checked arithmetic in region implementation | Yes | 4096-byte arena alignment | Partial | Align absolute addresses for all requested power-of-two alignments |
| Per-process ownership | host process ID in region metadata | Yes | Current address space is implicit and single CPU | Partial | Attach region metadata to an explicit address-space owner |
| Hosted equivalent | private OS calls in `runtime/memory/...cpp` | Yes | N/A | Yes | Keep host details behind the generic result contract |

The important distinction is explicit: a reservation owns an interval of
virtual addresses; commitment owns backing pages for a subrange. The hosted
backend implements that distinction. The current bare-metal backend cannot.

## 3. Public generic API

The API is in `gxos::runtime::virtual_memory`:

```cpp
enum class MemoryProtection {
    NoAccess, ReadOnly, ReadWrite, ReadExecute, ReadWriteExecute
};

struct VirtualMemoryRegion {
    void* base;
    size_t reservedSize;
    size_t committedSize;
    // opaque private ownership and generation state
};

VmResult reserve(size_t size, size_t alignment, void* preferredBase,
                VirtualMemoryRegion* region);
VmResult commit(VirtualMemoryRegion&, size_t offset, size_t size,
                MemoryProtection protection);
VmResult decommit(VirtualMemoryRegion&, size_t offset, size_t size);
VmResult protect(VirtualMemoryRegion&, size_t offset, size_t size,
                 MemoryProtection protection);
VmResult release(VirtualMemoryRegion&);
VmResult query(const void* address, VirtualMemoryInfo* information);
```

`VmResult` is a stable contract-level result type. Host error codes are kept
only in `lastDiagnostic()` diagnostics and are not exposed as API semantics.

## 4. Region ownership

`VirtualMemoryRegion` is an owning, non-copyable, non-movable capability. Its
opaque state and generation are private. A successful reservation records the
owning host process or the current bare-metal address-space owner. The caller
must release it explicitly or its destructor performs best-effort cleanup.

Release clears the public fields and invalidates the private state. Operations
on the cleared object return `AlreadyReleased`; an address query after release
returns `NotFound`. The generation check prevents an old region object from
being accepted against a reused metadata slot.

The current implementation is single-process and single-address-space aware;
host operations are serialized by the region metadata lock. It does not claim
SMP safety for the kernel backend.

## 5. Page and alignment rules

- Native page size is 4096 bytes for the current AMD64 bare-metal path. Hosted
  builds query the host page size.
- The first implementation uses one page as the minimum region alignment.
- A requested alignment must be a nonzero power of two and at least one page.
- Reservation sizes are rounded up to a page with checked arithmetic.
- Commit, decommit, and protection offsets and sizes must already be nonzero
  page multiples; they are not silently truncated.
- Preferred addresses must be page-aligned, satisfy the requested absolute
  alignment, and be inside the supported address range. A supplied preferred
  address is an exact-placement request; it is not a hint.
- Empty sizes, null output pointers, range overflow, outside-region ranges, and
  unsupported protection values are rejected with explicit results.
- The hosted maximum region is bounded to 1 TiB on 64-bit hosts (1 GiB on
  32-bit hosts). The bare-metal compatibility arena is 256 KiB with eight
  metadata slots and is intentionally not a general allocator.
- No Windows-style 64 KiB allocation granularity is claimed for bare metal.

## 6. Reservation semantics

Hosted reservation selects and exclusively owns a non-overlapping virtual
interval with no-access protection and no committed backing. It can later be
partially committed, decommitted without losing the interval, queried, and
released. Overlapping exact preferred-base requests fail with
`AddressUnavailable`.

Bare metal currently uses Strategy C: a bounded, eagerly-backed arena. The
arena storage exists before `reserve`, so the implementation provides exclusive
reservation metadata and later commitment metadata, but it does not provide a
true unbacked reservation. This is reported by `VirtualMemoryStats` and
`lastDiagnostic()` and is not presented as collector-grade reservation
semantics.

## 7. Commit semantics

Commit must remain within an owned region and uses checked range arithmetic.
Newly committed pages are explicitly zero-filled before becoming readable or
writable. A repeated commit of already committed pages is idempotent when the
protection is unchanged. A repeated commit with a different protection applies
that protection as an explicit transition, equivalent to a commit followed by
`protect`.

The hosted implementation tracks commitment per page and rolls back newly
committed pages if a multi-page operation fails. Bare metal records committed
pages in the bounded compatibility metadata and only accepts `ReadWrite`, the
one protection that its current non-paged kernel can honestly support.

## 8. Decommit semantics

Decommit retains the reservation interval, clears commitment metadata, and
permits a later recommit. Hosted decommit removes usable access and discards
the host backing where supported. Recommitted pages are explicitly zeroed and
therefore do not expose stale contents.

On bare metal, decommit clears the compatibility commitment bit and zeros the
arena bytes, but cannot remove a page-table mapping because the kernel has no
generic mapping layer. It therefore must not be described as releasing a
physical frame or making a page fault. This limitation is part of the selected
Strategy C contract.

## 9. Release semantics

Hosted release removes all committed mappings and the reservation, releases
host backing, clears ownership state, and invalidates the region. Double
release and stale use are reported safely. Release is never an arbitrary
address-range operation; only the owning region can release its interval.

Bare-metal release clears the arena metadata and invalidates the region. It
does not yet release physical frames because no general frame ownership exists.
The active-stack and active-code cases are not accepted as special release
operations; the future address-space layer must reject such ranges before
unmapping them.

## 10. Protection semantics

Hosted regions support no-access, read-only, read/write, read/execute, and
read/write/execute transitions through private host calls. The generic API does
not require RWX for the first collector surface. Protection applies only to
committed pages; uncommitted pages remain inaccessible.

The hosted tests verify read/write, read-only write rejection, restoration to
read/write, and no-access transitions. The bare-metal backend returns
`ProtectionUnsupported` for non-read/write requests and reports
`protectionEnforced=false`. No global kernel mapping is changed.

## 11. Query semantics

`query(address, information)` accepts an address within an owned region and
reports the containing page, page size, reservation range, committed byte
count, generation, committed state, and protection. It reports reservation
metadata even when the queried page is not committed. An address outside an
active region or after release returns `NotFound`; a null output pointer is
`InvalidArgument`.

## 12. Hosted implementation

The hosted implementation is private to
`runtime/memory/guidexos_virtual_memory_region.cpp`:

- Windows uses native reservation/commit/decommit/release/protection calls,
  with `PAGE_NOACCESS` for reserved and decommitted pages.
- POSIX uses anonymous `mmap`, `mprotect`, `madvise(MADV_DONTNEED)` where
  available, and `munmap`.
- Metadata is process-owned, page-granular, and protected by a mutex.
- Host-specific failures are converted to `VmResult` and retained as a short
  diagnostic only.
- Preferred-base requests are exact when supplied; a host that cannot honor
  the address returns `AddressUnavailable`.

The public header contains none of the host API names and exposes no raw host
handle.

## 13. Bare-metal implementation

The bare-metal branch uses a static 256 KiB, 4096-byte-aligned compatibility
arena and eight bounded reservation records. It detects overlap, records
generation ownership, zeroes new and recommitted pages, supports partial
commit/decommit, and supports release/reuse of reservation metadata.

The audit found no existing generic physical-frame allocator, PTE editor,
address-space object, TLB invalidation service, or expected-fault recovery
path. The implementation consequently does not introduce a parallel page-table
manager or hardcode a collector address. It reports its missing enforcement and
physical accounting explicitly.

## 14. Physical-page ownership

Hosted physical backing belongs to the host VM subsystem and is released by
decommit or release. `VirtualMemoryStats::physicalBackingAccounting` is true
for the hosted implementation.

The bare-metal compatibility arena is static storage, not a page-frame pool.
Its statistics deliberately report `physicalBackingAccounting=false`. A true
implementation requires one owner record per mapped frame, unmap-on-decommit,
release-on-release, and a zero-on-recommit invariant.

## 15. TLB behavior

Hosted TLB behavior is delegated to the host protection and mapping calls. The
bare-metal backend performs no page-table changes and therefore has no TLB
invalidation policy. The future address-space implementation must invalidate
the affected AMD64 translation after PTE permission or presence changes,
following the existing interrupt/scheduler locking rules and adding an SMP
shootdown policy before claiming SMP support.

## 16. Guard/no-access feasibility

The hosted backend can reserve boundary pages as no-access and the generic
metadata can identify an uncommitted middle/boundary layout. The current
hosted probe intentionally does not crash the normal test process; it reports
the expected-fault harness as blocked.

The QEMU test leaves boundary pages uncommitted and reports expected-fault
validation as `BLOCKED`. Bare-metal page faults currently log and halt, with no
recoverable expected-fault mode. Compile/mapping evidence is therefore
recorded, but normal validation never intentionally faults.

## 17. Tests

`runtime/tests/guidexos_virtual_memory_tests.cpp` independently covers reserve,
query, zero initialization, read/write, partial commit, partial decommit,
recommit zeroing, protection transitions, alignment/overflow/out-of-range
validation, overlap rejection, stale use, double release, range reuse, and
bounded cleanup cycles. The hosted smoke is
`scripts/smoke-native-virtual-memory.ps1`.

The inactive platform adapter probe is
`runtime/tests/guidexos_nativeaot_virtual_memory_adapter_probe.cpp` and uses
the real adapter functions under
`tools/dotnet/runtime-pack/src/platform/`.

The opt-in bare-metal test is built with
`GXOS_NATIVE_VIRTUAL_MEMORY_QEMU_TEST` and run by
`scripts/smoke-native-virtual-memory-qemu.ps1`. It runs an ordinary baseline
boot first and then parses independent serial markers for each VM operation.

Observed validation on 2026-07-16:

| Test | Result |
| --- | --- |
| Hosted generic lifecycle | PASS |
| Hosted protection enforcement | PASS |
| Hosted cleanup/accounting | PASS |
| Hosted expected-fault guard harness | BLOCKED |
| Inactive adapter probe | PASS |
| QEMU baseline boot | PASS |
| QEMU reserve/overlap/commit/zero/read-write | PASS |
| QEMU partial commit/decommit/recommit zeroing | PASS |
| QEMU release/reuse/metadata cleanup | PASS |
| QEMU protection transition | BLOCKED |
| QEMU physical-page accounting | BLOCKED |
| QEMU expected-fault guard test | BLOCKED |

## 18. Known limitations

- Bare-metal reservation is eagerly backed; it is not true virtual
  reservation/commit separation.
- Bare-metal backing is a static 256 KiB compatibility arena.
- There is no generic physical-frame allocator, page-table manager, guard-page
  fault recovery, or TLB update service.
- Bare-metal protection is not enforced, and only read/write is accepted.
- The implementation is single CPU and does not claim SMP safety.
- No swapping, file-backed mappings, shared memory, copy-on-write, NUMA,
  large-page, write-watch, or arbitrary fixed-address replacement support is
  added.
- The current primitive is not yet sufficient for Workstation GC initialization
  despite the hosted adapter probe passing.

## 19. Future generic users

Potential users are native loaders, future allocators, native subsystems, and
language runtimes that need an owned region lifecycle. Each user must choose
its own policy for executable permissions, guard pages, large pages, and
address placement. The generic region API must remain independent of those
users and must not acquire collector-specific state.

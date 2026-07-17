# guideXOS Native Virtual-Memory Regions

Status: true bare-metal reserve/commit lifecycle implemented for the opt-in
AMD64 kernel path. The generic API remains runtime-neutral. No GC is initialized,
no finalizer/helper thread is started, and no collection is triggered.

## 1. Scope and boundary

The public API is `runtime/memory/guidexos_virtual_memory_region.h`. It models
an owning reservation capability and the lifecycle

```text
reserve -> commit -> protect/query -> decommit -> recommit -> release
```

The bare-metal implementation is selected by `GXOS_TRUE_VIRTUAL_MEMORY` in
`kernel/Makefile`. The older eager compatibility implementation remains
available only when that define is absent; it is not used by the experimental
kernel build.

## 2. Design invariant

Reservation owns only a virtual interval. Commitment owns one physical frame
per committed page and one page-table mapping per committed page. These are
separate states and are independently visible through `VirtualMemoryInfo` and
`VirtualMemoryStats`.

The bare-metal backend uses one explicit current `AddressSpace`. It has no
collector-specific state, no GC callbacks, no finalizer logic, and no second
allocator hidden behind the generic VM API.

## 3. Boot-time frame supply

The UEFI bootloader allocates 512 pages (2 MiB) of `EfiLoaderData` immediately
before the final memory map and passes the identity-mapped range through:

```cpp
BootInfo::RuntimeFramePoolBase
BootInfo::RuntimeFramePoolPages
```

`kernel/core/address_space.cpp` is the sole owner of this pool for VM-region
and page-table frames. The bootloader maps the pool before `ExitBootServices`,
so the kernel can zero and inspect every frame through its identity mapping.

## 4. Address-space ownership

`kernel/core/include/kernel/address_space.h` defines the generic owner and
accounting surface. The current implementation is deliberately one-address-
space and one-CPU, but the owner pointer is retained in every region record so
stale handles cannot operate after teardown.

The address-space layer owns frame state, page-table edits, physical zeroing,
raw PTE queries, protection flags, and local TLB invalidation. The region layer
owns interval metadata and lifecycle policy.

## 5. Virtual-range allocation policy

The true backend uses a bounded first-fit range:

| Property | Value |
| --- | ---: |
| Range base | `0x0000000100000000` |
| Range size | 64 MiB |
| Page size | 4096 bytes |
| Metadata slots | 32 regions |
| Maximum region | 1024 pages / 4 MiB |

Requested preferred addresses are exact placements. Candidate intervals must
be page-aligned, satisfy the absolute requested alignment, not overlap an
owned reservation, and contain no already-present page-table mapping.

## 6. Reservation semantics

`reserve` rounds the requested size to a page, allocates one metadata record,
and claims only the virtual interval. It allocates zero ordinary VM data
frames, does not install present PTEs, and reports `mappingPresent=false` for
an uncommitted page. Exact, beginning, ending, containing, and adjacent
overlap cases are covered by the QEMU test.

This is a true unbacked reservation, not an arena over pre-existing storage.

## 7. Commit semantics

`commit` processes each page independently:

1. allocate a frame from the shared pool as `FrameOwner::VmRegion`;
2. zero the frame before making it accessible;
3. allocate and zero missing page-table pages from the same pool;
4. install the PTE and invalidate the local translation;
5. publish the committed metadata only after mapping succeeds.

Repeated commit is idempotent when protection is unchanged. A protection
change on an already committed page is applied as a page-table flag update.
Multi-page commit has bounded rollback: frames and PTEs newly created by the
failed operation are removed and returned.

## 8. Decommit semantics

`decommit` retains the reservation metadata but removes each committed PTE,
zeros the owned frame, and returns it to the shared frame pool with release
reason `Decommit`. It invalidates the affected translation before the frame is
reused. A later recommit obtains a frame and explicitly zeroes it, so stale
contents are not exposed.

## 9. Release semantics

`release` unmaps every committed page, verifies the removed physical frame
matches region metadata, zeroes and returns each frame with release reason
`Release`, and clears the interval metadata and generation. The released range
can be reserved again at the same preferred address. Double release and stale
handle use return explicit results.

## 10. Protection semantics

The true AMD64 backend enforces:

| Generic protection | PTE behavior |
| --- | --- |
| `NoAccess` | physical identity retained, `Present=0`, `NX=1` |
| `ReadOnly` | `Present=1`, `Writable=0`, `NX=1` |
| `ReadWrite` | `Present=1`, `Writable=1`, `NX=1` |

Executable protections are rejected as `ProtectionUnsupported`. CR0.WP is set
when the address-space layer initializes so supervisor writes honor read-only
PTEs. Physical ownership is preserved during protection transitions.

## 11. Page-table ownership and mapping queries

`address_space.cpp` walks the existing AMD64 four-level tables rooted at CR3.
Missing PML4/PDPT/PD/PT pages are allocated from the same explicit pool,
zeroed, marked present/writable, and counted as `FrameOwner::PageTable`.
Large-page entries are rejected by the 4 KiB mapping path.

The raw mapping query distinguishes no page-table entry from a non-present
PTE. VM query cross-checks the raw physical address and presence bit against
region metadata before returning success.

## 12. TLB behavior

Every map, unmap, and permission update executes local AMD64 `invlpg` and
increments `VirtualMemoryStats::tlbInvalidations`. The test asserts a nonzero
counter after the lifecycle. SMP shootdown is not implemented; the backend
must not be presented as SMP-safe until that policy exists.

## 13. Query and accounting contract

`query` reports reservation, commitment, physical frame, protection, page
range, and generation. `stats` reports total/free/allocated frames, region
frames, page-table frames, decommit/release return counts, mapping count,
metadata capacity, and active entries. A reservation-only operation leaves
the region-owned frame count unchanged.

## 14. Exhaustion and rollback

The bounded metadata pool returns `OutOfMemory` after 32 active records. The
64 MiB virtual range returns `AddressUnavailable` after sixteen maximum-size
4 MiB reservations. A test-only frame-allocation limit makes a four-page
commit fail after two allowed VM frames; the test verifies no committed pages,
no leaked region frames, and no present mappings remain.

## 15. Expected page faults

The normal page-fault path remains fatal for unexpected faults. The QEMU test
installs a temporary opt-in callback through `kernel/core/interrupts.h`.
The AMD64 stub preserves the interrupted frame, calls `exception_dispatch`,
and resumes only when the callback validates the exact address, read/write
bit, protection bit, and instruction length.

The test validates direct read/write access plus expected faults for read-only
writes, no-access reads, reserved-uncommitted pages, decommitted pages, and
released pages. The hook is not active outside the test window.

## 16. Teardown

`teardownAddressSpace` releases full, partially committed, and reservation-
only regions owned by the current address space, then invalidates the owner.
Stale region operations return `AlreadyReleased` or `NotFound`. The teardown
test verifies zero active regions, zero mappings, zero region-owned frames,
and free-plus-page-table frames equal to the known pool total.

## 17. Hosted implementation

The hosted branch remains in
`runtime/memory/guidexos_virtual_memory_region.cpp`. Windows uses
`VirtualAlloc`/`VirtualFree`/`VirtualProtect`; POSIX uses `mmap`/`mprotect`/
`madvise`/`munmap`. It preserves the same generic lifecycle but delegates
physical and TLB behavior to the host. Hosted expected-fault execution remains
intentionally blocked in the normal process smoke.

## 18. NativeAOT adapter boundary

`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_virtual_memory_adapter.*`
wraps one `VirtualMemoryRegion`. The adapter exposes `trueReservationSemantics`
and `backendModeName`, and a caller can require true reservation semantics at
reserve time. The inactive probe uses the real adapter and passes in hosted
mode. It does not initialize Workstation GC or create a managed heap.

## 19. Validation

Validation run on 2026-07-16:

| Check | Result |
| --- | --- |
| Hosted generic VM smoke | PASS |
| Hosted adapter probe / true-mode contract | PASS |
| UEFI bootloader rebuild with frame-pool fields | PASS |
| Experimental AMD64 kernel build | PASS |
| QEMU baseline boot | PASS in the default run |
| QEMU true VM smoke / `ALL_PASS` | PASS |
| Frame allocation/release | PASS |
| Metadata and virtual-range exhaustion | PASS |
| Protection and expected-fault cases | PASS |
| Rollback and teardown | PASS |
| TLB and leak checks | PASS |
| Hosted thread lifecycle / scheduler-event regressions | PASS |
| Bare-metal native-thread QEMU lifecycle | PASS |
| Generic ELF smoke | PASS |
| Managed static artifact and bounded-allocation proofs | PASS |
| Managed live execution proof | BLOCKED by pre-existing `RhpReversePInvokeAttachOrTrapThread2` map symbol |

The reproducible entry points are
`scripts/smoke-native-virtual-memory.ps1` and
`scripts/smoke-native-virtual-memory-qemu.ps1`. The QEMU script now reports
kernel build, QEMU discovery, native `ALL_PASS`, allocation/release,
capacity/exhaustion, protection faults, rollback, teardown, TLB, leaks, and
direct read/write as separate fields.

## 20. Limitations and decision

This completes the requested VM substrate, not full NativeAOT readiness. The
implementation has no SMP shootdown, process isolation beyond one explicit
owner, swapping, file-backed/shared mappings, COW, NUMA, large pages, write
watch, execute permissions, or generic critical-section integration. Existing
managed proof and broader scheduler/thread/ELF regression suites are separate
from this focused VM validation and must remain gated before GC initialization.

Decision for this VM pass: **Outcome A — true bare-metal reserve/commit
lifecycle complete**. The exact next experiment is the generic critical
section/mutex primitive needed by the inactive Workstation GC boundary; do not
initialize GC yet.

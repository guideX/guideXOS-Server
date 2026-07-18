# Native Address-Space Reservation Design

This note records the generic address-space layer behind the true bare-metal
virtual-memory backend. It is intentionally independent of NativeAOT and GC.

## Ownership hierarchy

```text
BootInfo frame pool
  └─ current AddressSpace
       ├─ PageTable frames (persistent page-table structure)
       └─ VirtualMemoryRegion
            ├─ reservation interval metadata
            └─ VmRegion frames + PTE mappings
```

`kernel/core/address_space.cpp` owns frame state and raw AMD64 page-table
operations. `runtime/memory/guidexos_virtual_memory_region_baremetal.cpp`
owns interval state and calls the address-space layer for every backing or
mapping transition.

## State transitions

| Operation | Virtual interval | VM data frame | PTE | Release accounting |
| --- | --- | --- | --- | --- |
| reserve | owned | unchanged | no present entry | none |
| commit | owned | allocate + zero | install present mapping | none |
| protect | owned | unchanged | change flags | none |
| decommit | owned | zero + release | clear entry | `Decommit` |
| release | clear | zero + release | clear all entries | `Release` |
| teardown | clear | release all | clear all | `Release` |

No operation silently converts a reservation into eager backing.

## Boot contract

The UEFI loader adds `RuntimeFramePoolBase` and `RuntimeFramePoolPages` to the
packed `BootInfo`, allocates 512 pages before the final UEFI memory map, zeros
the allocation, and includes it in identity mappings. Kernel initialization
rejects a missing, unaligned, or oversized pool rather than falling back to a
second allocator.

## Range policy

The first implementation reserves from `0x100000000` through a 64 MiB bounded
range using first-fit page candidates. Preferred bases are exact. There are 32
metadata records and each region is limited to 4 MiB. These finite limits make
metadata and virtual-range exhaustion deterministic and observable.

## Page-table policy

The AMD64 walker uses the existing CR3 root and four 512-entry levels. Missing
intermediate tables are allocated from the same pool, zeroed before use, and
counted separately as page-table frames. Large-page entries are rejected by
the 4 KiB path. The current implementation does not reclaim empty
intermediate page-table pages; they remain address-space-owned until a future
page-table compaction policy exists.

The frame ledger also records whether a VM data frame currently has a leaf
mapping. `mapPage` rejects non-VM frames, duplicate mappings, and frames already
marked mapped; `releaseFrame` rejects a mapped frame. `unmapPage` clears that
ledger bit and decrements the address-space `mappingCount`, which is the
cross-check used by the VM-region statistics and QEMU leak assertions.

## Protection policy

The first true backend supports NoAccess, ReadOnly, and ReadWrite. All pages
are NX. NoAccess keeps the physical identity in a non-present PTE while the
region retains ownership. CR0.WP makes supervisor writes respect ReadOnly.
Executable protections are rejected explicitly.

## Fault policy

Ordinary unexpected faults remain fatal. The test-only callback is installed
only around one deliberately faulting instruction and may resume only after
validating the fault. No demand paging, lazy commit, or GC-specific fault
behavior exists.

## Teardown policy

The current owner is invalidated only after all active regions have been
unmapped and their frames returned. Handles retain stale private pointers but
generation/owner checks reject later operations safely. The QEMU test covers
fully committed, partially committed, and reservation-only regions.

## Evidence

The focused QEMU test passes reservation without a VM-frame delta, commit
frame allocation and zeroing, partial commit/decommit, recommit zeroing,
protection faults, deterministic rollback, metadata/range exhaustion, release
and range reuse, page-table agreement, TLB invalidation, and teardown leak
checks. The hosted adapter probe, managed single-allocation proof, bounded
repeated-allocation/OOM proof, native-thread QEMU lifecycle, event/scheduler
regressions, and generic ELF smoke also pass. GC remains inactive.

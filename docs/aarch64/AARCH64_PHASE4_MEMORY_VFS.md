# AARCH64-4 Common Kernel Memory, Boot Resources, VFS, and Runtime Proof

## Result

AARCH64-4 is implemented on `AARCH64_SUPPORT`.  The Phase-4 image reaches a
common kernel entry after `ExitBootServices`, consumes a loader-supplied FAT32
ramdisk, uses the existing common FAT/VFS stack, allocates scheduled task
stacks through the common physical allocator, and completes a bounded
preemption plus VFS durability proof:

```text
AARCH64_PHASE4_PASS
```

The target remains QEMU `virt,gic-version=2,acpi=off` with `cortex-a53` and
`512M` of RAM.  No virtio storage device is used.  GICv3, SMP, userspace,
demand paging, and real hardware storage remain outside this phase.

## Boot contract

`aarch64/phase4/phase4_contract.h` defines the versioned
`gxos_aarch64_phase4_handoff`.  The Phase-4 loader reuses the validated Phase-2
ELF/DTB/EBS path, then opens `ramdisk.img` from the ESP and reads the complete
image into an `EfiLoaderData` buffer before `ExitBootServices`.  The buffer is
not freed and its base/size are carried in the handoff with
`GXOS_AARCH64_PHASE4_FLAG_RAMDISK_VALID`.

The kernel validates the handoff before using any resource.  It checks the
magic, version, exact structure size, required flags, kernel and bootstrap
stack bounds, memory-map layout, DTB bounds, and ramdisk bounds.  It then
constructs `kernel::boot::CommonBootInfo`, which is architecture-neutral apart
from the explicit ARM64 architecture value and carries:

* the raw UEFI memory map and descriptor geometry;
* kernel, bootstrap stack, handoff, DTB, and ramdisk ranges;
* DTB-discovered usable RAM ranges;
* UART and GIC MMIO ranges.

`common_boot_info.cpp` provides overflow-safe validation.  The Phase-4 host
controls cover malformed descriptor geometry and malformed optional ranges.

## Physical memory and heap

`common_physical_allocator.cpp` is a bounded production allocator over
4-KiB pages.  It consumes `EfiConventionalMemory` descriptors, clips them to
DTB-discovered RAM, subtracts the kernel, bootstrap stack, handoff, memory map,
ramdisk, DTB, and MMIO ranges, and supports aligned allocation, arbitrary
release, reuse, double-free rejection, and bounded exhaustion behavior.

Allocator metadata is static kernel state and therefore lies inside the kernel
reservation.  The Phase-4 scheduler stack callback requests 16 pages from
this allocator for every task and rejects null or misaligned results.  The
proof reports the allocator before and after task creation; the verified boot
values were 92,880 total pages, 92,800 free after five scheduled stacks, and
80 pages used.

The common kernel heap remains the existing free-list allocator in
`cxx_runtime.cpp`.  Phase 4 adds aligned allocation/free entry points without
changing the existing C++ new/delete ABI.  The boot proof writes and verifies
three differently aligned buffers, performs 1,000 allocate/write/free cycles,
and reports `heap-alloc-cycles=1000`.

## Ramdisk, FAT, and VFS

`scripts/stage-aarch64-phase4-ramdisk.ps1` copies the repository FAT32 image
to the Phase-4 ESP and stages deterministic short-name entries:

```text
/PHASE4/HELLO.TXT
/PHASE4/NESTED/PROOF.TXT
```

The loader-supplied buffer is attached with `ramdisk::create_at`, so the common
block layer, existing FAT32 driver, and existing VFS are exercised without a
virtio or other storage path.  The RAM-disk block descriptors explicitly set a
null flush callback: memory-backed writes are synchronously durable, and the
block layer treats a missing flush callback as `BLOCK_OK`.

The kernel proof covers:

* root mount and visibility of the existing application directory;
* `stat`, bounded reads, EOF behavior, and a persistent open handle;
* nested path traversal and directory enumeration;
* writable FAT/VFS semantics by creating `/phase4/scratch.txt` and reading it
  back after the write/flush boundary.

The deterministic markers include `VFS directory enumeration: PASS`, `VFS file
read: PASS`, and `VFS write/read: PASS`.  The writable proof uses the existing
FAT path and exposed RAM-disk buffer; it does not require a new filesystem
implementation.

## IRQ, scheduler, and VFS durability

`kernel/core/irq_registry.cpp` provides a small architecture-neutral registry.
The ARM64 exception path acknowledges the existing GICv2 timer PPI, routes it
through the registry, rearms the physical timer, and passes the exception frame
to the common scheduler.  Phase 3's direct timer path remains unchanged when
the Phase-4 build macro is absent.

The Phase-4 proof first runs the existing three-task cooperative register and
stack-integrity workload.  It then enables three non-yielding register workers,
the filesystem worker, and the common completion task.  The filesystem worker
repeatedly seeks and reads the persistent fixture handle on every slice and
periodically enumerates `/phase4`; timer preemption remains active throughout.
The bounded completion target is 10,000 timer preemptions.

The common scheduler resets its round-robin cursor at each bounded phase, so a
fresh preemptive run does not inherit the cooperative phase's selection cursor.
This keeps the transition deterministic while leaving task ownership and
context handling in the common scheduler.

## Build and validation

Build and test scripts:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase4.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase4.ps1
```

The test script verifies PE/COFF and ELF machine types, runs the host memory
controls, stages the deterministic image, copies a fresh UEFI variable store
for each run, and performs three fresh QEMU boots.  The default timeout is
120 seconds because the proof intentionally leaves QEMU in a bounded-marker
plus `WFI` state and ARM64 emulation speed varies with host load.

The completed three-boot result was:

| Boot | Pages used | Heap bytes | Context switches | Preemptions | VFS reads | VFS enumerations | Unexpected IRQs | Exceptions |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | 80 | 32 | 20,006 | 10,000 | 1,240,912 | 77,557 | 0 | 0 |
| 2 | 80 | 32 | 20,006 | 10,000 | 299,184 | 18,698 | 0 | 0 |
| 3 | 80 | 32 | 20,006 | 10,000 | 300,252 | 18,765 | 0 | 0 |

The Phase-3 three-fresh-boot regression suite was rerun after the Phase-4
changes and passed.  Phase 1 and Phase 2 regression suites remain the
repository gates for the earlier loader and foundation contracts; no Phase-4
path changes their build definitions or handoff versions.

## Changed surface

The Phase-4 build is deliberately isolated behind `GXOS_AARCH64_PHASE4` and
adds the following reusable pieces:

* versioned common boot-info validation;
* common physical page allocation and release;
* aligned common heap entry points;
* generic IRQ registration and dispatch;
* ARM64 Phase-4 serial, main, and loader ramdisk integration;
* build, stage, run, host-control, and three-boot test scripts.

Existing Phase-1/2/3 targets keep their original macros and source sets.  The
AMD64 production path, hosted scheduler, and existing filesystem interfaces
were not replaced.

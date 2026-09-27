# Disk Manager: Current Implementation Status

This describes the current implementation after DM5. Historical phase details are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md), [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md), [DM3 initialization report](DISK_MANAGER_PHASE_DM3_INITIALIZE_DISK.md), [DM4 boot provenance and transport safety report](DISK_MANAGER_PHASE_DM4_BOOT_PROVENANCE.md), and [DM5 UI and diagnostics report](DISK_MANAGER_PHASE_DM5_UI_AND_DIAGNOSTICS.md).

## Current behavior

The bare-metal Disk Manager enumerates registered block devices without adding a synthetic disk, parses normalized MBR/GPT state, and presents up to 128 validated partition entries plus bounded unallocated regions. The resizable view has a scrollable device pane, partition/unallocated list, proportional map, selected-object Properties, and Storage diagnostics. Disk, GPT/MBR partition, and gap selection follow stable identity across refresh. It offers **Initialize Disk...** only for raw eligible disks, defaults to GPT, and retains MBR as a compatibility choice. The DM2–DM4 storage safety services remain authoritative.

The hosted Windows viewer labels attached `.img` files as images and does not enumerate host physical disks. It previews validated 512-byte MBR ranges only; it does not parse GPT or classify remaining image space as unallocated. Image-backed RAM disks are read-only and volatile. Its current window remains fixed at 920×560. Mount paths are reported at device level because VFS does not associate a mount with a partition identity.

Initialization creates an empty partition table only. It does not create or delete partitions, format, repair, resize, wipe, convert, or mount a device.

## Initialization safety

The storage service keeps one exclusive operation lease from preflight and metadata snapshot through confirmation, identity revalidation, writes, flushes, parser verification, rescan, and cleanup. It pins the exact target registration so ordinary unregister cannot remove or rebind the device during the operation. Forced removal marks the registration offline; an inaccessible tombstone prevents slot reuse until the pin is released. Registry registration IDs are monotonic and non-reused, and generation saturates instead of wrapping.

The common preflight still requires current target identity and registry generation, valid geometry, read/write capability, trusted persistence, successful required flush, unmounted/non-root state, definitely non-boot provenance, a confidently raw partition state, and the exclusive lease. It verifies the final state by reading the written sectors and using the normal parser. Failure attempts bounded restoration where possible and reports uncertainty. No force-initialize option exists.

## Boot provenance and transport capability

UEFI BootInfo v3 carries a bounded copy of the firmware loader source's Device Path plus resolved PCI segment/BDF where available. Legacy v2 or invalid metadata becomes `Unknown`. The kernel can match exact PCI plus NVMe namespace or supported legacy ATAPI channel/target. A match protects the entire parent disk, including when firmware names one partition. Different authoritative endpoints may be classified `DefinitelyNotBoot`; missing or unsupported identity stays `Unknown` and fails closed.

ATA PIO can satisfy boot provenance for supported legacy IDE/ATAPI paths and may pass the existing initialization preflight when valid IDENTIFY flush support is present and all other safety gates pass. Standard-port fallback and unsupported SATA identity remain `Unknown`. AHCI has no shared block registration. NVMe has BDF/namespace provenance but no trustworthy shared-queue Flush implementation, so durability is unknown and initialization remains blocked. USB mass storage has no shared block registration or SYNCHRONIZE CACHE path. RAM and image-backed RAM disks are volatile and cannot satisfy durable initialization. No physical-device initialization was performed.

## Persistence and geometry

Flush results distinguish successful supported flush, synchronous-durable completion, unsupported/unknown semantics, and failure. Missing callbacks are not treated as durable. ATA uses E7h / EAh only when valid IDENTIFY word 83 bits advertise the corresponding command, then polls readiness and reports completion errors/timeouts. NVMe is blocked until a namespace-aware Flush path can safely use controller-specific queues and check completion.

The parser and initializer validate geometry and checked ranges and support 512-byte and 4096-byte logical-sector test devices. This does not imply every transport supports 4Kn initialization. Existing filesystem drivers may have narrower sector-size support.

## Verification and build status

The deterministic storage-manager/parser/initializer and Disk Manager model suite passed **188 checks, 0 failures**. It covers MBR/GPT layout and CRCs, both supported test sector sizes, bounded free-space computation, GPT usable-range and degraded-copy behavior, contextual action availability, boot provenance and legacy fallback, stable selection and slot reuse, registry pins, removal during flush/read-back, RAM-disk lifetime, durability disagreement, and fake-port ATA flush command/readiness/error/timeout behavior. The tests use only in-memory storage.

`git diff --check` passed. The normal `cmd /c build.bat` stopped before C++ compilation because `third_party/mbedtls` is missing; dependency policy was not changed. The available MinGW compiler cannot provide a valid bare-metal UI compile because it targets Windows and collides with the kernel headers; MSVC `cl` and QEMU are unavailable. No visual/runtime smoke test was performed.

The hosted `disk_manager.cpp` translation unit did pass MinGW C++17 syntax-only validation; its only warning was the existing misleading-indentation warning in `gui_protocol.h:96`.

## Current outcome and next work

DM5 completes the authoritative read-only UI and diagnostics groundwork. The remaining VFS partition-mount identity limitation is documented, and hosted image viewing remains deliberately limited. Recommended DM6 work is a new Create Partition operation built on the storage lease, exact target identity, validated unallocated extents, alignment and table-specific bounds, durable write/read-back verification, rollback, and fake-media tests. No such operation is implemented in DM5. See the [DM5 report](DISK_MANAGER_PHASE_DM5_UI_AND_DIAGNOSTICS.md) for the UI behavior and build/runtime limits.

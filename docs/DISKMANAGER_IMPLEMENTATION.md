# Disk Manager: Current Implementation Status

This describes the current implementation after DM4. Historical phase details are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md), [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md), [DM3 initialization report](DISK_MANAGER_PHASE_DM3_INITIALIZE_DISK.md), and [DM4 boot provenance and transport safety report](DISK_MANAGER_PHASE_DM4_BOOT_PROVENANCE.md).

## Current behavior

The bare-metal Disk Manager enumerates registered block devices, parses normalized MBR/GPT state, and offers **Initialize Disk...** for eligible raw disks. It defaults to GPT and retains MBR as a compatibility choice. The UI displays the selected device's capacity, sector size, read/write and durability summary, registry identity details, boot provenance, mount/root state, model, serial, and initialization rejection reason where available.

The hosted Windows viewer inspects attached `.img` files and does not enumerate host physical disks. The test suite registers only in-memory fake disks. Image-backed RAM disks are read-only and volatile.

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

The deterministic storage-manager/parser/initializer suite passed **155 checks, 0 failures**. It covers MBR/GPT layout and CRCs, both supported test sector sizes, boot provenance and legacy fallback, registry pins and slot reuse, removal during flush/read-back, RAM-disk lifetime, durability disagreement, and fake-port ATA flush command/readiness/error/timeout behavior. The tests use only in-memory storage.

Targeted C++14 syntax-only compilation passed for the block/storage/parser/initializer/RAM-disk/ATA/NVMe sources and the UEFI loader/GUID definitions. BootInfo static checks passed. The bare-metal `kernel_apps.cpp` change was not validated by the available host compiler: MinGW's Windows headers and target configuration collide with the bare-metal types, and MSVC `cl` is unavailable. The normal build stopped at `verify-mbedtls-profile.ps1` because `third_party/mbedtls` is missing; dependency policy was not changed. QEMU is not installed, so no disposable-image proof was run.

## Current outcome and next work

DM4 completes the boot-provenance, registry-coordination, and persistence-testing foundation. ATA PIO can become eligible through the existing common safety engine under supported legacy-path and valid-flush conditions; this is not a claim of physical-hardware validation. Recommended next work is AHCI registration and flush, NVMe per-controller queues plus checked Flush, USB shared registration plus SYNCHRONIZE CACHE, transport detach integration, and a two-disk QEMU or disposable-hardware proof. See the [DM4 report](DISK_MANAGER_PHASE_DM4_BOOT_PROVENANCE.md) for the full matrix and exact blockers.

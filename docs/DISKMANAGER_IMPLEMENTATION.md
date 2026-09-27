# Disk Manager: Current Implementation Status

This describes the current implementation after DM6. Historical phase details are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md), [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md), [DM3 initialization report](DISK_MANAGER_PHASE_DM3_INITIALIZE_DISK.md), [DM4 boot provenance and transport safety report](DISK_MANAGER_PHASE_DM4_BOOT_PROVENANCE.md), [DM5 UI and diagnostics report](DISK_MANAGER_PHASE_DM5_UI_AND_DIAGNOSTICS.md), and [DM6 safe partition-creation report](DISK_MANAGER_PHASE_DM6_CREATE_PARTITION.md).

## Current behavior

The bare-metal Disk Manager enumerates registered block devices without adding a synthetic disk, parses normalized MBR/GPT state, and presents up to 128 validated partition entries plus bounded unallocated regions. The resizable view has a scrollable device pane, partition/unallocated list, proportional map, selected-object Properties, and Storage diagnostics. Disk, GPT/MBR partition, and gap selection follow stable identity across refresh. It offers **Initialize Disk...** only for raw eligible disks, defaults to GPT, and retains MBR as a compatibility choice. On a validated eligible unallocated region it exposes contextual **Create Partition...**. The create flow supports healthy GPT Basic Data and supported MBR FAT32-LBA primary partitions, integer-MiB or maximum sizing, and a bounded GPT name. It requires a separate confirmation, rescans through the normal parser, and selects the new partition by identity. Newly created partitions remain unformatted and unmounted.

The hosted Windows viewer labels attached `.img` files as images and does not enumerate host physical disks. It previews validated 512-byte MBR ranges only; it does not parse GPT or classify remaining image space as unallocated. Image-backed RAM disks are read-only and volatile. Its current window remains fixed at 920×560. Mount paths are reported at device level because VFS does not associate a mount with a partition identity.

Initialization creates an empty partition table only. DM6 partition creation writes only the new partition-table entry and required GPT checksums. Neither operation formats, repairs, resizes, wipes, converts, or mounts a device. Hosted `.img` viewing remains read-only.

## Destructive operation safety

Initialization and partition creation use the same exclusive storage-operation lease and DM4 target pinning. Each service keeps the lease through identity/generation revalidation, metadata snapshot, writes, flush, normal-parser verification, rescan, and cleanup. Partition creation reparses the table under the lease and matches the exact selected gap against freshly recomputed unallocated regions before it calculates any write range. Ordinary unregister cannot remove or rebind the pinned target. Forced removal marks the registration offline; an inaccessible tombstone prevents slot reuse until the pin is released. Registry registration IDs are monotonic and non-reused, and generation saturates instead of wrapping.

Both write paths require current target identity and registry generation, valid geometry, read/write capability, trusted persistence, successful required flush, unmounted/non-root state, definitely non-boot provenance, a supported parser state, and the exclusive lease. Initialization only accepts confidently raw metadata; creation only accepts a validated unallocated extent on healthy GPT or supported primary MBR. Both verify written sectors through the normal parser. Creation updates both GPT copies or only the intended MBR entry, preserves unrelated metadata, and attempts bounded restoration on write/flush/verification failure. Unverifiable restoration reports uncertainty. No force-initialize or force-create option exists.

## Boot provenance and transport capability

UEFI BootInfo v3 carries a bounded copy of the firmware loader source's Device Path plus resolved PCI segment/BDF where available. Legacy v2 or invalid metadata becomes `Unknown`. The kernel can match exact PCI plus NVMe namespace or supported legacy ATAPI channel/target. A match protects the entire parent disk, including when firmware names one partition. Different authoritative endpoints may be classified `DefinitelyNotBoot`; missing or unsupported identity stays `Unknown` and fails closed.

ATA PIO can satisfy boot provenance for supported legacy IDE/ATAPI paths and may pass the existing initialization preflight when valid IDENTIFY flush support is present and all other safety gates pass. Standard-port fallback and unsupported SATA identity remain `Unknown`. AHCI has no shared block registration. NVMe has BDF/namespace provenance but no trustworthy shared-queue Flush implementation, so durability is unknown and initialization remains blocked. USB mass storage has no shared block registration or SYNCHRONIZE CACHE path. RAM and image-backed RAM disks are volatile and cannot satisfy durable initialization. No physical-device initialization was performed.

## Persistence and geometry

Flush results distinguish successful supported flush, synchronous-durable completion, unsupported/unknown semantics, and failure. Missing callbacks are not treated as durable. ATA uses E7h / EAh only when valid IDENTIFY word 83 bits advertise the corresponding command, then polls readiness and reports completion errors/timeouts. NVMe is blocked until a namespace-aware Flush path can safely use controller-specific queues and check completion.

The parser and initializer validate geometry and checked ranges and support 512-byte and 4096-byte logical-sector test devices. This does not imply every transport supports 4Kn initialization. Existing filesystem drivers may have narrower sector-size support.

## Verification and build status

The deterministic storage-manager/parser/initializer/partition-creation and Disk Manager model suite passed **234 checks, 0 failures**. It covers MBR/GPT layout and CRCs, 512-byte and 4096-byte fake sectors, bounded free-space computation, degraded/conflicting GPT rejection, full and partial gap creation, sequential creation, contextual action availability, safe GUID/name handling including collision rejection, byte-level GPT/MBR preservation checks, write/flush/verification/rollback failures, boot provenance, stable identity and registry pins, device removal, durability gates, and fake-port ATA flush behavior. The tests use only in-memory or sparse fake storage.

`git diff --check` passed. MinGW C++14 freestanding syntax-only checks passed for the new partition service, bare-metal `kernel_apps.cpp`, and `kernel_compositor.cpp` with `GXOS_BARE_METAL`. The hosted `disk_manager.cpp` C++17 syntax check passed with the existing `gui_protocol.h:96` indentation warning. The normal `cmd /c build.bat` stopped before C++ compilation because `third_party/mbedtls` is missing; dependency policy was not changed. MSVC `cl` and QEMU are unavailable. The installed `i686-elf-g++` lacks C++ standard headers needed by the UI translation unit. No visual/runtime smoke test or physical-disk write was performed.

## Current outcome and next work

DM6 completes safe GPT and supported primary MBR partition creation on validated unallocated regions. The remaining VFS partition-mount identity limitation is documented, and hosted image viewing remains deliberately limited. Recommended DM7 work is a separate, safety-reviewed formatter for an already-created unformatted partition; it should retain exact target/partition identity checks, geometry limits, durable write/read-back verification, rollback and uncertain-state reporting, and must not auto-mount. See the [DM6 report](DISK_MANAGER_PHASE_DM6_CREATE_PARTITION.md) for implementation and validation details.

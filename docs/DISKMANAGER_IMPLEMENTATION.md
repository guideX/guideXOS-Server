# Disk Manager: Current Implementation Status

This describes the current implementation after DM7. Historical phase details are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md), [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md), [DM3 initialization report](DISK_MANAGER_PHASE_DM3_INITIALIZE_DISK.md), [DM4 boot provenance and transport safety report](DISK_MANAGER_PHASE_DM4_BOOT_PROVENANCE.md), [DM5 UI and diagnostics report](DISK_MANAGER_PHASE_DM5_UI_AND_DIAGNOSTICS.md), [DM6 safe partition-creation report](DISK_MANAGER_PHASE_DM6_CREATE_PARTITION.md), and [DM7 safe FAT32-formatting report](DISK_MANAGER_PHASE_DM7_FAT32_FORMAT.md).

## Current behavior

The bare-metal Disk Manager enumerates registered block devices without adding a synthetic disk, parses normalized MBR/GPT state, and presents up to 128 validated partition entries plus bounded unallocated regions. The resizable view has a scrollable device pane, partition/unallocated list, proportional map, selected-object Properties, and Storage diagnostics. Disk, GPT/MBR partition, and gap selection follow stable identity across refresh. It offers **Initialize Disk...** only for raw eligible disks, defaults to GPT, and retains MBR as a compatibility choice. On a validated eligible unallocated region it exposes contextual **Create Partition...** for healthy GPT Basic Data or supported primary MBR FAT32-LBA partitions. The create flow supports integer-MiB or maximum sizing, a bounded GPT name, separate confirmation, and identity-based rescan. New partitions remain unformatted and unmounted.

For an eligible **Unformatted** or **Unknown filesystem** partition on a safe supported disk, the selected-partition action becomes **Format...**. DM7 accepts only 512-byte logical sectors and a fully zero-filled partition. The dialog identifies the disk and partition, capacity, existing state, automatic FAT32 cluster size, and optional volume label. It requires explicit Format confirmation. On success Disk Manager rescans and selects the same partition by identity; Properties shows cached FAT32 metadata where available. Used/free space stays unknown. DM7 never mounts the formatted partition or opens File Manager.

The hosted Windows viewer labels attached `.img` files as images and does not enumerate host physical disks. It previews validated 512-byte MBR ranges only; it does not parse GPT or classify remaining image space as unallocated. Image-backed RAM disks are read-only and volatile. Its current window remains fixed at 920×560. Mount paths are reported at device level because VFS does not associate a mount with a partition identity.

Initialization creates an empty partition table. DM6 partition creation writes only the selected table entry and required GPT checksums. DM7 writes FAT32 filesystem metadata only inside the selected partition; it does not modify the partition table or wipe all data clusters. No operation repairs, resizes, moves, deletes, converts, or automatically mounts a partition. Hosted `.img` viewing remains read-only.

## Destructive operation safety

Initialization, partition creation, and FAT32 formatting use the shared exclusive storage-operation lease and target pinning. Each service keeps the lease through identity and registry-generation revalidation, metadata snapshot, writes, flush, read-back verification, rescan, and cleanup. Target registrations cannot be rebound while pinned. Forced removal marks the registration offline and prevents slot reuse until the pin is released. Registration IDs are monotonic and non-reused, and generation saturates instead of wrapping.

All three write paths require current target identity and registry generation, valid geometry, read/write capability, trusted persistence, successful required flush, unmounted/non-root state, definitely non-boot provenance, and the exclusive lease. Initialization accepts confidently raw metadata. Creation recomputes unallocated regions and derives a bounded partition entry. Formatting reparses the current table, matches the exact selected GPT GUID or MBR identity tuple, verifies its range and non-overlap, rejects mounted/root/boot-protected targets, and scans the entire partition. Only all-zero media is eligible; recognized filesystems, unreadable media, and unknown nonzero data fail closed. There is no force-format option.

Initialization, creation, and formatting attempt bounded restoration after write, flush, or verification failure. Formatting snapshots the affected partition prefix up to its two FATs and root cluster, capped at 1 MiB. Unverifiable restoration reports uncertain state. No physical-device operation was performed.

## FAT compatibility and geometry

The existing FAT driver recognizes FAT16, FAT32, and exFAT. Its FAT32 BPB parser accepts 512–4096-byte device sectors, but VFS detection is 512-byte-only, so DM7 formats only 512-byte sectors. The driver uses the BPB root cluster, FAT count, FAT size, and volume label. It does not read FSInfo or backup boot metadata. FAT writes mirror to all copies. DM7 writes matching FAT copies, FSInfo at relative sector 1, backup boot sector 6, backup FSInfo sector 7, and publishes the primary boot sector last.

FAT32 formatting requires at least 65,525 clusters and allows at most 120,000. Sectors per cluster are selected deterministically from powers of two between 1 and 64, using the first value that keeps the cluster count in range. BPB total sectors must fit 32 bits. These bounds keep FAT metadata within rollback memory; 4Kn and larger volumes return explicit blockers. Volume labels are padded to 11 FAT characters, may be blank, and remain independent of GPT partition names. Production volume IDs come from VirtIO RNG and must be nonzero; only storage tests can inject deterministic IDs.

## Boot provenance and transport capability

UEFI BootInfo v3 carries a bounded copy of the firmware loader source's Device Path plus resolved PCI segment/BDF where available. Legacy v2 or invalid metadata becomes `Unknown`. The kernel can match exact PCI plus NVMe namespace or supported legacy ATAPI channel/target. A match protects the entire parent disk, including when firmware names one partition. Different authoritative endpoints may be classified `DefinitelyNotBoot`; missing or unsupported identity stays `Unknown` and fails closed.

ATA PIO can satisfy boot provenance for supported legacy IDE/ATAPI paths and may pass destructive preflight when valid IDENTIFY flush support is present and all other safety gates pass. Standard-port fallback and unsupported SATA identity remain `Unknown`. AHCI has no shared block registration. NVMe has BDF/namespace provenance but no trustworthy shared-queue Flush implementation, so durability is unknown and writes remain blocked. USB mass storage has no shared block registration or SYNCHRONIZE CACHE path. RAM and image-backed RAM disks are volatile and cannot satisfy durable destructive-operation policy.

## Persistence and geometry

Flush results distinguish supported successful flush, synchronous durable completion, unsupported or unknown semantics, and failure. Missing callbacks are not treated as durable. ATA uses E7h / EAh only when valid IDENTIFY word 83 bits advertise the corresponding command, then polls readiness and reports completion errors or timeouts. NVMe remains blocked until a namespace-aware Flush path safely uses controller-specific queues and checks completion.

The partition parser, initializer, and partition creator validate checked geometry for 512-byte and 4096-byte logical-sector devices. That does not imply every transport or filesystem supports 4Kn. DM7's FAT32 formatter is 512-byte-only to match the current VFS path.

## Verification and build status

The deterministic storage-manager, parser, initializer, partition-operation, FAT32 formatter, and Disk Manager model suite passed **276 checks, 0 failures**. It covers MBR/GPT layout and CRCs, 512-byte and 4096-byte fake sectors, bounded free-space computation, degraded/conflicting GPT rejection, partition creation and preservation, formatter geometry and labels, clean/recognized/ambiguous/unreadable media, FAT32 BPB/FSInfo/backup/FAT/root structures, GPT/MBR and adjacent-range preservation, write/flush/verification/rescan/rollback failures, target and registry changes, boot/root/mount/durability gates, driver BPB recognition, and unmounted post-format state. Tests use in-memory fake storage only.

`git diff --check` passed. MinGW C++14 freestanding syntax-only validation of bare-metal `kernel_apps.cpp` with `GXOS_BARE_METAL` passed; reported UI warnings are unrelated existing warnings. The storage tests passed with one unused-helper warning from `fs_fat.cpp`. `cmd /c build.bat` stopped before C++ compilation because `third_party/mbedtls` is missing. MSVC `cl` and QEMU are unavailable. No runtime/QEMU or physical-device write was performed.

## Current outcome and next work

DM7 completes deterministic, rollback-bounded FAT32 formatting on eligible existing partitions and leaves them unmounted. The test suite uses the production FAT32 BPB parser through a bounded read-only adapter, but the VFS still lacks general partition-aware mounting, particularly for GPT volumes. DM8 should establish partition-to-mount identity and add a reviewed mount workflow plus directory and file I/O validation. See the [DM7 report](DISK_MANAGER_PHASE_DM7_FAT32_FORMAT.md) for exact geometry, write ordering, and remaining limitations.

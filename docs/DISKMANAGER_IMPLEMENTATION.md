# Disk Manager: Current Implementation Status

This describes the state after DM2. Historical findings and phase planning are in the [DM1 audit](DISK_MANAGER_PHASE_DM1_AUDIT.md) and [DM2 safety foundation report](DISK_MANAGER_PHASE_DM2_SAFETY_FOUNDATION.md).

## Current behavior

There are two Disk Manager presentations:

- The bare-metal app in `kernel/core/kernel_apps.cpp` enumerates the block registry, captures target identities, and displays a bounded view of the normalized read-only MBR/GPT parser. It scans registry slots independently of device count, preserves selection by identity across refresh, probes filesystems only from validated partitions, and exposes refresh only.
- The hosted Windows path in `disk_manager.cpp` does not enumerate physical host disks. It displays attached `.img` files for inspection using its legacy four-slot MBR viewer. The image view is read-only.

Neither presentation initializes, partitions, repairs, formats, wipes, or securely erases a device. Suggested mount points and active MBR flags do not imply mounted or boot state.

## Shared storage foundation

The block layer now validates sector geometry and LBA ranges, offers buffer-length-aware I/O, applies declared alignment and transfer limits, reports explicit flush outcomes, and advances a registry generation when topology changes. The storage manager reports read/write/flush/removable capabilities and capacity, captures/revalidates bounded target identities, checks VFS mounts/root backing, and returns structured preflight issues. Its large parser scratch is static BSS storage to fit the 16 KiB boot stack; callers must serialize preflight until DM3 adds an operation lock.

The partition parser supports bounded MBR and GPT reads, primary/backup GPT CRC validation, degraded-state reporting, overlap/range checks, and conservative raw classification. It supports 512- through 4096-byte logical sectors for partition parsing. The bare-metal UI displays up to 16 partitions while the parser retains at most 128.

ATA PIO advertises cache-flush support only when valid IDENTIFY word 83 data says the command is supported. NVMe writes have no implemented Flush command and durability remains unknown. AHCI and USB mass storage are not active registrations in the shared block layer. RAM disks report volatile persistence; their synchronous no-cache flush does not claim durable storage. Attached image RAM disks are read-only and own their transferred backing buffer.

The VFS mount table identifies a backing block device but not a partition identity or offset, so mount protection covers the whole device. The filesystem mount paths remain 512-byte-only and reject other logical sector sizes even though the partition parser supports 4Kn.

## Verification and remaining work

The deterministic in-memory storage suite passed 58 checks with no failures. C++14 syntax checks passed for the affected hosted and bare-metal Disk Manager, storage, parser, VFS, filesystem, and driver translation units, with two existing ext4/UFS directory-name warnings. The complete build remains blocked because `third_party/mbedtls` is absent; the tracked setup path is `scripts/bootstrap-mbedtls.ps1`. No dependency bootstrap was performed for DM2.

The firmware boot device is not authoritatively identified when it has no active root mount. Target revalidation also has no registry lock/operation lease spanning a later write. These, unknown NVMe durability, inactive AHCI/USB paths, and 512-byte-only filesystem mounts remain DM3 blockers. Disk Manager writes remain disabled.

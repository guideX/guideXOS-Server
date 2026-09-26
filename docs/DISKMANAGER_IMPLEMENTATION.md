# Disk Manager: Current Implementation Status

This document describes the implementation as audited on 2026-09-26. It supersedes historical completion claims in this file. A UI control, data structure, probe, or method name does not prove that a storage operation is implemented or safe.

## Current behavior

There are two distinct Disk Manager implementations:

- The hosted desktop process in `disk_manager.cpp` draws a disk list, volume rows, an MBR-derived partition map, mount suggestions, and image controls. Its Windows refresh path creates a synthetic disk; it does not enumerate physical Windows disks. The hosted image library reads `.img` files for inspection and does not write them.
- The bare-metal kernel app in `kernel/core/kernel_apps.cpp` enumerates the shared block-device registry, reads sector zero, displays up to four MBR primary entries, and probes a few filesystem signatures. Its only storage UI action is refresh.

The shared block layer and drivers determine which devices the bare-metal app can see. Driver enums or UI labels do not imply that a transport is registered and operational. In particular, AHCI is not implemented by the legacy ATA initialization path, and USB mass-storage code is not registered with the shared block layer.

## Capability status

| Capability | Current status |
|---|---|
| Disk enumeration | Bare-metal app reads active block descriptors with a slot/count iteration issue when registry holes exist. Hosted Windows mode displays a synthetic descriptor. |
| Device selection | UI selection is by current list position/block slot, not stable physical identity. |
| Partition table | MBR sector signature and four primary entries only; structure and bounds are not fully validated. Extended/logical partitions are not traversed. |
| GPT | No GPT header or entry parser was found. |
| Filesystem detection | Signature/probe paths exist for some FAT, exFAT, ext, and Tar-like data. Probing is not equivalent to mounting or health validation. |
| Mount state | The hosted UI suggests mount points and attempts a weak lookup; it does not reliably establish that a specific partition is mounted. Partition-aware VFS mount is a stub. |
| Image attachment | Hosted `.img` inspection is read-only. A non-Windows source branch can attach small images through a read-only RAM disk; that path is not the actual bare-metal Disk Manager app. |
| Partition creation | Disabled/no-op UI handler. No Disk Manager partition-table write is performed. |
| Formatting | Disabled/no-op UI handler. There is no in-repository FAT/exFAT mkfs implementation suitable for Disk Manager. |
| Delete/resize/initialize | Not implemented in Disk Manager. |
| Mount/unmount actions | Not implemented in Disk Manager. |

The kernel block API has read and write callbacks, but write support varies by driver. `flush()` treats a missing callback as success; NVMe has no flush callback. The current API and drivers do not establish a reliable durability guarantee for destructive workflows. Disk Manager itself does not write sectors.

## Important limitations

- Disk Manager reads fixed 512-byte buffers and assumes 512-byte logical sectors in MBR and filesystem probing. Several filesystem paths also use fixed-size buffers or sector arithmetic that is unsafe or incorrect for 4Kn devices.
- The shared descriptor lacks durable device identity, read-only/removable flags, controller path, and media-generation state. A block index is transient and must not be used alone to target future writes.
- `isSystem`, `Boot/System`, partition health, free-space gaps, and suggested mount paths are heuristic UI labels. They are not authoritative boot provenance, filesystem-health, or mount-state results.
- `vfs::mount_partition()` is a planning stub. Existing filesystem mounts do not provide a general partition-offset adapter and the mount table does not retain a partition identity/offset.
- FAT file-write paths exist for mounted FAT volumes, but that is not filesystem creation. No Disk Manager formatting capability is present.

## Verification and next work

The detailed architecture, transport and filesystem matrices, source references, test/build results, risks, operation design, and phase roadmap are in [Disk Manager Phase DM1 Audit](DISK_MANAGER_PHASE_DM1_AUDIT.md).

DM1 did not write to disks or images. A hosted syntax-only check and a bare-metal syntax-only check passed. The full build stopped before compilation because `third_party/mbedtls` is missing. No relevant storage test suite was found, and QEMU was unavailable in the environment. These checks do not prove hardware transport behavior.

The next phase should establish a safe block-operation foundation: geometry-correct bounded sector I/O, honest flush support, explicit device identity and writability, registry synchronization/removal rules, target revalidation, and deterministic failure reporting. Disk initialization and other writes should remain unavailable until those prerequisites and disposable-media tests are in place.

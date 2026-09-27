# Disk Manager DM7: Safe FAT32 Formatting

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `d7a61ddb3d701bc1d29cbb363074de33f6b1a6e0`
- The starting worktree was clean. The locally recorded branch was even with `origin/DISK_MANAGER_IMPROVEMENTS`; a fetch attempt failed with GitHub SSH public-key authentication, so that remote state was not freshly verified.
- DM1 through DM6 and `DISKMANAGER_IMPLEMENTATION.md` were reviewed before implementation.

## FAT implementation audit

The production implementation is `kernel/core/fs_fat.cpp` with layouts in `kernel/core/include/kernel/fs_fat.h`. It recognizes FAT16, FAT32, and exFAT; FAT12 is not a separate driver type. FAT32 detection requires the on-disk `FAT32   ` type text, a BPB bytes-per-sector value equal to the block-device logical sector size, a nonzero sectors-per-cluster value, a nonzero FAT count and FAT32 FAT size, and usable total/data geometry. The driver reads sectors through a 4096-byte static sector buffer. Its FAT32 parser accepts matching device/BPB sector sizes from 512 through 4096 bytes, but does not enforce the FAT specification's minimum cluster count or all BPB invariants.

The FAT32 reader uses the BPB root-cluster field, computes the data region after the reserved sectors and all advertised FAT copies, and uses the BPB volume label. FAT writes mirror updates to each advertised FAT; the driver does not honor the FAT32 mirroring-disable flag when reading. FSInfo and backup boot sectors are not read by the existing driver. Root iteration follows the root cluster and skips volume-label entries. Allocation treats FAT entries as free when zero and reserves cluster 2 for the new root. The driver exposes no authoritative free-space statistics. Its VFS caller currently rejects logical sector sizes other than 512 bytes, so 4Kn formatting would not be end-to-end compatible. The writer therefore supports 512-byte sectors only.

The normal VFS mount API has no GPT partition-mount path. Its whole-device FAT probing understands FAT MBR primary entries, while a GPT volume cannot yet be passed as an identified partition. DM7 adds a test-only, read-only adapter around the production FAT32 BPB parser. It supplies the selected partition's starting LBA directly, without registering a VFS mount. This proves that the driver accepts the formatter's FAT32 BPB and geometry for both GPT and MBR test partitions; it does not claim general partition-aware mounting.

## Architecture and revalidation

Reusable request/result and layout types are in `kernel/core/include/kernel/fat32_formatter.h`; the implementation is in `kernel/core/fat32_formatter.cpp`. Disk Manager gathers the selected disk and partition identity, table scheme, GPT disk/partition GUIDs or MBR signature and tuple, expected registry generation, and optional label. The deterministic geometry calculator is separate from device I/O. The service acquires the shared destructive-operation lease, pins and revalidates the target, applies existing write/durability/mount/root/boot checks, reparses the table, matches the selected partition, verifies exact bounds and non-overlap, and calculates all write ranges relative to that bounded extent.

GPT matching requires healthy agreeing copies, the same disk GUID, and one unique partition GUID with unchanged type and exact bounds. MBR matching uses disk signature plus entry number, type, start, and count, and refuses hybrid, extended, protective, and LBA 0 extents. A missing or changed partition, target, or registry generation aborts before formatter writes. The target and partition checks are repeated after snapshotting and before execution. GPT/MBR parsing is in the safety/service layer; the FAT layout calculator and relative-sector writer operate on the bounded partition geometry.

Preflight recognizes FAT32/FAT, exFAT, NTFS, and ext signatures in the bounded prefix. It then scans the entire selected partition in bounded chunks. Only an entirely zero-filled partition is accepted. Read errors fail closed; any other nonzero unrecognized data is classified ambiguous and rejected. No durable DM6-created-partition marker is assumed, and there is no force-format path.

## Supported geometry and layout

- Logical sector size: exactly 512 bytes. 4Kn and other sizes return the explicit unsupported-sector-size status.
- BPB total sectors must fit the 32-bit FAT32 field. The partition must produce at least 65,525 data clusters.
- The deterministic policy tests sectors-per-cluster values `1, 2, 4, 8, 16, 32, 64` in order and selects the first that leaves no more than 120,000 data clusters. This bounds FAT size and rollback memory. A 10,000,000-sector test volume exceeds the supported cluster policy. Effective maximum volume size is about 3.66 GiB at 64 sectors per cluster; exact boundaries account for reserved and FAT sectors.
- FAT layout uses 32 reserved sectors, two FAT copies, root cluster 2, FSInfo at relative sector 1, backup boot sector 6, and backup FSInfo at sector 7. Checked 64-bit arithmetic validates the FAT size, first data sector, root-cluster extent, BPB total, absolute LBAs, and complete partition boundary.
- The 32-bit hidden-sector BPB field contains the partition start when representable; it is zero when the start LBA exceeds that field. The current FAT reader does not use this field.
- The rollback snapshot covers the partition prefix from its boot sector through the end of the root cluster, including the complete reserved region and both FAT copies. Its hard limit is 1 MiB of static snapshot memory. Layouts that exceed it fail before any write.

The BPB uses the formatter OEM text, matching bytes-per-sector, deterministic cluster sizing, reserved/FAT counts, total sectors, FAT32 size, root cluster, FSInfo and backup locations, nonzero volume ID, volume label, and `55 AA` boot signature. It does not add bootloader code or make the partition bootable.

## Metadata creation, ordering, and rollback

Production obtains a nonzero volume ID from the existing VirtIO RNG. If entropy is unavailable, formatting stops before writing. Only the storage-test build can inject a deterministic ID. Labels are bounded to 11 supported FAT characters, uppercased, padded to 11 bytes, and may be blank. GPT partition names remain independent table metadata. A nonblank label is also represented by one volume-label entry in the otherwise empty root directory.

The formatter initializes reserved FAT entries 0 and 1 and marks root cluster 2 allocated to end-of-chain in both FAT copies. FSInfo signatures, free-cluster count (`clusterCount - 1`), and next-free hint 3 agree with the initial FAT. Backup boot and FSInfo sectors match their primary structures. No file data clusters are wiped; only the FAT tables and root cluster needed for a fresh filesystem are initialized.

Write order is: both FAT copies and their remaining zero sectors; root cluster; backup boot sector; backup FSInfo; primary FSInfo; primary boot sector last; trusted flush; independent read-back verification; then normal partition-table rescan. Every writer call uses a relative sector that is range-checked against the selected partition and the device. On write, flush, verification, or rescan failure after writes begin, the complete snapshotted prefix is restored, flushed, and byte-verified. Successful restoration is reported; failed or unverifiable restoration reports `Filesystem state uncertain` and does not claim success.

## Verification

The formatter's read-only verifier independently reads the on-disk BPB, checks all supported geometry and region bounds, boot signature and volume ID/label, FSInfo and backup consistency, both complete FAT copies and reserved entries, root-cluster allocation and contents, and the relationship between FAT regions and the partition. The normal parser must still find the same partition after formatting.

Fake-media tests independently compare every byte outside the selected partition. GPT tests preserve the entire disk outside the target extent and compare disk GUID, partition GUID/type/name/bounds/attributes after format. MBR tests preserve all of LBA 0. Neighboring partitions and immediate guard regions are checked byte-for-byte. The production FAT32 BPB parser is exercised through the bounded read-only test adapter on successful GPT and MBR formats; tests confirm it performs no writes. The root and all FAT/FSInfo structures are also checked by a separate byte-level verifier.

The deterministic suite passed **276 checks, 0 failures**. It includes existing DM2–DM6 coverage plus FAT32 geometry, labels, GPT/MBR formatting, complete outside-range comparisons, clean/recognized/ambiguous/unreadable media, read/write/flush/verification/rescan failure behavior, rollback success/failure, root/mount/boot/durability gates, target and partition changes, registry generation changes, operation-lease contention, 4Kn rejection, table preservation, driver BPB recognition, and unmounted post-format state. Test storage is in memory; no physical disk is written.

The C++14 bare-metal syntax-only check for `kernel/core/kernel_apps.cpp` passed with the installed MinGW compiler; its warnings are existing unrelated UI warnings. `git diff --check` passed. The test build reports an existing unused-helper warning in `fs_fat.cpp`. The normal full build stops in the existing dependency check because `third_party/mbedtls` is absent. MSVC `cl` and QEMU are unavailable. No runtime/QEMU or physical-device formatting was performed.

## UX, result, and next phase

An eligible selected **Unformatted** or **Unknown filesystem** partition exposes contextual **Format...**. The dialog identifies the disk, partition number/name and stable GUID or MBR entry/type, exact LBA bounds, capacity, clean existing state, FAT32 output, automatic cluster size, and optional filesystem label. It requires explicit Format confirmation and provides Cancel. Failures before the dialog are shown as a specific status; operation failures display the stage, rollback outcome, and uncertainty. After success Disk Manager rescans and selects by disk and partition identity. Properties shows cached FAT32 label, ID, sector and cluster geometry, cluster count, FAT count, filesystem capacity, and unmounted state. Used/free statistics remain unknown. The formatter never calls VFS mount and never opens File Manager.

**Outcome A — Safe FAT32 formatting is implemented and passes deterministic structural, boundary, compatibility, and rollback checks.** Runtime hardware behavior is not claimed. DM8 should add partition-aware mount identity and a production read-only/mount workflow for GPT and MBR partitions, then test directory iteration and file I/O without making DM7 auto-mount.

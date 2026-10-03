# Disk Manager Phase DM26 — Safe FAT32 Quick Reformat

**Outcome B.** The new FAT32-to-FAT32 Quick Reformat service, separate confirmation, full metadata replacement, persistent interrupted-operation marker, and deterministic 512-byte/4Kn fake-device coverage are implemented. The required AHCI and USB QEMU qualification, transport regression set, full interruption matrix, and UEFI Release build were not completed in this environment. No physical host storage was used.

## Phase gate and repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `41c1be732f8c70e889390daf788db41851456d43` (`storage: add safe partition deletion`)
- Phase audit: DM25 was the latest report; no DM26 report or implementation existed; `.phase` was absent.
- The DM25 report recommends DM26 as the next phase.
- Cached branch state before changes: ahead 1, behind 0.
- `git fetch` failed with `git@github.com: Permission denied (publickey). fatal: Could not read from remote repository.` Remote freshness is unknown.
- A pre-existing user edit in `.gitignore` and untracked `kernel/out/` were preserved and excluded from the DM26 commit. Root `out/` holds ignored build/test evidence.
- `.phase` was established for DM26 and advanced at closeout to `last_completed=DM26`, `next_expected=DM27`.

## Separate formatting contracts

The existing `format_fat32_partition` KnownZero path remains the blank-media contract. It still scans every logical sector and only uses its bounded metadata writes and small KnownZero rollback after complete zero verification. Quick Reformat has a separate API and does not issue a whole-partition zero scan or use KnownZero rollback.

Quick Reformat is restricted to guideXOS-supported FAT32 volumes with two FAT copies, supported geometry, a matching primary/backup BPB, a backup boot sector inside the new 32-sector reserved region, and at least 32 old reserved sectors. It supports 512-byte and 4096-byte logical sectors and uses the shared FAT32 geometry calculator. Other filesystems and unsupported FAT32 layouts are rejected. An interrupted DM26 transaction can be retried from its identity-bound marker even when the old BPB no longer probes as FAT32.

The contextual Disk Manager action is `Reformat...`; the dialog names the disk/partition, current FAT32 label, capacity, unmounted state, new FAT32 filesystem, and chosen label. It defaults the new label to the old label and requires a separate `Reformat FAT32` confirmation. It warns that old files become inaccessible and that ordinary file data is not securely erased. The normal blank-media `Format...` action continues to use the full-scan service. No force-unmount is performed.

## Eligibility and identity

Quick Reformat reuses the storage operation lease and destructive formatter checks for the current device registration/incarnation, exact table and partition identity, selected partition extent, logical sector size, writable/trusted persistence capability, valid geometry, and root/boot/mount policy. It rejects mounted partitions (therefore open mounted handles/directories), root or protected boot targets, stale registration/table snapshots, read-only or untrusted devices, invalid bounds, and unsupported geometry. It revalidates on preflight, after the user confirmation when the operation starts, and immediately before the first destructive write while holding the lease. The operation does not alter the partition table.

## Invalidation, marker, and point of no return

The first write stores an identity-bound `GXDM26RF` recovery record in an otherwise unused old reserved sector from relative sectors 8–31. It records the partition scheme, start/count, sector size, partition number, GPT disk/type/unique GUIDs or MBR signature/type, old volume label/ID, old backup-boot index, its own sector, version, and checksum. The selected location avoids old FSInfo and backup boot sectors. A retry searches only this reserved-sector range and accepts the marker only when all persistent identity fields and the checksum match. The current device registration/incarnation is freshly pinned by each retry; it is not persisted in the marker.

After capturing old geometry and writing the marker, the service invalidates the old backup boot sector and then the primary boot sector by zeroing signature bytes 510 and 511. It writes a complete logical sector for each operation, including on 4Kn devices. A trusted Flush must complete, and the service rereads both sectors and the marker before clearing either FAT. This makes both old boot copies non-authoritative before FAT replacement proceeds.

The reported state is conservative and monotonic: `BEFORE_DESTRUCTIVE_COMMIT` before the marker write; `IN_PROGRESS` once a marker/invalidation/metadata write may have reached media; `NEW_FILESYSTEM_WRITTEN_NOT_DURABLE` after primary publication but before a trusted final Flush; and `DURABLE` only after marker cleanup, final Flush, reread/verification, and partition rescan. No rollback to the old filesystem is attempted. Post-commit errors return `REFORMAT_INCOMPLETE`, identify an unformatted or uncertain final probe state, and explain that the previous filesystem may no longer be usable and that a fresh preflight is required before retry.

The persistent marker remains available through interrupted writes and is removed only after the new primary BPB has been durably flushed and verified. If marker cleanup or its Flush is uncertain, the operation reports incomplete; retry is safe whether the new FAT32 BPB or marker is the surviving on-media state.

## Metadata initialization and publication order

1. Write the retry marker.
2. Invalidate backup then primary old FAT32 boot signatures; Flush and reread both plus marker.
3. Zero every new reserved sector except primary publication and the retained marker.
4. Zero the complete FAT1 and FAT2 extents in separate, bounded batches.
5. Write media/reserved entries and root-cluster EOC into sector zero of both FATs.
6. Zero the entire new root cluster, then add only the requested root volume-label entry if non-empty.
7. Write primary FSInfo, backup FSInfo, and new backup boot sector.
8. Flush and verify BPB/FSInfo/backup boot, every FAT sector, reserved-region zeros, and every root-cluster sector.
9. Publish the primary BPB last, Flush, reread and verify with the production structure verifier and full FAT checks.
10. Clear the retry marker, Flush again, verify the new filesystem and cleared marker, and rescan the partition table.

The bounded zero buffer is the existing static 1 MiB formatter scratch buffer, not stack or partition-sized storage. Batch sizing uses logical sector size and the common device maximum-transfer capability; FAT logic contains no USB/AHCI transport branching. All block writes are whole logical sectors. A final batch is clamped to the remaining FAT/reserved/root extent.

## Results and evidence

- Blank-format regression remains in the same suite: small blank, 10 GiB sparse blank, and 4Kn blank paths still require full zero coverage. The 10 GiB blank fixture reads exactly 10 GiB in 10,240 bounded 1 MiB requests.
- 10 GiB Quick Reformat deterministic sparse-device coverage passes: both FAT copies are fully cleared and counted; Quick Reformat records zero full-scan bytes/requests; writes remain inside the partition; a data-cluster canary remains byte-for-byte unchanged; and independent FAT32 verification accepts the new volume. The fake clock is synthetic, so this test does not provide elapsed-time or throughput evidence.
- 4Kn deterministic coverage passes: full logical-sector invalidation/metadata writes, full mirrored FAT comparison, root/reserved verification, unchanged ordinary-data canary, GPT partition GUID/extent preservation, fresh volume ID/label, and no full-volume scan.
- An injected failure during FAT2 clearing returns `REFORMAT_INCOMPLETE` with `IN_PROGRESS`; an identity-matched marker enables a retry without probing the corrupted old FAT32 BPB. Retry completes. A 4Kn GPT remount after a fresh device-registration incarnation confirms the old `multi.bin` path is absent and fresh file create/read works.
- Additional deterministic fault coverage injects failures at every Quick Reformat metadata write position, all four Flush boundaries, and final-primary verification corruption. Each case is classified as pre-commit failure or incomplete/durability failure as appropriate, then a fresh preflight and retry succeeds.
- Mounted-partition preflight rejection passes before any reformat write.
- The storage suite retains the DM25 partition deletion regression coverage. The production defaults `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged; NVMe destructive callbacks remain disabled.
- The independent verifier is the repository's deterministic `independent_verify_fat32` test implementation, not an external image tool.
- These tests use in-memory fake block devices and do not access a physical host disk.
- Full storage suite: **888 checks, 0 failures**, including 249 USB Mass Storage checks. The separate DM20 trace-classifier regression has **7 tests, all passing**.
- 10 GiB fake-device metrics: 10,737,418,240 partition bytes; 1,310,720 bytes per FAT copy; 1,310,720 bytes cleared in each FAT; 19,456 reserved bytes; 33,280 root-cluster bytes; 2,675,200 total metadata bytes across 17 writes (about 0.025% of partition size). The synthetic test clock reports zero ticks, so elapsed time/throughput are not measured.
- AMD64 production kernel build passed in an isolated output directory. `build-uefi.ps1` passed the UEFI x64 Release bootloader, AMD64 kernel, dependency check, and ESP setup.
- The captured storage/classifier output is preserved at `out/dm26-storage-manager-tests.log`; the AMD64 build artifacts are under `out/dm26-production-kernel` and the standard UEFI build output is under `kernel/build/amd64` and `ESP`.

## Qualification boundary

DM26 is Outcome B because, although QEMU and MSBuild were found by the repository build script and the UEFI x64 Release build passed, this turn did not complete the requested 512-byte AHCI Quick Reformat QEMU lifecycle, 4Kn USB Quick Reformat QEMU lifecycle, cold-boot tests, or current-source ATA/AHCI/USB transport regression set. Deterministic fake-device evidence is not a substitute for those controller/runtime proofs. Failure injection covers every metadata-write position, each Flush boundary, final-primary verification corruption, and retry; device removal/hotplug during each stage and read/rescan failures remain untested. The report does not claim those gates passed.

There is no separately added filesystem incarnation counter. Existing mounted/open-handle blockers prevent reformat while VFS objects are active; the 4Kn device-registration restart test proves prior file paths disappear after a fresh mount. Broader stale-reference behavior outside that lifecycle was not runtime-qualified. Ordinary old file-data sectors intentionally remain byte-for-byte recoverable; Quick Reformat is not secure erase.

For DM27, first complete the missing AHCI 512-byte and USB 4Kn QEMU lifecycle/restart/independent-image proofs, run the current-source transport regression set, and add device-removal plus read/rescan failure coverage before considering Outcome A. No GPT repair work was started in DM26.

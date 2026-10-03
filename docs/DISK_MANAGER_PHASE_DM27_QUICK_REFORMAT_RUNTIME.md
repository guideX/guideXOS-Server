# Disk Manager Phase DM27 — Quick Reformat Runtime Qualification

**Outcome B.** The current-source 512-byte AHCI and 4Kn USB Quick Reformat lifecycles, cold restarts, independent image verification, current-source USB hotplug and DM25 deletion regressions, and expanded deterministic failure coverage passed. The 10 GiB Quick Reformat lifecycle, a real QEMU removal-during-reformat cycle, cold restart after an actually interrupted marker, and Disk Manager UI/cache path remain outside the completed evidence. No physical host disk was passed to QEMU.

## Phase gate and repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `e4da4be35529ea20a9ccc3783f5884a9347b9645`
- Starting `.phase`: `last_completed=DM26`, `next_expected=DM27`; no DM27 report or implementation was present. The phase gate passed.
- Cached branch divergence at the gate: ahead 0, behind 0. Fetch failed with `git@github.com: Permission denied (publickey).` Remote freshness is unknown.
- The pre-existing `.gitignore` edit was preserved and excluded from the DM27 commit. Existing untracked `kernel/out/` and ignored `out/` evidence were preserved.
- Final `.phase` is advanced to `last_completed=DM27`, `next_expected=DM28` at closeout.

## Runtime contract and implementation hardening

The blank-media `Format` path remains separate: it scans the entire partition and uses KnownZero authorization. Quick Reformat remains limited to supported FAT32-to-FAT32 volumes and never runs the full blank scan. It requires explicit destructive confirmation, exact target identity, valid FAT32 geometry, and writable storage with trusted Flush. It invalidates the old boot copies, clears and verifies all new metadata, publishes the primary BPB last, performs a final trusted Flush and verification, clears its identity-bound retry marker, rescans, then allows fresh VFS mounting.

The QEMU proof calls the public production `probe_fat32_quick_reformat_partition` and `quick_reformat_fat32_partition` APIs used by Disk Manager, then checks VFS file operations. It does not call a private formatter shortcut. Runtime-only serial markers identify the invalidation Flush, FAT clearing, primary publication, and final Flush without per-batch log spam. Read request accounting was added alongside existing byte/write/Flush/tick accounting.

Memory use remains bounded independently of partition and FAT size. The formatter owns one fixed 1 MiB static zero-scan buffer, one logical-sector I/O buffer and one verification buffer (up to 4096 bytes each), a fixed rollback record array of 64 bytes for the KnownZero blank-format path, and small geometry/transaction/marker state. Quick Reformat does not allocate FAT snapshots or use the blank-format rollback records. A byte-level stack-usage measurement was not collected.

The interrupted-operation marker is stored in one of reserved sectors 8–31 of the selected partition. It includes a checksum and binds retry to the partition scheme, extent, logical sector size, partition number, GPT disk/partition/type GUIDs or MBR identity, and the exact marker sector. Registration incarnation is checked by the live operation lease, not persisted. The marker remains through metadata verification and is cleared and flushed only after the fresh filesystem is durable. Deterministic tests verified persistence across device re-registration and rejection of a mismatched GPT identity. A QEMU cold restart with an actually interrupted marker followed by retry was not run.

Post-write rescan failure now has the distinct `DurableButRefreshFailed` state: the service reports a verified, durable FAT32 filesystem with failed runtime refresh instead of classifying the filesystem write as failed. The diagnostic instructs a storage refresh or restart to rediscover it.

## 512-byte AHCI proof

Evidence: `out/dm27-qemu-ahci-rerun/`.

The fresh disposable 600 MiB QEMU secondary disk was initialized as GPT, given a FAT32 partition, and populated under `DM9PROOF` with a `DM9/PROOF.BIN` fixture and a deterministic 96 KiB multi-cluster `DM9/MULTI.BIN`. The Quick Reformat boot passed explicit destructive preflight with the mounted-target block active, then ran through the shared AHCI block callbacks and trusted Flush. It created `DM27FRESH`, a new volume ID, and a fresh `FRESH.BIN`; the old named paths were absent. Fresh file I/O and unmount/remount succeeded.

The partition's GPT unique GUID remained `5146898A52BF5944936AADD7C983D8BF`; its extent remained LBA 2048 through 1,228,766 (1,224,671 sectors). The volume ID changed from `B0A4CC02` to `17E4D73E`. The old file and multi-cluster data canaries at LBAs 3310 and 3326 were unchanged. Their SHA-256 hashes were `E86FD2B4E2E178615CB3C1B81FC72438437B99726BD5B0345D306780900EBA74` and `5266A64DB27F2CA016EEA8A06BB82D9FEE88AB38528C5B780BDB6419DE8FA3AE`. Quick Reformat is not secure erase; old file data remains physically recoverable even though old filesystem metadata is gone.

The independent read-only verifier checked primary and backup GPT header/entry-array CRCs and byte identity, FAT32 BPB/label/volume ID, complete mirrored FAT contents, reserved-region initialization, primary and backup FSInfo, backup boot, root allocation/content, exact new payload, no old paths, and unchanged old data sectors. Exactly six sectors differed between the pre- and post-reformat images, all in the allowed reserved/FAT/root/new-file regions. The cold-restart image was byte-identical to the post-reformat checkpoint; the same fresh volume and exact file bytes were rediscovered after restart.

The reformat reported 162 read requests, 19 write requests, four Flushes, 642,560 metadata bytes written, zero blank-scan requests, and 64 formatter elapsed ticks. Each FAT copy was 306,688 bytes; the reserved region was 19,456 bytes and the root cluster was 8,704 bytes. These are measurements from this 600 MiB image; no 10 GiB runtime timing is claimed.

The separate ordinary 600 MiB AHCI lifecycle passed initialization, partition creation, blank FAT32 format, mount, directory/file write and read, unmount, remount, and restart rediscovery. Current-source ATA lifecycle and independent-image verification passed in `out/dm27-ata-current-source/`.

## 4Kn, 10 GiB, and transport coverage

The final combined 4Kn USB Quick Reformat proof passed in `out/dm27-qemu-4kn-320m-final/`. The 320 MiB QEMU USB disk used explicit 4096-byte logical and physical sectors. Its GPT partition spans 81,659 sectors; the initial scan created a valid FAT32 volume with 81,467 clusters and passed the high-cluster allocation hint. Quick Reformat reported 90 reads, 15 writes, four Flushes, 827,392 metadata bytes, zero blank-scan requests, and 3,712 formatter ticks. It removed the old paths, preserved both old data canaries, wrote and remounted an exact 98,304-byte fresh file, and passed a third cold-restart boot. The independent verifier confirmed unchanged GPT, valid FAT metadata and mirrored FATs, only 33 changed sectors in allowed metadata/new-file ranges, and byte-identical post-reformat and cold-restart images. The old volume ID `48A66606` changed to `044B7B29`; the new volume label is `DM27FRESH`. The profile produced no 512-byte writes. The initial 640 MiB run was stopped before the slow blank scan finished; the first 320 MiB run completed formatting but exposed a stale proof-harness capacity floor despite having 81,467 clusters. That harness bound was corrected, and the final fresh run passed. Both earlier runs and their logs remain preserved.

The deterministic 10 GiB sparse-device Quick Reformat test passed: it cleared both complete FAT copies, initialized reserved/root metadata, reported zero whole-partition scan requests, and retained an old data-sector canary. On a 10,737,418,240-byte partition it reports 1,310,720 bytes per FAT, 2,675,200 bytes total metadata, and 17 writes; the test clock is synthetic, so no runtime or throughput is claimed. Quick Reformat avoided scanning all 10,737,418,240 partition bytes, unlike blank Format, which still requires full zero verification. A 10 GiB AHCI Quick Reformat lifecycle was not completed. Existing normal 10 GiB blank-format runtime evidence remains separate from this Quick Reformat result.

Current-source USB writable private/shared callbacks, Sync Cache, exact readback, canaries, and restoration of the disposable image passed in `out/dm27-usb-write-rerun/`. The current-source 100-cycle write/Sync Cache/read/restore run passed in `out/dm27-usb-stress/`; the image returned byte-for-byte to its initial hash. The read-only proof passed and the image hash was unchanged in `out/dm27-usb-readonly/`. The USB lifecycle/restart and independent GPT/FAT32 verifier passed with ten mount/write/read/unmount cycles in `out/dm27-usb-lifecycle-current-source/`.

The historical USB CSW timeout remains unclassified; current DM21 diagnostics were not disabled. A current-source DM14-style USB hotplug proof passed in `out/dm27-usb-hotplug-current-source/`, covering idle mounted detach, open-handle detach, same-media reinsertion with new incarnations, different-media replacement, VFS write removal, interrupted multi-TD data-out, and interrupted Sync Cache. The current-source DM25 AHCI deletion proof passed in `out/dm27-dm25-current-source/`; after cold restart the GPT deletion was valid and the former partition range and all unrelated bytes remained unchanged. These are regression proofs, not removal during Quick Reformat.

## Failure, retry, and compatibility coverage

The final deterministic storage suite passed **901 checks, 0 failures**, including 249 USB Mass Storage checks. The separate USB trace-classifier suite passed all seven tests.

The new removal matrix injects removal before invalidation, after invalidation Flush, during FAT1, FAT2, and root clearing, during prepublication verification, and after primary publication before final Flush. Each case rejects success, invalidates the exact target registration, and completes a fresh identity-bound retry; the FAT1 case also registers a replacement and proves it receives no writes. This is deterministic callback injection, not QEMU physical hotplug.

Read-failure injections cover preflight, invalidation verification, FAT verification, and final BPB verification. Preflight failure leaves both old boot copies byte-identical and issues no writes. Post-commit verification failures remain incomplete/non-success states and retry succeeds. A forced rescan failure after the final trusted Flush and verification returns `DurableButRefreshFailed`, with the final FAT32 probe state retained. Corrupted final-primary verification remains classified as not durable. Interrupted-marker tests prove the marker survives re-registration of the same fake medium, rejects a mismatched GPT disk identity, and permits explicit retry. A real interrupted QEMU operation followed by cold restart and retry was not run.

The 4Kn deterministic test covers re-registration and fresh VFS mount state, but this phase does not add a separate global filesystem-generation counter or claim broad stale-reference behavior beyond existing mounted/open-handle blockers. A separate Disk Manager UI click-flow and parser/volume-label cache assertion were not run.

Blank-format 512-byte, 4Kn, and large-volume semantics remain covered by the deterministic suite; the known 10 GiB AHCI blank-format runtime remains separate. DM25 deletion deterministic coverage remains green; a current-source QEMU delete/recreate runtime proof is not claimed. The AMD64 kernel and UEFI x64 Release builds passed. Production NVMe remains read-only: `GXOS_DM16_NVME_WRITE_PROVEN` and `GXOS_DM16_NVME_FLUSH_PROVEN` are not enabled by the production configuration, so writable NVMe was not exercised.

## Qualification boundary and next phase

DM27 is **Outcome B**. The 512-byte AHCI and 4Kn USB Quick Reformat runtimes, current-source USB hotplug, and current-source DM25 AHCI deletion regressions passed. Remaining gaps are the 10 GiB Quick Reformat runtime, a real removal/reinsert/retry during Quick Reformat, cold-restart persistence and recovery of an actually interrupted marker, and the Disk Manager UI/cache path. No outcome is inferred from deterministic tests where runtime proof was requested.

Recommended DM28: **GPT Redundancy Repair and Recovery**, covering primary/backup authority selection, header and entry-array CRC failures, explicit bounded repair, boot/root/mount safety, and 512-byte/4Kn verification without writing partition data.

## Evidence index

- AHCI Quick Reformat manifest: `out/dm27-qemu-ahci-rerun/dm27-manifest.txt`
- AHCI independent verifier: `out/dm27-qemu-ahci-rerun/disk-inspection.txt`
- AHCI first/reformat/cold-restart serial logs: `out/dm27-qemu-ahci-rerun/first-boot.serial.log`, `rediscovery-boot.serial.log`, `cold-restart-boot.serial.log`
- Storage tests and classifier: `out/dm27/storage-manager-tests-final-post-runtime.log`
- ATA: `out/dm27-ata-current-source/`
- USB lifecycle, writable, read-only, and 100-cycle evidence: `out/dm27-usb-lifecycle-current-source/`, `out/dm27-usb-write-rerun/`, `out/dm27-usb-readonly/`, `out/dm27-usb-stress/`
- 4Kn final manifest, verifier, and serial logs: `out/dm27-qemu-4kn-320m-final/dm27-4kn-manifest.txt`, `disk-inspection.txt`, and `first-boot.serial.log`, `rediscovery-boot.serial.log`, `cold-restart-boot.serial.log`
- Earlier preserved 4Kn attempts: `out/dm27-qemu-4kn/`, `out/dm27-qemu-4kn-320m/`
- Current-source DM14 hotplug manifest and serial/transport traces: `out/dm27-usb-hotplug-current-source/manifest.txt`, `qemu-serial.log`, and the QMP/UHCI trace files
- Current-source DM25 deletion verifier: `out/dm27-dm25-current-source/disk-inspection.txt`
- Deterministic 10 GiB Quick Reformat accounting: `out/dm27/storage-manager-tests-final-post-runtime.log`

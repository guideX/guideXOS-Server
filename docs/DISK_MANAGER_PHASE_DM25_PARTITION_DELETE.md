# Disk Manager Phase DM25 — Safe Partition Deletion

**Status: Outcome A for the requested DM25 gates.** Deletion removes one exact GPT or primary-MBR partition-table entry, verifies durable metadata, reparses the table, and leaves the former partition data untouched. The 512-byte AHCI QEMU proof passed restart persistence and an independent full-image differential verifier. 4Kn GPT and MBR deletion pass deterministic full-sector transaction tests. The current-source 640 MiB 4Kn USB FAT32 lifecycle and restart regression also passed with independent image verification.

## 1. Phase and repository gate

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`.
- Branch: `DISK_MANAGER_IMPROVEMENTS`.
- Starting HEAD: `c1694ad168ce8466940d027e8996c3633b164181` (`disk manager: add 4Kn FAT32 support`).
- Prompt identity: DM25, expected previous completion DM24, next phase DM25; the prompt was current and not a duplicate.
- No authoritative `.phase` marker exists in the repository. No marker was created or changed.
- At task start the only tracked worktree change was the user-owned `.gitignore` line adding `/out` (without a final newline). It remains separate and is excluded from the DM25 commit. Existing and new `out/` evidence is preserved.
- Cached remote comparison at start: 0 ahead / 0 behind. `git fetch` failed with `git@github.com: Permission denied (publickey)`, so remote freshness is unknown. No remote or credentials were changed.
- No host physical disk was passed to QEMU.

The requested DM3/4/5/6/7/8/9/14/22/23/24 reports and `DISKMANAGER_IMPLEMENTATION.md` were read and audited, along with partition-table writers, GPT transaction/rollback, MBR entry mutation, exact partition identity, mount/root/boot checks, storage-operation leases, parser/rescan paths, Disk Manager actions, and FAT32 formatter entry points. The audit found no truthful existing Reformat path: FAT32 formatting still requires the complete selected partition to be zero, and deleting/recreating an entry does not establish that precondition.

## 2. Deletion semantics and UI

**Delete Partition removes the selected partition-table entry. It does not zero the partition, wipe filesystem metadata, erase file data, or securely sanitize the device.** The old extent becomes unallocated in the partition map and is no longer mountable through the normal partition workflow.

The bare-metal Disk Manager adds **Delete Partition...** for eligible selected partitions and uses a separate explicit confirmation. The dialog identifies the disk, partition number, capacity, filesystem and name/label when known, and mount state. It states that partition metadata is removed while sectors remain on disk, and directs the user to unmount first. Closing/canceling releases the prepared plan and lease. There is no automatic force-unmount.

## 3. Identity, preflight, and revalidation

The operation uses the shared exclusive storage-operation lease and pins the exact disk registration/incarnation across preparation, confirmation, execution, verification, rescan, and cleanup.

GPT identity is tied to the registration/incarnation, selected entry index, unique partition GUID, start/end LBA, and the parser state plus header/entry-array fingerprints. MBR identity is tied to the registration/incarnation, primary slot, type, start LBA, sector count, disk signature, and table fingerprint. The request is never reinterpreted as “whatever currently occupies partition number N.”

The service reparses and validates the table before presenting confirmation. After confirmation it rechecks the lease and registration, geometry, read/write and trusted-Flush capability, root and mount state, boot provenance, parser generation/fingerprint, and selected entry identity before writing. A stale selection invalidates the action and requires refresh/reselection. Target loss, registry replacement, or changed metadata fails closed.

Mounted partitions, including live stale mount backing, open-file or directory use that keeps the mount alive, block deletion until explicit unmount. Root/system backing is always blocked. DefinitelyBoot is blocked. Unknown boot identity fails closed where the relationship could identify the boot source. When firmware provides a unique AHCI hard-drive media node, the actual boot partition is protected while a separately identified nonboot partition on that disk can remain eligible; a missing or ambiguous media identity protects the disk conservatively.

## 4. GPT transaction, verification, and rollback

The service clears exactly the selected GPT entry in each in-memory array. It does not renumber remaining entries. It recalculates both entry-array CRCs and both header CRCs while preserving unrelated array entries and header bytes other than the required CRC fields.

The snapshot is bounded to the logical sectors containing the selected entry in the primary and backup arrays plus the two header sectors. An entry may share a sector with neighboring entries; the implementation clears only its byte span and preserves the neighbors. With the standard 128-byte entry arrays, this is four snapshot sectors total on both 512-byte and 4Kn media. The maximum containing span is bounded even for supported nonstandard entry sizes. No sector in the old partition extent is snapshotted.

Write/Flush/read-back order:

1. Flush before mutation and revalidate the table and snapshotted metadata.
2. Write the selected backup-array sector span, then the backup header; Flush and verify the backup GPT copy.
3. Write the selected primary-array sector span, then the primary header; Flush and verify both GPT copies and CRCs.
4. Reparse the table, confirm the selected entry is absent and unrelated entries remain, reconstruct unallocated gaps, and revalidate the original registration.

This order keeps a valid old primary GPT while the backup copy is updated, then completes the primary copy only after backup verification. A successful GPT deletion performs three Flush calls (pre-write and after each copy) and reports exact logical sectors read, written, and write ranges.

On write, Flush, read-back, or rescan failure, rollback restores only the saved array/header sectors on the pinned original registration, Flushes, and verifies the restored metadata. The original identity and lease are checked before rollback writes. If the original media disappears or is replaced, rollback is not sent to the replacement and the result reports uncertain state. Failure injection covers backup/primary stages, Flush, verification, rescan, rollback failure, and removal.

## 5. Primary MBR transaction

The service supports ordinary primary MBR entries only. It snapshots the full logical LBA0 sector, clears exactly the selected 16-byte entry at the standard byte offset, then writes the full logical sector. The disk signature, boot code, other three entries, MBR signature, and all bytes outside the selected entry are preserved. It Flushes, reads back, verifies the selected entry is empty and unrelated bytes match, reparses, and rebuilds gaps. Extended/logical and hybrid MBR cases remain unsupported.

On 4Kn, MBR still resides in logical LBA0. Standard MBR offsets and bytes 510–511 remain unchanged; the entire 4096-byte logical sector is snapshotted, written, and verified.

## 6. Mount safety, gap reconstruction, and data isolation

Deletion blocks the exact mounted partition. A deterministic VFS test mounts partition A and confirms its deletion probe returns MOUNTED while unmounted partition B on the same disk remains eligible. Direct mounts, stale-but-live backing, root backing, and open handles follow existing VFS lifetime rules; no force-unmount was added.

After the final table reparse, the parser rebuilds free regions from surviving partitions. Tests cover the deleted extent joining/coalescing adjacent free space and all disk/usable-range boundaries.

Fake-disk byte-diff tests prove GPT and MBR writes leave all unrelated sectors/bytes unchanged. The AHCI image verifier compares the full 600 MiB raw image against a pre-delete checkpoint: the entire former partition range is byte-for-byte identical, all bytes outside the allowed GPT metadata LBAs are byte-for-byte identical, all unrelated GPT entry bytes match, and the protective MBR is unchanged. Delete never scans the old partition.

Deleting and then creating a partition over the same extent does not make it blank. The old FAT32 formatter's mandatory whole-partition zero scan correctly returns `AMBIGUOUS_EXISTING_DATA` without writes. Reformat/Quick Format is explicitly deferred; it needs a distinct safe formatter contract rather than reuse of the KnownZero token.

## 7. QEMU proof and regressions

### 512-byte AHCI Delete Partition proof

`scripts/run-dm25-qemu-partition-delete.ps1` used a new disposable 600 MiB raw secondary disk on Q35/ICH9 AHCI port 1. The boot disk was on port 0; QEMU received no physical host disks. The first boot initialized GPT, created and formatted a FAT32 partition, mounted it, created/read a test file, and cleanly unmounted it. A second boot rediscovered and remounted the filesystem, reread the file, and executed Delete Partition after explicit proof confirmation. A third boot verified the empty but valid GPT persisted across restart.

Delete result: success; 231 logical sectors read; 4 logical sectors written; 3 Flush calls; read-back and rescan passed; no rollback. The exact writes were backup array LBA 1228767, backup header LBA 1228799, primary array LBA 2, and primary header LBA 1. The selected partition was slot 1, LBA 2048 through 1228766 (628,080,128 bytes). Both GPT copies and CRCs pass. The independent verifier confirms the partition data and every nonmetadata image byte remain identical to the pre-delete image. The third boot reports a valid empty GPT.

An initial disposable proof attempt exposed an alias in the proof harness's saved pre-delete table state; the service result was not accepted. The harness now keeps an independent pre-delete table copy. That failed run and its image remain preserved under `out/dm25-ahci-partition-delete-20261003-071902/`; the accepted run is `out/dm25-ahci-partition-delete-20261003-072043/`.

### 4Kn deletion proof

GPT and primary-MBR deletion pass deterministic 4096-byte-sector FakeDisk transaction coverage. It validates full-sector snapshots/writes/rollback, GPT CRCs, entry sharing within an array sector, neighboring-entry preservation, MBR boot code/disk signature/other entries/signature preservation, and post-delete parser/gap state. The GPT slot-sharing cases demonstrate why only the chosen entry bytes are cleared. No 4Kn deletion QEMU proof was required; the deterministic transaction model is the 4Kn deletion proof.

### Transport and compatibility regressions

- **ATA:** A fresh 68 MiB legacy ATA PIO lifecycle, restart rediscovery, and independent GPT/FAT32 image verification passed using `scripts/run-dm9-qemu-proof.ps1` against current source.
- **AHCI:** The primary 512-byte deletion proof above passed. Its first two boots also ran the normal initialize/create/format/mount/file lifecycle and restart persistence. After the delete proof, a separate fresh 600 MiB AHCI normal lifecycle and restart passed with independent verification (`out/dm25-ahci-normal-regression/`). DM22's separate 10 GiB AHCI FAT32 lifecycle passed with its independent verifier.
- **USB writable and 4Kn FAT32:** A current-source 640 MiB 4Kn USB lifecycle and restart passed. It scanned all 163,579 partition sectors in 639 reads, wrote seven FAT32 metadata sectors, mounted the volume, created a directory, wrote/read a 96 KiB file, unmounted/remounted, restarted, rediscovered, reread the file, and passed the independent 4Kn verifier. Evidence: `out/dm24-qemu-4kn-20261003-072541/`.
- **4Kn metrics:** Logical and configured physical block size were 4096 bytes; the partition was 670,019,584 bytes. FAT32 used 8192-byte clusters, 81,693 clusters, and 80 sectors per FAT copy. The scan took 166,704 ticks (about 27m47s in this proof's PIT accounting). Both GPT copies, CRCs, FAT mirror, FSInfo, backup boot sector, and the 12-cluster 96 KiB file chain passed independent verification; the raw image SHA-256 is `EFF248C14D85415A66A62D76BD93EBFCB1CE66D5F10E16F26C84F5175C6980BB`.
- **USB read-only:** A current-source DM12 UHCI USB read-only mount, deterministic file read, clean unmount, full boot, and unchanged image hash passed. The reference image SHA-256 remained `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`. One setup attempt used a non-DM12 proof kernel and did not reach the proof marker; that run is preserved separately and is not counted as a device failure.
- **DM21 diagnostics:** The existing WRITE(10)/CSW trace and classifier workflow remains unchanged. All 7 trace-classifier tests pass; the historical DM21 failure remains unclassified.
- **NVMe:** Production defaults remain `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`. The read-only registration/fallback and destructive-preflight rejection tests pass; no NVMe write behavior was added.

## 8. Tests and builds

The storage-manager runner passes **878 checks, 0 failures** (including all **249 USB checks**), and `tests/classify_dm20_usb_trace_test.py` passes **7 tests**. DM25 adds GPT/MBR success and CRC verification, exact byte preservation, 512/4Kn coverage, rollback and I/O failures, Flush/read-back/rescan failures, identity staleness and replacement/removal, mount/root/boot rules including partition-level same-disk distinction, gap coalescing, delete/recreate, and no-wipe behavior. The former DM24 baseline of 862 checks is preserved; DM25 adds 16 passing checks.

The default AMD64 production kernel builds in an isolated output directory; DM25 delete and DM12 read-only proof kernels compile from current source, and the UEFI x64 Release bootloader builds. DM22 10 GiB and DM24 4Kn proof builds also pass. Two pre-existing compiler warnings remain in `fs_fat.cpp` and `fs_ext4.cpp`; they are unrelated to DM25.

## 9. Reformat decision, limits, and next phase

**Reformat Partition is deferred.** The current formatter is intentionally a KnownZero formatter and rejects any nonzero contents after scanning the entire partition. Reusing it for an existing filesystem would either reject useful requests or require weakening the established unknown-data protection. A later Quick Format/Reformat or explicit Wipe/Full Format design needs its own transactional and recovery contract.

DM25 adds no resize, move, merge, recovery, undelete, GPT repair, filesystem resize, secure erase, or NVMe writes. Deletion works only for supported GPT partitions and normal primary MBR entries. QEMU deletion is a 512-byte AHCI proof; 4Kn deletion is deterministic FakeDisk coverage. Firmware boot-partition separation is allowed only when the media node yields an unambiguous exact partition identity; unknown relationships remain fail-closed.

**DM26 recommendation: Safe Reformat / Quick Format Architecture.** The delete/create lifecycle now exposes the formatter's all-zero-media rule directly. Define a truthful, bounded alternative for intentional existing-filesystem replacement without weakening the KnownZero formatter.

## 10. DM25 closeout

- Starting HEAD: `c1694ad168ce8466940d027e8996c3633b164181`.
- Ending HEAD is the DM25 commit containing this report; its hash, cached ahead/behind count, fetch result, and push result are stated in the task closeout.
- The user-owned `.gitignore` edit is intentionally excluded from the commit. `out/` evidence remains ignored and preserved.
- An isolated DM12 proof build generated `kernel/out/dm25-usb-readonly-regression/`. Cleanup was rejected by the local command policy, so the generated files remain untracked and are excluded from the DM25 commit.
- No `.phase` marker is in use, so there is no marker transition to write.
- Successful closeout identity: `last_completed=DM25`; `next_expected=DM26` (recorded here because no authoritative phase marker exists).

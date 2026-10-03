# Disk Manager Phase DM24 — 4Kn FAT32 Formatting and VFS

## 1. Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `828f00d4bebc0ade35f87e016c330e169e47d76a` (DM23)
- Cached `origin/DISK_MANAGER_IMPROVEMENTS` comparison: 0 ahead / 0 behind.
- The tracked `.gitignore` edit adding `/out` predated DM24 and remains separate from this work. Existing and new ignored `out/` evidence is preserved.
- `git fetch` failed with `git@github.com: Permission denied (publickey). fatal: Could not read from remote repository.` Upstream freshness is unknown.

DM2, DM6–DM8, DM22, DM23, and the implementation report were reviewed. No physical host disk is passed to any DM24 QEMU run.

## 2. 512-byte assumption audit

The audit covered partition-table parsing and writing, partition views, storage scans, FAT formatting/parsing and I/O, VFS mounting, Disk Manager presentation, host verification, and QEMU proof code.

Remaining 512-byte values are structural or deliberately scoped: MBR fields stay at their standard offsets and its signature is bytes 510–511 of LBA0; FAT BPB/FSInfo fields retain their standard offsets; FAT directory entries are 32 bytes and FAT entries are four bytes; GPT entry sizes and CRC coverage are byte-defined. The FAT formatter checks and zeroes the rest of a 4096-byte boot/FSInfo sector after the traditional 512-byte structure. ext/UFS probes and FAT16/exFAT mounting remain 512-byte-only. The hosted `.img` preview is a separate read-only 512-byte MBR viewer. USB packet sizes, graphics coordinates, and general 512-byte regression fixtures are not logical-sector assumptions.

FAT32 end-to-end support is explicitly limited to logical sizes 512 and 4096. The block layer's wider 512–4096 power-of-two geometry contract is not a claim that 1024- or 2048-byte FAT formats are supported.

## 3. Device geometry and transport

`BlockDevice.sectorSize` remains the authoritative logical sector size. AHCI parses ATA IDENTIFY words 106–118, rejects malformed reported sizes, and exposes the logical size and physical size when IDENTIFY makes them available. Unknown physical geometry remains zero/unknown; it is never inferred from capacity or image alignment. AHCI diagnostics include both values.

The QEMU `ide-hd` device class used by the existing AHCI proof runner rejects logical block sizes other than 512; the realization check is in [QEMU `hw/ide/qdev.c`](https://gitlab.com/qemu-project/qemu/-/blob/7425b6277f12e82952cede1f531bfc689bf77fb1/hw/ide/qdev.c). DM24 therefore uses the already-supported writable USB Mass Storage path. USB SCSI READ CAPACITY reports 4096 bytes per logical block to the production block registration. USB Mass Storage does not expose physical-sector size through the current driver, so runtime diagnostics report physical size as unknown; the disposable QEMU device is configured with physical size 4096.

## 4. MBR and GPT on 4Kn

Partition code reads and writes whole logical sectors. MBR partition entries remain at byte 446, the disk signature at byte 440, and `0x55AA` at bytes 510–511 of 4096-byte LBA0. The remaining bytes are initialized deterministically. The GPT protective entry is in that same MBR structure; no signature is moved to bytes 4094–4095.

GPT LBA1 is the primary header. Entry-array sector counts use `ceil(entryCount * entrySize / logicalSectorSize)` with checked arithmetic; the array remains 16 KiB for 128 128-byte entries, so it occupies four 4Kn sectors. Header and entry CRCs cover their declared byte ranges. The backup array and header occupy the final logical blocks. All range and usable-LBA calculations remain in logical-block units.

The existing 1 MiB partition boundary converts to 256 4Kn sectors. Parser, initialization, creation, bounds, and alignment tests cover both 512-byte and 4096-byte devices. The independent raw verifier checks the protective MBR, both GPT copies and CRCs, array placement, usable bounds, and 256-sector alignment.

## 5. Partition views and blank scan

Partition endpoints retain the parent registration identity and sector size exactly. View LBA 0 maps to the partition start; bounds, translated reads/writes, stale-registration behavior, and mount identity use the same checks as 512-byte views. Tests verify translation and reject stale parents.

The DM23 full-partition zero scan remains mandatory. Its fixed aligned 1 MiB buffer holds 256 4Kn sectors. Batch size is rounded to complete logical sectors, the last batch covers the exact remainder, and scan totals and the one-use blank token bind the 4096-byte geometry. No format writes occur before complete zero coverage. A nonzero byte or read failure aborts. KnownZero rollback records logical-sector LBAs and restores all 4096 bytes of each touched sector.

## 6. FAT32 geometry, metadata, and addressing

The formatter and parser support 512 and 4096 bytes per sector. Sectors per cluster are powers of two bounded by `clusterBytes <= 32768`; for 4Kn, valid formatter choices are 1, 2, 4, or 8 sectors (4, 8, 16, or 32 KiB clusters). The chooser reduces the cluster size when necessary to meet FAT32's minimum cluster count. Fixed sector counts, including 32 reserved sectors, retain their logical-sector meaning.

The formatter builds a fully zeroed logical-sector boot record with `BPB_BytsPerSec=4096`, keeps standardized BPB fields and the boot signature at their prescribed offsets, and writes a complete matching backup boot sector. FSInfo fields retain offsets 0, 484, 488, 492, and 508; the remainder of each full logical sector is deterministic zero. The primary and backup FSInfo sectors are checked independently and compared over all 4096 bytes.

FAT32 entry `N` is addressed as byte offset `N * 4`, logical sector `offset / bytesPerSector`, and in-sector offset `offset % bytesPerSector`, with checked arithmetic. A 4Kn FAT sector contains 1024 entries. FAT writes update both configured mirrors, preserve unrelated entries and sector bytes, and retain reserved bits. The root-cluster LBA is derived from `firstDataSector`, sectors per cluster, and the root cluster. Directory entries remain 32 bytes (128 per 4Kn sector); iteration crosses prior 512-byte boundaries and the full 4096-byte boundary. Existing LFN traversal is retained.

FAT file and directory I/O uses the mounted endpoint's geometry. Sub-sector writes use full-sector read/modify/write and preserve surrounding bytes. Tests exercise offsets 1, 511, 512, 4095, 4096, and 4097 and lengths 1, 31, 512, 513, 4095, 4096, and 4097. Multi-sector and multi-cluster files are verified after unmount/remount and restart.

The parser rejects unsupported bytes-per-sector values, mismatches between BPB and device geometry, invalid cluster-byte limits, invalid FAT counts/size, insufficient FAT capacity, bad total/volume bounds, and invalid root geometry. FAT32 partition mounts accept matching 512- or 4096-byte devices; unrelated ext/UFS, FAT16, and exFAT paths remain restricted. VFS partition mounts retain parent generation, GPT/MBR identity, exact bounds, and the normal partition view.

## 7. Writes, rollback, and failure injection

Formatting preserves the DM22 small-write property: six sectors without a volume label, seven with a label. The number of logical sectors is unchanged on 4Kn; bytes written scale with the 4096-byte logical sector. Rollback records remain at most eight 8-byte entries (64 bytes), recording KnownZero state rather than copying sector contents.

Deterministic 4Kn tests inject scan read failures and nonzero sectors; metadata write failures for FAT1, FAT2, root, backup boot, backup FSInfo, primary FSInfo, and primary boot; Flush failures; and verification corruption beyond byte 512. Successful rollback restores the complete 4096-byte KnownZero sector and keeps neighboring sectors and partition-boundary canaries unchanged. Failure statuses report rollback and uncertainty truthfully.

## 8. Geometry and test coverage

The geometry matrix includes minimum FAT32-valid 4Kn geometry and representative 3, 10, 32, 64, and 128 GiB plus larger representable values. BPB `TotSec32` counts logical sectors: its byte-capacity ceiling is sector-size dependent. Hidden sectors are absolute logical sectors and are rejected when the 32-bit BPB field cannot represent the partition start.

The full storage suite passed **862 checks, 0 failures**, including **249 USB checks**. It covers protective MBR structure; GPT placement/CRC and 256-sector alignment; partition views; complete 4Kn scan accounting and token geometry; formatting, BPB mismatch rejection, bounded writes, rollback, FAT entry boundaries and mirrors, FSInfo, root and directory traversal, partial writes, multi-cluster persistence, and the 512-byte regression and NVMe read-only gates. All **7 USB trace-classifier tests** also passed.

**Outcome A for the requested DM24 qualification gates.** The current-source regression matrix passed:

- 4096-byte FAT32 lifecycle on the writable QEMU UHCI USB device: full blank scan, GPT initialization, aligned partition creation, FAT32 format, VFS file write/read, unmount/remount, and cold restart persistence; independent 4Kn image verification passed.
- 512-byte regressions: ordinary 600 MiB AHCI lifecycle, 10 GiB AHCI FAT32 lifecycle with high-cluster allocation, and 600 MiB ATA lifecycle; each completed restart and independent image verification.
- USB regressions: fresh 80 MiB writable lifecycle with ten VFS stress cycles, restart persistence, and independent image verification; read-only FAT32 registration/mount/read; and direct UHCI removal/reinsert, stale-handle, replacement-media, interrupted-write, and Sync Cache hotplug stages.
- NVMe read-only production gate: the QEMU NVMe candidate registered with `write=no`, `flush=no`, and `shared-write=disabled`; the private 100-cycle write/Flush/read/restore proof passed against its disposable image and restored the full image hash.
- Storage suite: **862 checks, 0 failures**, including **249 USB checks**; all **7 USB trace-classifier tests** passed. Current-source AMD64 production/proof kernels and UEFI x64 Release builds passed.

Supporting evidence directories under `out/`: 4Kn lifecycle `dm24-qemu-4kn-usb-20261002-final`; 600 MiB AHCI `dm24-regression-ahci-600m-20261002`; 10 GiB AHCI `dm24-regression-ahci-10g-20261002`; ATA `dm24-regression-ata-600m-20261002`; writable USB `dm24-regression-usb-512-80m-corrected`; USB read-only `dm24-regression-usb-readonly-20261002b`; USB hotplug `dm24-regression-usb-hotplug-20261002`; and NVMe read-only `dm24-regression-nvme-readonly-20261002`.

QEMU 11's IDE-backed AHCI proof device only accepts 512-byte logical blocks, so the 4Kn end-to-end lifecycle used QEMU UHCI USB Mass Storage, whose READ CAPACITY path reports 4096 bytes to the production block layer. The production USB driver reports physical geometry as unknown. A preliminary read-only harness launch used the ordinary production kernel and therefore lacked the DM12 proof marker; its fixture was attached read-only and its hash did not change. A preliminary writable launch used the unrelated DM9 kernel and was stopped before writes. Both are excluded from the passing matrix; the corrected DM12 and DM13 proof-kernel runs above passed. The earlier AHCI/IDE geometry rejection logs remain preserved under `out/`.

## 9. QEMU 4Kn configuration and evidence

The DM24 proof uses QEMU `pc,usb=off`, an explicit `piix3-usb-uhci,id=uhci`, and a disposable raw image attached as:

```text
usb-storage,id=dm24disk,bus=uhci.0,drive=dm24secondary,removable=on,
serial=DM24USB01,logical_block_size=4096,physical_block_size=4096,
discard_granularity=4096
```

The kernel must select the matching USB serial, prove the target is not the boot device, and report `sectorSize=4096` from SCSI READ CAPACITY before it writes. No host physical disk is attached. The manifest records the QEMU version/hash, exact arguments, 4096-byte geometry, image hash/size, UEFI/kernel hashes, and both boot logs.

The QEMU device registered as a definitely non-boot USB target with **163,840 LBAs × 4096 bytes = 640 MiB**. The production SCSI READ CAPACITY result, rather than an image-alignment assumption, provided the logical sector size. Runtime physical size was unknown to USB Mass Storage, although QEMU was configured with 4096-byte physical blocks.

The guest initialized GPT, parsed the primary and backup copies, created its 1 MiB-aligned partition at **LBA 256 through 163,834**, and completed the full scan before formatting. The scan covered **163,579 logical sectors / 670,019,584 bytes** in **639 reads**, with a largest read of **1 MiB**. The guest reported `scanTicks=167499`; at the PIT's 100 Hz this is 1674.99 seconds (27m55s) and approximately **0.381 MiB/s**. The complete first-boot run took about 45 minutes of host wall time while other QEMU workloads were active.

FAT32 geometry was **4096 BPS, 2 sectors/cluster, 8192-byte clusters, 163,579 volume sectors, 81,693 clusters, 80 FAT sectors per copy, first data sector 192, root cluster 2**. Seven metadata sectors were written (28,672 bytes), and the KnownZero rollback log used 56 bytes. The proof set FSInfo's next-free hint to cluster 70,001. It mounted, created and read a deterministic 96 KiB file across 12 clusters, unmounted, remounted and reread it. The cold-restart boot independently rediscovered GPT and remounted/read the same file.

The final-source kernel hash was `0CB79CABF53D68518F519438D9AD5879F2D2F6C0ABEC0C5F32510F8A529DD46A`, and the final image SHA-256 was `B0B8577486CBE160B07F15A3E76232278EAA4386DD24F509CCF4C10E34663DEA`. The host verifier passed with **start LBA 256, end LBA 163,834**, matching backup boot and FSInfo, agreeing GPT CRCs and entry arrays, mirrored FAT entries, root cluster 2, the high-cluster directory/file chain, and all 98,304 payload bytes. It checked FAT entries in three sectors and confirmed the 4Kn MBR signature remained at bytes 510–511. Its summary is in `disk-inspection.txt`; first and restart serial logs and exact QEMU arguments are in `dm24-manifest.txt`. All artifacts and the final raw image are preserved under `out\dm24-qemu-4kn-usb-20261002-final`. Earlier AHCI/IDE startup rejection attempts remain under `out\dm24-qemu-4kn-*`.

The independent verifier is [verify-dm22-qemu-image.py](../scripts/verify-dm22-qemu-image.py). It accepts an explicit logical size or detects GPT geometry, applies LBA × sector-size byte offsets, checks protective MBR/GPT CRCs and alignment, validates BPB/FSInfo/backup structures, mirrored FAT entries, root directory, high-cluster file contents, and file bytes.

## 10. Builds, memory, and remaining limits

The ordinary AMD64 production kernel and UEFI x64 Release bootloader build from the final source. The DM24 proof kernel is separately compiled with its lifecycle opt-in. The scan buffer remains a fixed 1 MiB allocation, independent of disk/FAT size. FAT I/O uses bounded 4096-byte sector buffers; formatter verification reuses static 4096-byte scratch instead of nested sector arrays on the boot stack. VFS detection uses one aligned 4096-byte static scratch sector. No new allocation scales with volume size, FAT size, or cluster count, and the bootstrap stack size is unchanged. `BlockDevice` gains only the four-byte physical-sector-size field.

The DM24 acceptance boundary is logical-sector sizes 512 and 4096. USB hub traversal, exFAT/NTFS formatting, FAT resize/repair, filesystem free-space accounting, asynchronous scan UI, and writable NVMe remain out of scope. NVMe remains production read-only with no write or Flush callback.

## 11. DM25 recommendation

After 4Kn and large FAT32 geometry are qualified, move to **Safe Delete Partition + Reformat Workflow**: require explicit destructive confirmation; protect boot/root/mounted targets; update GPT primary/backup metadata or clear only the selected MBR entry; revalidate target and partition identity; preserve unrelated partition data; and report rollback/uncertainty. Reformatting an existing partition should remain a separate explicit action. DM25 is not part of DM24.

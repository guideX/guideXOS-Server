# Disk Manager Phase DM22 — FAT32 Scalability

**Status: Outcome B.** The large-volume FAT32 implementation meets the geometry, bounded-write/rollback, failure-injection, high-cluster, and 10 GiB AHCI lifecycle goals. The current-source USB read-only reference and private/common write stress pass. One requested boundary remains: the writable USB full lifecycle did not finish because the preserved traced UHCI run advanced too slowly through the mandatory full-partition blank scan. No filesystem or USB transport error was observed in that scan, and no full writable USB lifecycle pass is claimed.

## Repository state

Work began on branch `DISK_MANAGER_IMPROVEMENTS` at DM21 commit `aeda60685ce0d64148d78c4fc0206de5dad99e19`. The tracked worktree was clean at start. Existing untracked `out/` evidence was retained. The cached remote comparison was 0 commits ahead and 0 behind. The requested normal fetch failed with SSH `Permission denied (publickey)`, so upstream freshness is unknown; no remote or credentials were changed.

DM22 changes the FAT32 formatter, parser, FAT I/O and allocation paths, proof instrumentation, tests, and QEMU runners. It does not add 4Kn FAT, exFAT formatting, repair, partition deletion or resizing, or NVMe writes.

## Exact old limit and constraints

The DM21 formatter explicitly capped FAT32 data clusters at 120,000, even though the format's cluster-number space is much larger. With 512-byte sectors and the prior 32 KiB cluster size (64 sectors per cluster), 120,000 clusters represent about 3.66 GiB of data area. For that geometry, each FAT copy needs `ceil((120,000 + 2) * 4 / 512) = 938` sectors. With 32 reserved sectors and two FATs, `firstDataSector = 32 + 2 * 938 = 1,908`. The old snapshot then extended through the root cluster: `(1,908 + 64) * 512 = 1,009,664` bytes. The implementation also had a fixed 1 MiB snapshot limit and rejected geometry when the calculated prefix exceeded it. The explicit 120,000-cluster policy was the practical ~3.66 GiB ceiling; the prefix snapshot and 1 MiB cap coupled otherwise-valid geometry to rollback memory.

The old accepted count was 65,525–120,000 clusters. FAT32's lower threshold remains 65,525; volumes below that belong to FAT12/FAT16 classification. The usable upper count is `0x0FFFFFEE`, giving data cluster numbers 2 through `0x0FFFFFEF` while avoiding reserved/bad/EOC values. The count range follows the FAT32 cluster-number format, not the former guideXOS 120,000-cluster policy ([Microsoft FAT32 specification](https://www.cs.fsu.edu/~cop4610t/assignments/project3/spec/fatspec.pdf)).

## Geometry and arithmetic

The formatter remains 512-byte-logical-sector only. Sectors per cluster are powers of two from 1 through 64; the deterministic chooser prefers the largest size that yields a valid FAT32 cluster count, with a hard 32 KiB cluster-byte ceiling. It does not select 128 sectors per cluster. BPB `TotSec32` limits a volume to `UINT32_MAX` sectors, or 2 TiB minus 512 bytes at 512 bytes per sector. This is the representable BPB geometry, not a claim of runtime proof at that maximum.

The formatter accepts 64-bit partition start/count inputs and uses checked 64-bit intermediates for FAT sizing and convergence, first-data offset, volume end, absolute-LBA addition, root-cluster translation, snapshot/write ranges, and partition bounds. Values are narrowed into 32-bit BPB and geometry fields only after range checks. BPB hidden sectors is zero when the partition start cannot fit that 32-bit field; the mounted partition view uses local sector numbers and does not depend on it. Rejected overflow or impossible FAT capacity is detected before writes.

The FAT32 BPB parser now validates power-of-two SPC, `bytesPerSector * SPC <= 32 KiB`, mirrored FAT mode, version zero, FAT32-only total/FAT-size fields, nonzero total sectors, FAT capacity for `clusterCount + 2`, standard cluster-count bounds, root-cluster range, and partition/volume bounds. FAT entry byte offsets and cluster-to-sector translation use checked 64-bit arithmetic. FAT entries are read on demand into a fixed sector buffer; the driver never caches an entire FAT. FAT writes update every copy and preserve the reserved high nibble. The same checked offset arithmetic protects the exFAT path, but exFAT support was not expanded.

FSInfo signatures and a valid next-free cluster are treated as advisory. Invalid, missing, or unreadable FSInfo does not prevent mount; allocation falls back to cluster 2. Free-cluster count is ignored as authoritative state. FAT allocation remains an on-demand linear scan in the worst case; no per-cluster bitmap was introduced. A chain traversal guard of 65,536 links and 32-bit VFS file sizes remain. At 32 KiB per cluster, the chain guard bounds a single traversal to roughly 2 GiB even though FAT32's on-disk file-size field is 32-bit. Mount slots, file handles, and paths also retain their existing fixed limits.

## Blank-media contract and format transaction

Formatting still requires the **entire selected partition** to be readable and zero-filled. A bounded 64 KiB static buffer scans all sectors. Recognized filesystems, any nonzero unknown content, and unreadable sectors are rejected. A newly created partition or free GPT gap is not treated as proof of zeroes. Every planned metadata sector is rechecked after the scan. This preserves the existing protection against overwriting unknown data; it also means preflight time remains O(partition size), separately from the bounded write set.

After authoritative zero verification, the formatter writes only sectors whose final contents differ from zero:

1. FAT copy 1 reserved-entry sector.
2. The corresponding FAT copy 2 sector.
3. The root-cluster sector only when a volume-label directory entry is present.
4. Backup boot sector 6.
5. Backup FSInfo sector 7.
6. Primary FSInfo sector 1.
7. Primary boot sector 0 as the final publication write.

Thus a labeled format writes seven sectors; an unlabeled format writes six. Unused FAT sectors and the remaining known-zero root-cluster sectors are left untouched. Both initial FAT sectors are independently read back and their reserved entries agree. Root cluster 2 maps to `firstDataSector + (2 - 2) * sectorsPerCluster`, so increasing FAT size moves the data region without changing root-cluster arithmetic. FSInfo begins with the exact fresh free-cluster count and a next-free hint after the root cluster. The label retains uppercase/padding to 11 characters and is written to BPB and root entry.

The primary boot sector is written last, then the formatter requires Flush, reads back and validates primary/backup BPB and FSInfo, FAT reserved entries/mirror, root cluster/label, geometry, and bounds, and rescans the partition table. It reports success only after those checks. A write, Flush, byte-verification, or rescan failure triggers rollback when the original target and partition identity remain valid. If registration/incarnation, geometry, partition identity, or lease validation fails, no rollback is sent through a replacement mapping. A verified rollback reports the old blank state; an unverified or unsafe rollback reports uncertain filesystem state rather than clean success. A possible failure during final publication therefore cannot be presented as durable success without Flush and readback.

Rollback stores only a relative sector and prior-state tag. Because every touched sector was revalidated as zero, it records `KnownZero` instead of a 512-byte snapshot. Each record is 8 bytes; eight slots are the explicit maximum, so rollback bookkeeping is capped at **64 bytes**. Current formats use 48 bytes without a label and 56 bytes with one. The cap and write count do not grow with FAT size.

## Tests and high-cluster coverage

The final storage suite passes **832 checks, 0 failures**, including all 249 USB checks and all 7 DM20 trace-classifier tests. New cases cover:

- Geometry for 3, 4, 8, 16, 32, 64, and 128 GiB; deterministic SPC steps; the exact minimum cluster-count boundary; 4Kn rejection; BPB sector-count overflow; and absolute partition-end overflow.
- The former 120,000-cluster boundary and the first geometry with 120,001 clusters.
- FAT capacity for every large matrix geometry (`entries >= clusters + 2`).
- An 8 GiB sparse fake partition, full zero scan, outside-partition canaries, six metadata writes, and 48 rollback bytes.
- Injected failure at each of the seven labeled metadata writes (both FAT copies, root label, backup boot, both FSInfo sectors, and primary boot publication); each restores exact blank bytes. Byte corruption, Flush failure, rollback failure, media removal, and partition-identity failure are also covered. Unverifiable rollback is reported uncertain.
- High FAT entries and a chain crossing distinct FAT sectors: `65525 -> 120001 -> 250000 -> EOC`, mirrored updates, reserved high-nibble preservation, FSInfo-hint allocation, cycle detection, and the final data cluster.

The independent host verifier seeks to GPT, BPB, FSInfo, FAT, directory, and file structures on demand; it does not load a whole image or FAT. It verifies both GPT copies/CRCs, primary and backup FAT32 structures, relevant mirrored FAT entries, root directory, bounds, and expected file content.

## QEMU proof and write/rollback metrics

The canonical runner is `scripts/run-dm22-qemu-large-fat32.ps1`. It creates a 10 GiB raw file by creating the file, setting the Windows sparse flag with `fsutil`, and extending its logical length with `SetLength`; it records both logical and allocated size. The image was attached only as a disposable secondary disk on Q35's ICH9 AHCI port 1. The boot disk was on port 0, and the manifest records no host physical disks passed to QEMU. QEMU was version 11.0.0.

The GPT partition is 10,736,352,768 bytes (20,969,439 sectors). It formatted with 512-byte sectors, 64 sectors per cluster (32 KiB), 327,566 data clusters, and 2,560 sectors per FAT copy (5,120 total FAT sectors). Against those 5,120 FAT sectors and a 20,969,439-sector partition, the formatter wrote only 7 sectors and retained 56 bytes of rollback records. The host image remained sparse: 10,737,418,240 logical bytes and 655,360 allocated bytes at final inspection.

The production AHCI lifecycle initialized GPT, created and formatted the >8 GiB partition, mounted FAT32, created a directory at cluster 120,001 using the FSInfo hint, wrote/read a 96 KiB file whose chain spans clusters 120,002–120,004, unmounted/remounted and reread it, rebooted QEMU, rediscovered and explicitly remounted the filesystem, and reread the exact payload. The independent verifier passed, including FAT mirror checks at high cluster addresses. This is the largest DM22 FAT32 volume proven in QEMU; 32/64 GiB were checked by pure geometry, not runtime.

Evidence is under `out/dm22-qemu-large-fat32-final/`: `dm22-manifest.txt`, `disk-inspection.txt`, `first-boot.serial.log`, `rediscovery-boot.serial.log`, `image-allocation-final.txt`, and the isolated production/diagnostic/USB build logs.

## Transport regressions and build status

- **AHCI:** A fresh 600 MiB normal-size lifecycle passed with GPT initialization, partition creation, FAT32 format/mount, file write/read, unmount/remount, restart rediscovery, and independent GPT/FAT32 verification. The first attempt with the legacy 300-second deadline expired during the required zero scan; the repeat used an 1,800-second first-boot deadline and passed. Evidence: `out/dm22-ahci-600m-regression/`.
- **ATA:** Five of five fresh 68 MiB images passed restart rediscovery and independent verification. A first 600 MiB attempt hit the legacy 300-second first-boot deadline while scanning the entire blank partition; the smaller 68 MiB retry is separate evidence, not a claim that the 600 MiB attempt passed. Evidence: `out/dm22-ata-68m-repeatability/` and `out/dm22-ata-regression/`.
- **USB read-only:** Current-source UHCI mounted a preserved 600 MiB FAT32 reference read-only, read its deterministic file, unmounted cleanly, and reached full boot. SHA-256 was identical before/after: `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`. Evidence: `out/dm22-usb-readonly-reference/`.
- **USB writable:** The current-source private write/read/restore proof, adjacent-sector canaries, common callback, and 100 write/Sync Cache/read/restore cycles passed; GPT initialization and partition creation also passed on fresh blank QEMU images. The TCG full-lifecycle attempt was stopped after about 20 minutes in the mandatory blank scan; it had no format marker and its automatic trace had grown to about 400 MiB. A fresh WHPX retry passed initialization and partition creation, then was stopped after about 17 minutes still in the full blank scan. Its UHCI trace showed successful 64-byte packet progress and no filesystem/transport error, but no format marker. These are recorded as incomplete, performance-limited attempts, not as successful writable FAT32 lifecycles or as USB timeout classifications. Evidence and post-stop image hashes are under `out/dm22-usb-regression-tcg/` and `out/dm22-usb-regression-whpx/`. An earlier WHPX attempt reported `Secure random data is unavailable` during Initialize Disk despite the QEMU VirtIO RNG ready message; its image remained unchanged and forensic record is in `out/dm22-usb-regression/`. The historic DM21 CSW timeout remains unclassified; DM22 did not reproduce or explain it.
- **NVMe:** Remains read-only. Production Makefile defaults are `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; suite coverage verifies registration/readability with absent write/Flush callbacks and rejects destructive preflight. No writable NVMe work was done.

AMD64 production and diagnostic kernels, the AHCI proof kernel, the USB proof kernel, UEFI x64 Release, and the DM22 read-only USB proof kernel build passed. The final host test/classifier log is `out/dm22-qemu-large-fat32-final/storage-manager-tests-final-boundaries.log`. No physical host disk was passed to QEMU.

## Remaining limits and next phase

The safe blankness scan still reads the full partition and scales with volume size. Only 10 GiB received a real large-volume lifecycle; 32/64/128 GiB are geometry-only, and the 2 TiB minus 512-byte BPB ceiling has not been runtime-tested. Formatting and FAT32 partition mounting remain 512-byte-only. FAT allocation can be linear in the number of scanned clusters; chain traversal is limited to 65,536 links, and VFS file sizes remain 32-bit. FSInfo free-count stays advisory, free-space UI remains unknown, and repair/recovery, nonzero-media reformat, and asynchronous VFS are out of scope.

DM23 recommendation: **4Kn FAT32 and partition-view filesystem support**. It follows the formatter scalability work and addresses the remaining logical-sector-size limitation without expanding into advanced partition operations or writable NVMe.

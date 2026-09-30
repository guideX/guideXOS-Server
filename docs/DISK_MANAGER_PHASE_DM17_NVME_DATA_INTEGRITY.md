# Disk Manager Phase DM17 — NVMe Data Integrity

**Outcome A for disposable QEMU NVMe writes and the common Disk Manager lifecycle.** The unchanged DM16 scenario reproduced a successful NVMe completion alongside failed data verification and a raw-image mismatch. Isolated experiments tied that symptom to QEMU 11 Windows sparse/raw backing configurations. With a dense raw disposable namespace and QEMU writeback cache, single and multi-block writes, flush ordering, image restoration, common callbacks, and five independent cold-start lifecycle images passed. Production source defaults still leave NVMe write and Flush callbacks disabled; the verified callbacks are an explicit build opt-in.

## Starting repository state

- Repository: D:\dev\guideXOSServer_DiskManagerImprovements
- Branch: DISK_MANAGER_IMPROVEMENTS
- Starting HEAD: 822a675f92b050f1ba53ced723215f659bf6baa5 (DM16)
- Starting tracked worktree: clean.
- Cached branch divergence: ahead 2, behind 0. Fetch failed because GitHub SSH authentication returned Permission denied (publickey); upstream freshness is unknown.
- Existing untracked out/ evidence was retained. DM17 runs use distinct out/dm17-* paths. NVMe private proof disks are under E:\guidexos-dm17-proof-disks.
- No host physical disk was attached to QEMU.

## DM16 baseline reproduction and diagnosis

The unchanged DM16 private proof was rebuilt and rerun before changing the driver or test. It reproduced the data-integrity failure: guest read-back did not match the deterministic pattern, and independent raw-image inspection found bytes changed at an unrelated tail location. The failing boundary began at LBA 0x12bff7 and covered nine 512-byte sectors (4608 bytes). The driver issued this as an eight-sector/4096-byte command followed by a one-sector command; the QEMU trace showed the expected opcode, namespace, LBA, NLB, PRP1, and successful CQ status. A success completion alone did not establish that the backing image contained the requested bytes.

The baseline artifacts are in out/dm17-baseline-dm16 and out/dm17-baseline-trace. QEMU experiments compared sparse and dense raw files and cache modes. Sparse raw files reproduced the mismatch under directsync and writeback, while a dense raw image with writeback completed with matching guest data and raw sectors. This isolates the observed failure to the QEMU 11 Windows sparse/raw backing configuration used by the old proof. The evidence does not establish a controller or NVMe command-construction defect. The safe transport improvement from DM17 also rejects an internally inconsistent byte count and block count, and retains the one-page command bound.

The original DM16 registration remained read-only during baseline reproduction. All destructive experiments used a separately attached, disposable namespace whose identity was DefinitelyNotBoot.

## Command, DMA, queue, and completion evidence

The proof build records the actual 64-byte SQ entry, checks the bytes copied into the queue slot, logs the doorbell value, and pairs each submission with CQ CID, SQID, SQHD, phase, status code, and queue ownership before and after consumption. The DMA record includes the caller buffer virtual and physical addresses, aligned bounce buffer aliases, PRP1/PRP2, transfer length, and pre-submit data prefix. This confirmed that write bytes were copied caller-to-bounce before submission and that the PRP physical address translated back to the expected bounce page. Single-page commands use PRP1 and zero PRP2. Transfers larger than one page are segmented into correctly sized commands rather than represented by a missing second PRP.

Queue-wrap runs passed while preserving expected CID/SQID/SQHD and phase progression. No completion was treated as data-integrity proof: the gate required guest read-back and a paused-host inspection of the exact raw sectors. Host-side write and restore snapshots checked the requested region and adjacent sectors against their expected bytes.

The new command-logic validation requires a nonzero block count, an exact byte-count/block-count ratio, and a logical block size that is a power of two from 512 through 4096 bytes. The focused storage suite adds nine NVMe geometry, layout, and queue assertions.

## Private NVMe integrity and Flush gates

The passing private run is out/dm17-integrity-r5, attempt 11. It used QEMU 11.0.0, q35, NSID 1, a disposable dense 600 MiB raw image, and writeback caching. The image's initial and final SHA-256 matched:

987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE

The proof passed ten single-sector write/read/restore checks; 2-, 7-, 8-, and 9-sector requests; ten seven-sector cycles; source-buffer offsets 128, 3584, and 3968 relative to a page; a 256-sector (128 KiB) segmented transfer; and the namespace's last LBA, 0x12bfff. The nine-sector case crosses the one-page command boundary and verifies both generated segments. Each pattern was read back, restored, and re-read.

The private gate then passed 100 write → Flush → read → restore → Flush → read cycles. A flushed marker remained readable after a clean QEMU quit and a fresh QEMU start. There were 254 paused-host raw-region inspections (127 write snapshots and 127 restore snapshots), all matching the expected data and adjacent-sector canaries. Host inspection failures: zero. The full image was byte-for-byte restored.

Evidence includes private-write-boot.serial.log, private-write-boot.nvme-trace.log, dm17-host-image-inspection.txt, dm17-manifest.txt, and the per-region write/restore snapshots in out/dm17-integrity-r5.

## Common callback and full lifecycle gates

The common callback proof in out/dm17-common-write-stage-r3 installed and exercised the shared NVMe write callback against the disposable namespace, checked exact common read-back and restoration, and independently inspected 256 raw write/restore states. Flush remained disabled in that isolated callback-only stage; the separate private Flush gate had already passed. The lifecycle profile subsequently exercised the shared persistence path with explicit NVMe callback opt-in.

Five fresh dense 600 MiB secondary images completed Initialize Disk, GPT partition creation, FAT32 format, mount, directory/file write and read, unmount/remount, and a second-boot rediscovery/persistence check. Each image was independently inspected after the QEMU runs and its SHA-256 matched the runner's manifest:

| Attempt | Lifecycle | Rediscovery | Final image SHA-256 |
| --- | --- | --- | --- |
| 10 | PASS | PASS | 5E19E589146AF772E76B6A3C12B21E662ED67B70DBDEAEB37CF04E9810AFBC2D |
| 11 | PASS | PASS | 90688AA90CBA3A3778D02617B901E3AAEAFEC2F49D7139D060CC7401634BCCB7 |
| 12 | PASS | PASS | 58AF3CD7881AAA698874D9C7AE123192836FB5D61BE61B5B6B050F80D0117F8A |
| 13 | PASS | PASS | 1C68CAC69A2979A74CEBAC3AAE9EFB1088E02F2593621D76E5526E9CDD87DA6A |
| 14 | PASS | PASS | 6C198CB147ED6D184974FF9A44D2239A199EA500E4517B5AA996D8A05EF4151D |

The cohort summary, runner logs, manifests, independent verification captures, and dense raw images are under out/dm17-lifecycle-cohort-final. Earlier attempts and their failures remain preserved. E:-hosted lifecycle attempts whose raw files changed after QEMU shutdown were excluded from the count; the accepted cohort is the five D:-hosted images independently rechecked against their manifests.

## FAT32 verifier semantics

The lifecycle verifier previously required the primary and backup FSInfo sectors to be byte-identical and required the primary free-cluster hint to equal a full FAT scan. That was stricter than FAT32's advisory semantics. The primary FSI_Free_Count is a last-known hint and may be stale; the backup FSInfo need not be updated with the primary. The verifier now checks both signatures separately, range-checks the primary free-count and next-free hints, reports the actual free-cluster count, and continues to validate the backup boot sector, GPT copies, FAT mirror, directory, file contents, and payload. The same rule is used by the USB lifecycle verifier. See the [Microsoft FAT specification](https://www.scs.stanford.edu/~zyedidia/docs/_other/fat.pdf).

For an accepted image, the independent verifier reported primary free hint 76590 and actual free clusters 76588. The two-cluster difference is permitted by the advisory field semantics; the FAT copies and backup FSInfo signatures passed.

## Callback policy and supported boundary

NVMe write and Flush callback registration are independently guarded by GXOS_DM16_NVME_WRITE_PROVEN and GXOS_DM16_NVME_FLUSH_PROVEN. The kernel Makefile defaults both switches to zero and rejects Flush without write. The normal/default build therefore continues to register NVMe read-only and cannot pass destructive-operation preflight. The verified profile explicitly opts into both macros only after the private data and Flush gate, common callback proof, and full lifecycle cohort passed. No physical-device write behavior is inferred from disposable QEMU proof.

The driver remains serialized with one outstanding command per controller, bounded by the aligned 4 KiB DMA bounce page and a single PRP per command. Large common transfers split into page-sized commands. Namespace support remains limited to NSID 1 per controller; FAT32 remains 512-byte-sector-only; controller reset, command replay, asynchronous queues, and real-hardware write acceptance are outside this proof.

## Tests, builds, and transport regressions

- Storage-manager suite: 751 checks, 0 failures (DM16 baseline: 742 checks), including 247 USB checks. Final log: out/dm17-storage-manager-tests-final.log.
- Private NVMe data integrity: PASS; 100 Flush cycles and restart read-back: PASS; byte-for-byte raw-image restoration: PASS.
- Common NVMe write callback proof: PASS; isolated stage intentionally kept common Flush callback disabled.
- Full NVMe lifecycle: 5/5 fresh images passed with the explicitly enabled common write and Flush callbacks; lifecycle, cold rediscovery, and independent raw-image verification all passed.
- ATA lifecycle regression: PASS, including cold restart and independent image inspection. Evidence: out/dm17-ata-regression-final.
- AHCI lifecycle regression: PASS, including restart rediscovery and independent image inspection. Evidence: out/dm17-ahci-regression-final.
- Writable USB lifecycle regression: PASS on a fresh 80 MiB blank image. Initialize, partition, FAT32 format, GPT isolation, VFS file read/write, ten mount/write/read/unmount cycles, cold restart, and independent GPT/FAT32/file verification all passed. Initial blank SHA-256: 33A3A11D54DE8EDE604C243CEDFDE1EF4B534D5EA3279C9DD57DF314045C23DF. Final SHA-256: 4594030456F661BBA7AC344842F3CF9AEE4F0E0467A85A4E4ABB0EDBCC024CD8. Evidence: out/dm17-usb-writable-regression and out/dm17-usb-writable-regression.raw.
- Read-only USB regression: PASS on the 600 MiB reference image; deterministic FAT32 file read and clean unmount passed, and its before/after SHA-256 remained EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE. Evidence: out/dm17-usb-readonly-regression.
- Explicit NVMe write-plus-Flush AMD64 profile build: PASS with GXOS_DM16_NVME_WRITE_PROVEN and GXOS_DM16_NVME_FLUSH_PROVEN enabled. Build log: out/dm17-amd64-proven-profile-build.log; preserved binary: out/dm17-nvme-write-flush-proven.elf.
- Default AMD64 production kernel build: PASS with both NVMe callback switches absent, leaving NVMe read-only. Build log: out/dm17-amd64-readonly-profile-build.log; preserved binary: out/dm17-nvme-readonly-default.elf.
- UEFI x64 Release bootloader build: PASS. Log: out/dm17-uefi-x64-release-build.log.

## Outcome and next work

DM17 reaches Outcome A for disposable QEMU NVMe integrity and the common Disk Manager lifecycle. Keep the shipping/default kernel profile read-only. Release profiles may explicitly set both proof switches after confirming that the build inputs are the verified revision. The original DM16 symptom is reproducible only with the sparse/raw QEMU backing setup used in that baseline; the dense/writeback profile passed byte-level checks and cold restart.

DM18 should address FAT32 scalability and safe rollback architecture beyond the existing approximately 3.66 GiB formatter ceiling. Retain the current conservative geometry and rollback limits until larger-volume behavior is designed and independently validated.

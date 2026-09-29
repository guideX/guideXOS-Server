# Disk Manager Phase DM16 — NVMe Flush and Durability

**Outcome C: NVMe private writes failed runtime data verification.** The driver now has serialized per-controller command ownership, strict completion validation, bounded PRP transfers, status diagnostics, and a no-data Flush command. The real QEMU write → Flush → read-back → restore gate returned success status for its commands but produced unrelated guest text instead of the canary, and the raw image did not return to its original hash. NVMe shared writes and Flush registration are disabled in the production build.

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `529aa8324359f75e0136dd74be06538410814168` (`Disk manager DM15 AHCI storage`)
- Starting tracked worktree: clean
- Starting cached divergence: ahead 1, behind 0
- Fetch: failed because GitHub SSH authentication was denied; upstream freshness is unknown.
- Existing untracked `out/` evidence was preserved. DM16 evidence is untracked under `out/dm16-*`; private raw NVMe images were placed on `E:\guidexos-dm16-proof-disks` because `D:` was nearly full during the proof attempts.

## Initial NVMe audit

The previous NVMe driver discovered PCI class `01/08/02`, parsed BAR0, mapped its register window, read CAP/VS, configured CC/CSTS, created admin queues, issued Identify Controller/Namespace, and registered NSID 1. It did not provide a common Flush callback or trusted persistence. Its single shared queues and command storage had no per-controller lock or explicit outstanding-command owner; CQ polling accepted insufficiently validated completion state. Its transfer path used a single PRP without a bounded DMA bounce page or safe multi-page split. It had no useful failure quarantine model.

The DM16 implementation now uses a serialized single-outstanding command per controller, bounded lock acquisition, command-ID ownership, CQ phase/CID/SQID/SQHD checks, deterministic zero-data Flush construction, raw status/SCT/SC diagnostics, bounded read/write/Flush deadlines, aligned one-page bounce transfers, controller quarantine on timeout or corrupt completion, and no ambiguous-write replay or reset. Identify geometry, MDTS, BDF, model, serial, firmware, VWC state, and NGUID/EUI64 are retained. The current safe profile still registers only namespace 1 per controller. NVMe 512-byte and 4Kn transport geometry are parsed; the existing FAT32 path remains 512-byte-only.

The old design's Flush blocker was not just the lack of a Flush opcode. The shared queue had no unambiguous command ownership or serialization, so a Flush could not safely share its command/CQ path with reads and writes. DM16 fixes that architectural issue in the driver and tests. It does **not** clear the runtime transfer blocker described below.

## Flush and persistence policy

Flush uses opcode `00h`, the discovered namespace ID, no PRP/MPTR, a unique queue-owned CID, the ordinary I/O SQ/doorbell/CQ path, and a longer bounded timeout. It is successful only after the expected CQE passes phase, CID, SQID, SQ head, and status validation. Invalid/failed/ambiguous completion revokes the controller registration rather than claiming durability. The no-data Flush and VWC policy follow the [NVMe Base Specification 2.0e](https://nvmexpress.org/wp-content/uploads/NVM-Express-Base-Specification-2.0e-2024.07.29-Ratified.pdf); the disposable controller setup follows [QEMU's NVMe device documentation](https://www.qemu.org/docs/master/system/devices/nvme.html).

Identify Controller VWC is recorded. When VWC bit 0 is clear, the driver classifies volatile cache as absent and does not need Get Features; when present, it reads the Volatile Write Cache feature and records enabled, disabled, or unknown state. FUA is not used. The intended contract remains write(s) → Flush → trusted durability. A VWC/Flush success does not override the required real-data validation gate.

Common NVMe write and Flush callbacks are guarded by `GXOS_DM16_NVME_DURABILITY_PROVEN`, which is not defined in the production build. The active production registration has `writeFn=null`, `flushFn=null`, and `flushSemanticsKnown=false`; it stays read-only and cannot pass destructive preflight. The internal private proof entry points exist only in proof builds. The guard must not be enabled until the DMA mismatch is fixed and the complete private gate passes.

## Private QEMU proof

The runner boots from a separate IDE-backed isolated ESP and attaches only a disposable raw file to a QEMU NVMe controller (`q35`, QEMU 11.0.0, serial `GXOSDM16NVME`, namespace 1, 512-byte logical blocks). The candidate was PCI `00:02.0`, model `QEMU NVMe Ctrl`, namespace size `0x12c000` sectors. The UEFI boot path identified the IDE boot device separately; the NVMe candidate was `DefinitelyNotBoot`. No host physical disk was attached.

The latest direct-synchronous run is `out/dm16-private-nvme-20260929-r15/`. The attached image is `E:\guidexos-dm16-proof-disks\dm16-secondary-privatewrite-15.raw`. Its initial SHA-256 was `987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE`; after QEMU exited it was `907D614AA879C1FCEE0F703EF37F6BD0C2A961438966EC859C224A2DB0F75FF9`. It was not restored.

The first private cycle attempted nine sectors across the 4 KiB command boundary. QEMU returned success status for write, Flush, read, restore, restore Flush, and restore read, but the first read did not match the canary; the cycle failed and no stress cycle counted. The serial trace and HMP snapshot show:

- WRITE opcode `01h`, NSID 1, CID `0x16`, LBA `0x12bff7`, NLB 7.
- PRP1 `0x3a9f7000`, PRP2 zero; the 4 KiB canary was present at the guest physical and kernel virtual buffer before submission.
- The queued 64-byte command at `0x3a9f4580` contained those expected fields.
- Read-back returned bytes `74 44 6f 63 6b 65 64 3a 3a ...` (`tDocked::...`) instead of the canary. The final target sector also contained unrelated guest text.

This isolates the failure beyond command construction: the queued command and pre-submit DMA bytes match the intended request, but runtime read-back is incorrect. The exact DMA/controller cause is not yet established. Earlier non-direct-synchronous runs that reported 100 guest cycles were rejected because the host image hash changed; they are not accepted evidence.

Evidence includes `private-write-boot.serial.log`, `private-write-boot.dma-snapshot.txt`, `kernel-build-privatewrite.log`, `dm16-manifest.txt`, `failure.txt`, and `post-shutdown-image-evidence.txt` in the run directory. `out/dm16-private-nvme-20260929-r2` through `r14` preserve intermediate attempts and their logs. Each is retained; none replaces the failed final gate.

## Lifecycle and safety results

The latest proof registered the namespace and read the blank tail successfully before the private write. It parsed the target as uninitialized. The private write then failed on its first cycle. Therefore DM16 did **not** enable the common callback, run destructive preflight, initialize GPT, create a partition, format FAT32, mount VFS, write a file, remount, restart, or run a lifecycle image through the independent GPT/FAT32 verifier. The failed private image is not a valid lifecycle image.

No controller reset/replay was attempted. On an I/O timeout, fatal controller state, or corrupt completion, the driver quarantines the exact registration; submitted writes remain uncertain and are not retried. A successfully completed write followed by unverified Flush cannot be classified Durable. Namespace registration generation remains the common block registry's lifetime authority.

## Tests, builds, and regressions

- Storage-manager suite: **742 checks, 0 failures**, including **247 USB checks**. The DM16 NVMe logic adds deterministic checks for queue ownership, CID/completion validation, zero-data Flush construction, geometry, PRP/MDTS limits, VWC, persistence, and write ambiguity. Log: `out/dm16-private-nvme-20260929-r15/storage-manager-tests.log`.
- AMD64 production kernel: passed with the durability-enable macro absent.
- UEFI x64 Release: passed.
- ATA fresh-image lifecycle: passed Initialize/Create/Format, VFS round-trip, unmount/remount, cold restart, and independent image verification. Evidence: `out/dm16-ata-regression/`.
- AHCI fresh-image lifecycle: passed Initialize/Create/Format, VFS round-trip, unmount/remount, restart rediscovery, and independent image verification. Evidence: `out/dm16-ahci-regression-lifecycle/`.
- USB writable: the 100-cycle private write/Sync Cache/read/restore proof and low-level shared callback proof passed. One TCG lifecycle attempt was stopped cleanly after it reached GPT partition creation but remained in the full-device zero scan; that partial run is retained under `out/dm16-usb-lifecycle-run/` and is not counted. The WHPX rerun passed Initialize, Create Partition, FAT32 Format, mount, file write/read, unmount/remount, 10 VFS mount/write/read/unmount cycles, cold restart persistence, and the independent GPT/FAT32/file verifier. Evidence: `out/dm16-usb-lifecycle-whpx-run/`; raw image: `out/dm16-usb-lifecycle-whpx.raw`. Hashes: blank `2DF1381A5F3A765B5A90CA39CC9793CC42BE473FBA459D1A5B64E1B49C04EA9D`, final `0203F5FC672F08BCFC4AE913B2A712E20794D6F21FE79DE5739C7D5DCEC7C88A`.
- USB read-only reference: QEMU reads passed, private/common writes were blocked, and the image remained byte-identical. Evidence: `out/dm16-usb-readonly-proof/`; image `out/dm16-usb-readonly.raw`, before/after SHA-256 `2DF1381A5F3A765B5A90CA39CC9793CC42BE473FBA459D1A5B64E1B49C04EA9D`.
- NVMe private write/Flush/read/restore: **failed first cycle**; 0/100 cycles accepted. Host image restoration failed.
- No regression test attached a host physical disk.

## Remaining limitations and next work

NVMe writes, Flush, trusted persistence, and destructive eligibility remain disabled. The immediate follow-up is to determine why QEMU's controller-side DMA transfer does not return the pre-submit guest buffer contents despite the correct queued PRP and command. Add a focused command/physical-memory DMA regression, fix and rerun the private write/Flush/read/restore gate, then run the full lifecycle and cold-restart verifier before enabling common callbacks. Continue to keep unknown boot provenance fail-closed. Namespace support remains limited to NSID 1 per controller; FAT32 remains 512-byte-only.

DM17 should first resolve and validate the NVMe DMA defect. Defer storage-capability expansion until NVMe cannot corrupt disposable proof data and passes the private durability gate.

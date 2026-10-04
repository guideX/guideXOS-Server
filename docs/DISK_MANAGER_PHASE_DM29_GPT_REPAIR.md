# DM29 — GPT Redundancy Repair

**Outcome B.** Both repair directions pass deterministic 512-byte and 4Kn tests and a real 512-byte AHCI/QEMU runtime proof. QEMU exercised the production repair service in each direction, restarted after each repair, and passed independent before/after image verification. The authoritative copy remained unchanged, each repair published its array before its header, and each operation required two Flushes. Outcome B remains because the QEMU fixture has one partition, 4Kn repair is deterministic-only, fresh ATA and USB transport regressions were not rerun, and the Disk Manager GUI was not clicked through.

## Phase gate and starting state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `10f5a1d6d37b4b4a0b3930ba5b7610c3d85d0116`
- Starting `.phase`: `project=guideXOS_SERVER_DISK_MANAGER_IMPROVEMENTS`, `last_completed=DM28`, `next_expected=DM29`
- The local phase history and implementation status contained no completed DM29 result.
- The existing worktree already had a user-owned `.gitignore` modification and untracked `kernel/out/`; both were preserved. Existing ignored/untracked `out/` material was left in place.
- Cached ahead/behind before work: `1 / 0` relative to `origin/DISK_MANAGER_IMPROVEMENTS`.
- `git fetch` failed with `git@github.com: Permission denied (publickey). Fatal: Could not read from remote repository.` Upstream freshness is unknown. No credentials or remotes were changed.

## Copy-state model and parser behavior

Each GPT side has its own state: `Valid`, `NotPresent`, `HeaderInvalid`, `HeaderCrcInvalid`, `ArrayInvalid`, `ArrayCrcInvalid`, `BoundsInvalid`, `Unreadable`, `Unsupported`, or `GeometryMismatch`. A CRC failure never makes a copy authoritative. The normal parser only selects an authority when exactly one side fully validates and the peer is in a narrowly repairable state.

When both sides validate, the parser compares location-independent header semantics (disk GUID, usable range, entry count and entry size) and then compares every CRC-covered byte of the complete entry arrays. This includes unused entries. It does not select primary over backup. A CRC-valid split-brain array, a different usable range, or another semantic disagreement is `GPT conflict | Manual recovery required`; neither partition list is exposed. An unreadable comparison also withholds the layout because agreement could not be proved.

When exactly one side validates, its partitions remain available for read-only inspection. Create and Delete are rejected until repair. Format and Quick Reformat require a healthy, equivalent GPT pair and are rejected on degraded GPT. Initialization remains the existing raw-disk-only operation. Both-invalid GPT is shown as `GPT damaged | No authoritative copy`; it is not offered automatic repair. Device-size/peer-LBA mismatch, unsupported layout, bounds-invalid metadata, unreadable media, and structurally invalid arrays also fail closed.

Protective-MBR presence and validity are diagnosed separately. Repair does not rewrite LBA0. If the MBR is independently invalid, the GPT copy repair can complete while that separate MBR defect remains visible; the result is not presented as a healthy protective MBR.

## Authority, identity, and confirmation

The service accepts only one fully valid source and a peer classified as missing, header-invalid, header-CRC-invalid, or array-CRC-invalid. It rejects two invalid copies and both-valid conflicts. A valid source’s current/peer LBAs and array location are checked against the actual logical-sector geometry before repair. A peer reference that implies an older or different device size is not relocated automatically.

The user sees a separate confirmation naming the exact disk, capacity, logical-sector size, damaged side and state, valid source, partition count, repair direction, and 64-bit authoritative fingerprint. It states that only the damaged GPT array/header will be written, partition data will not be modified, the protective MBR will be preserved, and all partitions must remain unmounted.

The confirmation is bound to the exact block registration/incarnation, disk identity and geometry, expected registry generation, operation owner, disk GUID, damaged-copy classification, and authoritative fingerprint. The fingerprint is deterministic FNV-64 over geometry, registration ID, the complete logical-sector header, and the complete padded entry-array sectors. It is intended to reject stale state, not to serve as a cryptographic signature. Execution revalidates the target, lease, safety policy, GPT diagnosis, and fingerprint before the first write.

Repair requires known-unmounted volumes and blocks root backing. Boot backing and unknown boot provenance both fail closed. The current boot-provenance architecture cannot prove that metadata writes to a boot disk are safe, even though the partition layout would remain unchanged; DM29 therefore keeps the conservative restriction. The selected media must also be writable and have trusted durability/Flush semantics. NVMe remains read-only (`NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`) and cannot enter the repair path.

## Write and interruption model

The service uses a pinned exclusive storage-operation lease. It retains the authoritative header and full array in bounded static storage (`GPT_MAX_ARRAY_BYTES`, currently 128 entries at up to 512 bytes each). It writes only the damaged side:

1. Flush the device before modification.
2. Write the full padded damaged-side entry array and reread it byte-for-byte.
3. Revalidate identity and safety, construct location-adjusted destination header fields, and write the damaged-side header last.
4. Require trusted Flush after publication.
5. Reread and verify the repaired header/array, reread the untouched authority and LBA0, and run the ordinary parser to confirm two valid equivalent copies and identical partition identities.

No rollback copy of the already-invalid side is retained. This is an explicit forward-only recovery model: the valid source remains untouched, and interruption may leave the destination incomplete/invalid. The parser can still rediscover the valid source, and a fresh confirmation can retry. Deterministic tests cover failure/removal during array writing, header-write failure, authoritative-copy preservation, and successful retries after both interruption points. Stale fingerprints and replacement media with identical capacity are rejected without writing the replacement.

## Verification and regression results

- Storage manager suite: **975 checks, 0 failures**, including 249 USB checks. This preserves the 904-check DM28 baseline and adds 71 checks.
- GPT cases include healthy copies; both authority directions; bad signatures, header CRCs, array contents and array-CRC fields; both-invalid; CRC-valid split-brain arrays; peer geometry mismatch; read-only and NVMe; mounted-at-confirmation; boot and unknown boot provenance; stale fingerprint; replacement registration; header-last ordering; required Flush; interrupted array/header retry; and 512-byte and 4Kn sectors.
- A 10 GiB sparse FakeDisk repair read 307 logical sectors, wrote 33 sectors (16,896 bytes), and issued two Flushes. Three partition-data canaries (first, middle, final) and the PMBR stayed byte-identical. The sparse-sector diff was limited to the damaged side. The read count includes service and parser reads during diagnosis, execution, and final rescan; it did not scale with disk capacity.
- The existing deterministic 10 GiB Quick Reformat and partition-delete regression coverage passed within the same suite. Create, Delete, Initialize, Format, and Quick Reformat policy was audited; Create/Delete and filesystem format probes reject degraded GPT. Initialization retains its existing raw-only policy.
- `tests/verify_dm29_gpt_test.py`: **1 test**, exercising 512-byte and 4Kn fixture images. The independent host verifier reports PMBR hash/state, both header and array CRC results, disk GUID, partitions, semantic equality, and byte-level changed LBA ranges. It is suitable for future QEMU before/after images; no production-service image was exported from this run.
- 512-byte AHCI/QEMU: began with a fresh 600 MiB disk, initialized GPT and FAT32 through the existing lifecycle proof, and created a known file. With QEMU paused immediately before AHCI registration, host tooling corrupted one active-entry byte in the backup array. The guest reported `primary=Valid`, `backup=ArrayCrcInvalid`, then completed the explicit production repair from primary to backup. After cold restart, the disk was healthy and the known file remounted and read correctly. The same sequence then corrupted the primary array, repaired primary from backup, and passed a second cold restart.
- Both QEMU directions reported **33 logical sectors written (16,896 bytes) and two Flushes**. The production result confirmed the authoritative copy, partition identities, PMBR, and partition data were preserved. The independent verifier found normalized healthy copies after each restart and limited changed LBAs to the repaired entry-array sector; PMBR hashes were unchanged.
- QEMU artifacts, serial traces, QMP transcripts, healthy/degraded/repaired raw images, and verifier JSON are preserved under `out/dm29-qemu-runtime-20261004-bidirectional/`. The raw images are sparse and 600 MiB logical size.
- USB trace classifier regression: **7 tests passed**.
- Bare-metal `-fsyntax-only` compilation passed for `kernel_apps.cpp`, `gpt_repair.cpp`, and `partition_table.cpp`; compiler output contains existing unused-parameter, conversion, and reorder warnings elsewhere in the kernel.
- Captured logs and the local test executable are preserved under `out/dm29-*`, including `dm29-storage-manager-test-final.log`, `dm29-storage-manager-build-final.log`, `dm29-verifier-test-final.log`, `dm29-usb-classifier-test.log`, and `dm29-baremetal-syntax.log`.
- The QEMU runtime test covers the AHCI repair path with one partition. A multi-partition runtime repair, 4Kn QEMU repair, fresh ATA lifecycle, fresh USB writable/read-only/hotplug lifecycle, 100-cycle transport stress, and GUI click-through were not run. Deterministic suite coverage and historical DM28/DM24 evidence are not presented as fresh DM29 transport regressions.

## Remaining limitations and next phase

The production service is qualified in both directions at 512 bytes in QEMU and at 512/4Kn in deterministic tests, including active-entry array reconstruction and large-capacity behavior. A real 4Kn repair run, multi-partition QEMU repair, and fresh ATA/USB regressions remain outstanding; these are the reason for Outcome B. Initialize Disk is still raw-only, so a degraded disk must be repaired or handled by separate recovery tooling before reinitialization is available through that action.

Recommended DM30: **Disk Manager Operational Polish and Recovery UX**, including clearer recovery diagnostics and the remaining 4Kn/multi-partition runtime qualification before adding another destructive primitive.

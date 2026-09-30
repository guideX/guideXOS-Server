# Disk Manager Phase DM18 — NVMe Production Graduation

## Outcome

**Outcome C — keep NVMe read-only and do not accept the transport regression gate.** The NVMe matrix narrows the integrity difference to sparse raw backing in the current Windows/QEMU setup after a pre-Flush guest read, but the exact cause remains unresolved. Separately, three current-source writable USB proof runs hit a UHCI bulk-IN/CSW timeout; one retry did so with no other QEMU process running. Until that regression signal is explained, DM18 cannot be accepted as a no-regression graduation phase.

DM18 did not change production NVMe command construction, PRP handling, queue ownership, completion validation, Flush handling, or callback gates. Optional proof-only trace and proof-harness changes add visibility and an explicit pre-Flush read. The normal build continues to define `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` by default.

## Starting state and DM17 baseline

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `c452b891c7090e02ff8dd07bd3ef236492639b80` (DM17)
- Initial tracked worktree: clean. Existing untracked `out/` evidence was preserved.
- Cached upstream state at start: `0` ahead / `0` behind. A fetch failed because SSH public-key authentication was unavailable, so remote freshness is unknown.
- DM17 had passed private exact-data checks, 100 Flush cycles, a cold restart, and five fresh dense-image lifecycle runs under its explicit proof-enabled profile. DM17 kept default production NVMe read-only.

DM18 re-ran private proof work only. It did not run ordinary production Initialize, Create Partition, FAT32 Format, VFS file I/O, a production restart, or a five-image production cohort because the ordinary kernel is intentionally read-only. DM17's explicitly enabled lifecycle cohort remains historical evidence and is not counted as a DM18 production result.

## Host, image, and proof setup

The host is Windows on NTFS (`E:` for secondary raw images). QEMU and qemu-img are `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. Images are 600 MiB (`629145600` logical bytes), raw, all logical bytes zero at creation. A newly created file is marked sparse with `fsutil sparse setflag` for sparse cases; the runner sets its logical length to 600 MiB. Dense cases omit the sparse flag; `qemu-img info` reports the full logical size allocated. The sparse baseline has `qemu-img actual-size=0` before boot.

The disposable namespace is QEMU NVMe NSID 1 with the default 512-byte logical block size. The machine is `q35,usb=off`; the NVMe controller is `nvme,id=dm16nvme,serial=GXOSDM16NVME,drive=dm16secondary`. The target is LBA `0x12bff7`, nine sectors (4608 bytes), byte offset `629140992` / `0x257fee00`; the preceding-sector guard is LBA `0x12bff6`. No host physical disk is passed to QEMU.

The candidate canonical profile for continued NVMe qualification is:

| Setting | Value |
| --- | --- |
| Image | Raw, 600 MiB, logically zero-filled |
| Allocation | Dense / fully allocated |
| QEMU backend | `cache=writeback,discard=ignore,detect-zeroes=off` |
| AIO | QEMU default (no explicit `aio=`) |
| Machine | `q35,usb=off` |
| Controller / namespace | `nvme,id=dm16nvme,serial=GXOSDM16NVME,drive=dm16secondary`; attached drive exposes NSID 1, 512-byte LBAs |

The DM16/DM17 proof runner now defaults to this profile; sparse cases require the explicit `-SparseImage` switch, and matrix cases remain explicit. This is a repeatable **diagnostic qualification profile**, not a claim that the default production NVMe write path has been qualified. Alternate allocation, cache, discard, detect-zeroes, and AIO settings belong in the matrix runner.

## Allocation and cache matrix

The original 15-case matrix used the DM16 ordering (write, Flush, read, restore) and independently captured the full raw-image hash. It produced these results:

| Allocation | Cache / option | Guest proof | Host image | DM18 interpretation |
| --- | --- | --- | --- | --- |
| Sparse hole | `writeback`, ignore/off, default AIO | Pass, 100 cycles | Full hash restored in this original attempt | Later pre-Flush-read repeats did not reproduce this host result |
| Sparse hole | `writethrough`, ignore/off | Pass | Image restored | Historical matrix result |
| Sparse hole | `none` | Guest readback failed | Not a pass | Backend mode does not satisfy proof |
| Sparse hole | `directsync` | Guest readback failed | Not a pass | Backend mode does not satisfy proof |
| Dense | `writeback`, ignore/off | Pass, 100 cycles | Image restored | Pass |
| Dense | `writethrough`, ignore/off | Pass | Image restored | Pass |
| Dense | `none` | Guest readback failed | Not a pass | Backend mode does not satisfy proof |
| Dense | `directsync` | Guest readback failed | Not a pass | Backend mode does not satisfy proof |
| Dense | `writeback`, `aio=threads` | Pass | Image restored | Pass |
| Sparse hole | `writeback`, `discard=unmap`, `detect-zeroes=unmap` | Guest proof reached pass | Full raw hash changed; image allocated a 64 KiB range | Do not use this option combination as canonical |
| Sparse target preallocation | Four cache modes and detect-zeroes variant | Mixed earlier results | Not authoritative | Excluded: pre-boot independent reads found nonzero target/guard bytes |

The target-preallocated cases are invalid controls. The preallocator's immediate in-process read saw zeros, while the independent image inspector later found nonzero data before QEMU boot. Earlier PowerShell/.NET and Python preallocation attempts also disagreed across process boundaries. The runner now rejects any fixture whose independent pre-boot target or guard snapshot is nonzero. No conclusion about writes to a correctly preallocated hole is supported by those attempts.

## Matched pre-Flush-read runs

DM18 changed the private stress proof to perform an NVMe READ immediately after WRITE completion, before Flush, then Flush, read again, restore the original zeros, and verify. The current matched sparse/dense runs used the same target, pattern generator, 100-cycle proof, cache mode, discard and detect-zeroes settings, and QEMU version.

| Attempt | Image | Guest WRITE / pre-Flush READ / Flush / post-Flush READ | Clean QEMU exit and host result |
| --- | --- | --- | --- |
| 103 | Dense raw | All 100 cycles passed; cycle 1 reported `preRead=00 preMatches=yes flush=00 postRead=00 postMatches=yes` | Independent Python inspection found target and guard zero; image SHA-256 remained `987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE` |
| 104 | Sparse raw hole | Guest proof passed | Host target was nonzero after QEMU exit; target contents differed from zero |
| 105 | Sparse raw hole | Guest proof passed; same explicit pre-Flush read | Target hash after exit: `A62B08EB0A78DC97487EED5372B3331920C8B7887B97A3B9162C03F75B60FCB6`; full image hash changed to `415AF4743EA5CD90CF96B545CFCD53F37A86A639950769CB904AEA4AB3D37D27` |
| 106 | Sparse raw hole | All 100 cycles passed; cycle 1 again reported both reads matching and Flush status `00` | Independent target hash `90D30EC7E12336F2E59F518D7E12D5A86122A9F68EA3D5E1C12BA82F7D74D43E`; guard hash `06EDB3E575B5BCAABF008920B4D73FCED477702DC42D45FE7B6D92E3B92FC0CE`; full image hash changed from `987523E7780392E283B404990C4E84E580BC75C451138B0C86C4F81C296EEEBE` to `B26BFAE2E0B5B6653C3533C7A1ED2244A3446711CC37E6E5AF69EADCDBB6B322` |

Attempt 106 began with a zero target and zero guard, and `qemu-img actual-size=0`. After QEMU exited, `qemu-img actual-size=65536`; `fsutil sparse queryrange` reported an allocated range at offset `0x257f0000`, which contains the target and guard. This shows allocation growth but does not identify why the bytes differ. The observed image is NTFS sparse raw; no claim is made that sparse behavior on all filesystems or QEMU versions is the same.

## Guest and QEMU trace evidence

The guest-visible result in attempts 104–106 is distinct from the later host raw-image result. WRITE, the immediate READ, Flush, the post-Flush READ, restore, and restore verification all completed successfully in the guest. The 100-cycle private proof emitted `private-write-flush-stress=PASS cycles=0x64 restored=yes`. QEMU was then stopped with QMP `quit` and exited normally; this was a clean QEMU process shutdown, not a guest OS shutdown or a restart-persistence test.

Focused QEMU tracing and paused HMP snapshots showed the expected command and source data. In attempt 105, the pattern WRITE command snapshot SHA-256 was `6EBD7FDF28B7159DD37E16922780945F47E32F30C01F7CBE53F4D2437B2CEBC0`; it targeted NSID 1, SLBA `0x12bff7`, NLB 7 (eight blocks), PRP1 `0x3a9f7000`, and 4096 bytes. Its PRP source-page hash `24E772E8F9947CAA09C79BE6654DA18BC56AAB78312FF3870CF660CF9EDD4C88` matched the expected pattern. The restore WRITE snapshot source page was all zero (SHA-256 `AD7FACB2586FC6E966C004D7D1D16B024F5805FF7CB47C7A85DABD8B48892CA7`).

The trace records matching NVMe WRITE/read requests and QEMU block backend operations at the expected offsets and lengths, including 4096 bytes at offset 629140992 and the final 512-byte sector at offset 629145088. Completion callbacks reported success. Thus the available trace does not show a wrong guest command, NSID, LBA, PRP payload, or backend offset. It also does not prove that the host filesystem persisted the intended zeros after QEMU exited.

## Root-cause assessment and production decision

The evidence narrows the symptom to the sparse raw path under the current host/QEMU configuration: sparse attempts with the explicit pre-Flush read repeatedly passed guest checks but failed post-exit host-image verification, while the matched dense writeback attempt passed. The original sparse writeback matrix run without the new pre-Flush read had passed and restored its hash. Therefore the changed proof ordering is also a possible interaction; allocation style is correlated with the current failure but has not been proven as the sole cause.

The preallocation helper's cross-process disagreement is an additional host-side fixture problem. It invalidates every preallocated-target result in the original and corrected matrix. The `detect-zeroes=unmap` case also failed to restore the full image and showed allocation outside the requested target range; the canonical profile disables detect-zeroes and ignores discard.

DM18 cannot distinguish conclusively among a QEMU raw-file backend interaction, Windows NTFS sparse-file behavior, a QEMU/host synchronization issue, or a guest-command-order interaction specific to the updated proof. Guest read-after-write and post-Flush integrity pass in these runs, but host bytes after QEMU exit do not. The root cause remains unresolved, so production promotion is unsafe.

## Callback status and requested production gates

- Default AMD64 NVMe read callback: enabled when the controller and namespace initialize successfully.
- Default AMD64 NVMe write callback: disabled (`NVME_WRITE_PROVEN=0`).
- Default AMD64 NVMe Flush callback: disabled (`NVME_FLUSH_PROVEN=0`).
- Default trusted persistence classification: unavailable; missing callbacks are not reported as durable.
- Proof-qualified builds: explicit write/Flush switches remain available for disposable diagnostic runs only.
- Ordinary production destructive preflight and Initialize: not run in DM18; default capability is read-only and cannot satisfy the required persistence gate.
- Create Partition, FAT32 Format, VFS file I/O, shutdown/restart persistence, and 5/5 production cohort: not run. DM17's explicit proof-profile results do not stand in for ordinary production results.

The storage suite retains its read-only and durability fallback checks, including initialization rejection for read-only devices and NVMe without a proven Flush path. A new focused descriptor test models default NVMe registration with read enabled and no write or Flush callbacks; it verifies unknown persistence, destructive-preflight rejection, and initialization rejection. Invalid namespace geometry and invalid commands remain rejected by the pure NVMe logic tests. DM18 did not change production callback registration policy.

## Builds, tests, and transport regressions

- Storage Manager suite: **752 checks, 0 failures**; includes 247 USB checks and the focused default-read-only NVMe fallback check. Log: `out/dm18-storage-manager-tests.log`.
- Normal AMD64 production kernel: **PASS**, separately built with `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0` into `kernel/build/amd64-dm18-production/bin/kernel.elf`; see `out/dm18-amd64-production-build.log`.
- UEFI x64 Release bootloader: **PASS**. The UEFI build wrapper was run with `-SkipKernel` because the production kernel was built separately. Log: `out/dm18-uefi-x64-release-build.log`.
- Dedicated private proof kernel: built and run by the DM18 private proof runner; its source enables the private proof hooks, not the default production write callbacks.
- ATA lifecycle: **PASS** on fresh attempt 18, including restart rediscovery and independent raw-image inspection. Evidence: `out/dm18-ata-regression/`.
- AHCI lifecycle: **PASS** on fresh attempt 18, including restart rediscovery and independent raw-image inspection. Evidence: `out/dm18-ahci-regression/`.
- Writable USB lifecycle: **not passed** in three fresh attempts. Attempt 1 hit a UHCI bulk-IN/CSW timeout during the 100-cycle private stress gate. Attempt 2 passed private 100-cycle write/Sync Cache/read/restore and reported destructive preflight Ready, then hit the same bulk-IN timeout during GPT initialization and reported metadata write failed. Attempt 3, with no other QEMU process running, hit the same timeout during the 100-cycle stress gate. All used WHPX. The uncontended repeat makes this a regression signal that blocks DM18 acceptance; causal attribution to the DM18 source changes remains unresolved. Evidence: `out/dm18-usb-writable-regression/`, `out/dm18-usb-writable-regression-r2/`, and `out/dm18-usb-writable-regression-r3/`.
- Read-only USB reference-image regression: **PASS**. Hash remained `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`; evidence: `out/dm18-usb-readonly-regression/`.
- Production NVMe lifecycle, restart, and cohort: not run because default NVMe remains read-only.

The 752-check suite retains FAT32 FSInfo semantics: primary and backup signatures are validated independently, and the primary free-cluster count remains advisory.

## Remaining limitations and next phase

No cross-process-consistent target-preallocation fixture has been established. There is no exact-sequence restart proof for the new pre-Flush-read test, and no ordinary-kernel NVMe destructive preflight or lifecycle. The observed sparse mismatch must not be generalized to every QEMU build, host filesystem, cache mode, or sparse implementation. No claim is made that the guest can detect arbitrary silent backing-store corruption during normal file operations.

DM19 should first reproduce and explain the UHCI bulk-IN/CSW timeout, then resolve the sparse backing / proof-ordering discrepancy with an independently verified fixture and repeatable host inspection. Rerun the transport regressions and NVMe read-only/promotion gates before moving to FAT32 scalability and rollback work.

## Evidence index

All raw test evidence remains untracked under `out/` and was preserved. Primary captures:

- Original cache/allocation matrix: `out/dm18-matrix/`
- Rejected target-preallocation audit: `out/dm18-matrix-prealloc-audit/`
- Dense pre-Flush-read attempt 103: `out/dm18-dense-preflush-writeback/`
- Sparse trace attempt 105: `out/dm18-sparse-payload-trace/`
- Sparse host audit attempt 106: `out/dm18-sparse-hostaudit-final/`
- Proof runner and matrix tools: `scripts/run-dm16-qemu-proof.ps1`, `scripts/run-dm18-nvme-matrix.ps1`, `scripts/preallocate_dm18_raw_target.py`, and `scripts/inspect_dm18_raw_image.py`

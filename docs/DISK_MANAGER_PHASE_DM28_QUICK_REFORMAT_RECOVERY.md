# Disk Manager Phase DM28 — Quick Reformat Recovery and Runtime Closure

**Outcome A for the DM28 gates.** The post-DM27 10 GiB AHCI runtime gap is closed with real elapsed-time and metadata accounting, the zero-scan assertion, and cold-restart byte verification. A real 4Kn USB removal at the destructive boundary left a durable interrupted marker; the same interrupted backing image cold-booted into explicit recovery, retried successfully, cleared the marker, and passed another cold restart. The final-source storage suite passes 904 checks. No host physical disk was passed to QEMU.

The optional interactive Disk Manager click-through was not run: the runtime proof boots use `-display none` and there is no configured GUI click harness for the guest. The app action-to-service wiring, rescan, interrupted-state controls, and confirmation-cancel call path were source-audited. The cancel path does not call a storage operation; no GUI write counter was captured.

## Phase gate and repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `ea83e6bc1949251f697f96ae71af04fb6bb318f2` (`storage: qualify DM27 quick reformat runtime`)
- Starting `.phase`: `project=guideXOS_SERVER_DISK_MANAGER_IMPROVEMENTS`, `last_completed=DM27`, `next_expected=DM28`; the gate passed. No DM28 report, implementation, or commit was present at the gate.
- Closeout `.phase`: `last_completed=DM28`, `next_expected=DM29`.
- Cached upstream divergence at the gate: ahead 0, behind 0. Fetch failed with `git@github.com: Permission denied (publickey).`; remote freshness is unknown.
- The pre-existing `.gitignore` user edit was preserved and excluded from the DM28 commit. Untracked `kernel/out/` and ignored `out/` proof/build evidence were preserved.
- DM27's remaining gates were real 10 GiB Quick Reformat runtime and timing, real removal during reformat, cold restart from the actually interrupted image, and Disk Manager/parser/cache behavior.

## Implementation and recovery contract

Quick Reformat remains an explicitly confirmed FAT32-to-FAT32 metadata replacement. It preserves the partition-table identity and extent, invalidates both old boot copies and Flushes before FAT replacement, initializes both full FATs and the full new root cluster, publishes the primary BPB last, verifies and Flushes, clears the retry record, then rescans. It does not zero ordinary file-data clusters and is not secure erase. The KnownZero blank-media formatter remains separate and still scans the complete partition.

The v2 `GXDM26RF` persistent marker binds the retry to partition scheme, start/count, sector size, partition number, GPT disk/partition/type GUIDs (or MBR identity), marker sector, GPT entry-array fingerprint, state, and checksum. Its point-of-no-return state is persisted and verified after old BPB invalidation. The marker is cleared only after the new filesystem has passed verification and durability steps. QRF proof builds expose bounded stage markers; the production build does not include the interruption pause.

VFS file and directory handles are now monotonic opaque `uint64_t` tokens rather than reusable slot numbers. APIs resolve a token to its live slot, so a closed reference cannot become valid when a slot is reused. The deterministic 4Kn reformat/re-registration test opens an old file and directory, closes/unmounts them, re-formats and mounts a fresh instance, then proves the old file read and old directory iteration return `VFS_ERR_INVALID` while fresh tokens and I/O work.

The Disk Manager's post-operation path calls `scanDisks()`, reparses partition tables, and rereads the BPB fields used for filesystem label and volume ID. An interrupted unknown-BPB partition is probed for the marker and shown as `Reformat interrupted`; Mount is only exposed for a probed FAT32 partition, while explicit Reformat retry remains available. The Reformat action reaches `quick_reformat_fat32_partition()` only through the explicit confirmation handler. Closing/canceling the confirmation dialog closes UI state and does not invoke a storage API or write the marker.

## Real 10 GiB AHCI Quick Reformat

The final-source proof used a disposable approximately 10 GiB QEMU secondary disk on Q35 ICH9 AHCI, with 512-byte logical sectors and a real FAT32 partition. The fixture was prepared by the current-source blank-format lifecycle and contained label `DM9PROOF`, a known volume ID, a directory, a small file, a deterministic 96 KiB multi-cluster file, and old-data canaries. The final QRF proof reused that preserved pre-reformat image and ran through the public probe/reformat service path used by Disk Manager.

The partition started at LBA 2048 and ended at LBA 21,004,254; the verifier confirmed its GPT identity and extent were unchanged. The label changed to `DM27FRESH`, and volume ID changed from `8BB8A986` to `CD4A188D`. Old paths disappeared; the new root contains the label and exact 98,304-byte `FRESH.BIN`; fresh file I/O, remount, cold restart, and independent verification passed. The image verifier found 201 changed sectors, all within permitted metadata/root/fresh-file regions, and confirmed both old file-data canaries were unchanged.

### Measured Quick Reformat work

| Measurement | Result |
|---|---:|
| Partition bytes | 10,754,195,456 |
| Logical sector size | 512 bytes |
| Sectors per cluster / cluster bytes | 64 / 32,768 |
| FAT sectors per copy | 2,564 |
| FAT bytes per copy | 1,312,768 |
| FAT1 / FAT2 bytes issued | 1,313,280 each (1,312,768 cleared each) |
| Reserved bytes written | 19,968 |
| Root-cluster bytes written | 33,280 |
| Total metadata bytes written | 2,679,808 |
| Read / write requests | 403 / 36 |
| Trusted Flush count | 5 |
| Elapsed time | 116 ticks at 100 Hz = 1.16 s |
| Effective write throughput | 2,310,179 bytes/s |
| `blankScanRequests` | 0 |

The separate KnownZero blank-format boot on this same 10 GiB class of media scanned all 10,754,195,456 partition bytes: 82,040 requests, up to 128 KiB per request, and 5,724 ticks (57.24 seconds). Quick Reformat wrote 2,679,808 metadata bytes and issued no full-volume blank-scan requests. These operations have different security properties: Quick Reformat leaves ordinary old data sectors untouched.

Two retained 600 MiB AHCI attempts were rejected before Quick Reformat: one reused the older 29-byte DM27 fixture while the current combined DM22/DM27 proof expects a 96 KiB `PROOF.BIN`; the other used a blank disk below the proof profile's 10 GiB size gate. The integrated 10 GiB boot then passed KnownZero format, QRF, and cold restart, but its runner invocation omitted the `-Dm22LargeProof` verifier-selection flag and ended at the independent verifier with a fixture-payload argument mismatch. The accepted QRF rerun used the exact pre-reformat image from that integrated blank-format boot with both proof flags; the runner invoked `--large-payload` and the independent verifier passed. These harness attempts are preserved separately and are not counted as failed storage operations.

## Real interruption, persistent state, and recovery

The proof used QEMU USB Mass Storage over UHCI with 4096-byte logical sectors and a disposable 640 MiB raw image. QMP removed the original USB device after `QRF_INVALIDATION_DURABLE`, synchronized at `QRF_FAT1_BEGIN`; QEMU acknowledged `DEVICE_DELETED`. The operation reported incomplete `ReformatInProgress` at `WriteFAT` and made no success claim. No retry was sent to the replacement registration. QEMU attached a different replacement image; its block statistics reported zero write operations and zero write bytes, and its SHA-256 stayed `152C359A60CC8F22238A15018E40FEF383829DAC2472E80DA6D35CBD5FF6C4E7`.

The independent interrupted-image inspection confirmed:

- GPT partition 1 and its extent (LBA 256–163,834) remained unchanged; both GPT copies and the marker's entry-array fingerprint matched. Disk GUID: `68176de6-b43d-4d2d-9afe-90b7ef6eedcc`; partition type GUID: `ebd0a0a2-b9e5-4433-87c0-68b6b72699c7`; unique partition GUID: `60a09cea-ab71-46e3-80b2-903d6916af12`.
- Both old primary and backup BPBs were invalid, so the incomplete volume was not mounted as healthy FAT32.
- Marker `GXDM26RF`, version 2, was present at relative sector 31 (absolute logical LBA 287 on the 4096-byte-sector device) in point-of-no-return state. The table fingerprint was CRC32 `7DEAB7A7`, and marker checksum was `86407611`.
- The pre-reformat volume ID was `41432D2C`. FAT1 and FAT2 were each still byte-identical to the old fixture (327,680 bytes each), and the root cluster was unchanged; the removal occurred after durable invalidation but before FAT1 clearing began.
- The interrupted raw image SHA-256 was `3FC182DEF2BFEFC373DEBD63342E323DBD611087670B19D33FE0D3572654D508`.

A full new QEMU boot used that same interrupted backing image (the recorded SHA-256 matched). Startup reconstructed `Reformat interrupted`, recorded marker version 2/sector 31/point-of-no-return, reported both BPBs invalid, kept auto-mount disabled, and exposed explicit retry. The retry reran destructive preflight and identity checks, completed both FATs, root, BPB publication and final Flush, then reported `markerCleared=yes`, `mount=PASS`, `freshFile=PASS`, `remount=PASS`. The volume ID changed to `B16E8F19` and label to `DM27FRESH`. A subsequent cold restart rediscovered the same volume and exact fresh file; independent verification passed, and its final image hash matched the post-reformat checkpoint.

Marker negatives are covered deterministically. The suite mutates the GPT disk GUID, table fingerprint (with a valid recomputed marker checksum), partition start, partition extent/count, unique partition GUID, and partition type GUID; each fails preflight without authorizing a write. Same-media device re-registration retains a valid retry marker. The 904-check suite also verifies marker clearing after success and durable-rescan-failure recovery.

## Parser, cache, and UI qualification

The formatter's success path calls `scanDisks()`. That scan rebuilds the in-memory disk/partition list from the current registration and parsed partition table, then `detectFs()` rereads the BPB and fills filesystem type, label, volume ID, and geometry. QEMU success and cold-restart serial evidence show the fresh label and volume ID rather than the old values. On interrupted media, the marker probe distinguishes the state from healthy FAT32; controls hide Mount and retain explicit Reformat retry.

The UI action path and cancel path were inspected in `kernel/core/kernel_apps.cpp`. `runFormatOperation()` dispatches Quick Reformat only after the explicit destructive-confirmation event; the Cancel/close path does not reach the formatter or write marker. The prompt's optional guest click-through was not run because the QEMU proof harness is headless and no guest GUI click automation is configured. Therefore zero writes on cancel is qualified by call-path audit, not by an interactive device write counter.

## Regression results

| Gate | Result and evidence |
|---|---|
| 512-byte AHCI Quick Reformat | PASS after the VFS-token change; 10 GiB QRF/cold-restart proof at `out/dm28-ahci-10g-qrf-post-vfs-token-verified/`. |
| 4Kn USB Quick Reformat | PASS after the VFS-token change through real interruption, explicit restart retry, fresh I/O/remount, and post-retry cold restart at `out/dm28-usb-interruption-post-vfs-token/`. |
| 10 GiB KnownZero blank format | PASS after the VFS-token change; full scan/format and ordinary file lifecycle are in `out/dm28-ahci-10g-integrated-post-vfs-token/`. Earlier independent blank-image verification is preserved in `out/dm28-10g-blank-format-regression/`. |
| DM25 AHCI partition delete | PASS in `out/dm28-dm25-ahci-delete-current-source/`; the independent verifier confirmed the deleted partition data remained unchanged. Deterministic regression also passes in the final suite. |
| ATA lifecycle | PASS in `out/dm28-ata-current-source/`. |
| Ordinary AHCI baseline | PASS in `out/dm28-ahci-baseline-current-source/`. |
| USB writable lifecycle | PASS in `out/dm28-usb-writable-lifecycle-current-source-retry/`, with restart and independent verification. |
| USB 100-cycle stress | PASS in `out/dm28-usb-100cycle-proof-v2/`; 100 write/Sync Cache/readback/restore cycles returned the disposable image to its original hash. |
| USB read-only reference | PASS in `out/dm28-usb-readonly-reference-current-source/`; mount/read/unmount left the reference image hash unchanged. |
| USB hotplug | PASS after the VFS-token change in `out/dm28-usb-hotplug-post-vfs-token/`; detach, stale file/directory/mount rejection, same-media reinsert, different-media replacement, interrupted write, and Sync Cache removal passed. |
| NVMe production gate | PASS: `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; production write/Flush callbacks remain gated off. Read-only fallback and destructive-preflight rejection remain in the deterministic suite. No writable NVMe workload was run. |
| Storage suite | PASS: 904 checks, 0 failures; 249 USB Mass Storage checks included. Seven USB trace-classifier tests pass. Evidence: `out/dm28-storage-suite-final/`. |

Several transport regressions above were run earlier in DM28 before the final handle-token hardening; the final-source 10 GiB AHCI QRF, real 4Kn USB QRF/recovery, USB hotplug, and full deterministic suite were rerun after that change. All raw-image runs used repository-owned disposable images; no host physical media was passed to QEMU.

## Builds and remaining limits

The final AMD64 production kernel, AHCI DM27/DM22 Quick Reformat proof kernel, 4Kn USB DM27/DM28 interruption proof kernel, and post-token DM14 USB hotplug proof kernel all built successfully. Build logs are under `out/dm28-production-kernel-build-retry.log`, `out/dm28-qrf-ahci-current-build-retry.log`, `out/dm28-qrf-usb-interrupt-current-build-retry.log`, and `out/dm28-dm14-current-build-post-vfs-token.log`. `git diff --check` passes.

Quick Reformat is not secure erase; old file data remains recoverable. No mid-reformat cancel was introduced; after the durable point of no return, recovery proceeds forward through an explicit retry. The interactive GUI click-through was not run, as described above. NVMe remains read-only. GPT repair remains out of scope.

## Recommendation and evidence index

DM28 closes its requested gates with **Outcome A**. Proceed to **DM29 — GPT Redundancy Repair and Recovery**; do not begin that work until the DM28 closeout commit is reviewed.

- 10 GiB QRF manifest, accounting serial, verifier, and cold restart: `out/dm28-ahci-10g-qrf-post-vfs-token-verified/dm28-10g-manifest.txt`, `rediscovery-boot.serial.log`, `disk-inspection.txt`, `cold-restart-boot.serial.log`
- 10 GiB blank-format runtime: `out/dm28-ahci-10g-integrated-post-vfs-token/first-boot.serial.log`
- Real USB interruption manifest, QMP transcript, interrupted-image inspection, retry serial, post-retry cold restart, and final verifier: `out/dm28-usb-interruption-post-vfs-token/`
- Final storage suite/classifier: `out/dm28-storage-suite-final/`
- Post-token USB hotplug manifest and serial: `out/dm28-usb-hotplug-post-vfs-token/`
- Current source changes and UI/VFS call-path audit: `kernel/core/fat32_formatter.cpp`, `kernel/core/vfs.cpp`, `kernel/core/include/kernel/vfs.h`, `kernel/core/kernel_apps.cpp`, and `kernel/core/qemu_dm9_storage_proof.cpp`

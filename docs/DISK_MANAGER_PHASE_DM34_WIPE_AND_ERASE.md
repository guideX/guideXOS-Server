# Disk Manager DM34: Partition Wipe and Full-Volume Erase

## Continuation 1 — host-only partition-wipe foundation

This continuation implements and qualifies the production engine for **Wipe Selected Partition**. It does not implement a GUI action, an entire-device erase, or secure sanitization. `.phase` remains `last_completed=DM33`, `next_expected=DM34`; this continuation does not close DM34.

## Terminology and scope

**Wipe Selected Partition** writes zeroes to every addressable logical sector in one validated partition. It preserves the partition-table metadata and every sector outside that partition. Existing filesystem contents in the selected partition are destroyed.

**Erase Entire Disk** is a separate, higher-risk operation that would overwrite every addressable sector, including all partitions, MBR/GPT metadata, the protective MBR, and primary and backup GPT structures. It is not implemented or enabled here, and cannot be requested by passing a whole-device range to the partition-wipe engine.

**Secure Erase** and **sanitization** do not describe this operation. A successful logical overwrite does not prove that remapped sectors, flash wear-leveling copies, or inaccessible device-internal storage were erased. No forensic-erasure claim is made.

## Source audit

| Capability | Production source and entry points |
| --- | --- |
| Disk and partition selection | `kernel/core/kernel_apps.cpp`: `DiskManagerApp::scanDisks`, `readPartitionTable`, `selectDisk`, `selectPartition`; snapshots are represented by `DiskEntry::identity` and `PartEntry::parsed`. |
| Validated MBR/GPT geometry | `kernel/core/partition_table.cpp`: `parse_partition_table`; `PartitionEntry` in `kernel/core/include/kernel/partition_table.h` carries start/end LBA and sector count. |
| Device capabilities and identity | `kernel/core/storage_manager.cpp`: `query_device_capabilities`, `capture_target_identity`, `target_identities_equal`, `revalidate_target_identity`, `valid_geometry`, `checked_lba_range`. |
| Checked block I/O | `kernel/core/storage_manager.cpp`: `write_sectors_safe`; `kernel/core/block_device.cpp`: `write_sectors_checked`, `flush_with_result`, and bounded operation diagnostics. |
| Exclusive destructive ownership | `kernel/core/disk_initialization.cpp`: `try_acquire_storage_operation`, `release_storage_operation`, `storage_operation_lease_is_current`, and `pin_storage_operation_target`. Format, Delete, GPT Repair, Initialize, and the new wipe job share this lease. |
| Registration pin and removal | `kernel/core/block_device.cpp`: `pin_device`, `unpin_device`, and `mark_device_offline`; a pinned removed registration remains an offline tombstone until release. A new registration receives a distinct ID. |
| Mount and boot protection | `kernel/core/storage_manager.cpp`: `query_partition_mount_protection`, `query_boot_protection`, and `query_partition_boot_protection`; `kernel/core/vfs.cpp`: `mount_partition_detailed`, `mount_identity_valid`, and `unmount`. Mount creation is blocked while a storage operation is active; unmount has no implicit wipe path. |
| FAT32 Format and Quick Reformat | `kernel/core/fat32_formatter.cpp`: `begin_fat32_format_job`, `step_fat32_format_job`, `cancel_fat32_format_job`, `format_fat32_partition`, and `quick_reformat_fat32_partition`. `KnownZero` is private to this formatter. |
| Cooperative UI operation pattern | `kernel/core/kernel_apps.cpp`: Disk Manager `update`, `runFormatOperation`, and `completeFormatOperation` advance and render the cooperative Format job. The wipe engine exposes its own begin/step/stop API and does not depend on a window. |
| Transport callbacks | `kernel/core/ata.cpp`, `ahci.cpp`, `usb_storage.cpp`, and `nvme.cpp` register the device-specific write and Flush callbacks described below. `kernel/makefile` defaults both NVMe proof flags to zero. |

## Safety contract

### Target identity and partition map

`PartitionWipeRequest` copies the selected disk identity, registration incarnation, registry generation, partition scheme, partition tuple, and MBR disk signature or GPT disk GUID. `begin_partition_wipe_job` pins that exact registration under the shared destructive-operation lease. Later UI selection changes cannot alter the job's copied request. The engine revalidates the same registration and reparses the authoritative partition map before every write batch.

Only a currently validated MBR or fully agreeing GPT map can authorize a write. Hybrid MBR, extended MBR, degraded/conflicting GPT, missing disk identity, stale partition geometry, unsupported scheme, or invalid partition identity is rejected. The current parser supplies the authoritative range; filesystem-reported geometry is not used. No write may include LBA 0, GPT metadata, a neighboring partition, or a guard sector unless that address is itself inside the selected map entry.

Range checks require a nonzero sector count, a representable inclusive end, a matching `endLba - startLba + 1` count, a range inside device capacity, and checked byte-size arithmetic. Each batch is whole logical sectors, capped by the 1 MiB aligned zero buffer and the device's declared maximum transfer. The last batch is shortened to the remaining partition sectors. The API does not accept an implicit whole-device extent.

### Protected and busy storage

Wiping is denied when the device is known to back boot storage or boot provenance is unknown. A partition-specific boot check is also performed against the validated partition table. The engine refuses MBR entries marked bootable and GPT EFI System, Microsoft Reserved, Windows Recovery, and BIOS Boot type GUIDs. This protects recognized system-storage markers even on media not currently identified as the boot device.

The selected partition must have a known, unmounted VFS relationship. A mounted, root-backed, stale, whole-device, or otherwise ambiguous mount is rejected. The engine does not detach filesystems or close handles. The shared lease rejects a competing Format, Delete, Repair, Initialize, or other destructive storage operation. An exact mount of a different partition can remain mounted while an unmounted peer is wiped; new mount creation is blocked for the lease duration.

### Write and durability eligibility

The production allowlist is ATA PIO, AHCI, and USB Mass Storage, and admission also requires the actual registered write callback plus established durability semantics:

- ATA PIO and AHCI must expose write support and a documented Flush callback. Their normal registrations do not claim synchronous-durable completion.
- USB Mass Storage must be writable and have a successful SCSI Synchronize Cache result. Without that result, capability derivation marks Flush support/semantics unknown and admission fails.
- NVMe is explicitly denied by the wipe engine even if a proof build supplies callbacks. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged.
- RAM disks are not in the production allowlist. Deterministic tests use `FakeDisk` memory through the real block and wipe paths.

The engine requires a known successful `flush_with_result` outcome after full coverage and after a Stop that follows writes. A non-removal write error also attempts Flush if the exact target remains online, to settle any prior successful writes. Removed or invalidated media is not addressed again. The engine does not perform a high-cost full readback in production; completion means the registered transport reported all requested writes and its established Flush/durability contract succeeded. Host tests verify the complete partition bytes independently. Future guest testing must stop the guest safely, then independently inspect the released image on the host.

### Cooperative writes, progress, Stop, and failure

`kernel/core/partition_wipe.cpp` implements `begin_partition_wipe_job`, `step_partition_wipe_job`, and `request_partition_wipe_stop`. Begin performs admission, captures totals, takes the lease, and pins the registration. One step performs at most one bounded write batch. The operation has explicit Not Admitted, Running, Stopped, Failed, and Completed states with separate failure reasons.

Progress advances only for successful full write callbacks or a transport's matching, bounded completed-sector diagnostic. It reports a contiguous completed prefix. If all sector writes finish but required Flush fails, progress remains at 99% and the result is Failed. Only durable full completion reports 100%.

Stop is honored at a safe batch boundary. Before any write it produces a stopped result with zero media changes and no Flush. After writes begin, it prevents another batch, attempts required Flush while the exact target remains valid, and reports the completed range. The result says that existing data may have been lost. A write error, device removal, invalidation, or failed required Flush produces Failed and never claims full completion. Failed LBAs, remaining sectors, block status/diagnostic, Flush outcome, successful sector/byte counts, `mediaMayHaveChanged`, target identity, operation ID, and ownership release are retained in the job result. The engine does not automatically resume a partial operation.

### Ownership and KnownZero

The operation owns the existing storage lease and exact device pin. Terminal paths release the lease once; the job retains its terminal state and result. Removal invalidates the old registration and stops further batches. A same-capacity replacement has a different registration and cannot receive the old job's I/O.

The wipe never creates or publishes a FAT32 formatter `KnownZero` token. A later Format performs its own independent scan and authorization. No reusable zero-coverage proof is necessary for this continuation.

## Deterministic host tests

The focused checks use the production wipe engine, `FakeDisk`, block registry, real partition parser, safety checks, checked block writes, Flush path, and storage-operation lease. The 512-byte MBR case compares every byte outside the selected partition, including MBR metadata, a neighbor partition, and guard sectors. The GPT case compares both metadata copies, the protective MBR, other partitions, and remaining sectors. A 4Kn case checks 4096-byte geometry, alignment, byte totals, and a 2/2/2/1-sector batch sequence.

Focused cases cover admission, read-only and NVMe rejection, ATA/AHCI/USB eligibility, zero and out-of-range extents, capacity arithmetic overflow, busy mounts, boot and GPT system partitions, duplicate ownership, immutable selection, final partial batches, Stop before and after writes, write and Flush failures, device removal, duplicate removal notification, same-capacity replacement, no post-invalidation I/O, and ownership cleanup. The test suite compares full in-memory contents, not only callback counters.

## Deferred GUI and runtime acceptance

The current Format dialog uses **Cancel** for the pre-publication operation and result wording such as **Format canceled. No filesystem metadata was written.** Wipe is destructive from its first write. The smallest consistent active-action wording is **Stop Wipe**; a stopped result with writes must say **Wipe stopped after partial overwrite. Existing data may have been lost.** The zero-write Stop result may say **Wipe stopped before any overwrite was attempted.**

The future confirmation should name the immutable disk and partition, show capacity, say that only the selected partition is in scope and that its partition table is preserved, explain that existing data will be overwritten, warn that Stop can leave partial destruction, and say this is not secure erasure. Use an explicit **Wipe Partition** action bound to the captured registration and partition identity, plus a clearly targeted Cancel control. Do not use an ambiguous generic Yes/No prompt. No wipe GUI is implemented in this continuation.

No QEMU or physical media was used. Future QEMU acceptance must exercise confirmation, progress repaint, Stop, full completion, failure/removal handling where safely injectable, and guest release; then independently verify the disposable image on the host. Those guest and GUI gates remain open.

## Entire-disk erase boundary

Entire-disk zero-overwrite needs a separate operation type and result contract. It covers LBA 0 through the last addressable logical sector, including every partition and all MBR/GPT structures and backups. It requires stronger target-bound confirmation and stricter boot-device protection. It must not be enabled by passing a synthesized partition entry or a whole-device range into this engine. Design and qualification remain for a later DM34 continuation.

## Qualification results

Starting state was branch `DISK_MANAGER_IMPROVEMENTS`, HEAD `a3a850c0eef5bb38612044aa4acbbc78cdbfdbaa`, `.phase` DM33→DM34, and a clean worktree.

- Focused wipe coverage: **17 checks, 0 failures**. Full storage manager suite: **1006/1006**, expanding the accepted 989-check baseline by the 17 focused cases. USB Mass Storage coverage contributed 249 checks within that total.
- Additional host checks passed: widget activation; USB trace classification **7/7**; GPT verifier **1/1**; DM30 fixture verifier **1/1**.
- Isolated AMD64 kernel build passed with `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`, producing `kernel/build/amd64-dm34/bin/kernel.elf` and `kernel.pe`.
- `build.bat` passed and produced `guideXOSServer.exe`.
- `build-uefi.ps1 -Arch amd64` passed the UEFI bootloader Release x64 build, AMD64 kernel build, and ESP setup. It checked QEMU prerequisites but did not launch QEMU.
- `build.ps1 -Arch amd64` stopped at the known unrelated PacMan Native ELF link errors for `pacman_audio_load_resources` and `pacman_audio_submit`. No PacMan source was changed; generated tracked PacMan objects were restored to the clean starting state.
- `git diff --check` passed. No QEMU guest or physical media was used. `.phase` remains `last_completed=DM33`, `next_expected=DM34`.

This is Outcome B — `DM34_ENGINE_READY`. The engine and host qualification are complete for this continuation; GUI confirmation, real QEMU erase acceptance with independent host-side image verification, and separately scoped entire-disk erase remain open. This does not close DM34.

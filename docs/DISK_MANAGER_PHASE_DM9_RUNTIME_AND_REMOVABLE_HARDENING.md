# Disk Manager Phase DM9 — Runtime Storage and Removable-Device Hardening

## Starting state and environment

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `ea7e9f14e8922a8c4da0cc4694c1e0f998ffa5ec`
- Starting worktree: clean. Local tracking state was 0 ahead / 0 behind `origin/DISK_MANAGER_IMPROVEMENTS`; an SSH fetch failed with `Permission denied (publickey)`, so remote freshness was unknown.
- The local history and DM1–DM8 reports were present. No unrelated work was carried into this phase.

QEMU 11.0.0 and `qemu-img` were installed under `C:\Program Files\qemu`; project QEMU scripts, OVMF firmware, and a working guideXOS ESP/runtime image were available. MinGW was installed at `C:\mingw64`; Visual Studio 2026 Community supplied MSBuild and the x64 bootloader toolchain. `cl.exe` was not on PATH. The repository bootstrap script for the pinned Mbed TLS 4.1.0 / TF-PSA-Crypto 1.1.0 dependency completed its integrity and profile checks. Dependency policy and version were unchanged. The full AMD64 kernel and UEFI bootloader built successfully.

The QEMU run used an isolated copy of the boot ESP and a newly created raw secondary image. No host physical disk was passed to QEMU or written. The guest was headless on serial; native GUI automation was unavailable, so this is a service/runtime proof and not a claim that the Disk Manager and File Explorer dialogs were clicked in QEMU.

## Outcome

**Outcome B — hardening improved, with a QEMU write-repeatability blocker still unresolved.**

The fake-device lifecycle and removal tests pass, and one complete Tier 2 QEMU run completed and survived a restart/remount. A later automated run against a fresh blank image reported that initialization rollback could not be verified, even though the image hash proved the failed attempt had not changed the image. The run does not expose the failing callback's status, so the underlying QEMU/ATA write failure is not diagnosed. It must be resolved before claiming repeatable runtime initialization.

## Tier 2 QEMU lifecycle evidence

A 600 MiB disposable raw secondary disk completed this service sequence on the first boot:

`Initialize GPT → Create Partition → FAT32 Format → Mount → mkdir → write/read → Unmount → Remount/read`

The harness required QEMU's ATA channel 0 slave, a flush callback, and `DefinitelyNotBoot` provenance. It uses the production storage manager, partition, FAT32, and VFS APIs. The primary master was detected as `DefinitelyBoot`; the secondary was `DefinitelyNotBoot`. The proof reports clean mounts after unmount.

The same image was then booted again. Rediscovery, explicit remount, byte-for-byte proof-file read, and clean mount state passed. No automatic mount was introduced. First-boot and restart serial logs are preserved in `out/dm9-runtime-proof/serial-600m-diagnostics.log` and `out/dm9-runtime-proof/serial-rediscovery-retry.log`.

An independent read-only Python verifier checked the preserved image in `out/dm9-runtime-proof/secondary-600m.raw`:

- Protective MBR; primary and backup GPT header CRCs; GPT entry-array CRC and agreement.
- One GPT Basic Data partition named `DM9 QEMU Proof`, from LBA 2048 through 1228766.
- The 1,031,168-byte leading gap remained zero-filled.
- FAT32 BPB, 512-byte sectors, 16 sectors per cluster, 76,593 clusters, `DM9PROOF` label, backup boot sector and FSInfo, and matching FAT copies.
- Root directory, `DM9` directory, and `PROOF.BIN` with the expected 29-byte deterministic payload.

The image SHA-256 is `B566BFEDAA8EE86FE4F6B37701D7FA834723AB95261560042C39E37D296E7B54`. Full verifier output is in `out/dm9-runtime-proof/disk-inspection.txt`; the image and serial evidence are intentionally left under `out/` and are not part of the source commit.

The first experiment used a 256 MiB blank secondary. GPT initialization and partition creation succeeded; FAT32 formatting correctly rejected the geometry because it could not meet the minimum FAT32 cluster count. That incomplete image is preserved as `out/dm9-runtime-proof/secondary-256m-incomplete-gpt.raw`.

The repeatable runner is `scripts/run-dm9-qemu-proof.ps1`. It creates its own new 600 MiB image beneath `out`, stages a copied ESP, checks serial failure markers, reboots the same image, and invokes the independent verifier. Repeated packaged-runner attempts reproduced an initialization failure before any write stage completed: `Rollback could not be verified; disk state is uncertain`. The latest run's initial and final image hashes match, proving no bytes changed. Its serial log and failure manifest are `out/dm9-qemu-audit-proof-3/first-boot.serial.log` and `out/dm9-qemu-audit-proof-3/run-result.txt`. The reported `flushStatus=0x06` is the result structure's reset default and is not evidence of the failing callback's status. The runner now records failure details and stops only the QEMU process associated with that run's serial log.

## Device lifetime and mount behavior

Block registration IDs are monotonic and not reused. Unregistration refuses a pinned registration. A physical-driver-style loss marks the exact registration ID offline; a pinned entry stays as an offline tombstone until its final unpin, and only then can its slot be reused. A no-media status also marks only the matching incarnation offline. Timeout and not-ready remain I/O states and do not by themselves unregister the device.

Partition views are bounded endpoints that pin the parent registration ID and generation. Each read, write, and flush checks the parent incarnation before invoking a callback. A reused registry slot or a reinserted device with identical backing bytes cannot redirect an old view to the new device.

VFS mount records retain the parent registration ID and exact partition identity. The new VFS errors distinguish device removal, not-ready, timeout, unsupported I/O, and stale mount identity. When the parent disappears, new operations fail as device removed; a stale view is not rebound. Open file and directory handles fail safely on later I/O and can be closed. An open object keeps unmount busy until it is closed. After closure, stale mount cleanup releases the view and removes the VFS mount record. A live flush failure keeps the mount for retry; loss during unmount flush permits stale cleanup.

The service tests cover removal during Initialize, Create Partition, and FAT32 Format. They assert no false success, no subsequent write callback after terminal loss, attempted rollback only when applicable, and an uncertain result when restoration cannot be verified. VFS tests inject no-media, not-ready, timeout, generic I/O, and flush failures. Read-directory distinguishes end-of-directory from a device error.

The DM9 suite also covers removal with open file and directory handles, removal during file write, removal during unmount flush, slot reuse by a different device, re-registering the same bytes as a new incarnation, and rejection by the old view before the replacement callback is reached. Existing and fresh mount state is compared through the VFS/Storage Manager model.

## Refresh and user-facing behavior

Disk Manager continues to use explicit Refresh/F5; no block-registry notification system exists. Refresh matches selections by stable registration ID and exact partition or gap identity, clears a selection when its device vanishes, and does not carry selection into a replacement incarnation. Unavailable mounts are counted and shown as unavailable; stale actions are disabled. The UI does not claim that an unknown device is removable. Current ATA, NVMe, and USB transports do not provide authoritative removable flags.

File Explorer's Refresh/F5 detects a stale mounted backing and returns navigation to `/`. Directory-enumeration errors are surfaced separately from normal EOF. Selecting a stale mounted-drive entry attempts explicit stale-unmount cleanup and refresh. The safe behavior is covered by model/service tests; actual QEMU GUI interaction was not available.

Errors map to user-readable messages including “Device was removed or is no longer available”, “Device is not ready”, “Storage I/O timed out”, “Mount is stale; parent identity changed”, and “Operation not supported”. Other callback failures map to generic I/O error. The UI does not show only raw numeric codes. Unmount is cleanup; it is not presented as safe-to-remove or eject.

## Transport audit

| Transport | DM9 finding |
|---|---|
| ATA PIO | Registered in the shared block registry with IDENTIFY geometry/model/serial and ATA channel/target plus PCI BDF when available. IDENTIFY word 83 gates ATA flush support. Exact boot-source matching can mark the QEMU secondary `DefinitelyNotBoot`. It is the only current real transport that passed a complete destructive/storage lifecycle, but the fresh-image initialization repeatability failure remains unresolved. |
| NVMe | Shared registration and BDF/namespace identity exist. There is no trustworthy namespace Flush path; one-page PRP1 transfers are capped at 4096 bytes and writes remain blocked by unknown durability. Hot removal is not wired. |
| USB mass storage | SCSI READ10/WRITE10 over BOT exists in a private USB storage layer, but it is not registered as a shared block device. There is no SYNCHRONIZE CACHE path or integrated shared-registry hotplug/removal lifecycle. |
| AHCI | No shared block registration or usable AHCI transport was found. Existing source describes future controller/FIS enumeration; this is a transport bring-up project, not a small registration-only change. |
| RAM / hosted image | RAM disks can register in the shared block layer but are volatile. The hosted image viewer uses read-only, volatile image-backed storage and does not expose host physical disks. |
| Partition block view | Not an independent transport registration. It is a bounded, generation-checked endpoint tied to the exact parent registration. |

No transport currently supports truthful eject or a general “Safe to remove” claim. DM9 adds no eject action.

## Stress and verification

`scripts/run-storage-manager-tests.ps1` passed **321 checks, 0 failures** (DM8 baseline: 312). The added deterministic coverage includes 100 registry register/unregister churn cycles with fresh IDs and 100 FAT mount/read/unmount cycles, plus loss/status/open-handle/replacement and operation-failure cases. Existing partition, identity, VFS, FAT, rollback, and destructive-operation safety checks remain included. Fake tests use in-memory devices only.

The full AMD64 kernel build and UEFI bootloader build passed after the repository-pinned Mbed TLS bootstrap. The QEMU proof kernel also built with `GXOS_DM9_QEMU_STORAGE_PROOF`. The independent verifier passed on the successful preserved image. No physical disk was used.

## Remaining limits and DM10 recommendation

- Fresh QEMU ATA PIO initialization is not repeatable yet; the failing write callback status is not currently surfaced by the operation result. Improve callback-stage/status diagnostics and reproduce the first GPT-array write/rollback failure before treating Tier 2 as stable.
- GUI interactions were not automated. No physical removable-media test was authorized or performed.
- No current transport publishes known removable status. No eject/safe-disconnect contract exists.
- USB MSC, AHCI, and NVMe hotplug/removal are not integrated into the shared block lifecycle. NVMe writes remain blocked on Flush durability.
- FAT32 VFS mounts remain 512-byte-sector-only. Larger/4Kn geometry, exFAT, filesystem repair, and journaling remain outside this phase.

**DM10 recommendation: Option A, transport reliability and lifecycle.** First make ATA PIO write/flush failure stage and status visible and make the QEMU secondary run repeatable. Then integrate one transport end to end, most plausibly USB MSC with durable flush and removal wiring if bounded; treat AHCI as its own bring-up otherwise. Defer filesystem/geometry expansion and advanced partition operations until runtime writes across real transports are reliable. DM9 adds no destructive operation.

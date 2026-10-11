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

## Continuation 2 — Disk Manager GUI and host qualification

Starting gate: `DISK_MANAGER_IMPROVEMENTS` at `beca18cab749cad327f123e9cdbda3cdf83ba22c`, commit subject `disk-manager: add DM34 partition wipe foundation`, clean worktree, and `.phase` `last_completed=DM33` / `next_expected=DM34`. The authoritative DM34 report showed that this GUI continuation had not already been implemented. The branch and phase marker were left unchanged.

### Production GUI integration

The implementation extends the existing `DiskManagerApp` in `kernel/core/kernel_apps.cpp`; it reuses the existing disk and partition rows, action widgets, textbox, modal, focused-button dispatch, progress bar, and cooperative `update()` loop. There is no second GUI framework or scheduler. **Wipe Partition** is shown only for a selected partition; a disk row and an unallocated range show the reason that a partition must be selected. The details pane reports the eligibility blocker for selected partitions. Admission requires the exact live device identity, validated MBR/GPT table and bounds, a supported write transport and known durability, read/write access, an unmounted and unprotected target, and an idle destructive-operation lease. NVMe is explicitly denied. The engine remains the authority at begin and before each batch.

The red destructive modal snapshots the selected device identity/incarnation, partition tuple, table scheme, MBR signature or GPT disk GUID. It shows device name, Disk Manager disk number, partition number, selected size and inclusive LBA range, selected-partition-only scope, partition-table preservation, data-loss warnings, Stop behavior, and the non-sanitization disclaimer. The initial focus is the textbox. The destructive button remains disabled until the user types `WIPE`, focuses the final button with Tab, and activates it through the ordinary pointer or focused-keyboard widget dispatcher. Before beginning, the UI revalidates current selection, exact registration and generation, online state, table identity and complete partition tuple, mount/root/boot protection, capabilities, bounds, and admission state. A stale or changed target ends in a visible not-admitted result and requires a new selection and confirmation.

The running modal displays the engine state and engine-reported written bytes, total bytes, and percent, plus **Stop Wipe**. The control calls `request_partition_wipe_stop`; each cooperative update advances at most one engine batch. The engine retains its durability policy and reports 100% only after required Flush success. A stopped operation reports zero overwritten bytes when Stop arrived before writes; otherwise it says data may be partially destroyed. Write, removal, or Flush failures use a failure message and retain the available engine reason. A completed result says the selected partition was overwritten with zeros and does not claim secure sanitization. Refresh follows terminal storage changes.

`KernelApp::requestClose` now consults the application close guard, and `AppManager::closeApp` does the same when no window is present. Disk Manager vetoes Alt+F4/titlebar close while a wipe owns storage, requests Stop, and keeps the window, job, target pin, buffer context, and lease alive while the normal update loop settles bounded work and required cleanup. The user can close after the terminal result appears. Shutdown/destructor code retains a best-effort stop path; it is not the normal active-close path.

### Host checks and builds

The new `disk_manager_wipe_ui.h` production policy helpers have focused host checks for action eligibility, whole-device and unselected rejection, protected/mounted/read-only/NVMe/unsupported targets, exact typed confirmation, target registration/generation and geometry changes, immutable request binding, engine progress mapping, duplicate/busy admission, terminal wording, and close-stop behavior. Widget activation checks exercise normal keyboard and pointer dispatch, a disabled initial destructive button, exact confirmation, duplicate activation rejection, and Stop Wipe routing. Engine-specific MBR/GPT bounds, partial Stop, write/Flush failures, 4Kn, removal, replacement, and lease checks remain in the existing production-engine test harness.

Final continuation-2 host result: **1,037/1,037 storage checks** (including 17 engine wipe cases and 31 UI-policy checks), widget activation pass, USB trace classification **7/7**, GPT verifier **1/1**, DM30 fixture verifier **1/1**, and byte-verifier self-tests **3/3**. Detailed final captured output is in ignored `out/dm34/host-tests-final.log`; verifier self-test output passed in `python tests/dm34_partition_wipe_image_test.py` and `out/dm34/verifier-tests-final.log`. `git diff --check` passed. Final-source UEFI x64 Release passed: `kernel/build/amd64/bin/kernel.elf` SHA-256 `A210E38342740125F5A8895041DB2F6367EA18D7B9381C863D0AC1A569DFD111` (3,016,248 bytes), matching `ESP/kernel.elf`; `BOOTX64.EFI` SHA-256 `45D647C71C3927E1810FBE2860B63FA752EBBC5C413F5B899F003927081C7546`. Final `build.bat` passed and produced `guideXOSServer.exe` (7,891,254 bytes), SHA-256 `2CF138C3CD61F64FAD0BA7492B27548976D62FBE5637786D810A52BE3E464EBB`. The UEFI build checked QEMU prerequisites and launched no guest. Source blob provenance: `kernel/core/kernel_apps.cpp` `79c8bf2d1ccad0eac4625317d3bba8adfd648a33`, `kernel/core/kernel_app.cpp` `39bf0cd7a55f7468c4735dfea5b7742d401e031a`, and `disk_manager_wipe_ui.h` `c7be9070ee6bc930ca1a00eb088256221a8b3324`. `kernel/core/main.cpp` matches its starting Git blob `d93803dd33d5c6886c9dadd9d9e38f61201bf479`; no temporary launcher or destructive hook remains. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain the makefile defaults. PowerShell `build.ps1 -Arch amd64` retains the unrelated PacMan Native ELF undefined symbols `pacman_audio_load_resources` and `pacman_audio_submit`; PacMan source was not modified.

### Disposable image fixtures and QEMU gate

`tests/dm34_partition_wipe_image.py` prepares two distinct writable raw images and pristine `.pre.raw` copies under ignored `out/dm34/partition-wipe/`. Each image has a valid MBR (disk signature `D34A5E34`), 512-byte sectors, 540,000 sectors / 276,480,000 bytes, selected MBR Partition 1 at inclusive LBA `2048–526335` (524,288 sectors / 256 MiB) filled initially with `0xA5`, and neighboring Partition 2 at LBA `530000–538191` filled with `0x5A`. The manifest records preimage SHA-256 `07104f2cfd3cf222b7325463bc683ecca038c7c0bfcd216ef40095d279d3d9bb`, target geometry and patterns, partition metadata, and preserved guard ranges. The same-size fixtures are separate files for Stop and complete runs.

No DM34 guest was launched. Before the first guest launch, QEMU enumeration found a foreign process: PID `15720`, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, command line owned by the separate `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` proof run, with QMP at `127.0.0.1:50844`. The ownership gate therefore stopped guest work; there was no QMP connection, input, screenshot, stop, or disk attachment to that process. The production boot/system disk was not used as a destructive target, and no host physical disk was attached.

The independent verifier compares every byte outside the selected partition to its pristine image. In completed mode it additionally requires every target byte to be zero; in stopped mode it requires a real target-region byte change without assuming a durable exact prefix. The verifier is `python tests/dm34_partition_wipe_image.py verify --manifest out/dm34/partition-wipe/fixture-manifest.json --image <post-image> --mode complete|stopped`. It has not been run against guest output because no guest operation occurred. QEMU Stop and completion results, guest PID/QMP/kernel provenance, write/Flush evidence, terminal UI screenshots, post-image hashes, no-post-Stop-write observation, and actual outside-region byte comparison remain unqualified.

### Outcome

This continuation is **Outcome B — evidence incomplete**. The production GUI, deliberate target-bound confirmation, cooperative progress/Stop, close guard, host checks, fixture preparation, and byte verifier are implemented. The foreign-QEMU gate prevented both required guest operations and independent post-guest verification; this is an evidence gap, not a demonstrated production correctness defect. No temporary launcher hook or destructive proof action was added. `kernel/core/main.cpp` remains the normal production startup source. `.phase` remains `last_completed=DM33`, `next_expected=DM34`. Entire-disk erase, secure erase, NVMe destructive writes, hardware testing, and automatic filesystem recreation remain deferred.

## Continuation 3 — real QEMU partition-wipe GUI qualification

This continuation started from the required clean `DISK_MANAGER_IMPROVEMENTS` branch at `83b73a5261de525abefcc2f8abf422a64907cec6` (`disk-manager: add DM34 partition wipe GUI safeguards`). `.phase` was `last_completed=DM33`, `next_expected=DM34`; it remains unchanged. The preceding continuation had stopped at the foreign-QEMU gate after identifying PID `15720` under `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`. That historical process was not touched. This continuation re-enumerated current process owners and found the exclusive QEMU lane clear before each launch. Three DM34-owned guests ran sequentially, never concurrently: Stop PID `30716` / QMP `127.0.0.1:60129`, completion PID `31384` / QMP `127.0.0.1:60130`, and the separate application-close PID `18832` / QMP `127.0.0.1:60131`. Each used `C:\Program Files\qemu\qemu-system-x86_64.exe` (SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`, QEMU `v11.0.0-12122-ga4bb4b10c9`), an isolated copied ESP, and exactly one writable DM34 raw image exposed as a USB mass-storage device. Owner manifests retain each actual live Win32 command line, image path, target-device serial and process/QMP identities under ignored `out/dm34/continuation-3/`. No host physical drive was attached. After each primary result, QEMU was shut down only after the UI reached a terminal state; process exit and exclusive host access to the image were confirmed before the next guest.

### Candidate, launch, and confirmation evidence

Both primary runs booted the intended committed production candidate. `kernel/build/amd64/bin/kernel.elf` and the copied `out/dm34/continuation-3/esp/kernel.elf` have SHA-256 `A210E38342740125F5A8895041DB2F6367EA18D7B9381C863D0AC1A569DFD111`, matching the accepted host-qualified kernel; the UEFI bootloader identity is SHA-256 `45D647C71C3927E1810FBE2860B63FA752EBBC5C413F5B899F003927081C7546`. `kernel/core/main.cpp` has the same Git blob `d93803dd33d5c6886c9dadd9d9e38f61201bf479` as the starting HEAD. The ordinary desktop Start Menu launched Disk Manager in both runs. No launch-only hook, destructive shortcut, source change, or wipe autorun was used.

The operator selected USB Disk 1 by serial, selected its MBR Partition 1, and opened the production **Wipe Partition** confirmation through the ordinary GUI. The disk and unallocated selections did not enable the partition action. In each confirmation the target was `usb-46F4:0001-p0-i0-10`, USB Mass Storage, serial `DM34STOP01` or `DM34COMP01`, Disk Manager disk 1, Partition 1, 256.0 MiB, inclusive LBA `2048–526335` (524,288 512-byte sectors). The modal stated that only the selected partition would be overwritten, the table and other partitions would be preserved, existing data would be destroyed, Stop would not restore overwritten data, and this was not secure erase or sanitization. An Enter attempt with an empty confirmation field did not start a wipe. The operator typed exact `WIPE`, focused the final destructive button, verified its focus on screen, then activated it through ordinary keyboard/widget dispatch. QMP showed no target writes before explicit activation. Stop and completion confirmation, selection, and activation evidence is in the corresponding `*confirmation*`, `*selected*`, and `*operation-started*` files in `out/dm34/continuation-3/`.

### Test A — GUI Stop after successful writes

`stop-test.raw` was pristine at SHA-256 `07104f2cfd3cf222b7325463bc683ecca038c7c0bfcd216ef40095d279d3d9bb` before PID `30716` attached it. The GUI showed **Running** and nonzero progress; the operator focused **Stop Wipe** and activated it through normal keyboard input while the job was active. The terminal UI reported **Stopped**, `75,497,472 / 268,435,456` bytes (`147,456 / 524,288` sectors, 28%), `Required Flush: succeeded`, and warned that the partition was partially destroyed and existing data may be lost. The completion-success message was not shown.

QMP block statistics for `dm34stop` recorded 576 successful write operations and `75,497,472` bytes, two Flush operations total (startup/preflight and post-write finalization), zero failed writes/Flushes and zero invalid writes/Flushes. The post-write Flush therefore completed successfully after Stop. The UI terminal result was captured before a separate QMP guest shutdown. The terminal result modal was then closed normally; Disk Manager returned to its refreshed state with the wipe action available again, showing that operation ownership had been released. No write batch followed the terminal Stop state.

After QEMU exit and exclusive image-release confirmation, the documented host verifier passed in stopped mode. It found `75,497,472` target bytes changed/zeroed, and compared all `8,044,544` bytes outside the selected partition against the pristine preimage. The partition table, neighboring partition and guard markers were preserved. The stopped image SHA-256 is `9a55a7b32e63054eb1b65642062ccbfab0a39470a1bba828ec246f68954984d0`. This verifier establishes the observed changed extent and preservation; it is not a claim that each byte in the extent had independent readback durability. The principal records are `stop-final-qmp-before-exit.json`, `stop-verifier.json`, `stop-after-hmp-enter.png`, `stop-lease-release-ui.png`, and `stop-owner-manifest.json`.

### Test B — GUI full completion and independent byte verification

`complete-test.raw` was separately pristine at the same expected initial hash before PID `31384` attached it. Disk Manager selected the same confirmed USB identity and partition geometry, then started the actual production job through the modal. Captured intermediate GUI stages included about 13%, 26%, 52%, 78%, and 92%; the final result reported **Completed**, `268,435,456 / 268,435,456` bytes, `524,288 / 524,288` sectors, `100%`, and **Required Flush: succeeded**. The UI stated “Selected partition overwritten with zeros.” QMP recorded 2,048 target writes totaling exactly `268,435,456` bytes, two Flush operations total (startup/preflight and final durability Flush), zero failed writes/Flushes and zero invalid writes/Flushes. The highest reported target write offset was `269,484,032`, the exclusive byte end of the selected partition. The guest remained responsive through progress and terminal capture.

After the Completed screen was recorded, the guest exited and QEMU disappeared; an exclusive host open of the completion image succeeded. The independent `complete` verifier compared every target byte and every byte outside the range to the prepared preimage. It verified all `268,435,456` selected bytes were zero and all `8,044,544` bytes outside the target were identical, including the MBR/table, preceding sectors, neighboring partition and guard sectors. Unexpected outside-target differences: **0**. The post-image SHA-256 is `52fcf2bfa1b56d51d320643be629c892f2ee0fee354d4a7b10975ca179827927`; the pristine preimage SHA-256 is `07104f2cfd3cf222b7325463bc683ecca038c7c0bfcd216ef40095d279d3d9bb`. The verifier output and QMP/UI records are `completion-verifier.json`, `completion-final-qmp-before-exit.json`, `completion-progress.jsonl`, the `completion-progress-stage-*.png` / `.json` snapshots, `completion-progress-terminal-sample.png`, and `completion-owner-manifest.json` under `out/dm34/continuation-3/`.

### Application-close runtime check

An optional third disposable copy was cloned byte-for-byte from the pristine prepared preimage (initial SHA-256 `07104f2cfd3cf222b7325463bc683ecca038c7c0bfcd216ef40095d279d3d9bb`) and run only after both primary images had been released. In PID `18832`, the normal desktop launched Disk Manager, the operator deliberately selected serial `DM34CLOS01` / Partition 1, repeated the same target-bound `WIPE` confirmation, and started the real GUI job. After QMP recorded nonzero progress, a normal Alt+F4 close request was sent. Disk Manager remained alive while the job settled to **Stopped**, `19,922,944 / 268,435,456` bytes (`38,912` sectors, 7%), with **Required Flush: succeeded** and a truthful warning that existing data may be lost. Two QMP samples two seconds apart showed no new writes after Stop: `19,922,944` bytes / 152 operations remained unchanged; total Flushes were 2 with no failures. A second Alt+F4 after the terminal result closed the app window. QEMU remained running with the target attached until the app-close screen was captured and the guest was then shut down normally. The verifier passed in stopped mode: exactly `19,922,944` changed target bytes and all `8,044,544` outside-target bytes preserved; post-image SHA-256 `72c19ccec1017f1d91e700f85e1561176ea37aef8a4dce46149c0fed67fb2edc`. This separately qualifies the real close guard and safe terminal close path. Evidence and the derived one-image verifier manifest are under `out/dm34/continuation-3/` (`appclose-owner-manifest.json`, `appclose-after-alt-f4.png`, `appclose-close-wait.png`, `appclose-stopped-qmp.json`, `appclose-after-terminal-alt-f4.png`, `appclose-verifier-pass.json`, `appclose-fixture-manifest.json`). The original Stop/completion fixture manifest was not altered.

### Retained host qualification and outcome

No production source changed, so the exact-source host qualification remains valid: storage suite **1,037/1,037 PASS**, including 17 engine wipe cases and 31 wipe UI policy cases; widget activation PASS; USB trace classification 7/7; GPT verifier 1/1; DM30 fixture verifier 1/1; byte-verifier self-tests 3/3; AMD64 kernel PASS; `build.bat` PASS; and UEFI x64 Release PASS. `build.ps1 -Arch amd64` retains the known unrelated PacMan Native ELF linker errors (`pacman_audio_load_resources`, `pacman_audio_submit`); PacMan was not modified. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged. No runtime device-loss campaign was repeated; immutable incarnation, same-capacity replacement rejection, and lease cleanup remain covered by the retained engine tests.

This continuation qualifies real partition confirmation, partial Stop, durable full completion, independent target-zero and outside-preservation checks, and runtime app-close settlement. It is **Outcome B — `DM34_PARTITION_GUI_QUALIFIED`**. The tracked changes are documentation only; no temporary launch or destructive proof hook exists, and `kernel/core/main.cpp` remains the starting production source. `.phase` stays `last_completed=DM33`, `next_expected=DM34`. Entire-disk erase is still unimplemented and unqualified; secure erasure/sanitization, hardware physical-drive testing, and DM35 have not started.

## Continuation 4 — host-only entire-disk zero-overwrite engine

This continuation started at the required clean `DISK_MANAGER_IMPROVEMENTS` branch and exact HEAD `b7d06a4c88e23b8a04cb7bfbe5eaa714a99a0ef0` (`disk-manager: qualify DM34 partition wipe in QEMU`). `.phase` was `last_completed=DM33`, `next_expected=DM34`. The authoritative DM34 report had no earlier whole-device engine. The branch, phase marker, and earlier Partition Wipe chronology remain unchanged. The whole-device GUI is not enabled, and no QEMU guest or physical device was used.

### Existing storage architecture audit

`partition_wipe.cpp` and its API establish the existing bounded write, progress, Stop, Flush, pin, and terminal-result conventions. `storage_manager.cpp` supplies registration-incarnation identity, capacity and sector geometry, checked block ranges, transport capabilities, mount protection and boot-source protection. `partition_table.cpp` parses authoritative MBR and GPT metadata. `disk_initialization.cpp` owns the single global destructive-operation lease. It pins the exact registration, blocks another storage-operation owner, and releases a removed registration only after the operation settles. FAT32 Format/Quick Reformat, Partition Wipe, Delete, GPT Repair, and other structured destructive operations all use that lease. VFS mount creation also refuses to proceed while it is held; whole-disk admission checks all active mounts for the parent registration.

The whole-device engine intentionally differs from Partition Wipe after its first write. It requires a valid, agreeing partition map and all safety prerequisites at admission and repeats them immediately before the first write. After any write attempt, it validates only the lease, exact registration/incarnation, registry generation, online state, immutable capacity, logical-sector size, transport, and write/Flush contract. It never parses the partition map again after LBA 0 may have been destroyed. Partition Wipe's existing partition-map revalidation is unchanged.

### Engine and admission policy

`WholeDiskEraseRequest` requires explicit `WHOLE_DISK_ERASE_ALL_ADDRESSABLE_SECTORS` scope and a captured disk identity. It has no caller-provided LBA or count. The job binds registration ID, registry generation, transport and provenance, controller/path identity where available, total sectors, sector size, LBA 0, full capacity, bounded batch size, operation ID, and shared exclusive ownership. The target is copied into the job, so later UI selection changes cannot retarget it.

Admission rejects missing or ambiguous boot provenance, the current boot device, root-backed media, any mounted/busy child or whole-device mount, unknown or unsupported partition maps, read-only devices, NVMe, geometry/capacity overflow, absent read support, and transports without a known successful durability contract. Only ATA PIO, AHCI, and USB Mass Storage with established successful Synchronize Cache are eligible. Required explicit Flush is checked at admission and throughout the job. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` are unchanged.

Historical boot-looking media has an explicit fail-closed classification. MBR active partitions and GPT EFI System, Microsoft Reserved, Windows Recovery, BIOS Boot, Linux root, or required-partition metadata are protected. NTFS/Linux/EFI/unknown MBR types, generic Microsoft Basic Data, unknown GPT types, system/boot/recovery labels, raw media, and invalid/degraded/hybrid/extended maps are ambiguous and denied. The initial data-layout allowlist is deliberately narrow: inactive MBR FAT32 data types and GPT Linux Home types. This policy does not claim that logical overwrite sanitizes remapped sectors or device-internal storage.

The exclusive lease is global, which is stronger than a parent/child-only lock: a whole-device job conflicts with Partition Wipe on any child, Format, Quick Reformat, Delete, GPT Repair, another whole-device erase, and any other structured operation using the shared lease. Host tests invoke those production entry points while erase owns the lease.

### Bounded execution and results

`kernel/core/whole_disk_erase.cpp` writes one zero-filled, logical-sector-aligned batch of at most 1 MiB (and no more than the registered transfer limit) per step. Checked arithmetic binds every range to `[0, totalLogicalSectors)`, includes the final addressable LBA, and shortens the final batch. Identity and capacity are checked before each batch. Progress counts only confirmed completed sectors; it remains at 99% after the final write until required Flush succeeds. The production API retains the operation ID, identity, total/written sectors and bytes, percentage, stop request, phase, failed LBA/range, block diagnostic, Flush status, durability result, possible map destruction, terminal state, and ownership release.

Stop before the first write reports zero writes and performs no Flush. Stop after a write settles at the next batch boundary, attempts required Flush if the exact registration remains online, and reports that the disk may be partially erased, its partition map/data may be destroyed, and it may be unusable. Write failure and removal retain the completed prefix and failed range without claiming completion. Flush failure after full logical coverage stays Failed at 99%. Removal invalidates the old pinned incarnation; no new batch or Flush targets a same-capacity replacement. Duplicate removal cannot double-release ownership. The engine never recreates a partition table and never creates a formatter `KnownZero` token.

### Host qualification

The new deterministic `FakeDisk` coverage is **34/34 whole-device checks**. It includes explicit-scope and missing-selection rejection; read-only, NVMe and USB-without-Synchronize-Cache rejection; ATA/AHCI/qualified-USB admission; current boot/root, mounted child, active MBR, GPT EFI, NTFS, generic GPT Basic Data and system-label protection; invalid/overflowing geometry; admission-time replacement; duplicate erase and conflicts with Partition Wipe, Format, Quick Reformat, Delete, GPT Repair and mount creation.

The MBR and GPT fixtures use distinct nonzero partition/free-space patterns, valid metadata copies, and nonzero first/final sectors. Independent full-image checks confirm every addressable byte is zero after completion, including the MBR/protective MBR, primary and backup GPT metadata, partition data, unallocated regions and final LBA. Coverage includes 512-byte sectors, 4Kn sectors, non-round capacity, shortened final batches, and batch bounds. Stop before writes, after GPT metadata destruction, and near the final 4Kn batch pass. Early/later write failure, Flush failure, removal during writes, removal before final Flush, duplicate removal, same-capacity replacement, and exactly-once ownership release pass.

The full storage manager suite is **1,071/1,071 PASS**, up from the accepted 1,037 baseline. It includes the unchanged 17 Partition Wipe engine checks and 31 existing wipe UI policy checks. The runner also reports widget activation PASS, USB trace classification 7/7, GPT verifier 1/1, and DM30 fixture verifier 1/1. `git diff --check` is recorded after the final documentation/edit pass.

### Builds and deferred GUI contract

The AMD64 kernel build passed with NVMe write and Flush proof flags at zero. `build.bat` passed. `build-uefi.ps1 -Arch amd64` passed the UEFI x64 Release bootloader, kernel and ESP setup and only checked QEMU prerequisites. `build.ps1 -Arch amd64` stopped at the previously known unrelated PacMan Native ELF undefined symbols `pacman_audio_load_resources` and `pacman_audio_submit`; PacMan was not modified.

The next graphical continuation must add a separate **Erase Entire Disk** action, not reuse **Wipe Partition**. The confirmation should show model/name, serial or available device identity, displayed disk number, total capacity, partition count, all-addressable-sector scope, destruction of every partition and partition table, loss of bootability, the risk that Stop leaves the disk unusable, and the non-sanitization statement. Require typing an explicit phrase plus an unambiguous device identifier; displayed disk number alone is insufficient. The final destructive control must not be activated by a default Enter press. GUI selection or confirmation code must not bypass engine admission.

Remaining DM34 gates are the entire-disk GUI, deliberate confirmation, real QEMU Stop and full-device completion, independent whole-image byte verification, guest boot/root/system rejection, app-close settlement, and device-identity/removal acceptance. No whole-disk QEMU erase or physical-media test occurred here.

### Outcome

This continuation is **Outcome B — `DM34_DISK_ENGINE_READY`**. The host-qualified production engine and tests are implemented, but this is not final DM34 acceptance. `.phase` remains `last_completed=DM33`, `next_expected=DM34`; DM35 has not started.

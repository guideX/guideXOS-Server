# Disk Manager Phase DM3 — Serialized Safe Disk Initialization

## 1. Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `6e8c89fa4d1a68cc63bc5c4378d83cc44dc8ddd4`
- Expected DM1 (`a8764286`) and DM2 (`6e8c89fa`) commits were present.
- The worktree was clean at start; the branch tracked `origin/DISK_MANAGER_IMPROVEMENTS` at 0 ahead / 0 behind.
- No physical disk or disk image was initialized during this phase.

## 2. Operation-lock design

`kernel/core/disk_initialization.cpp` owns one storage-management lease. Acquisition returns an explicit monotonically increasing owner token. Nested acquisition through the same lease is rejected, a busy lease rejects competing initialization, stale tokens cannot release a later owner's lease, and token wrap fails closed. The lease is retained from preflight and metadata snapshot through the user confirmation screen, post-confirmation validation, writes, flush, parser verification, rescan, and cleanup. Execution enters a committing state that rejects a second execution and prevents a copied lease from cancelling the operation; only its completion path releases it. Cancel before execution and handled completion/failure paths release it. A short atomic spin lock protects only lease metadata; no global UI lock is held.

The shared preflight parser scratch and initialization buffers are bounded static storage because the kernel boot stack is small. The confirmation screen is synchronous in the existing bare-metal app architecture. Once the user confirms, the bounded metadata operation runs in that event handler; there is no storage worker/event queue yet.

## 3. Boot-device provenance architecture

The UEFI loader obtains `EFI_LOADED_IMAGE_PROTOCOL` and opens the loader's `DeviceHandle` through `EFI_SIMPLE_FILE_SYSTEM_PROTOCOL` to load files. That handle or its UEFI device path is not exported into the canonical BootInfo v2 handoff. BootInfo v2 carries memory map, framebuffer, ACPI, command line, ramdisk, NIC, and kernel-base data, but no firmware block handle, device path, namespace, disk GUID, serial, or storage-controller identity. The legacy `BootDevice` field is not populated into the canonical handoff used by the kernel.

The kernel recognizes UEFI BootInfo or Multiboot during entry. The ATA PIO registration path receives IDENTIFY model/serial/capacity but does not preserve a controller PCI path that can be matched to UEFI's loader device path. The NVMe scanner identifies namespace 1 and has a PCI scan location internally, but there is no loader namespace or PCI identity in BootInfo to match against it. A transport name, enumeration order, MBR active flag, or guessed disk number is not proof of boot provenance.

No clean exact-match path exists for the active transports without extending the loader/kernel handoff and the transport registration identity model. DM3 therefore adds explicit `BootProvenance` to block descriptors and does not invent a match. ATA and NVMe register `UNKNOWN`; synthetic RAM disks are marked `DEFINITELY_NOT_BOOT`. The fake block device can be set to each state.

## 4. Boot safety policy

`query_boot_protection()` maps descriptors to `BOOT_DEVICE_IS_TARGET`, `BOOT_DEVICE_DEFINITELY_NOT_TARGET`, or `BOOT_DEVICE_IDENTITY_UNKNOWN`, and revalidates identity around the query. Initialization rejects boot-backed and unknown-boot targets. Only an explicit `DEFINITELY_NOT_BOOT` descriptor may continue. VFS protection separately rejects any mounted or root-backed block device.

Because ATA/NVMe provenance currently remains unknown, this policy intentionally leaves physical ATA/NVMe devices ineligible even when `/` is mounted elsewhere.

## 5. Persistence policy

Initialization accepts only `PERSISTENCE_SYNCHRONOUS_DURABLE`, or `PERSISTENCE_FLUSH_REQUIRED` with a registered flush callback and known semantics. The operation flushes before the first write and after the metadata sequence. Both successful reports must say their semantics are known and return either synchronous-durable completion or a successful supported flush. Unknown durability, unsupported flush, and failed flush cannot report success. Rollback uses the same trusted persistence contract and verifies restored sectors after its flush.

RAM disks remain volatile even though their no-cache flush callback succeeds synchronously. A flush callback does not turn volatile memory into persistent storage.

## 6. NVMe decision

NVMe initialization remains disabled. The current NVMe descriptor has no Flush callback and correctly reports unknown persistence. A Flush command carries no data buffer, but the existing queue path is tied to the current global controller/queue assumptions and has no isolated command-level hardware/fake-queue validation seam. Adding a callback now would not establish tested namespace-specific durable semantics. The UI reports the actual durability/boot-provenance blocker. No durability promise was added.

## 7. Initialization eligibility

`validate_initialize_target(snapshot, scheme, lease, validation)` combines DM2's destructive-target checks with initialization-specific constraints. It checks the current registry generation and target identity, geometry/capacity, reads and writes, trusted persistence, mounted/root state, explicit boot provenance, a confidently `DISK_STATE_NOT_INITIALIZED` parser result, metadata-sector contents, supported 512/4096 logical sectors, scheme capacity, and current lease ownership. The complete bounded snapshot is read again before confirmation. Execution repeats safety validation after confirmation and checks the captured metadata snapshot and identity immediately before writing, and revalidates identity before every sector write and after parser rescan.

Initialization rejects valid MBR/GPT, degraded GPT, invalid tables, unsupported tables, unreadable media, nonzero metadata sectors in the planned write set, read-only targets, unknown durability, mounted/root targets, boot/unknown-boot targets, stale identity/generation, unsupported logical sectors, too-small layouts, and MBR capacity above its 32-bit LBA limit. There is no force option.

## 8. Request/result model

The reusable storage API is declared in `kernel/core/include/kernel/disk_initialization.h`:

- `InitializeDiskRequest`: captured `TargetIdentity`, requested scheme, nonzero-random MBR signature policy, secure-random GPT GUID source, and expected registry generation. Deterministic GUID/signature injection exists only in `KERNEL_STORAGE_TEST` builds.
- `InitializeDiskPlan`: operation owner token, target and generation snapshot, layout, generated identity, and confirmation readiness. Storage keeps a private copy and rejects a caller-modified plan before any write.
- `InitializeDiskResult`: structured status and stage, captured target, requested scheme, completed write stages, flush result, parser-verification result, final detected state, bounded rollback outcome, uncertainty flag, and diagnostic text.

Disk Manager displays status text but does not interpret low-level write errors or perform storage writes itself.

## 9. GPT layout

The deterministic interoperable layout uses **128 entries × 128 bytes = 16 KiB**. The primary header is LBA 1; its array begins at LBA 2. The backup header is the final logical LBA; its array occupies the 16 KiB immediately before that header. This uses 32 sectors on 512-byte devices and 4 sectors on 4096-byte devices. The first usable LBA is aligned to 1 MiB: LBA 2048 at 512 bytes or LBA 256 at 4096 bytes. The last usable LBA is one before the backup array. Checked capacity and overflow-safe arithmetic reject layouts without a usable region.

Both headers use revision 1.0, a 92-byte header, matching nonzero disk GUIDs, correct current/backup/array LBAs, bounded entry geometry, an empty zero-filled array, the array CRC32, and a header CRC32. GUID bytes get RFC 4122 version-4 and variant bits in EFI GUID on-disk byte order. Production GUID bytes come from the existing VirtIO RNG utility; an unavailable entropy device or failed request aborts before writing.

## 10. GPT write ordering

The operation snapshots LBA 0, the primary header and array, the backup array, and the backup header before modification. It writes backup array, backup header, primary array, primary header, then the protective MBR. The protective MBR is the last compatibility marker; it has one `0xEE` entry, a saturated 32-bit protective range where needed, and `0x55AA` at bytes 510–511 of the logical sector. After all writes, the operation flushes and runs the normal parser verification.

Raw multi-sector writes are not atomic. Power loss can leave metadata partially written or leave GPT structures without the final protective MBR. DM3 does not claim transactionality or automatically repair such a disk; a later scan reports whatever the parser can establish.

## 11. MBR layout

MBR initialization writes a complete zeroed logical sector, then a nonzero disk signature at byte 440, four empty partition entries at bytes 446–509, and `0x55AA` at bytes 510–511. It does not preserve unknown boot code or invent partitions. The same byte offsets apply to 512-byte and 4096-byte logical sectors. Production disk signatures use the existing random source; tests may inject a deterministic nonzero value.

## 12. Rollback behavior

The exact metadata sectors overwritten by the selected layout are snapshotted before confirmation. On write, flush, or verification failure, restoration is attempted only when at least one write was attempted and the same target identity still resolves. Restoration removes the marker/header sectors before restoring arrays, then uses the trusted flush model, compares the full snapshot byte-for-byte, and confirms the normal parser sees `Not Initialized`. The result distinguishes no attempt, verified success, and failure/uncertain state. Identity change prevents writes to a replacement device. If restoration cannot be trusted, the operation forces a parser rescan where the original identity remains available and returns an uncertain failure. This is bounded recovery, not atomic rollback.

## 13. Read-back verification

After flush, the storage layer invokes the normal DM2 partition parser from fresh device reads. GPT succeeds only when the parser reports valid primary and backup GPT, matching copies, valid protective MBR, the expected disk GUID in both copies, and zero used partitions. MBR succeeds only when the parser reports valid MBR, no protective entry, zero used partitions/table entries, and the expected disk signature. The storage layer parses again for its final rescan state and revalidates identity before success. Generated buffers alone are never considered proof.

## 14. Disk Manager UX

The bare-metal Disk Manager adds `Initialize Disk...` for raw disks. It presents target name/model/serial when present, transport, capacity, logical-sector size, and `Not Initialized`; GPT is the model default and recommended choice, with MBR labeled for compatibility. The confirmation identifies the same captured target and states that only partition-table metadata will be written. Initialize and Cancel are separate actions. Refresh, selection, and duplicate initialization are disabled while the dialog is active. The operation stage is shown without fabricated percentage progress; completion shows the final parsed state and rollback result. Successful scans show `Online`, GPT/MBR, zero partitions, and unallocated space. Partition creation and formatting remain unavailable.

The modal continues to display the captured target identity if a later rescan changes the current selection. A post-operation rescan is performed only after the storage layer's verified parser rescan and lease release.

## 15. Unsupported-target behavior

The UI shows a structured reason such as mounted/root device, table not raw, stale identity, unknown boot identity, unknown durability, read-only, unsupported sector size, insufficient GPT capacity, or MBR 32-bit LBA limit. ATA raw disks show the unknown boot-provenance blocker. NVMe raw disks show that durable Flush support is unavailable, and their capability row reports unknown durability; their boot provenance also remains unknown in the storage policy. AHCI and USB mass-storage are not registered in the shared block layer. The hosted Windows Disk Manager remains an image viewer and does not enumerate host physical disks.

## 16. Test architecture

`tests/storage_manager_test.cpp` uses deterministic memory-backed fake block devices registered only in the fake harness. The harness does not open physical block devices. It injects read, write, flush, device identity, registry generation, and read-back corruption failures. GUID and MBR signature injection is compiled only for the test build. The suite covers both sector sizes, GPT/MBR layouts and parser verification, raw-only restrictions, mount/root/boot/persistence rules, registry and identity changes, lock contention/stale owners/cleanup, and write/flush/verify/rollback failures.

No generated raw image fixture was needed: the independent byte verifier inspects in-memory metadata separately from the normal parser and checks PMBR, both GPT header locations/geometry, GUID consistency, empty arrays, and CRCs.

## 17. Validation results

- `scripts/run-storage-manager-tests.ps1`: **114 checks passed, 0 failures**.
- Targeted C++14 bare-metal syntax-only compilation passed for Disk Manager, initializer, storage manager, parser, block layer, ATA, NVMe, and RAM-disk translation units. Reported warnings are existing unused-parameter and unrelated UI warnings; the new initializer emitted no warning.
- `git diff --check`: passed at review checkpoints.
- `cmd /c build.bat`: blocked before C++ compilation because `third_party/mbedtls` is absent. The repository build script reported `scripts/bootstrap-mbedtls.ps1 -Install`; dependency policy was not changed and no dependency was installed.
- No real hardware or physical developer disk was accessed. No external image fixture was written.

## 18. Transport limitations

| Transport | Initialization result | Reason |
|---|---|---|
| ATA PIO | Blocked | IDENTIFY-gated flush can establish the persistence capability, but boot provenance remains unknown. |
| NVMe | Blocked | No Flush command/capability and boot provenance remains unknown. |
| RAM disk | Blocked | Explicitly volatile, even though its synchronous no-cache flush succeeds; not durable. |
| AHCI | Unavailable | No active registration in the shared block layer. |
| USB mass storage | Unavailable | No active registration in the shared block layer. |

Thus there is currently **no real registered transport eligible** for initialization. Fake devices demonstrate that the service can initialize only when a target explicitly provides known non-boot provenance and trusted persistence.

## 19. Remaining blockers and outcome

DM3 is **Outcome B — initialization logic is implemented and tested, but no real transport currently satisfies the complete boot/persistence contract**. Authoritative loader-to-block-device provenance is not propagated; ATA/NVMe are therefore rejected. NVMe durability also remains unknown. There is no registry/hot-unplug synchronization primitive shared by the block registry and storage operation lease; current shared transports do not expose an active hot-unregister path, and every write is preceded by identity revalidation, but future hotplug work must coordinate registry mutation with an active lease before enabling writes. A future real transport also needs disposable-hardware or emulator validation of its flush and identity behavior.

## 20. Recommended DM4 scope

Add a versioned UEFI boot-source handoff based on the loader's actual device path and Block I/O identity, then preserve enough controller and device identity during ATA/NVMe discovery to compare that source without heuristics. Add block-registry mutation coordination with storage leases before hotplug/unregister is exposed. Provide a testable transport flush seam and independently validate NVMe Flush if it is pursued. Keep DM4 focused on identity/provenance and operation-state UX; partition creation, formatting, repair, force initialization, and conversion remain later phases.

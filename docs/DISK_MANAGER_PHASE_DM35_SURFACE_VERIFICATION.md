# Disk Manager DM35 — Read-Only Surface Verification Foundation

**Outcome B — `DM35_SCAN_ENGINE_READY` (host-only foundation)**  
**Phase marker remains:** `last_completed=DM34`, `next_expected=DM35`  
**Scope:** cooperative read-only sector-read verification; GUI and QEMU qualification are deferred.

## Roadmap and repository gate

The continuation began on `DISK_MANAGER_IMPROVEMENTS` at expected HEAD
`ba35f5013f642edad03273b855adf5c0a7c80173`, subject
`disk-manager: close DM34 partition wipe and whole-disk erase`. The tracked
worktree was clean. `.phase` contained `last_completed=DM34` and
`next_expected=DM35`.

The accepted final DM34 closeout says DM35 is ready but does not assign a
different DM35 objective. `docs/Development Priorities.txt` and
`docs/ROADMAP.md` are broader project roadmaps and do not define a Disk Manager
DM35 scope. No other DM35 report, branch ref, or active registered worktree
contained competing DM35 implementation work. The proposal supplied for this
continuation therefore became the working scope. No branch, worktree, or Git
topology changes were made. DM34 implementation and acceptance were preserved.

## Production source audit

| Area | Existing production mechanism and DM35 use |
| --- | --- |
| Checked block reads | `storage::read_sectors_safe` validates geometry, capacity, range, transfer size, buffer length and alignment, then calls `block::read_sectors_checked`. The block layer pins the registration for each synchronous callback and preserves transport status/diagnostics. |
| Registration identity | `TargetIdentity` includes the registry generation, monotonic registration ID, slot, transport, driver index, capacity, logical-sector size, and available controller/device identity. The new job copies this identity and never resolves a new target from a name, serial, or matching capacity. |
| Lifetime and conflicts | `StorageOperationLease` is the shared storage-operation owner. `pin_storage_operation_target` pins the exact registration. Existing Format, Partition Wipe, Whole-Disk Erase, and other storage operations use the same lock. Offline pinned devices remain tombstones until release; a replacement gets a different registration ID. |
| Geometry and batches | `valid_geometry` accepts power-of-two logical sectors from 512 through 4096 bytes and rejects zero capacity and byte multiplication overflow. Device `maxTransferBytes` and required buffer alignment are enforced. |
| Cooperative precedent | `Fat32FormatJob` advances one bounded scan read per step and retains the lease/pin between steps. Its blank-media scan also checks contents and may publish private `KnownZero` evidence; DM35 does neither. The Wipe/Erase jobs demonstrate the same shared lease, exact pin, bounded-step, Stop, and terminal-release lifecycle. |
| Transport reads | ATA PIO, AHCI, USB Mass Storage, and NVMe register block read callbacks. AHCI advertises its 128 KiB DMA transfer bound; USB and NVMe expose their own read bounds. NVMe read registration is unconditional; write and Flush callbacks remain behind separate proof macros, whose defaults and tested values are both zero. Virtio Block and RAMDisk also provide ordinary block reads. |
| Diagnostics | The block layer exposes the last `OperationDiagnostic`, including request LBA/count/status and an optional transport diagnostic, and global read/write/Flush counters. These counters are global; unrelated discovery or transport initialization traffic must be separated by before/after deltas in a future guest run. |
| Mount and boot policy | Existing destructive APIs continue to use destructive-target, boot/root, mounted-volume, and partition protection. The DM35 engine performs reads only, does not request write/durability capability, and does not mount or unmount. It does not claim a consistent filesystem snapshot when other activity can change sectors during a scan. |

The existing `Fat32FormatJob` is not a surface-health scanner: it rejects
non-zero media while proving a bounded partition blank before formatter
publication. No production media-health or historical-data correctness API was
found. The new engine does not introduce a second device registry, transport
framework, or scheduler.

## Production API and scan semantics

The new API is in `kernel/core/include/kernel/surface_verification.h` and
`kernel/core/surface_verification.cpp`:

- `begin_surface_verification_job` copies the exact target snapshot and an
  explicit `(startLba, sectorCount)` range, validates range and geometry,
  acquires the shared operation lease, pins the exact registration, and
  requires a live read callback. Writable, Flush, and persistence capabilities
  are not required.
- `step_surface_verification_job` performs at most one checked read request.
  It revalidates lease, registration, capacity, geometry, and buffer support
  before work and revalidates target identity after a successful callback.
- `request_surface_verification_stop` sets a cooperative Stop request. The
  current synchronous read, if any, settles first. The next safe boundary
  starts no further batch and releases ownership.

The request and scan bounds are copied into the job. Later changes to a GUI
selection or caller-owned request cannot redirect a running operation. A
whole-device caller supplies LBA 0 and the captured device capacity. A
partition caller must supply the extent from a currently validated partition
entry; the engine validates that extent against device capacity but does not
parse or infer a partition map on its own.

Each step is capped at 128 KiB and also respects the transport's declared
maximum. ATA PIO is capped at one logical sector per step because its callback
issues per-sector commands; this avoids multiplying the PIO command timeout
across a large engine batch. Other callbacks remain synchronous. Stop cannot
interrupt an in-flight transport callback, so response latency is bounded by
that callback's own timeout/settlement behavior, not by the engine API alone.
The engine never loops over the full range in one call.

Progress counts complete successful read requests only. `readableSectors` and
`bytesRead` cover a contiguous prefix; attempted-but-failed sectors do not
count as readable. A failed multi-sector request is reported by its exact
requested LBA and count plus the original `block::Status` and matching
transport diagnostic when available. The failed range is not claimed to
contain individually identified bad sectors. The policy is fail-fast: stop on
the first failed read request, do not retry it, and leave the failed request and
remaining range unverified.

Terminal classifications distinguish:

- **Completed:** every selected request returned success; diagnostic text says
  “All selected sectors were readable during this scan.”
- **Stopped:** the user requested Stop; completed coverage is retained and
  remaining sectors are unverified. Stop is not a failed-disk result.
- **Read failure:** transport status and failed request range are retained.
- **Device removed/offline:** a no-media result or exact offline tombstone ends
  the job.
- **Registration invalidated / target identity changed:** generation or exact
  snapshot revalidation failed. Any registry-generation change fails closed,
  including an unrelated device registration change.
- **Unsupported geometry / read unavailable / infrastructure or ownership
  failure:** admission or scan setup cannot safely continue.

No scan code issues a write, Trim, Discard, Format, repair, erase, flush, or
`KnownZero` publication. One successful scan means only that the selected
sectors were readable at their respective read times. It does not verify the
historical correctness of their contents, establish SSD NAND health, or certify
the device's future reliability. Concurrent filesystem writes can also make
the scan a non-snapshot observation.

## Device removal and operation ownership

The exact target remains pinned while the job runs. Physical removal or an
offline transition invalidates the old target; a pinned tombstone cannot be
reused for a same-capacity replacement. The old job stops before another
request. A replacement can be scanned only after a fresh identity snapshot and
new admission. Normal terminal paths release the shared operation owner and
pin once; host regressions verify the operation lock and registration can be
used again after settlement.

The shared lock rejects duplicate scans and excludes active Format, Partition
Wipe, Whole-Disk Erase, and other storage operations. The job is deliberately
not wired into application lifetime yet. A future GUI must keep the job alive
through Stop settlement before allowing its owning window/application to close.

## Host verification

`run_surface_verification_tests()` uses the production engine, block registry,
checked storage read path, operation lease, and controlled in-memory `FakeDisk`
media. It passes **48/48 focused checks**, including:

- full 512-byte and 4Kn scans, first/final sector coverage, and a non-round
  final batch;
- a partition-bounded extent, empty/out-of-range/overflow requests,
  unsupported geometry, and missing read capability;
- read-only admission, including an NVMe-typed read-only registration;
- first, middle, and near-final read failures with status/range retention and
  no retry;
- Stop before the first read and after partial progress;
- duplicate admission, both directions of conflict with destructive
  operations, and exactly-once ownership release;
- registry invalidation, active removal, same-capacity replacement isolation,
  and fresh admission for the replacement;
- complete fixture-byte comparisons and zero write/Flush deltas for complete,
  stopped, failed, and removed scans.

The complete host storage suite passes **1,158/1,158** checks, including the
existing device-lifetime/removal and Format/Wipe conflict regressions and 249
USB checks. `scripts/run-storage-manager-tests.ps1` also passes widget
activation, USB trace classification **7/7**, GPT verifier **1/1**, and the
DM30 GPT fixture verifier **1/1**.

Builds passed:

- AMD64 production kernel with `NVME_WRITE_PROVEN=0` and
  `NVME_FLUSH_PROVEN=0`;
- repository `build.bat` application build;
- UEFI x64 Release bootloader build.

No QEMU guest or physical storage was used. No destructive guest campaign was
repeated. PacMan sources and build behavior were not changed.

## Continuation 2 — production GUI integration and deferred guest qualification

The proposed Disk Manager flow is:

1. Offer **Verify Entire Disk** for a selected disk and **Verify Partition**
   for a selected partition. Capture the exact target registration and, for a
   partition, its validated table identity and bounded extent.
2. Show cooperative progress, successfully read sectors/bytes, current LBA,
   and remaining unverified coverage. A percentage, if shown, means completed
   readable coverage only; it is not a disk-health percentage.
3. Show **Stop Scan**. Wait for the in-flight synchronous read to settle, then
   stop before the next batch and preserve the incomplete range.
4. Report Completed/readable, Stopped/partial, Read failure with transport
   status and affected request range, removal, registration invalidation, or
   infrastructure failure distinctly. Never label every sector in a failed
   batch unreadable without additional bounded evidence.

The first four items above describe the GUI contract now integrated in
Continuation 2. Real transport/QEMU read scans, guest counter deltas, and
runtime UI qualification remain deferred. `.phase` is intentionally not
advanced to DM36.

### Host result: `DM35_GUI_READY`

Starting state was the required `DISK_MANAGER_IMPROVEMENTS` branch at
`0549fb02fb1ffd14e6edd31d771e40d2364bfab5`, with a clean tracked worktree and
`.phase` at `last_completed=DM34`, `next_expected=DM35`. The authoritative
engine evidence above was retained. This is a successful host continuation;
DM35 remains active and the phase file remains unchanged.

### Production GUI integration

`kernel/core/kernel_apps.cpp` integrates verification in the existing
`DiskManagerApp` operation modal, cooperative `update()` loop, inventory
refresh path, operation lease, window-close guard, and ordinary widget
dispatcher. It does not introduce another window or operation framework.
`kernel/core/include/kernel/disk_manager_surface_verification_ui.h` contains
the production target-binding, result wording, progress, and action-focus
helpers; `kernel/core/include/kernel/kernel_apps.h` stores the captured target
and job. The two scope actions are created as widget IDs **18** (Verify Entire
Disk) and **19** (Verify Partition), after existing widget IDs. Stop Scan
reuses the existing modal Cancel button, widget ID **12**, preserving DM34's
Stop Erase and Stop Wipe routing.

**Verify Entire Disk** is exposed for a selected parent disk row only. Its
extent is constructed from the captured device capacity as LBA 0 through
`totalLogicalSectors - 1`; no caller-provided arbitrary extent is accepted.
Admission requires the exact current registration, valid nonzero capacity and
logical-sector geometry, read capability, and an idle shared operation lease.
It does not require write or Flush capability and does not inherit erase's
boot/root/mount denial policy. A running or mounted volume can be read when
the actual registration/read/lifetime checks permit it.

**Verify Partition** is exposed for a selected, valid partition row. Admission
captures the parent registration/incarnation and registry generation, exact
partition number and MBR/GPT table identity, parsed bounds, and logical-sector
geometry. The selected map is freshly parsed and compared with the inventory
cache before opening the explicit confirmation and again immediately before
Start. The entry must still match exactly, and checked range arithmetic keeps
the scan inside device capacity. Unallocated space, invalid maps, stale
registrations, changed entries, and unsupported read paths cannot start a
partition scan. The engine receives only the immutable captured extent; later
inventory selection changes cannot redirect it.

Neither action starts a job on selection. The confirmation identifies scope,
device/model and serial, capacity, sector size, exact scan range, and
read-only behavior, and requires a separate **Start Verification** activation.
It explains that active data can change during reads and the scan is not a
stable snapshot. No typing phrase is required.

The progress panel reads engine state and counters: total and successfully
examined sectors, last attempted LBA, engine percentage, and current state.
Only successful completed terminal state may display 100%. **Stop Scan** is
widget ID 12 and routes through the ordinary keyboard/pointer widget
dispatcher to `request_surface_verification_stop`. Tab includes the active
verification state, visibly focuses Stop Scan, and Enter/Space dispatch the
focused ID through the existing dispatcher. Dynamic action focus excludes
hidden, disabled, and stale widgets. The action buttons use the same
dispatcher; repeated Start is unavailable after transition to Running.

Completed UI says “All selected sectors were readable during this scan” and
shows the completed count. Stopped UI says “Verification stopped — remaining
sectors were not checked” and retains coverage. Read failures show the exact
failed requested LBA/count, block status and bounded transport details when
available, plus successful coverage; a failed multi-sector request is not
described as individually bad sectors. Device removal and registration/target
identity changes have distinct terminal messages.

Window Close and Alt+F4 during a scan request Stop and keep the app/job alive.
The cooperative update path settles the bounded synchronous read, observes
Stop before another batch, waits for engine terminal settlement and ownership
release, then closes the window. Close after terminal settlement is allowed.
During F5 inventory refresh, the panel continues to show the captured target;
the running job never follows a changed disk number or same-capacity
replacement. Removal is handled by the pinned engine registration and cannot
continue on a newly registered device.

The engine's shared `StorageOperationLease` remains authoritative. Focused
regressions cover a duplicate surface scan and active scan conflicts with
FAT32 Format, Quick Reformat, Partition Wipe, Entire-Disk Erase, Delete
Partition, GPT Repair, unregister, and new mount admission. Existing reverse
conflict and DM34/DM33 widget regressions remain in the full host suite.

### Host regressions and build evidence

`run_disk_manager_surface_verification_ui_tests()` passes **34/34** checks,
using production target builders, real block registration/capability and
partition parsing, engine-backed active Close settlement, and the production
widget activation/focus helpers. The read-only engine regressions pass
**48/48**. `scripts/run-storage-manager-tests.ps1` passes **1,192/1,192**
storage checks, including **249** USB checks, existing device lifetime and
removal/replacement, operation conflicts, DM34 Stop Erase, Partition Wipe
Stop, and DM33 Cancel/widget routing. Widget activation passes; USB trace
classification passes **7/7**, GPT verification **1/1**, and the DM30 GPT
fixture verifier **1/1**. The final runner log is
`out/dm35-storage-test-7d31e6097e1242e38c6c5561e6694ddc.log`.

The isolated AMD64 production kernel build passed with
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`:
`out/dm35-amd64-build-4aa77619a8c443cdbd5129bc811ef0a2.log`.
`build.bat` passed; the previous ignored `guideXOSServer.exe` was restored
byte-for-byte after the build. UEFI x64 Release passed with isolated output
and intermediate directories:
`out/dm35-uefi-release-0bb3ceba4005430a836323c4575d4bf0.log`.
`git diff --check` passed.

The separate `build.ps1 -Arch amd64` attempt stopped at the known unrelated
PacMan Native ELF link errors `pacman_audio_load_resources` and
`pacman_audio_submit`, recorded in
`out/dm35-powershell-build-b58a64bcf3d34349aa89ea953ad2c066.log`. PacMan
sources were not changed; its tracked generated outputs were restored to
their pre-run contents. This does not affect the passing AMD64 kernel,
`build.bat`, or UEFI Release results. No QEMU guest, physical media, or
destructive test was used.

### Deferred QEMU acceptance contract

The following DM35 guest campaign is prepared but was **not run** in this
host-only continuation. Use the previously qualified paced PS/2 QEMU input
helper and manifest-verified disposable media:

A. **Complete disk verification:** run the real GUI against a patterned
   disposable device, observe genuine read progress and successful completion,
   and prove zero scan-attributable writes and no media changes.
B. **Partition verification:** choose one partition and prove reads stay
   inside its captured extent using counters or production diagnostics.
C. **Stop Scan:** after reads begin, activate Stop through normal pointer or
   keyboard dispatch; prove safe terminal settlement and lease release.
D. **Application Close:** Alt+F4 during an active scan must request Stop and
   settle before window/job destruction.
E. **Device removal:** remove a scanning disposable USB device; verify the
   old registration settles as removed/invalidated and a same-capacity
   replacement receives no continuation of the old scan.
F. **Read failure:** use a controlled fault-injection fixture or the existing
   production-linked host proof if QEMU cannot inject a precise safe failure;
   do not damage media or invent a bad-sector diagnosis.
G. **Independent integrity:** after each completed or stopped run, release the
   media image and compare its SHA-256 with the pristine starting image.
   Separate scan reads from USB registration/startup reads or Flush activity.

DM35 remains active, `.phase` remains `last_completed=DM34`,
`next_expected=DM35`, and no DM36 work has started.

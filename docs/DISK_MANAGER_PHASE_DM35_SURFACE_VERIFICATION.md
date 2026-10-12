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

## Deferred GUI and guest continuation

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

The GUI, application Close behavior, real transport/QEMU read scans, guest
counter deltas, and future UI runtime qualification remain for the next DM35
continuation. `.phase` is intentionally not advanced to DM36.

# Disk Manager Phase DM11 — USB Mass Storage Integration

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `d52553b2959bbb3625f9fb768d68e16f515123b6` (`Disk manager DM10 ATA transport reliability`)
- Tracked worktree: clean. Existing untracked `out/dm9-*`, `out/dm10-*`, and `out/mbedtls-bootstrap/` evidence is preserved.
- Recorded upstream comparison before implementation: `HEAD` equals `origin/DISK_MANAGER_IMPROVEMENTS` (0 ahead / 0 behind). `git fetch origin` failed with `git@github.com: Permission denied (publickey)`, so the tracking ref is not freshly verified.
- DM1 through DM10 reports and `docs/DISKMANAGER_IMPLEMENTATION.md` were read. Production USB, block registry, storage manager, partition views, VFS, and unplug/removal code were traced before implementation.

## Pre-implementation USB audit

### Host controller and enumeration

- The AMD64 HCI is UHCI-only. It scans PCI for the first UHCI controller and exposes its two root ports; it does not implement EHCI or xHCI. QEMU USB proof must use a compatible UHCI controller if attempted.
- The generic USB core reads device/configuration descriptors, stores VID/PID and root-port information, records interface class/subclass/protocol and endpoint descriptors, sets the configuration, and polls root-port connection state.
- At the starting revision there was no production call to `usb::init()` or `usb::poll()`. The USB HID initializer only cleared HID state, and did not start USB enumeration. Thus the source-level HCI and descriptors did not establish an active runtime USB stack.
- `usb::poll()` noticed a root-port disconnect but only marked its `Device` record absent. It did not release mass-storage state or unregister a block device. New enumeration did not dispatch any class driver.
- Configuration parsing discarded actual interface numbers and did not associate endpoints with interfaces. Mass storage therefore selected the first bulk IN/OUT endpoints on the device, rather than the endpoints belonging to the matching interface.
- UHCI bulk transfers used 64-byte packets and reset the endpoint data toggle to DATA0 at the beginning of every API call. Multi-command BOT sequences therefore lacked correct endpoint-toggle continuity. Endpoint halt clearing/reset recovery was absent.

### Mass Storage / BOT / SCSI

- `usb_storage.cpp` contains Bulk-Only Transport (CBW → optional data → CSW) and SCSI INQUIRY, TEST UNIT READY, READ CAPACITY(10), READ(10), WRITE(10), and REQUEST SENSE commands. The only modeled LUN is LUN 0.
- BOT checked CSW signature and tag, but not exact CBW/CSW transfer lengths, CSW residue, or invalid CSW status values. A zero-progress short transfer could loop forever. CSW command failure collapsed to `XFER_STALL`; REQUEST SENSE was not used for ordinary command failures. No Mass Storage Reset or CLEAR_FEATURE endpoint-halt recovery existed.
- Capacity command failure was ignored. READ CAPACITY(16), SYNCHRONIZE CACHE, MODE SENSE write-protect inspection, VPD serial retrieval, and media-change polling were absent. Capacity/block-size validity and transfer-size arithmetic were not checked.
- READ/WRITE(10) accepted 32-bit LBA and 16-bit count parameters without validating LBA/count, null buffers, media bounds, zero counts, or multiplication overflow. The callbacks were private to the standalone driver; there was no adapter into `kernel::block`.
- `usb_storage::probe()` had no call site. `release()` only changed its private active flag and did not unregister a shared block entry.

### Shared storage foundation and integration boundary

- `BDEV_USB_MASS` already exists. The common registry assigns a fresh monotonic registration ID per attachment, pins callbacks in flight, refuses normal removal while pinned, supports offline tombstones for forced loss, and prevents a reused slot from satisfying an old registration ID.
- Partition views pin the exact parent registration and bound all translated I/O. VFS partition mounts retain that incarnation and partition identity; stale file/iterator I/O fails, handles can close, and stale mounts can be explicitly cleaned up. This is the existing removal architecture to reuse.
- The capability model already distinguishes read/write callbacks, known/unknown removable state, flush semantics, and durability class. The FAT32 partition mount path is intentionally 512-byte-only; it becomes read-only automatically when the endpoint is read-only or cannot prove durable writes.
- USB has no firmware boot-source matcher. Its boot provenance must remain `Unknown`. The destructive-operation preflight fails closed for unknown boot identity, unknown persistence, and read-only targets; DM11 must not add a USB override.

### Scope decision

The gap is not just shared registration. The generic USB core is not started by production boot, class probing/removal are not dispatched, the BOT transaction checks and recovery are incomplete, the UHCI endpoint toggle model is insufficient for multiple BOT commands, and SCSI capacity/identity/sense/flush handling is incomplete. DM11 will implement the bounded shared read-only integration and the correctness foundations it needs, then use fake BOT/SCSI devices for Level 1 proof. USB filesystem writes and destructive Disk Manager eligibility remain disabled until a real supported controller/device proves the complete write-plus-SYNCHRONIZE-CACHE path. No physical removable drive is a test target.

## Implementation and validation

### Shared block integration and identity

- The production boot path now supplies the kernel physical load base to AMD64 UHCI, initializes the USB host/core after shared block initialization, and polls USB root ports from the main loop. The existing AMD64 controller is PIIX3-compatible UHCI with two root ports; no EHCI/xHCI implementation was added.
- USB interface parsing preserves actual interface numbers, alternate setting 0, endpoint ownership, and endpoint max packet size. The MSC probe selects SCSI-transparent Bulk-Only interfaces and their own bulk endpoints. The supported path is LUN 0; additional LUNs are not enumerated.
- Successful SCSI probe registers `BDEV_USB_MASS` through the common block registry. It exposes READ-only access, exact capacity and logical block size, SCSI vendor/product/revision text, VPD page 0x80 serial when available, VID/PID, root port, interface, and LUN. Disk Manager names the transport `USB Mass Storage` and shows removable/identity/cache-sync details from fields, not name heuristics.
- SCSI INQUIRY RMB supplies the authoritative removable bit. USB address is not used as durable identity. The exact block registration ID is new for every attachment; VID/PID, topology, interface, LUN, and serial are supplemental identity. The existing registry generation, pin, partition-view, and VFS backing identity prevent an old snapshot from resolving to replacement media.
- The driver retains removed contexts until the shared registration is no longer present, so a pinned/offline registration cannot be recycled into a live callback context. Disconnect unregisters the exact registration; normal VFS stale-view and stale-mount handling is reused.

### BOT, SCSI, geometry, and durability

- BOT uses exact CBW/CSW sizes and checks signature, command tag, residue, and CSW status. Data transfers are split into bounded chunks no larger than 64 KiB. Failed transport stages attempt Bulk-Only Mass Storage Reset and CLEAR_FEATURE on both bulk endpoints. Unrecoverable reset faults and offlines the device.
- SCSI support includes INQUIRY, VPD serial page 0x80, TEST UNIT READY, REQUEST SENSE, MODE SENSE(6) write-protect check, READ CAPACITY(10)/(16), READ(10)/(16), WRITE(10)/(16), and SYNCHRONIZE CACHE(10). Large LBAs use the 16-byte commands. Capacity rejects zero/unsupported logical block sizes, invalid last-LBA arithmetic, and byte-capacity overflow; it does not assume 512-byte sectors.
- REQUEST SENSE is issued after a failed SCSI command and preserves raw sense key/ASC/ASCQ in transport diagnostics. NO MEDIA, NOT READY, ILLEGAL REQUEST/unsupported, and DATA PROTECT/read-only map to distinct shared statuses. Other sense conditions remain I/O errors. Diagnostics retain VID/PID, interface, LUN, BOT stage, opcode, CSW status, sense, last transport/block status, and cache-sync state.
- A successful SYNCHRONIZE CACHE is recorded as supported and exposes the shared flush callback. An ILLEGAL REQUEST response is recorded as unsupported; other failures are recorded as failed. Unsupported/failed cache sync means persistence is unknown. No synchronous-write contract is claimed. Even when cache sync succeeds, USB's common `writeFn` remains null until hardware-backed BOT/HCI write and flush behavior is proven. This keeps ordinary USB FAT mounts read-only and all destructive operations ineligible. File write ability and destructive metadata durability remain separate gates.

### Fake transport, mount, lifetime, and failure proof (Level 1)

- The storage suite passes **419 checks, 0 failures**: the prior 340 DM10 checks plus 79 USB checks. The fake BOT/SCSI backend exercises the production BOT/SCSI command and shared-block callback code, not an alternate USB registration implementation.
- Fake proof covers shared USB registration/removal, LUN 0 identity and geometry, RMB removable state, VPD serial, READ(10), multi-sector/boundary checks, 4Kn geometry, READ CAPACITY(16)/READ(16) above 2 TiB, MBR and primary/backup GPT parsing, bounded partition views, FAT32 VFS mount, root enumeration, deterministic file reads, and MBR preservation.
- The FAT32 fixture was formatted and populated through the existing formatter/VFS on an in-memory disk, then exposed to the USB tests through fake BOT. File writes through mounted USB are rejected as read-only. The private BOT WRITE(10)/(16) implementation is tested for bounded success, write protection, command failure/sense, data-OUT failure, and end-of-media rejection; this is not public block/VFS write proof.
- Lifetime proof covers mounted and open-file removal, stale I/O, safe close, stale mount cleanup, exact unregister, same-media reinsertion with a fresh registration, different VID/PID media replacing the same port/address pattern, stale target/view rejection, and 100 attach/register/parse/mount/read/unmount/remove cycles. The stress fixture leaves zero block registrations, USB contexts, or mounts.
- Failure injection covers CBW, data IN/OUT, CSW read, invalid signature, wrong tag, command-failed CSW, failed REQUEST SENSE, timeout, no media, not ready, illegal request, medium error, write protect, capacity failure/overflow, disconnect during data, and failed BOT reset recovery. Recovery/reset faults are bounded and fail closed. These tests access in-memory media only.

### QEMU and physical proof (Levels 2 and 3)

- Level 2 was attempted with **QEMU 11.0.0**, `pc,usb=off`, `piix3-usb-uhci` (the controller supported by this tree), QEMU `usb-storage` BOT, and a repository-owned 600 MiB secondary image opened read-only. The guest enabled PCI I/O and bus mastering, found UHCI at `00:03.00`, detected root port 0 connected, and reached the main loop. USB MSC registration remained zero because the first enumeration control transfer timed out; the backing image SHA-256 remained unchanged.
- QEMU UHCI tracing shows it fetched the kernel queue head and processed the setup/data/status TDs, while the guest timed out with the TD active bit still set. This narrows the unresolved fault to completion/status visibility in the UHCI path; the exact cause is not established. Therefore QEMU USB registration, partition parsing, mounting, unplug, and persistence are **not** claimed. The hardware proof is Level 1 only, with a failed Level 2 attempt.
- No physical USB media was attached or accessed (Level 3: not performed). Automated tests and QEMU used only memory fixtures and a repository-owned read-only image; no host physical disk was passed to QEMU.

### Regressions, builds, and outcome

- Storage tests: **419 checks, 0 failures**. Full AMD64 kernel build: pass. UEFI bootloader build: pass. `git diff --check`: pass.
- The DM10 ATA QEMU regression passed **5/5 fresh 600 MiB secondary-image lifecycles** against the DM11 shared-storage changes. Each run completed GPT initialization, partition creation, FAT32 formatting, mount/file round trip, unmount/remount read, QEMU restart, rediscovery/remount, and independent GPT/FAT32 inspection. No host physical disks were attached. Evidence is at `out/dm11-ata-regression-proof/`.
- An earlier invocation with `-SkipBuild` did not include the compile-time DM9 proof harness and timed out waiting for its marker before any disk write; its image hashes remained identical. It is preserved at `out/dm11-ata-regression/` and is not counted as an ATA regression attempt.
- **Outcome B.** The fake transport proves the shared storage/partition/VFS path and safe lifecycle, but the real UHCI completion timeout prevents claiming supported-controller registration or end-to-end hotplug on QEMU. Shared USB writes stay disabled; unsupported or failed cache sync is unknown durability; USB destructive operations remain ineligible.
- Transport eligibility: ATA PIO remains eligible only under the existing complete shared safety gates; USB Mass Storage is shared-block readable and removable but read-only, with cache-sync state reported truthfully and boot provenance unknown; NVMe and AHCI are unchanged and remain ineligible for destructive operations.
- DM12 recommendation: diagnose and fix UHCI TD completion/status visibility under QEMU, then rerun the real Level 2 attach/read/mount/unplug gate. Only after read proof should BOT WRITE and SYNCHRONIZE CACHE be proved on QEMU disposable media and shared writes reconsidered. Do not add a USB-specific destructive-operation path.

### Evidence

- USB Level 1 and full suite: `out/dm11-full-build/` contains build logs; the storage test runner prints its 419-check result.
- QEMU Level 2 attempt, serial, trace, command line, image hashes, and manifest: `out/dm11-qemu-usb-probe-qhload/` (plus earlier preserved attempts under `out/dm11-qemu-usb-probe-*`).
- ATA regression evidence: `out/dm11-ata-regression-proof/repeatability-manifest.txt` and per-attempt serial logs, image hashes, and independent inspection reports under `out/dm11-ata-regression-proof/attempt-01/` through `attempt-05/`.

# Disk Manager Phase DM14 — USB Hotplug Lifecycle Hardening

## Outcome

**Outcome A** for the tested direct-root-port PIIX3 UHCI/QEMU path. Real QEMU
hot-unplug was detected while mounted, including with open file and directory
handles, during a multi-TD data-out write, during a VFS write, and between
SYNCHRONIZE CACHE submission and its CSW. Same-media reinsertion and different
same-capacity media in the same virtual port received fresh registrations.
Deterministic tests also exercised destructive-operation removal, rollback
identity checks, stale handles/views, slot reuse, and repeated hotplug churn.

Deliberately interrupted VFS writes may leave filesystem contents inconsistent;
DM14 requires accurate errors and identity containment, not filesystem recovery.

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `723ac561ee89f19dcae3c037b24de716438f9e70` (DM13)
- Tracked worktree was clean at start. All existing untracked `out/` evidence,
  including DM9–DM13 QEMU evidence, was retained.
- Cached `origin/DISK_MANAGER_IMPROVEMENTS` was equal to the starting HEAD
  (ahead/behind `0/0`). Normal fetch failed with
  `git@github.com: Permission denied (publickey)`; remote freshness is unknown.
- Reports through DM13, `docs/DISKMANAGER_IMPLEMENTATION.md`, and the UHCI,
  USB lifecycle, BOT/SCSI, block-registration, partition-view, mount, handle,
  Sync Cache, and destructive-operation paths were reviewed.

## QEMU mechanism and topology

The proof uses QEMU 11.0.0 with its PIIX3 UHCI controller, direct root port 1
on bus `uhci.0`, USB device ID `usbdisk`, and only repository-owned disposable
raw images. QMP issues `device_del`, waits for the `DEVICE_DELETED` event, then
uses QMP `human-monitor-command` with HMP `drive_add` to create a new raw-file
backend and QMP `device_add` to put a USB Mass Storage device with the same ID
back on `uhci.0`, port 1. Reusing the device ID and port makes the replacement
test deterministic. The runner waits on serial markers and QMP events with
deadlines and preserves command transcripts, topology snapshots, logs, hashes,
and raw-image inspection under `out/dm14-*`.

QEMU reports the direct UHCI root-port topology. The fixture uses QEMU's default
VID/PID (`46f4:0001`); no serial descriptor is specified. The A and B raw files
have the same size, sector size, VID/PID, and port, while their FAT file payloads
differ. No host USB passthrough or physical disk is attached.

## Disconnect detection and removal ordering

The runtime already polls USB root ports from the main loop. DM14 adds an
immediate topology check to UHCI TD waits and BOT/block operations. UHCI reads
root-port connect status and the write-one-to-clear connection-change latch;
the latch prevents a fast detach/reinsert between software polls from making
the old address appear online again. Active TD waits cancel when the monitored
device disconnects. The normal USB poll then retires the old address and
enumerates any currently connected device as new.

The removal sequence is:

1. Root-port disconnect or connection-change latch is observed.
2. The current transfer stops; no later BOT transaction starts for that USB
   address.
3. The exact block `registrationId` is marked offline immediately. Writes and
   Sync Cache fail, and removal revokes trusted flush state.
4. Registry generation changes; target snapshots and partition views no longer
   validate. Existing VFS mount backing checks return device-removed.
5. Open file and directory handles return device-removed; close remains safe.
   Open handles keep explicit unmount busy until closed.
6. USB polling releases the exact transport registration. Stale mounts can
   then be cleaned up; pinned operations release their exact registration pin.
7. A later enumeration receives a monotonic, distinct registration identity.

An offline slot is not reused while pinned. Slot reuse is safe after release
because registration IDs do not alias, partition-view generations change, and
VFS mounts retain their parent registration identity.

## Mounted idle and open-handle removal

The real QEMU proof mounts FAT32 with no open file or directory handles, removes
the device, checks the mount backing reports removal, and cleans the stale mount.
It then reinserts media A and reads the deterministic payload through a fresh
mount. A second removal occurs with a file and directory iterator open. Reads,
writes, and directory iteration return `VFS_ERR_DEVICE_REMOVED`, iteration is
not reported as EOF, close is safe, and unmount is busy until handles close.
The stale mount cleans up afterward.

The QEMU proof verifies the old target snapshot and block endpoint stay stale
across reinsert. File contents from A are then verified after a fresh mount.

## Active data-out and VFS-write interruption

The proof-only gate emits a serial marker immediately after the WRITE CBW is
accepted and before the data-out phase. The runner removes the device at that
marker. One case interrupts a 4 KiB FAT/VFS file write; the VFS call fails and
the stale mount becomes removable. Another interrupts a 128-sector (64 KiB)
raw block write that spans multiple UHCI TDs. The transport reports
`USB_WRITE_REMOVED_UNKNOWN` and the block layer reports
`BLOCK_ERR_WRITE_UNCERTAIN`; no automatic BOT retry occurs.

The observed raw image is inspected after QEMU exits. In the captured run, the
interrupted raw write's target LBA on A remained unchanged. The VFS interruption
may change B's FAT/file sectors and the independent DM13 content verifier then
reports the intentionally changed payload. The raw image state is recorded as
evidence; filesystem validity after interruption is not a DM14 requirement.

## Write outcome and Sync Cache interruption

Write outcomes remain distinct: a write rejected before submission is
`DefinitelyNotWritten`; a write whose data-out completion is unknown is
`PartiallyOrPossiblyWritten`; a completed write awaiting a failed flush is
`WrittenButNotDurable`; and `Durable` requires a successful validated Sync Cache.
Removal after data-out begins is not retried and is not reported as success.

For Sync Cache, the proof first completes a WRITE, then waits until the
SYNCHRONIZE CACHE CBW has been accepted and removes the disk before a valid CSW
can complete. The flush returns `BLOCK_ERR_DURABILITY_UNVERIFIED`, the exact
registration is offlined, trusted-flush status is revoked, and the operation is
not reported durable. There is no blind retry on reinsertion.

## Destructive operations and rollback identity

The deterministic storage suite removes media around Initialize Disk GPT,
Create Partition backup/primary metadata and flush, and FAT32 format metadata
stages. It checks structured failure/uncertain results, operation-lease release,
no false success, and that rollback runs only while the exact original
registration, incarnation, availability, and geometry still match. If that
identity is lost, rollback is skipped. Reusing the same USB address, root port,
capacity, sector size, and VID/PID does not authorize rollback onto replacement
media.

These destructive interruption boundaries are fake-transport tests; the real
QEMU proof focuses on idle/open-handle removal, VFS write, multi-TD data-out,
and Sync Cache.

## Reinsertion and replacement identity

Real QEMU removal/reinsertion on the same port proves:

- Same media A receives a fresh registration and can be parsed and mounted
  again. Old target snapshots, file handles, mount handles, and block endpoints
  do not revive.
- Different media B uses the same capacity, sector size, default QEMU VID/PID,
  QEMU device ID, and UHCI port. Its unique file payload is available through a
  fresh mount only; the old A endpoint fails and never reads B.
- A device without a usable USB serial descriptor still receives a fresh
  registration. Stable physical identity can remain `Unknown`; memory safety
  relies on the new registration/incarnation, not serial identity.

The fake suite repeats same-port, same-capacity, same-VID/PID, no-serial A→B
replacement for 25 cycles and separately retains the existing 100-cycle fake
USB attach/parse/mount/read/unmount/remove lifecycle. It verifies registry and
mount slots are released, generation identities stay distinct, stale callbacks
do not access replacement contents, and fresh registrations read the expected
replacement data.

## Disk Manager and File Explorer

Disk Manager Refresh/F5 re-enumerates online block registrations, clears the
removed disk's selection, and does not transfer selection to a new incarnation.
Unavailable mounts are shown as stale; mount/format actions require a current
target identity and disappear for the removed target. Refresh remains manual
because the block registry has no UI notification event.
When unmount cleanup removes a stale USB mount after a failed final flush,
Disk Manager reports that USB storage was removed and writes may be incomplete;
it does not claim that the mount remains active after cleanup.

File Explorer's Mounted drives view checks mount backing before navigation.
Opening a stale entry returns a removal error and cleans the mount after handles
allow it; it does not redirect the path to replacement media. Directory I/O
errors remain distinct from EOF. A newly mounted replacement appears as a new
valid entry after refresh.

## Hotplug stress

The deterministic suite completed 25 repeated same-port replacement cycles,
plus the existing 100-cycle fake USB mount/read lifecycle, with no block,
transport, partition-view, VFS-mount, or handle leaks. The real QEMU proof
reuses the same controller root port and QEMU device ID for each removal and
reinsertion in its end-to-end lifecycle.

## Test, regression, and build results

- Storage manager suite: **648 checks, 0 failures**, including **247 USB
  checks**; logs: `out/dm14-storage-tests-final.log`.
- Real QEMU hotplug proof and image inspection: `out/dm14-qemu-hotplug-proof-final-5/`
  (runner manifest, QMP transcript, serial log, topology, A/B images and hashes,
  interrupted-write raw inspection).
- QEMU test-only proof kernel build: `out/dm14-hotplug-proof-build-final.log`.
- Full AMD64 kernel build with proof-only hooks disabled:
  `out/dm14-amd64-release-build.log`.
- UEFI x64 Release build: `out/dm14-uefi-x64-release-build.log`.
- DM12 read-only USB reference-image proof passed at
  `out/dm14-readonly-regression-02/`; SHA-256 remained
  `EF75FE75254EEE78E3E7DA54556CB358F83B1E54AFB5770D8CF9D1AC13D89DAE`.
- One fresh ATA full lifecycle and restart/rediscovery proof passed at
  `out/dm14-ata-regression/` with independent GPT/FAT32 inspection; summary:
  `out/dm14-ata-regression/dm10-manifest.txt`.
- Normal writable USB lifecycle regression passed **3/3 fresh 68 MiB images**:
  each ran Initialize GPT, Create Partition, Format FAT32, shared-block VFS
  write/read, ten mount/write/read/unmount cycles, cold restart persistence,
  and independent GPT/FAT32 verification. Evidence is in
  `out/dm14-usb-lifecycle-02/`, `out/dm14-usb-lifecycle-03/`, and
  `out/dm14-usb-lifecycle-04/`; cohort summary:
  `out/dm14-usb-lifecycle-cohort.txt`. The first attempt hit a short-CSW
  timeout during Initialize; its disposable image was independently verified
  all-zero and excluded from the pass count.
- `git diff --check`: PASS. PowerShell QEMU-runner parse: PASS.
- Full AMD64 release kernel build after the final source change:
  `out/dm14-amd64-release-build-final.log`; proof-kernel build:
  `out/dm14-hotplug-proof-build-final.log`; UEFI x64 Release build:
  `out/dm14-uefi-x64-release-build.log`.

## Remaining limitations and DM15

Only direct UHCI root-port devices are supported; USB hub traversal remains
unsupported. USB WWID boot paths that require a serial descriptor and hub paths
remain `Unknown` for boot provenance. Hotplug invalidation itself works without
serial identity. No physical USB or host disk was used.

The real QEMU proof does not claim filesystem crash consistency after removal
mid-write. It verifies truthful errors and that stale references cannot reach
replacement media. The destructive interruption boundaries are covered by fake
transport tests; physical USB testing remains optional and was not performed.

If DM14 reaches Outcome A, recommend **AHCI or NVMe** as DM15 based on remaining
transport coverage. NVMe Flush/durability, FAT32 scalability (about 3.66 GiB
formatter ceiling), 4Kn filesystem support, and advanced partition editing are
still separate gaps; for adoption, expanding SATA/PCIe transport coverage takes
priority over more partition editing.

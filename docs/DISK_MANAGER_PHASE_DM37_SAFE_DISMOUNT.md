# DM37 — Safe Volume Dismount Foundation

**Outcome B — `DM37_DISMOUNT_ENGINE_READY` for the host-qualified production
VFS lifecycle.** `.phase` remains `last_completed=DM36` and
`next_expected=DM37`. The Disk Manager GUI flow and guest/QEMU acceptance are
future work.

## Scope and repository gate

The authoritative roadmap and history had no competing approved DM37 scope.
The next phase was DM37 safe volume dismounting, so implementation extended
the existing VFS and FAT driver rather than adding a second mount manager.

The work started from commit
`a2afa18a56c95b00491f9b1ca2a26acde7669567` on
`DISK_MANAGER_IMPROVEMENTS`. Repository identity, `.phase`, and the existing
worktree were checked before implementation. The pre-existing untracked
`scripts/__pycache__/dm34_qmp_gui_input.cpython-312.pyc` was not modified; its
SHA-256 remained
`B25DA7AB48CCDCDA8A9AE4AC6856770F0D1353B78F39F2EC5A0134D426ED28D5`.
No branch, worktree, merge, rebase, reset, clean, stash, or phase-file change
was made.

## VFS and FAT lifecycle audit

- `kernel/core/vfs.cpp` owns a bounded mount table, file-handle table, and
  directory-iterator table. Path calls select the most specific active mount.
- FAT file descriptors and the directory iterator belong to
  `kernel/core/fs_fat.cpp`. The previous FAT unmount forcibly invalidated open
  file slots; it now refuses teardown while any file slot or directory
  iterator references the volume.
- VFS writes update FAT and directory sectors synchronously. FAT entries are
  read and updated on demand; there is no dirty whole-FAT cache or queued
  metadata writeback to invent or drain. Write-handle close already flushes.
  Successful writable FAT dismount performs a final device Flush. A partition
  without supported durable write/Flush capability remains read-only and is
  not asked to claim durable settlement.
- Partition-backed mounts already use bounded partition block views and retain
  the parsed MBR/GPT partition identity. Block registrations carry an exact
  registration ID, sector geometry, and boot provenance; storage operations
  can pin a captured target identity and serialize through
  `StorageOperationLease`.
- Existing Format, Wipe, Erase, surface-verification, and FAT32 integrity
  operations use that shared storage-operation lease. Dismount takes the same
  lease and exact registration pin, so it conflicts with those operations.
- There was no VFS mount-reference count, in-flight VFS operation count, or
  admission state. This was the missing safe-dismount prerequisite.

## Production lifecycle and identity contract

`vfs::capture_mount_identity()` captures an immutable snapshot containing the
mount slot and non-reused mount generation, block-device index and registration
ID, registry generation, logical-sector size, partition scheme/entry identity,
partition extent, and exact partition-view slot/generation. The new
`vfs::dismount(const MountIdentity&)` uses that snapshot; the existing
`vfs::unmount(path)` remains available as a wrapper that captures the exact
current mount before calling the identity-based API. Repeated or stale
identities fail before device I/O.

The VFS now tracks open file and directory references plus active operations
per mount. File and directory operations acquire a short-lived mount
operation reference. A small kernel-native metadata spinlock protects table
state and reference transitions; it is released before filesystem or block
callbacks. Handle slots are reserved and committed under the same lock, so a
new open cannot become a live handle after quiescence starts. Operations on one
handle are serialized, and a close in progress remains an active reference
until it finishes. Partition validation uses the same fail-fast mount-admission
gate as mount creation because the existing parser uses one bounded static
scratch table; it cannot overlap dismount or another table parse.

Dismount validates the exact mounted identity and backing registration,
rejects root, `/boot`, and `/system` mount paths, and rejects boot provenance
unless it is definitely not the target. It obtains the shared storage-operation
lease, pins the exact target when online, then atomically changes the mount
from `Mounted` to `Quiescing`. Existing opens, directory iterators, active
path/handle I/O, another mount admission, and aliases that still depend on the volume all
prevent teardown. Busy attempts restore `Mounted` before performing filesystem
I/O. While quiescing, new path operations, opens, aliases, and handle activity
are rejected.

Dismount holds the shared storage-operation lease through teardown, including
when the original registration is already offline. An online target is pinned
by its captured identity. Offline stale cleanup takes no device pin and issues
no device I/O; the lease prevents a same-index replacement from entering VFS
mount admission while the old FAT volume slot and partition view are released.

After successful admission, writable FAT32/exFAT settlement checks the exact
partition identity again and issues the supported device Flush. A Flush or
backing check failure restores `Mounted` only if the original registration,
partition, and mount remain valid. Otherwise, stale state is detached without
further device I/O and the error is returned. FAT's own unmount check is a
second guard against open descriptors. Partition views and the mount slot are
released only after filesystem teardown succeeds. RAII lease ownership
releases the storage lease exactly once on every exit path.

## Host qualification

Seven DM37 production-path assertions were added to
`tests/storage_manager_test.cpp`. They cover:

1. Open-file and open-directory busy rejection; an active I/O attempt; open and
   duplicate-dismount attempts from the Flush callback; and successful
   teardown after the handle closes.
2. Mount-generation isolation, stale and repeated requests, and a real
   FAT32-file write/dismount/remount/readback round trip.
3. Root and `/system` aliases plus their shared backing volume are protected.
4. Known boot backing is protected; stale cleanup after removal performs no
   Flush.
5. Unsupported durability capability makes the mounted partition read-only;
   its dismount makes no write or Flush request.
6. A changed MBR partition extent rejects the captured identity and cleans up
   without flushing the stale extent.
7. An active storage-operation lease rejects dismount. Removal during final
   Flush terminates the original registration; a reentrant mount on the
   same-capacity replacement is rejected before reading it. The replacement
   receives no writes or Flush and inherits no mount state.

The latest complete `scripts/run-storage-manager-tests.ps1` run passed
**1,489/1,489 storage checks, 0 failures**, including the DM37 coverage, DM36
FAT32 integrity engine **259/259**, DM36 GUI policy **31/31**, DM35 engine
**48/48** and GUI **34/34**, DM34 wipe **17/17**, whole-disk erase **34/34**,
wipe UI **31/31**, erase UI **39/39**, and **249 USB checks**. Widget
activation passed; the USB trace classifier passed **7/7**, the GPT verifier
passed **1/1**, and the DM30 fixture verifier passed **1/1**. The full test
run used only deterministic host/test devices.

## Builds and compatibility

- Freestanding AMD64 kernel: PASS, using the isolated output
  `out/dm37/kernel-build/` and `NVME_WRITE_PROVEN=0` /
  `NVME_FLUSH_PROVEN=0`.
- Root `build.bat`: PASS; `guideXOSServer.exe` built.
- UEFI x64 Release bootloader target: PASS.
- `git diff --check`: run after the final source and documentation edits.

Build logs and artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Isolated AMD64 `kernel.elf` | `FF9E055978096DA98365E37A21D4AE30F23E2D4ECB410B1D60BD32DC10CF00B8` |
| Hosted `guideXOSServer.exe` | `2F5E95AA404F27E7A2CE5E217AFA9F1F9F6086A76D912BDE0B8E485F802990` |
| UEFI x64 Release bootloader output | `7B7418D3E5FB34916F4D4CC5982D1D1DC64C3538DBBB16594E736BC90560A754` |

The corresponding logs are `out/dm37/kernel-build.log`,
`out/dm37/build-bat.log`, and
`out/dm37/uefi-x64-release-build.log`.

Production C++ changes use bounded kernel structures and existing integer and
memory primitives. They introduce no STL, exceptions, RTTI, or dynamic
allocation. Hosted-test vectors remain test-only. NVMe write/Flush proof flags
remain disabled. No QEMU or physical media was used. The separate top-level
PowerShell/PacMan Native ELF linker issue was not part of these requested build
targets and PacMan was not modified.

## Deferred work and next continuation

No Disk Manager GUI action was added. A future flow should let the user select
a mounted FAT32 data volume, choose **Dismount Volume**, review the exact target
and busy status, explicitly confirm, show the settled Unmounted state, run
DM36 only after a separate user action, and allow explicit remount afterward.
There must be no automatic dismount → check → repair → remount sequence.

This continuation does not qualify the graphical confirmation path or a
guest lifecycle. QEMU guest acceptance belongs to a later DM37 continuation.
Recommended next work is to connect the identity-based API to the Disk Manager
confirmation and busy-state UI, add any UI-specific tests, then perform
source-matched guest qualification. Keep `.phase` at DM36→DM37 until those
remaining acceptance gates are complete; do not advance to DM38 here.

## DM37 Continuation 2 — production Disk Manager safe-dismount GUI

**Outcome B — `DM37_GUI_READY` for host integration and qualification.** The
work started from `2a43d1331095cf5a5447475c79e2ca6a78a9a3d7` on
`DISK_MANAGER_IMPROVEMENTS`. `.phase` remains `last_completed=DM36` and
`next_expected=DM37`. No QEMU guest or physical media was used; guest lifecycle
acceptance remains outstanding.

The production Disk Manager in `kernel/core/kernel_apps.cpp` now presents
**Dismount Volume** for a uniquely identified mounted partition. Inventory
scanning copies each active mount through `vfs::copy_mount_snapshot()` under
the VFS metadata lock and matches the partition extent, scheme, GPT GUID or
MBR type, device registration, registry generation, sector geometry, and
partition-view generation. Ambiguous matches disable the action. Opening the
confirmation captures an immutable `MountIdentity`, device target, parsed
partition, and exact mount path without dismounting or flushing anything.

The final activation revalidates the captured mount and device, lifecycle,
backing partition, geometry, boot/root/system protection, busy counts, shared
storage-operation ownership, and durable-write capability. It rereads the
partition table and rejects a changed, missing, or duplicate partition entry.
Offline original registrations are reported as removed/unavailable. The
production call is `vfs::dismount(capturedIdentity)`; VFS admission remains
authoritative and performs quiescence, FAT teardown, and required durability
settlement. No duplicate GUI Flush or asynchronous job was added. There is no
cancel/progress claim during the synchronous call; Close can only be handled
after it returns. The existing explicit **Mount...** action remains available
and uses its normal registration and mount admission path.

The confirmation displays device, partition, exact mount path, lifecycle,
registration/generation, partition extent and sector size, real open-file,
directory and active-operation counts, and the actual durability capability.
Cancel and Escape close without dismount. The standard widgets require an
explicit pointer or keyboard activation; initial focus is Cancel, Tab reaches
Dismount only while admission is ready, and Enter/Space use the ordinary
widget dispatcher. Disabled confirmation cannot start. Runtime IDs are
reported during Disk Manager initialization: Mount 5, Dismount Volume 6,
Confirm Dismount 11, Cancel/result dismissal 12. A successful result is shown
only for `VFS_OK`, refreshes the inventory to Unmounted, and states the block
device remains registered. It does not imply ejection. DM36 Check FAT32 is
enabled only when its ordinary admission is READY, so a successfully
dismounted eligible volume can be checked through a separate user action.
There is no automatic remount, integrity check, repair, format, wipe, or erase.
Failed terminal paths show the real VFS error and current mount/backing state.

Production-linked host coverage uses a real formatted and mounted FAT32 test
device to capture the GUI target, verify that confirmation does no dismount or
Flush, reject changed/duplicate table entries and stale mount generations,
observe file and directory busy references, exercise active-I/O rejection,
perform the VFS dismount, and explicitly remount/read back preserved content.
Widget dispatcher checks cover focus, unrelated textbox Enter, Cancel, disabled
activation, Space, pointer activation, and repeat activation. These host checks
exercise shared production VFS and widget code; they do not render the full
bare-metal Disk Manager or qualify physical keyboard timing.

The final storage suite passed **1,491 checks, 0 failures**. This includes
DM36 metadata/structural **259/259**, DM36 GUI **31/31**, DM35 scan **48/48**,
DM35 GUI **34/34**, DM34 wipe **17/17**, whole-disk erase **34/34**, wipe UI
**31/31**, erase UI **39/39**, and **249 USB checks**. Widget activation passed;
USB trace classification passed **7/7**, GPT verification **1/1**, and the
DM30 fixture verifier **1/1**. `git diff --check` passed. The hosted
`build.bat` completed successfully. The UEFI x64 Release bootloader rebuilt
successfully. The freestanding AMD64 kernel build with
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` passed after all final source
changes, including the partition-table revalidation refinement and responsive
toolbar sizing. The final isolated build output was written to
`C:\Users\guideX\AppData\Local\Temp\dm37-gui-kernel-build\`; D: had reported
zero free bytes during the first attempt. No PacMan source or generated object
was changed for the known unrelated top-level linker failure.

Final build artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| Freestanding AMD64 `kernel.elf` | `6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B` |
| Hosted `guideXOSServer.exe` | `C846EFDB1D6AC8695C1E0554AC60A7C5BA230C21E2056597C2CA278F5ACFDA20` |
| UEFI x64 Release bootloader | `A0F1E0A46D166D1C99828AE07A80C2ECDAF8A2517442C07CB74BC316D5A455C1` |

The pre-existing untracked Python cache remains unchanged at SHA-256
`B25DA7AB48CCDCDA8A9AE4AC6856770F0D1353B78F39F2EC5A0134D426ED28D5`.
Implementation, tests, and host evidence are recorded in this continuation.
Review, commit, and the single requested push attempt remain to be recorded.
Guest/QEMU safe-dismount, durability-failure,
removal/replacement, Close, and visual/input acceptance remain the next DM37
continuation. Do not advance `.phase` or begin DM38.

## DM37 Continuation 3 — private guest reached Mounted; acceptance incomplete

**Outcome B — `DM37_GUEST_EVIDENCE_INCOMPLETE`.** The repository gate matched
the requested source revision `c40efd34eb67aa71b23e4f48f5243d8f18d11771`
(`disk-manager: add DM37 safe dismount GUI`) on
`DISK_MANAGER_IMPROVEMENTS`. `.phase` was and remains `last_completed=DM36`,
`next_expected=DM37`. The continuation made no production-source changes. A
documentation-only commit records this attempt; the source-under-test remains
the exact starting revision. The pre-existing untracked Python cache retained
its required SHA-256.

### Private runtime and fixture preparation

The fresh AMD64 freestanding build passed with
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`. Its kernel SHA-256 is
`6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B`. The
private UEFI x64 Release bootloader rebuild passed with SHA-256
`736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7`. Both
were copied into `out/dm37/dismount-guest/runtime/esp/` and match those hashes
there. The read-only OVMF code image is
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. The
runtime manifest records source hashes, build commands, firmware, and the
private ESP. QEMU was 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

The read-only fixture verifier passed on the preserved 600 MiB DM30 source;
the DM36 pristine source is byte-identical to it. Seven independent writable raw USB
copies were prepared under `out/dm37/dismount-guest/fixtures/`; each is
629,145,600 bytes with SHA-256
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`. Each
has a 512-byte logical sector, 1,228,800 total sectors, and GPT partition 1
named `DM9 QEMU Proof` at LBA 2,048 for 1,226,719 sectors (end LBA 1,228,766).
The actual partition GUID is
`829681EB-8BBF-45B1-814A-C54A926DB193`; the GPT disk GUID is
`8A849145-40A5-4DC6-928E-C2BBCC8AEB0F`. FAT32 preflight passed with 16 sectors
per cluster, 32 reserved sectors, two FATs of 599 sectors, 76,593 clusters,
root cluster 2, FSInfo sector 1, backup boot sector 6, volume ID `6A699895`,
and label `DM9PROOF`. The fixture manifest records each serial, QEMU device ID,
initial hash, and final hash. Its partition GUID was corrected from a manifest
placeholder by decoding the GPT entry from the fixture.

The run attached only `DM37IDLE01.raw` and the private ESP. No physical storage
was attached. QEMU PID 36256 (parent 37376) belonged to this workspace and used
QMP `127.0.0.1:58951`; its owner manifest contains the exact command line and
topology. The guest used WHPX, standard VGA at 1280×800, UHCI USB mass storage,
and the implicit PS/2 keyboard and relative mouse. The image was a disposable
raw USB block backend with writeback cache; no guest file write was requested.

### Gate A result

The guest booted to the ordinary desktop. Disk Manager was launched through
the Start Menu, the `DM37IDLE01` USB device was selected, and its FAT32 GPT
partition was selected through the normal inventory. The GUI showed the
`DM9 QEMU Proof` partition at the expected extent and then showed
`Mounted at /mnt/disk1-part1 (Read/Write)` with one mounted partition. The
mounted state, target selection, and mount result are captured in
`out/dm37/dismount-guest/runs/A-idle-dismount/`.

The current guest initialization log reported Mount widget 5, Dismount Volume
6, Confirm 11, and Cancel/result dismissal 12, matching the source-verified
IDs. Those IDs were not treated as evidence that activation occurred.

The Dismount Volume button was visible and could be hovered, but repeated
pointer and keyboard attempts did not activate it. The confirmation dialog
never opened. Accordingly, there is no captured confirmation identity, no
explicit confirmation, no call to `vfs::dismount(capturedIdentity)`, no VFS
terminal result, and no observed Unmounted transition. The partition remained
present and mounted until the owned QEMU process was closed. This is an input
qualification gap; it does not demonstrate a VFS defect. QMP acknowledgments
were not treated as proof of GUI action delivery. Screenshots whose names
refer to a confirmation attempt show the unchanged main window, not a dialog.
Key captures are `fat32-partition-selected.png`, `mount-action-result.png`,
`mounted-state.png`, `dismount-hmp-hover-3.png` (hover only),
`confirmation-after-button-click.png` (still mounted), and
`final-mounted-state.png`. The serial log, QMP counter snapshots, process
census, and shutdown verification are alongside them in the same run folder.

QMP target counters at the start of the final input-attempt window were
35,477 reads / 18,356,224 bytes, zero writes, and one Flush. At guest exit they
were 39,987 reads / 20,665,344 bytes, zero writes, and the same one Flush; all
reported failed read/write/Flush operation counts were zero. The extra reads
accumulated during the selected mounted-volume UI session and cannot be
attributed to a dismount. The single Flush was already present from startup /
registration activity and did not increase during the attempt. QMP also set
its `account_invalid` and `account_failed` metadata flags, so these counters do
not constitute operation-scoped dismount evidence.

### Ownership stop and media verification

A live process census found another project's QEMU while Gate A was still in
progress. One census showed PID 11008 from
`C:/gxR3work/out/taskmanager-memorydetails-qualification-r3/lifecycle-one-managed-producer-20261018`
with QMP port 61431. The final stop census showed PID 1872 from the same other
project's `allocator-locator-interlifetime-gap-20261018` run with QMP port
50620. These were identified only from process metadata and command lines;
their QMP endpoints and media were not contacted or changed. No other DM37
guest was launched. The campaign-owned PID 36256 was closed through its own
QMP endpoint. Verification then showed that PID 36256 was gone, port 58951 was
closed, the image opened with exclusive access, and `DM37IDLE01.raw` matched
its pristine SHA-256 exactly. All six unattached disposable copies and both
preserved source images also matched the pristine hash. No intentional or
unexpected target modification was found.

### Gate reconciliation

| Gate | Continuation 3 evidence |
| --- | --- |
| A — successful dismount | Mount succeeded; Dismount activation and confirmation did not. No VFS dismount result. |
| B — busy-volume rejection | Not run in the guest. Existing production-linked host busy-reference tests remain retained. |
| C — protected volumes | No guest root/boot/system inventory reconciliation. Existing host protection evidence remains retained. |
| D — durability | Not run; no controlled guest file write, dismount Flush, remount readback, or persistence claim. |
| E — DM36 workflow | Not run; no dismount → checker → remount sequence. |
| F — removal/replacement | Not run; only the idle fixture was attached. Existing host race evidence remains retained. |
| G — failure/rollback | Not run; no targeted storage or Flush failure was injected. Existing host failure evidence remains retained. |
| H — keyboard, pointer, Cancel, Close | Pointer hover worked, but activation was not delivered. Tab/Enter/Space, Cancel/Escape, selection-change, repeated activation, and Close lifecycle remain unqualified in the guest. |
| I — independent media and ownership | Partially passed for the idle fixture: owned QEMU exited, QMP closed, exclusive access succeeded, and image hash was unchanged. No dismount lease settlement or operation-scoped dismount I/O was observed. |

No host test suite was rerun because production source did not change. The
existing 1,491/1,491 storage result and prior DM36–DM34, USB, widget, GPT, and
DM30 host qualifications remain retained. The private AMD64 kernel and UEFI
Release bootloader builds passed as recorded above. No DM38 work began.

The exact remaining acceptance requirement is a source-matched guest run in a
clear owned QEMU lane with verified production input delivery. It must complete
Gate A first, then Gate E and the remaining B–I evidence in the prescribed
order. Keep `.phase` at DM36→DM37 until every mandatory guest and host
acceptance requirement is satisfied.

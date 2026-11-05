# DM37 — Safe Volume Dismount Foundation

**Outcome B — `DM37_BUSY_REFERENCE_GUEST_UNAVAILABLE`.** Continuation 16 adds
production-linked host qualification for Gates B and C and a safe, source-
matched guest inventory observation. No supported production workflow was
found that keeps a real file or directory reference open while Disk Manager
is used, so guest Gate B remains unavailable. The guest showed the running
root device as protected and exposed no Dismount control for its selected
partition; no dismount attempt was made. Gates A, D (USB BOT), and E remain
accepted. `.phase` remains `last_completed=DM36` and `next_expected=DM37`;
DM37 is incomplete and DM38 has not started.

**Latest continuation status (2026-11-01): Outcome B —
`DM37_BUSY_REFERENCE_GUEST_UNAVAILABLE`.** The production-linked storage
suite passes 1,503/1,503 checks, including actual file/directory ownership,
active-I/O, close-settlement, and protected/stale-target dismount cases. Gate
B guest testing could not be performed safely through an existing production
workflow. Gate C host checks fail closed, and the guest observation is limited
to the protected root inventory and absent Dismount control. Continuation 16
did not change production source or repeat the accepted writable Gate D
workflow. See its report at the end of this document.

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

## DM37 Continuation 4 — activation boundary review; foreign QEMU owns the lane

**Outcome B — `BLOCKED_BY_FOREIGN_QEMU` for this continuation.** The required
repository gate still matched branch `DISK_MANAGER_IMPROVEMENTS`, starting
HEAD `e2e47c3c85463c92ef586ab3ef71ea23aa4eac4f`, and `.phase`
`last_completed=DM36`, `next_expected=DM37`. The tracked worktree had no source
changes. The pre-existing untracked
`scripts/__pycache__/dm34_qmp_gui_input.cpython-312.pyc` remains at SHA-256
`B25DA7AB48CCDCDA8A9AE4AC6856770F0D1353B78F39F2EC5A0134D426ED28D5`.

A fresh process census found three active QEMU guests from other projects:
PID 1872 in `gxR3work/.../taskmanager-memorydetails-qualification-r3` with
QMP port 50620, PID 20384 in another run of that project with QMP port 62449,
and PID 12116 in the sibling `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
project with QMP port 54569. Their process metadata showed separate project
outputs and `-display none` guest configurations. No foreign QMP endpoint was
contacted and no foreign media was inspected or changed. Following the prior
DM36/DM37 exclusive-lane gate, no new DM37 guest was launched. This is an
environmental stop, not a dismount result.

### Host-side reconstruction of the first failed transition

The already-preserved guest run used source revision
`c40efd34eb67aa71b23e4f48f5243d8f18d11771`. Source review and its saved
screenshots narrow the earlier failure to the activation handoff:

1. `confirmPartitionMount()` closes the mount dialog and calls `scanDisks()`.
   `scanDisks()` saves the selected disk incarnation and selected partition,
   refreshes the inventory, then restores that partition when its identity
   still matches. The saved post-mount screenshot shows the same GPT partition
   selected and one mounted partition at `/mnt/disk1-part1`.
2. Inventory population matches the active mount to the exact partition and
   captures its mount identity. `Dismount Volume` visibility and enabled state
   use the same predicate: the selected partition is mounted, has exactly one
   mount identity, and that identity is valid. The button is visible in the
   post-mount screenshot; the code therefore makes it enabled for that frame.
   The saved hover screenshot also places the cursor over the Dismount button
   and shows its hover highlight.
3. Recorded click attempts left the main window and its `Mounted at
   /mnt/disk1-part1 (Read/Write)` status unchanged. No confirmation dialog or
   stale-target status appeared. The production path would call
   `beginDismountConfirmation()` from widget 6; a rejected target capture would
   update the status. The evidence therefore places the first unobserved
   transition between the pointer activation attempt and that confirmation
   handler. It does not separate a lost PS/2 press/release from a compositor
   widget-dispatch failure because the guest had no bounded app-side input or
   dispatch trace. QMP/HMP acknowledgments alone do not settle that boundary.

The old guest used `-display none`, a standard VGA framebuffer, and the
implicit relative PS/2 mouse and keyboard. Its screenshots record guest cursor
movement and hover, but there was no QEMU window to foreground and no visible
window pointer-grab state to qualify. The prior input files record Tab presses
and clicks, but no Right-arrow navigation to the Dismount action followed by
Enter or Space. Disk Manager cycles Tab through panes; the Actions pane uses
left/right arrows to move among enabled controls, and Enter or Space dispatches
the focused action. The prior keyboard captures leave Refresh highlighted, so
they do not prove that Dismount received a keyboard activation.

Mount identity capture and the confirmation path were not reached. The guest
serial log reports widget IDs Mount 5, Dismount 6, Confirm 11, and
Cancel/result dismissal 12. There is no confirmation snapshot of mount
generation, no Cancel/Escape exercise, no `vfs::dismount(capturedIdentity)`
call, no Dismount result, and no `Unmounted` transition. No dismount storage
lease was acquired, and no Gate A Flush can be attributed to this action.

### Preserved runtime, target, and I/O evidence

The run used source revision `c40efd34eb67aa71b23e4f48f5243d8f18d11771`;
`kernel/core/kernel_apps.cpp` is SHA-256
`D93BBD2C1B966F74D5ADC1EFE3B3F20DF506151A533CE24F82BBDB6781A4E812`, and
`kernel/core/kernel_compositor.cpp` is
`AF0E078B905ACDC564ED4C26A5EF84F795EDDE31F1F9001752DED85DB14214C0`. The
source-matched AMD64 kernel SHA-256 is
`6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B`; the
private ESP contains the same kernel, UEFI x64 Release `BOOTX64.EFI`
`736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7`, and
`ramdisk.img` `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`.
OVMF SHA-256 is
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. QEMU
was 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`), binary SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; no physical host storage was
attached.

The selected disposable target was USB serial `DM37IDLE01`, QEMU device
`dm37idle01`, 629,145,600 bytes / 600 MiB, with 512-byte sectors and 1,228,800
disk sectors. GPT partition 1, `DM9 QEMU Proof`, starts at LBA 2,048 and spans
1,226,719 sectors through LBA 1,228,766; its partition GUID is
`829681EB-8BBF-45B1-814A-C54A926DB193`. FAT32 geometry is 16 sectors per
cluster, 32 reserved sectors, two 599-sector FATs, 76,593 clusters, root
cluster 2, FSInfo sector 1, backup boot sector 6, volume ID `6A699895`, and
label `DM9PROOF`. The mount path was `/mnt/disk1-part1`; the inventory showed
parent block identity 1, registration 2, and partition view slot 1 / generation
2. The confirmation-only mount generation was never captured.

For the prior run's final input-attempt window, the target counter snapshot
rose from 35,477 reads / 18,356,224 bytes to 39,987 reads / 20,665,344 bytes.
Writes stayed at zero; Flush stayed at one, already present from startup and
registration. Failed read/write/Flush counts were zero, but QMP marked its
accounting metadata invalid and failed, and the accumulated reads are not
operation-scoped. The current idle fixture hash is still
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`, matching
the recorded initial and final hashes. The previous owned QEMU exited, QMP
port 58951 closed, and exclusive host access was verified.

No source was changed, so this continuation did not rerun the host suite or
builds. The retained source-linked result is 1,491/1,491 storage checks, plus
the previously recorded widget activation and DM37-related suites. The prior
freestanding AMD64 kernel, `build.bat`, and UEFI x64 Release builds passed with
the NVMe proof flags above. Gate A and the separate DM36 Check/Remount workflow
remain unqualified; Gates B–I remain open. Keep `.phase` at DM36→DM37 and do
not begin DM38.

## DM37 Continuation 5 — visible QEMU Gate A verified

**Outcome B — DM37_GATE_A_VERIFIED.** The repository gate started on branch
DISK_MANAGER_IMPROVEMENTS at HEAD
f4eb7180063f8a43fa4ac38e4bd140b96b761ca1. The phase marker remains
last_completed=DM36, next_expected=DM37. No production source changed. The
pre-existing untracked Python cache remains at SHA-256
B25DA7AB48CCDCDA8A9AE4AC6856770F0D1353B78F39F2EC5A0134D426ED28D5.

### Owned visible guest and target

A fresh initial QEMU census found no QEMU process. One source-matched private
guest ran in a visible, foreground GTK window; it did not use -display none.
The owned process was PID 16368 (parent PID 9468), QEMU 11.0.0
(v11.0.0-12122-ga4bb4b10c9), QMP 127.0.0.1:58953. The runtime manifest records
ordinary production startup, no test-only hooks, and no physical host storage.
Both NVMe proof flags remained zero.

The production source revision was
c40efd34eb67aa71b23e4f48f5243d8f18d11771. Runtime hashes were: kernel
6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B,
kernel_apps.cpp D93BBD2C1B966F74D5ADC1EFE3B3F20DF506151A533CE24F82BBDB6781A4E812,
kernel_compositor.cpp AF0E078B905ACDC564ED4C26A5EF84F795EDDE31F1F9001752DED85DB14214C0,
UEFI x64 Release bootloader 736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7,
ramdisk 5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD,
OVMF 2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792, and
QEMU binary A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02.
The private ESP canonical file-tree SHA-256 is
4FEDDBE35800ADE1DAE72590453BE352369CE66E1C18D7AEB99EBA66FB75B0BF; it hashes
sorted relative POSIX paths, NUL separators, each file SHA-256, its decimal
length, and LF. Its desktop.cfg hash is
4AAF37B7847D0002997AE18824694E5B2538225FEFC8D41635D5AE343DC7A07C.

The attached disposable target was USB serial DM37IDLE01, shown by Disk
Manager as usb-46F4:0001-00-0-10. It was GPT partition 1, named DM9 QEMU Proof,
FAT32 volume label DM9PROOF, partition GUID
829681EB-8BBF-45B1-814A-C54A926DB193, start LBA 2048, 1,226,719 sectors,
ending at LBA 1,228,766, with 512-byte sectors. The full image length is
629,145,600 bytes. QEMU attached the raw fixture with readonly=on.

### Activation boundary and confirmation

The earlier non-activation is explained by the pane sequence, not by a
reproduced production defect. Disk Manager cycles Tab through Disks,
Partitions, Actions, and Details. Left/Right only moves action focus in
Actions; a Right press while Details was selected correctly did not select a
button. After reaching Actions, paced Right presses visibly moved focus from
Properties to Diagnostics, Verify Partition, Check FAT32, and Mount. The focus
captures are in the run folder. After the normal Mount action, Disk Manager
kept the same partition selected, reported Mounted at /mnt/disk1-part1
(Read Only), and focused Dismount Volume.

Mouse press and release were delivered as separate QMP PS/2 transitions to
the guest Start button, Disk Manager menu item, and USB disk row. Each changed
the visible guest UI. This run did not send a mouse click to the Dismount
button because the keyboard action path opened the production confirmation.

The confirmation identified the selected USB device, GPT partition 1, mount
path /mnt/disk1-part1, device registration 2 and registry generation 5,
extent LBA 2048 through 1,228,766, and sector size 512. It showed zero open
files, zero open directories, zero active I/O, read-only durability, and
“Ready to dismount the captured volume.” The numeric mountGeneration is not
rendered by the dialog. The production capture and revalidation compare the
full MountIdentity, including mount index and mountGeneration, device
registration, partition identity, and partition-view generation.
Storage diagnostics reported “Definitely not boot device”; the production
admission path retained root, boot, and system protection checks.

Escape was tested first. After the guest processed it, the confirmation
closed, the selected volume remained Mounted at /mnt/disk1-part1, and the
partition remained available. QMP showed zero writes and no Flush increase;
the ready confirmation and post-cancel screen show no dismount result or
active operation. The confirmation was reopened through the same GUI action.
One Tab moved focus from Cancel to the green Dismount button. Enter was sent
while that button was visibly focused. The screen remained on the ready
confirmation during synchronous identity and partition-table revalidation.
A later Space probe was sent while the previous frame still showed Ready; the
guest exposes no per-key dispatch trace, so the exact consumption time of
that follow-on key cannot be recovered. No mouse button event was sent to the
confirmation.

The production path captures and revalidates the target, then
runDismountOperation calls vfs::dismount with the captured MountIdentity. Its
VFS_OK branch displayed “Volume Dismounted.” After the production scan, the
main view showed Unmounted and Mount path Unknown, the original mount was no
longer enumerated, the selected FAT32 partition remained available, and the
Mount action returned. Disk Manager repainted the successful state and kept
the block device registered; it did not eject or safely remove it. No
automatic FAT32 check, repair, or remount ran.

### Media I/O and shutdown

QMP target counters are cumulative and include boot, registration, GUI
refresh, identity validation, and the post-operation scan:

| Snapshot | Reads / bytes | Writes / bytes | Flushes |
| --- | ---: | ---: | ---: |
| Boot baseline | 31 / 207,872 | 0 / 0 | 1 |
| Before mount | 163 / 275,456 | 0 / 0 | 1 |
| After mount and scan | 2,866 / 1,659,392 | 0 / 0 | 1 |
| Settled after Cancel | 6,080 / 3,304,960 | 0 / 0 | 1 |
| Before explicit dismount | 12,521 / 6,602,752 | 0 / 0 | 1 |
| Settled Gate A result | 13,694 / 7,203,328 | 0 / 0 | 1 |

No failed read, write, or Flush operation was reported. QEMU marked
account_invalid and account_failed on the block counters, so the read totals
cannot be attributed to individual calls. The read-only confirmation required
no write Flush, and the single Flush was already present at the boot
snapshot. The intermediate confirmation and Cancel snapshots are retained
alongside the table in the run folder.

The full fixture SHA-256 was
C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2 both before
launch and after dismount and QEMU shutdown. The owned process was stopped
through its own QMP endpoint. A fresh final census found no QEMU process,
port 58953 was closed, and an exclusive read-only host open succeeded for the
629,145,600-byte fixture.

No source changed, so host tests and builds were not rerun. The retained
baseline remains 1,491/1,491 storage checks, plus accepted DM36 checker/GUI,
AMD64 kernel, build.bat, and UEFI x64 Release results. The DM36
Dismount → Check FAT32 → Remount workflow was not run. Guest Gates B–G and
the remaining pointer/Close parts of Gate H remain open; this run verified
Gate I ownership and media integrity for the clean fixture. Keep .phase at
DM36→DM37 and do not begin DM38.

Screenshots, QMP snapshots, input transition notes, serial output, ownership
manifest, and gate-a-evidence.json are retained in
out/dm37/dismount-guest/runs/A-idle-dismount-cont5-20261019-visible/.

## DM37 continuation 6 — workflow attempt blocked by foreign QEMU

**Outcome B — `BLOCKED_BY_FOREIGN_QEMU`.** Gate A remains accepted from
continuation 5. This run did not repeat Gate A as a standalone campaign and
did not reach Gate E. DM37 remains active; `.phase` is still
`last_completed=DM36` / `next_expected=DM37`, and DM38 has not started.

### Repository and runtime provenance

The run started at `7b477fc2ce61e2d5632c6cc72bdc3f1049d691de`
(`docs: record DM37 Gate A guest dismount`) on
`DISK_MANAGER_IMPROVEMENTS`. The repository source revision recorded by the
private production runtime is `c40efd34eb67aa71b23e4f48f5243d8f18d11771`;
ordinary production startup was used, test-only hooks were false, no physical
host storage was attached, and `NVME_WRITE_PROVEN=0` /
`NVME_FLUSH_PROVEN=0`.

Runtime evidence is in
`out/dm37/dismount-guest/runtime/runtime-manifest.json`. Recorded SHA-256s:

| Artifact | SHA-256 |
| --- | --- |
| Production and staged kernel | `6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B` |
| UEFI bootloader and staged `BOOTX64.EFI` | `736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7` |
| OVMF firmware | `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792` |
| QEMU executable | `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02` |
| Staged ESP `ramdisk.img` | `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD` |

The runtime manifest also records the source ESP inputs and private ESP
artifacts. The manifest is the authoritative full provenance record.

### QAC ownership and launch

QAC was found and verified operational at
`D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`
(version 2.0.0). Reservation `3e8f7054c5a94cdba2fa112764ade7f9` for project
`guideXOSServer` queued at position 1, then received the exclusive
`acceptance` lease before QEMU launch. The launch used:

```text
qac run --project guideXOSServer --mode acceptance -- "C:\Program Files\qemu\qemu-system-x86_64.exe" [captured QEMU arguments] -name dm37-cont6-workflow
```

The complete child command line and process identities are preserved in
`out/dm37/dismount-guest/runs/E-workflow-qac-20261020-r1/qac-process-ownership.json`.
There is no checked-in DM37 guest runner script; the full production QEMU
invocation was the runner command, with the visible UI/QMP probes performed
while that QAC-owned child remained alive. Its original QEMU arguments and
child exit code are preserved in the process record and QAC log.
QEMU child PID 23008 used QMP `127.0.0.1:58954`. QAC logged two warnings that
its exclusive conditions were compromised by one unmanaged QEMU process.
The run was not continued into storage operations. The owned child exited
with code 0; QAC released the reservation through its `qac run` lifecycle
with reason `Registered child process exited.` No separate release command
or private lock was used.

A one-time read-only post-run census identified foreign PID 2464, parent PID
27840, executable `C:\Program Files\qemu\qemu-system-x86_64.exe`, workspace
`D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`, and QMP endpoint
`127.0.0.1:56898`. This foreign guest was not contacted, stopped, or queried
for attached media. Its census record is
`out/dm37/dismount-guest/runs/E-workflow-qac-20261020-r1/postrun-qemu-census.json`.

### Fixture and guest observations

The disposable fixture was `DM37WORKFLOW01.raw`, serial `DM37WORKFLOW01`,
QEMU block device `dm37workflow_drive`, 629,145,600 bytes, attached
read-only. It contains GPT partition 1, partition GUID
`829681EB-8BBF-45B1-814A-C54A926DB193`, starting at LBA 2048 for 1,226,719
sectors. Its FAT32 volume label is `DM9PROOF`, with 512-byte logical sectors,
16 sectors per cluster, 2 FATs, 599 sectors per FAT, and root cluster 2. The
initial and final full-image SHA-256 were both
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`.
The guest boot log records the workflow device as read-only USB mass storage
and mounts only persistent ATA device `ata0m` at `/`. The workflow target's
mount state was not explicitly verified through Disk Manager; no target
mount identity was recorded and no mount operation was issued. The preflight
and fixture manifest are in
`out/dm37/dismount-guest/runs/E-workflow-qac-20261020-r1/preflight.json` and
`out/dm37/dismount-guest/fixtures/fixture-manifest.json`.

The guest booted to the desktop. Pointer attempts produced a view containing
the Disk Manager window and a desktop context menu, but the exact launch
event was ambiguous. In a later Start-menu probe, Disk Manager was visibly
highlighted; Enter opened File Explorer instead. Escape closed File Explorer.
These observations do not establish a delayed-key defect or qualify normal
Disk Manager activation. The host computer-use bridge also timed out through
its documented retry sequence, so no further host UI automation was
available. The screenshots are `menu-diskmanager-context.png`,
`start-menu-meta-key.png`, `diskmanager-enter-launch.png`, and
`file-explorer-escape-close.png` in the run folder.

No mounted target was identified and no Mount, explicit Dismount, Check FAT32,
or Remount action ran. There is no VFS mount identity or generation, checker
result, post-remount filesystem access, or Gate E completion evidence. Busy
volume Gate B, protected-volume Gate C, Close behavior, and the remaining
keyboard/input Gate H cases were not qualified. No production source changed;
the prior 1,491/1,491 storage result and previously accepted AMD64 kernel,
`build.bat`, and UEFI x64 Release results remain the applicable validation.

### I/O, shutdown, and remaining work

The workflow fixture QMP counters were cumulative: boot baseline 31 reads /
207,872 bytes, 0 writes / 0 bytes, and 1 Flush; final pre-exit count was 163
reads / 275,456 bytes, 0 writes / 0 bytes, and 1 Flush. Failed reads, writes,
Flushes, and unmaps were all zero. QEMU marked `account_invalid` and
`account_failed`, so reads cannot be attributed to individual guest actions.
The single Flush was present at boot and is not attributable to a workflow
operation. The read-only fixture hash remained unchanged. ACPI powerdown was
requested but the guest stayed running; after confirming there was no active
storage operation, the owned QEMU child was closed with QMP `quit`. QMP then
closed and QAC observed child exit and released its lease. The process and
counter evidence, including `guest.serial.log`, is in the run folder above.

Continue only after a future fresh QAC ownership check grants a usable
acceptance lane. Retry the UI activation path, then qualify the full
Mount → explicit Dismount → completed FAT32 Check → explicit Remount workflow
with accessible files and current mount identity. Gate E and Gates B, C, D,
F, G, and the remaining Gate H cases remain open; Gate I applies to every
future guest. Do not start DM38 before DM37 Outcome A.

## DM37 Continuation 7 — retry with clear QAC census; partial guest evidence

**Outcome B — `DM37_PARTIAL_GUEST_ACCEPTANCE`.** This retry was performed
after the user reported that QAC showed no foreign QEMU. Gate A remains
accepted from continuation 5 and was not repeated as a standalone campaign.
The real guest mounted the disposable FAT32 target read-only and read a file
through the production VFS path. The primary Mount → explicit Dismount →
Check FAT32 → explicit Remount workflow remains incomplete because guest UI
input stopped producing visible state changes before the Dismount confirmation
could be opened. `.phase` remains `last_completed=DM36` /
`next_expected=DM37`; DM38 has not started.

### Repository and runtime

The workspace was on `DISK_MANAGER_IMPROVEMENTS` at starting and ending HEAD
`b14bfe0d40d157cb318c382ab6b457cb41a6f0cb` (`docs: record DM37 QAC workflow
blocker`). The continuation brief's expected starting prefix `7b477fc2` did
not match this later continuation-6 workspace state; this retry followed the
user's direct request to retry after the QAC census was clear. `.phase` still
records `last_completed=DM36` and `next_expected=DM37`. No production source
changed. Pre-existing staged output deletions, `.gitignore` modification, and
the untracked Python cache were left untouched.

The source-matched private runtime and fixture were reused. Runtime hashes
remain recorded in `out/dm37/dismount-guest/runtime/runtime-manifest.json` and
the continuation 6 report: production/staged kernel
`6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B`,
UEFI bootloader/staged bootloader
`736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7`,
OVMF firmware
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`,
and QEMU executable
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remained in effect.

### QAC ownership and launch

QAC CLI at
`D:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe`
was verified available; coordinator 2.0.0 was Online (PID 408). The guest
was launched with `qac run --project guideXOSServer --mode acceptance --`
followed by the original QEMU executable and complete QEMU argument vector;
the exact child command line is captured in
`out/dm37/dismount-guest/runs/E-workflow-qac-20261020-r5/qac-retry-record.json`.
The exclusive acceptance reservation was
`2f7a382acfad442db1975cc3069af0e4`, owned by verified
`LEON2_RDT\guideX (PID 25544)`. QEMU PID 13588 was its direct child and used
QMP `127.0.0.1:58959`.

During the run, fresh `qac status` reported one QEMU process and zero foreign;
`qac list` identified PID 13588 as managed, and `qac inspect` reported
`degraded=false`. The CLI wrapper also emitted two warnings claiming one
unmanaged QEMU; those warnings conflict with the contemporaneous status and
reservation inspection. No other QEMU endpoint or media was contacted. After
the guest exited, QAC reported child exit code 0 and released the reservation
through the `qac run` lifecycle. Final status was zero QEMU processes, zero
foreign, zero reservations, and zero managed sessions. No separate lock or
manual reservation release was used.

### Fixture and guest results

`DM37WORKFLOW01.raw` (serial `DM37WORKFLOW01`, block device
`dm37workflow_drive`) was attached read-only. It is a 629,145,600-byte GPT
image with FAT32 partition 1, GUID
`829681EB-8BBF-45B1-814A-C54A926DB193`, starting at LBA 2048 for 1,226,719
sectors, label `DM9PROOF`. Its initial and post-exit full-image SHA-256 were
both `C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`.

Disk Manager showed the intended partition mounted at
`/mnt/disk1-part1 (Read Only)`. The shell listed
`/mnt/disk1-part1/DM9/PROOF.BIN` and read the contents
`guideXOS DM9 QEMU proof 001`. Screenshots and QMP snapshots are in
`out/dm37/dismount-guest/runs/E-workflow-qac-20261020-r5/`, including
`mounted-in-disk-manager.png`, `shell-ls-dm9.png`, `shell-cat-proof.png`,
`status-after-file-read.json`, and the later Dismount-navigation captures.

The correct mounted partition and Dismount action were reached in the GUI;
however, Enter/Space did not open the confirmation, and subsequent QMP
`ret`, `send-key ret`, Escape, and Ctrl+Alt+T probes produced no visible UI
transition. The dialog was never confirmed. No dismount, FAT32 check, or
remount was performed. There is no dismount result, post-check completion,
current remount generation, or post-remount access evidence. These input
observations do not prove a production keyboard defect. Busy-volume Gate B,
protected-volume Gate C, and remaining Gate H behavior were not qualified.

### I/O, exit, and remaining work

The target remained read-only. The final pre-exit QMP sample showed
32,351,232 bytes read, zero writes, zero unmap bytes, and one Flush; QEMU
marked the accounting data invalid/failed, so reads and the boot-time Flush
cannot be attributed to individual GUI or file operations. The full image
hash remained unchanged.

ACPI powerdown was requested, but the guest remained running. The owned QEMU
process was then exited using QMP `quit`; QAC observed the process exit and
released the lease. This confirms QEMU/QMP closure and image-handle release,
but it is not evidence of a clean in-guest operating-system shutdown. The
fixture itself was read-only and byte-identical.

Gate E remains open at explicit Dismount → completed FAT32 Check → explicit
Remount. Gates B, C, D, F, G, and remaining H cases also remain open; Gate I
applies to every future guest. No host regression suite or build was rerun;
this retry was the requested QAC QEMU guest verification. Production source
did not change. The retained host baseline is 1,491/1,491 storage checks,
with the previously accepted AMD64 kernel, `build.bat`, and UEFI x64 Release
evidence. Do not advance `.phase` or start DM38 before DM37 Outcome A.

## DM37 Continuation 8 — preflight blocked by conflicting active QAC ownership

**Outcome B — `BLOCKED_BY_QAC_OR_FOREIGN_QEMU`.** Starting and ending HEAD
were `e110046086ff4d06cf681580eb4d2df265a426ef` on
`DISK_MANAGER_IMPROVEMENTS`; `.phase` remains `last_completed=DM36` /
`next_expected=DM37`. No production source changed, no DM37 guest was started,
and no QAC reservation was requested for this continuation.

### Repository and relocated artifacts

The 125 pre-existing staged deletions are confined to
`out/appmodel-typed-dispatch-flags/`, `out/mc7-proof/`, and
`out/wallpaper-pack/`. Those 125 paths are absent from the D: `out` tree and
present under `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out`;
108 relocated Git blobs match HEAD exactly and 17 have different contents.
The unstaged `.gitignore` change only adds a final newline. The pre-existing
untracked `scripts/__pycache__/dm34_qmp_gui_input.cpython-312.pyc` was left
untouched. No index operation was performed.

The DM37 runtime remains source-matched. Its manifest records source revision
`c40efd34eb67aa71b23e4f48f5243d8f18d11771`; all recorded production source
hashes match the current worktree, and commits after that revision change only
the two DM37 documentation files. The production AMD64 kernel and staged
private ESP kernel both hash to
`6D8DF6E65656B38DEA37F3704EC2639910C074EB448644FDB19FED5B47B7A29B`. The UEFI
x64 Release bootloader and staged private ESP bootloader both hash to
`736F0532DC491403ECAE3CC75A5F9815F03CAEFDD6D11CF853EE8483E81A53A7`. OVMF
firmware hashes to
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`; QEMU
11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) hashes to
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain required.

The accepted clean `DM37IDLE01.raw` fixture remains at
`out/dm37/dismount-guest/fixtures/DM37IDLE01.raw` on D:. The file is
629,145,600 bytes and its independently recomputed SHA-256 is
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`, matching
the recorded pristine hash. `DM37WORKFLOW01.raw` is in the same D: fixture
directory and also currently hashes to the pristine value. The E: tree does
not contain a relocated DM37 fixture/runtime tree. No fixture or runtime was
regenerated or overwritten.

### QAC census and input evidence

The installed QAC CLI was the recorded 2.0.0 binary. Coordinator PID 408 was
online. The first `qac status`, `qac list`, and `qac inspect` census showed
verified reservation `c405e92f4b9c4f32865d0f3e7cad53eb`, owner
`LEON2_RDT\guideX` PID 18588 and child QEMU PID 24124. QAC labeled it
`guideXOSServer`, but its QEMU command line used
`D:\dev\guideXOSServer_FONT_SUPPORT\out\font15-validation` for OVMF, ESP,
and serial output and named `FONT15-live-guest-v3`. That child exited
naturally before the next census; QAC then reported zero sessions.

Immediately before a DM37 launch, a fresh census found a new verified
acceptance reservation `eb86ad86e6614aa497c5718a82910ccc`, owner
`LEON2_RDT\guideX` PID 29304 and child QEMU PID 35140, queue 0, and
`degraded=false`. The command line used
`D:\dev\guideXOSServer\docs\evidence\settings-s22-v03-package-20261018` and
the sibling repository's `out\release-profile` PacMan ISO, with guest name
`guideXOS-S22R10-PacMan-boot1`. QAC reported one managed QEMU and zero
foreign; its process arguments still showed a different workspace from this
DM37 checkout. This second session and QMP endpoint 65252 were not contacted
or changed, and no DM37 reservation was requested. The acceptance lane was
therefore treated as occupied.

No new guest screenshots or dispatch trace were produced. Review of the
retained continuation-7 screenshots shows fresh rendered frames, pointer
movement to the Dismount button, and a later desktop capture with a visible
File Explorer icon after a Return probe. The Dismount activation capture still
shows no confirmation or state transition. This rules out concluding that the
guest was globally frozen, but QMP acknowledgements and screenshots do not
prove separate key-down/key-up or mouse-button delivery at the guest PS/2
controller, input queue, or widget handler. The first observed failed
transition remains the ordinary Dismount action activation; its precise
boundary between QEMU delivery and widget dispatch is unresolved. No
production input fix or temporary tracing was justified without a safe new
guest run.

### Gate E, integrity, shutdown, and retained qualification

Continuation 7's actual read-only guest result remains: mounted at
`/mnt/disk1-part1`; `DM9/PROOF.BIN` read through VFS with contents
`guideXOS DM9 QEMU proof 001`. No Dismount confirmation/result, DM36 check,
or explicit remount was observed. Gate E therefore remains incomplete. That
run's target counters reported zero writes and unmaps and one Flush, but QEMU
marked accounting invalid/failed, so the Flush and accumulated reads cannot
be attributed to the file read or a specific operation. Its fixture hash
matched after exit. ACPI powerdown did not complete; QMP `quit` ended the
owned process, which closed QMP and released the QAC reservation, but this
was not a clean guest OS shutdown. Continuation 8 launched no guest and has
no shutdown or QAC cleanup action of its own.

No host tests or builds were rerun because production source did not change;
retain the prior **1,491/1,491 storage checks** and source-matched build
evidence. Gate A remains accepted. Gate E, B, C guest scope, D, F, G, and
remaining H cases remain open; Gate I continues to require per-run media and
ownership verification. No commit or push was made, the existing staged and
unstaged state was preserved, `.phase` was not advanced, and DM38 did not
start.

## DM37 Continuation 8 follow-up — QAC input trace and partial Gate E

**Outcome B — `DM37_INPUT_DIAGNOSTICS_READY`.** This follow-up used the
documented QAC FIFO admission path after the previously observed owner
released its reservation. It completed the real Mount → Dismount → FAT32
Check → Remount transitions on the clean read-only fixture, but could not
re-read `DM9/PROOF.BIN` after remount. Gate E therefore remains incomplete;
this is not DM37 acceptance and does not authorize DM38.

### QAC admission and ownership

The installed QAC 2.0.0 CLI help and source support `qac run` for a direct
`qemu-system-*.exe` child, with queued admission, and the read-only `status`,
`list`, `queue`, and `inspect` operations. The CLI waits on its reservation
while it is pending; no undocumented scheduler behavior or direct QEMU launch
was used. No global QAC settings or other repository were changed.

The run used project identity `guideXOSServer`, mode `acceptance`, reservation
`2c060850d2f640428021acbcc836ece5`, and owner
`LEON2_RDT\guideX` PID 21872. QAC requested the reservation at
`2026-10-22T22:46:17.7031284Z`, granted it at
`2026-10-22T22:47:44.062518Z`, and registered direct QEMU child PID 25532.
The CLI recorded FIFO queue position 1 and later grant. During the run,
`qac.stderr.log` recorded a degraded warning because the QEMU process census
was incomplete. This means continuous exclusive census health is not proven,
even though the run began through QAC admission and no foreign process was
reported in the subsequent status. Final inspection reported `Released`,
`degraded=false`, owner verified, and reason `Managed child exited.` QEMU PID
25532 exited and QMP port 59139 closed. A subsequent QAC listing showed a
new managed reservation `755126b0030d418587a54c7d40da7c5b` with child PID
15972. That later owner was not contacted, interrupted, or changed.

### Source-matched runtime and artifacts

The candidate manifest is
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run3\runtime-manifest.json`.
It identifies source revision `e110046086ff4d06cf681580eb4d2df265a426ef`
on `DISK_MANAGER_IMPROVEMENTS`; all 13 recorded production source hashes
were recomputed and matched the worktree before the guest run. The candidate
enabled bounded diagnostics with
`GXOS_DM37_INPUT_DIAGNOSTICS`; `NVME_WRITE_PROVEN=0` and
`NVME_FLUSH_PROVEN=0` remained in force.

The run3 freestanding kernel and private runtime ESP kernel both hash to
`70433AE2BCC68FFC8508A3A0439817ED9AAAE14E1F70669428447186F8921C87`. The
UEFI Release bootloader and private ESP `EFI/BOOT/BOOTX64.EFI` both hash to
`B4AE9B5CF968F6B97447CCC595A0F84EF22B001394AF14389C05C6C98AFBABA4`. The
ramdisk hash is
`5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`; the
private OVMF image copied from the D: DM37 runtime hashes to
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`. The
QEMU executable was version 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) with SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

The campaign fixture and earlier DM37 fixture/build evidence remain on D:;
the run1–run3 diagnostics, QAC invocation/reservation records, serial logs,
QMP traces, screenshots, and run3 private runtime are on E: under
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\`.
The run3 fixture was the D: file
`out/dm37/dismount-guest/fixtures/DM37WORKFLOW01.raw`, attached through
QEMU's read-only block option. No relocated fixture copy was used. After
QEMU exited, a Windows exclusive read handle succeeded and the complete
629,145,600-byte image SHA-256 was
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`, equal
to the starting hash. No fixture or runtime evidence was deleted or
overwritten.

### Input dispatch and bounded diagnostics

The production diagnostic path records bounded event identifiers across
QMP-observed injection, PS/2 keyboard or mouse receipt, input-queue result,
legacy dequeue, desktop key routing, compositor focus, Disk Manager key and
action focus, action attempt, widget handler, Dismount handler, and
confirmation-open transitions. It avoids sensitive text and hot-path
allocation. The run3 serial ring contains exactly its 512-record limit; it
does not imply later stages were absent. Pointer-motion diagnostics are
capped at 16 records.

For Dismount, the serial trace and screen evidence show the focused action at
index 4/widget 6, ordinary Enter activation, accepted widget dispatch,
handler entry, and the correct production confirmation. The confirmation
identified USB device Disk1, GPT partition 1, `/mnt/disk1-part1`, Mounted,
read-only, with zero open references and zero active I/O. Escape/Cancel
protection left the mounted volume intact in the observed confirmation path.
After explicit confirmation, the production VFS call returned success and
the Disk Manager reported Unmounted while retaining the partition and device
registration.

The prior run2 delayed-Space observation motivated a narrow focus reset after
storage-modal settlement: the pane returns to Partitions and action focus is
reset before later input can select another operation. Focused host
regressions cover that reset and delayed activation case. In run3, the
keyboard path independently traversed the panes and action row and activated
Dismount, the checker, and Mount. The guest did not show an unintended
post-confirmation operation before the diagnostic cap; a separate guest
delayed-key stress case remains unqualified.

QEMU `query-mice` identified a relative PS/2 mouse (`absolute=false`); an
absolute-motion event was rejected by QEMU. Relative movement and an earlier
button edge reached desktop/compositor dispatch, but X calibration was not
stable. In the final Shell-focus attempt, relative `+100` moved the pointer
left and `-600` clamped it at the left screen edge. The Shell remained
visible but inactive after Disk Manager closed, so a typed command burst and
a later single Return produced no visible Shell input. QMP acknowledgements
alone do not prove Shell delivery; after the 512-record cap, the chain from
those inputs through PS/2 and focus to Shell processing is unobserved. This
does not identify an earlier failure in the Disk Manager keyboard dispatch
path, which was traced successfully for Dismount and Check.

The ordinary application Close path was exercised after the remount settled.
Guest serial recorded Alt+F4 recognition, a close request for the Disk
Manager window, and an accepted result; the next settled screenshot shows
the Shell. This is not a claim of Close-during-dismount acceptance. The Mount
confirmation has no Tab focus-cycle behavior: the visible Mount action was
selected, its target identity was reviewed, and Enter invoked the ordinary
affirm path.

### Gate E workflow and integrity

**Stage A — Mount and initial proof read:** Disk Manager mounted USB serial
`DM37WORKFLOW01`, GPT partition 1, FAT32 label `DM9PROOF`, read-only at
`/mnt/disk1-part1`. Shell VFS read `DM9/PROOF.BIN` as
`guideXOS DM9 QEMU proof 001` (29 bytes); the command closed its file
reference before Dismount.

**Stage B — Dismount:** the correct confirmation identity was shown and
explicitly confirmed. The production VFS Dismount returned success; the
partition became Unmounted, with device registration preserved and no open
references or active I/O.

**Stage C — Check FAT32:** the existing read-only checker ran against that
same unmounted partition. It reported verification passed, 40 metadata reads
(925,184 bytes), 306,688/306,688 FAT bytes compared, 3 chains/clusters, 2
directories, 7 entries, 15 structural reads, a scan of 76,593 orphan
clusters (3,063,808 bytes), zero findings, no repair, and released checker
ownership. The result reported no structural inconsistencies and no
incomplete result.

**Stage D — Remount:** the same GPT partition was explicitly selected and
mounted read-only at `/mnt/disk1-part1`. The settled Disk Manager screen
shows Mounted and partition-view generation 3, following generation 2 before
the check. The QMP/serial trace captured the remount Enter down event and
queue acceptance, then reached the 512-record cap. The later screenshot
proves the mounted state, but not a new file read. Because Shell focus could
not be re-established with the available pointer calibration, the required
post-remount `PROOF.BIN` read remains missing. Do not mark Gate E passed.

The final pre-exit QMP sample for `dm37workflow_drive` reported 77,308
cumulative reads / 40,678,400 bytes, zero writes / zero write bytes, and one
cumulative Flush. QEMU marked `account_invalid=true` and `account_failed=true`;
these counters are run-level and cannot be attributed to the checker, proof
read, or an individual operation. The fixture remained QEMU-read-only, no
NVMe writes or Flushes were proven, and the final full-image hash was
unchanged.

ACPI `system_powerdown` was acknowledged but the guest remained running
after 20 seconds. With VFS activity settled and the fixture read-only, QMP
`quit` terminated the owned emulator. This was emulator termination, not a
clean guest OS shutdown. QAC verified child exit and reservation release;
the QMP listener closed and exclusive fixture access was restored.

The exact run evidence is under
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run3\runs\qac-input-focus-fix-20261022-r3\`.
The existing source validation for this candidate remains **1,491/1,491
storage checks passing**, along with the focused input/focus, DM37 GUI and VFS
lifecycle, DM36 checker/admission, storage ownership/removal, DM34/DM35
widget/lifecycle, widget activation, USB trace classifier, GPT verifier,
DM30 fixture verifier, AMD64 freestanding kernel, `build.bat`, and UEFI x64
Release results. No production source changed after that validation; this
follow-up added no tests or code.

The repository started this follow-up at HEAD
`e110046086ff4d06cf681580eb4d2df265a426ef`, branch
`DISK_MANAGER_IMPROVEMENTS`, and `.phase` `last_completed=DM36` /
`next_expected=DM37`. The 125 pre-existing staged deletions remain confined
to `out/appmodel-typed-dispatch-flags/`, `out/mc7-proof/`, and
`out/wallpaper-pack/`; the unstaged `.gitignore` final-newline edit and
untracked Python cache are preserved. DM37 code/tests/docs remain separate
from those deletions. No phase advancement or DM38 work occurred.

## DM37 Continuation 9 — Shell focus handoff diagnosed; Gate E remains open

**Outcome B — `DM37_FOCUS_HANDOFF_DIAGNOSED`.** This QAC-managed run
reproduced the initial read-only mount and 29-byte VFS read, then isolated
the Shell handoff after Disk Manager closed. The existing production
Escape/Grave recovery restored Shell input and submitted a harmless `help`
command. The run did not complete Dismount, the DM36 checker, a new remount,
or the decisive post-remount proof-file open/read/close. It is not Gate E
acceptance and does not authorize DM38. `.phase` remains DM36→DM37.

### Run4 provenance, QAC, and artifacts

Run4 used the documented QAC 2.0.0 `qac run --project guideXOSServer --mode
acceptance --` path. Reservation `8b84465adf184cc6a1f416744168127c` admitted
direct QEMU child PID 24052; verified owner PID 31704. The QAC stderr log
recorded three warnings that the reservation was degraded because “1
unmanaged QEMU process(es) detected.” At the authoritative 19:02:17 UTC
snapshot, `status` and `list` showed one managed QEMU and zero foreign, and
`inspect` reported `degraded=false`, owner verified. A separate acceptance
reservation was queued at that time; it was not contacted or changed. The
warning means uninterrupted census health is not claimed. At 19:10:43 UTC,
QAC reported zero QEMU processes and no queued sessions; run4 inspection was
`Released`, reason `Managed child exited.`

The source-matched runtime manifest is
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run4\runtime-manifest.json`
(SHA-256
`EEC6B1F1ECFED244CC663E31AA7148B86286257E76AAE6A186828320A0CC7990`). It
records the starting revision `cb99d67fb06fbe18f38971ae201e8439334d402a`
and 15 production-source hashes; all 15 were recomputed from the actual
worktree and matched. Runtime hashes were independently recomputed:

- Kernel: `F53BD7B37554A4D4568034155B1DA180CA48A828787F4817F851D94C599351F2`
- UEFI bootloader: `E65C0A2CBF50534CDB01602800F579446B27B2BE7597BDB461E1E83F94BC0DF4`
- OVMF: `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`
- Ramdisk: `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`

Diagnostics were bounded to 512 records and 16 pointer-motion records, with
an explicit incomplete/loss marker. They were compiled with
`GXOS_DM37_INPUT_DIAGNOSTICS_POSTCHECK_ONLY`; the checkpoint arms after the
FAT32 result dialog closes. Since run4 did not enter that checker, no
post-check `DM37-IN` trace was armed or captured. The host QMP event log and
screenshots therefore establish the actions and visible results, but do not
prove every keyboard transition through PS/2 and the guest queue. The earlier
run3 full buffer remains incomplete evidence, not evidence that later input
was absent.

The private runtime and run evidence remain on E: under
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run4\`;
the fixture remained at
`D:\dev\guideXOSServer_DiskManagerImprovements\out\dm37\dismount-guest\fixtures\DM37WORKFLOW01.raw`.
No relocated fixture or sibling ESP was used or overwritten. The 629,145,600
byte fixture was QEMU-block-read-only, with physical media absent and
`NVME_WRITE_PROVEN=0` / `NVME_FLUSH_PROVEN=0`.

### Shell handoff and observed workflow

Run4 mounted USB serial `DM37WORKFLOW01`, GPT partition 1, FAT32 label
`DM9PROOF`, read-only at `/mnt/disk1-part1`. The initial Shell VFS read of
`DM9/PROOF.BIN` showed exactly `guideXOS DM9 QEMU proof 001` and `29 bytes
read` in `shell-initial-vfscat.png`; the file reference was closed by the
ordinary `vfscat` path. This is the pre-dismount read only.

The code audit and guest screen agree on the handoff gap. `desktop::handle_key`
routes to a focused compositor window before checking the separate Shell
surface. After Disk Manager closes, compositor focus is cleared but the Shell
is not automatically marked active. The Shell branch processes input only
when `s_shellActive` is true and it is not minimized. On screen, typed `help`
after Disk Manager Close produced no characters. Pressing Escape closed the
inactive Shell; the existing `grave_accent` toggle reopened it active. `help`
then appeared at the prompt, Return submitted it, and the help output was
visible. The relevant screenshots are `after-escape-close.png`,
`shell-focus-test-before-submit.png`, `shell-grave-reopened.png`,
`shell-active-help-typed.png`, and `shell-active-help-output.png` in the run4
evidence directory. This uses the existing production path; no test-only
focus setter or new shortcut was used.

After recovery, Disk Manager reopened through the typed Shell command and
the exact USB identity, read-only state, mount path, and partition-view
generation 2 were visible. The Dismount confirmation never opened despite
further ordinary action-pane and Enter/Space attempts. No Dismount, checker,
remount, or post-remount VFS read occurred. The Shell/action focus attempt is
not treated as proof that a delayed Enter/Space could not activate a later
operation. Guest serial contains accepted Alt+F4 close requests for Disk
Manager windows 3E9–3EB earlier in this run; the final reopened instance was
closed with Escape. No Close-during-operation or delayed-key stress case was
qualified.

QMP `system_powerdown` produced a `POWERDOWN` event but left the guest
running. Once filesystem activity was settled, the production Shell's
`shutdown` command completed; serial recorded
`[SHUTDOWN] final reason=system-shutdown exitCode=0`. QAC recorded child exit
code 0 and released the reservation. QMP closed with the guest. Afterward an
exclusive host read handle was acquired for the fixture; its final size and
SHA-256 remained 629,145,600 bytes and
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`.
Run-level QMP counters for `dm37workflow_drive` showed 27,761,152 read bytes,
zero writes, and one Flush, but `account_invalid=true` and
`account_failed=true`; they cannot be attributed to an individual stage.

The focused diagnostics, Shell recovery, storage-manager suite
(1,491/1,491), widget activation, auxiliary verifiers, AMD64 freestanding
kernel, `build.bat`, and UEFI x64 Release validations all passed before this
documentation-only update. `git diff --check` is recorded with the final
commit validation. Gate A remains accepted. Gate E's final read remains
required, as do the unattempted busy-volume, protected-admission, writable
durability, removal/replacement, failure-injection, and delayed-key/Close
cases. The next continuation should requalify the full workflow under QAC
using the now-identified Shell recovery and must still capture a fresh
post-remount VFS open/read/close before claiming Gate E.

## DM37 Continuation 10 — reliable action-pane activation and ordered FAT32 workflow

**Outcome B — `DM37_WORKFLOW_VERIFIED` for the ordered read-only workflow in
one QAC-managed guest.** The Run6 guest completed a fresh Shell VFS read after
an explicit Dismount → FAT32 Check → Remount cycle. The fixture remained
unchanged and the guest shut down through the production Shell. Gate A remains
accepted; this does not qualify the other DM37 safety gates or start DM38.
`.phase` remains `last_completed=DM36`, `next_expected=DM37`.

### Keyboard queue defect and focused regression

Run5's rapid Escape/Enter sequence exposed a defect in the legacy PS/2 API:
`get_key()` removed one queued event but returned a shared `s_lastKey` value
that a later IRQ could already have replaced. The Boolean `s_hasKey` could also
be cleared while additional key-down events remained queued. The narrow fix
makes `has_key()` scan pending transitions for a key-down, and makes
`get_key()` pop and return the oldest queued key-down while skipping releases.
The bounded event queue and production widget dispatcher remain in use.
`tests/ps2keyboard_legacy_queue_test.cpp` covers Escape followed by Enter,
interleaved releases, FIFO values and diagnostic sequence numbers, and a
release-only tail. It passed with zero failures.

### Run6 provenance and QAC ownership

The run used branch `DISK_MANAGER_IMPROVEMENTS`, starting from
`e516da393e43e4198859feb4e8248d1c24ba1d97`, with the PS/2 queue fix present in
the working tree. The private runtime manifest reported 16 source hashes and
no source mismatches. The rebuilt kernel was compiled for freestanding AMD64
with `GXOS_DM37_INPUT_DIAGNOSTICS`, `NVME_WRITE_PROVEN=0`, and
`NVME_FLUSH_PROVEN=0`; the manifest's kernel SHA-256 is
`4BFD226CA145D0298A3C299DD44194B08606F6AAAD72F84085B3E271D52DD563`.
The bootloader SHA-256 is
`E65C0A2CBF50534CDB01602800F579446B27B2BE7597BDB461E1E83F94BC0DF4`, OVMF is
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`,
ramdisk is `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`,
NvVars is `48E7A7E0F277E90036E951B2E2A484A736AE706FB25B4AA654B8FA361E920667`,
and QEMU is `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.
The private ESP and manifest are under
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run6-keyqueue-fix\`.

QAC 2.0.0 granted acceptance reservation
`783b379e44c94f0790d6b8912d7d38d6` to owner PID 23496. Its managed QEMU child
was PID 32672 at `127.0.0.1:59142`. The wrapper emitted temporary incomplete
census and “1 unmanaged QEMU” warnings. Contemporaneous supported status/list/
inspect snapshots reconciled the running child as managed, with zero foreign
sessions and `degraded=false`; this is not a claim of continuous census
health. After guest exit, QAC reported exit code 0, reservation state
`Released`, reason `Managed child exited`, and `degraded=false`; final status
showed zero QEMU processes and zero managed sessions. QMP port 59142 closed.
The evidence is in
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\input-diagnostics-run6-keyqueue-fix\runs\qac-keyqueue-corrected-workflow-20261024-r6\`.

### Action-pane activation and confirmation

The built-in Start Menu `[LaunchDispatch]` opened Disk Manager. With the
manager in its known initial pane, Tab entered ACTIONS at Refresh; a harmless
Refresh was activated through the ordinary keyboard/widget path and the
visible state updated. In the mounted state the enabled action order was
Refresh, Properties, Diagnostics, Verify Partition, Dismount Volume. Tab then
four Right presses selected Dismount at action index 4 (production widget 6);
Enter opened the confirmation. Its Cancel and Dismount widgets were 12 and 11.
Escape canceled the first confirmation and the target remained Mounted. The
same ordinary navigation reopened it; Tab moved from Cancel to Dismount and
Enter confirmed. No direct handler call or test-only focus setter was used.

The retained `DM37-IN` chain for the decisive first confirmation records the
Enter PS/2 key and queue admission, legacy FIFO pop, desktop/compositor focus,
Disk Manager action focus index 4/widget 6, action attempt, widget-handler
dispatch, Dismount handler entry, and confirmation open. The visible
confirmation/result screens and the subsequent checker/remount screenshots
are retained in the Run6 directory. Diagnostics were boot-enabled with an
append-only 512-record cap; the buffer overflowed before later Shell proof
events. Missing post-cap records are not interpreted as evidence that the
events did not happen.

After the final Disk Manager operation, Escape closed the manager through its
ordinary keyboard path; Grave restored active Shell input. The final
`vfscat` command was typed and submitted at the Shell prompt, returned its
29-byte result, and restored the prompt. No pointer activation was used, and
this run did not qualify Close during a synchronous operation or delayed-key
stress behavior.

### Ordered workflow and media proof

The target was read-only USB serial `DM37WORKFLOW01`, GPT partition 1, FAT32
label `DM9PROOF`, at `/mnt/disk1-part1`. The first mount attempt was followed
by a harmless confirmation-cancel test and a preliminary successful Dismount,
FAT32 Check, and Remount before the first proof-file read was captured. That
extra cycle is preserved as history. The qualifying ordered subsequence starts
with that explicit Remount, followed by the first captured read, a second
explicit Dismount, a completed Check, a further explicit Remount, and the
fresh read. Thus the first captured read is not described as preceding the
preliminary cycle.

The first captured Shell command was
`vfscat /mnt/disk1-part1/DM9/PROOF.BIN`. It printed
`guideXOS DM9 QEMU proof 001` and reported 29 bytes. The captured payload is
`guideXOS DM9 QEMU proof 001\r\n` (hex
`6775696465584F5320444D392051454D552070726F6F66203030310D0A`). After the
second Dismount, FAT32 Check, and explicit read-only Remount, Disk Manager
again showed `/mnt/disk1-part1` mounted on the same registration and partition;
the visible partition-view generation was 3. The internal numeric VFS
`mountGeneration` is not printed by the retained guest UI/serial diagnostics;
production VFS assigns a new monotonically increasing generation to each
mount, and Dismount success followed by a separate path-based `vfscat` open
provides the fresh-mount evidence. The final `vfscat` independently printed
the same 29-byte payload and returned to the Shell prompt. Serial contains two
separate opens of `/mnt/disk1-part1/DM9/PROOF.BIN`. The production
`cmd_vfscat` path reads through EOF and calls `vfs::close(handle)`; because the
diagnostic buffer had overflowed, no direct post-remount close trace is
claimed. The command result, prompt return, serial open, and production close
call together document that lifecycle without claiming a missing trace.

The second Dismount confirmation captured USB device
`usb-46F4:0001-p0-i0-10`, device registration 2, registry generation 5, GPT
partition 1, LBA 2048 through 1,228,766 at 512 bytes/sector, and
`/mnt/disk1-part1`. It showed zero open-file references, zero directory
references, zero active I/O, read-only media, and no write Flush requirement.
The production result showed `Volume Dismounted`, state `Unmounted`, zero
references, and no automatic checker, repair, or format. The same partition
remained present and the device registration was retained.

Both FAT32 checks completed through Disk Manager. The retained result screen
reported Verification Passed; 40 metadata reads / 925,184 bytes; all
306,688/306,688 FAT bytes compared; 3 chains/clusters, 2 directories, 7
entries; 15 structural reads; 76,593 orphan clusters scanned; and zero stored
findings. No repair was run and the checker lease was released before the
explicit Remount.

The read-only fixture is
`D:\dev\guideXOSServer_DiskManagerImprovements\out\dm37\dismount-guest\fixtures\DM37WORKFLOW01.raw`,
629,145,600 bytes. Its starting and final SHA-256 are both
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`; the
post-run exclusive host read handle succeeded. Pre-shutdown QMP block stats
for the fixture showed 67,975 reads / 36,804,608 bytes, zero writes, and one
Flush, but `account_invalid=true` and `account_failed=true`; these cumulative
counters are not attributable to an individual operation.

### Shutdown, validation, and remaining gates

After the final read, the normal Shell `shutdown` command completed. Serial
recorded `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop
expected=1`; QAC observed child exit code 0, released the reservation, and
QMP closed without `quit`.

Host validation for this production change passed: storage manager 1,491
checks / 0 failures (including 249 USB Mass Storage checks); widget activation;
DM37 diagnostics, input focus, Shell focus, and PS/2 queue regression; USB
trace classification (7 tests); DM29 GPT verifier tests; and DM30 GPT fixture
tests. The AMD64 freestanding kernel build passed with the diagnostic define
and both NVMe proof flags disabled. The UEFI x64 Release MSBuild completed
with 0 warnings and 0 errors. `build.bat` completed successfully after its
dependency checks (the compiler emitted existing warnings). `git diff --check`
passed. The run did not use physical media, prove NVMe writes or Flushes,
force Dismount, or repair the fixture.

Gate A remains accepted and Gate E's ordered read-only maintenance workflow
is evidenced. Busy-volume Gate B, protected-admission Gate C, writable
durability Gate D, device removal/replacement Gate F, controlled I/O/Flush
failure Gate G, and delayed-key/Close qualification remain open. No DM38 work
is authorized by this outcome; keep `.phase` at DM36→DM37.

## DM37 Continuation 11 — writable save verified, Dismount incomplete

This continuation added one controlled write to a separate disposable USB
FAT32 image after transport and write-Flush preflight. It is partial guest
acceptance: the production save path completed its write and Flush, and a
read-only host inspection after clean guest exit found the exact expected
payload. The production Dismount confirmation, final Dismount Flush, guest
Unmounted state, explicit Remount, and in-guest reread were not completed.
This run therefore does not qualify writable durability.

### QAC ownership and runtime

The source-matched DM37 runtime ran in QAC 2.0.0 exclusive acceptance
reservation `0c49e5587865490795d5d148319e7e69`, owner PID 26860, child QEMU PID
624, QMP `127.0.0.1:52787`. Prelaunch and running QAC snapshots showed one
managed QEMU, zero foreign QEMU processes, no queue, and `degraded=false` in
reservation inspection. QEMU exited with code 0 after the production Shell
shutdown; QAC marked the reservation Released with reason `Managed child
exited`. Final QAC status/list showed zero QEMU processes, zero foreign
processes, zero reservations, and zero managed sessions. The child and owner
PIDs were absent, the QMP port was closed, and an exclusive read handle to the
working image succeeded. No QMP `quit` was used. QEMU logged a non-fatal
`Ignoring request for interrupt vector 0` warning; the QAC census remained
healthy.

The run used the verified kernel SHA-256
`4BFD226CA145D0298A3C299DD44194B08606F6AAAD72F84085B3E271D52DD563`,
bootloader SHA-256
`E65C0A2CBF50534CDB01602800F579446B27B2BE7597BDB461E1E83F94BC0DF4`, and
OVMF SHA-256
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.
`NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`, and no physical media were
attached. The complete launch and process identities are in
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation11\preflight.json`,
`runs\run1-writable-preflight\launch-request.json`,
`qemu-running-process.json`, and `qac-run.log`.

### Writable transport and controlled save

The source fixture
`D:\dev\guideXOSServer_DiskManagerImprovements\out\dm37\dismount-guest\fixtures\DM37DURABLE01.raw`
was copied to
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation11\fixtures\DM37DURABLE01.run1.raw`.
Both initial images were 629,145,600 bytes with SHA-256
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`. The
QEMU target was writable USB Mass Storage BOT device `dm37durable11`, backing
node `dm37durable11_drive`, serial `DM37DURABLE01`, on the QEMU UHCI
controller. The guest registered USB MSC VID:PID `46F4:0001`, 1,228,800
512-byte blocks, removable, `access=read-write`, and
`sync-cache=succeeded`. Disk Manager showed the selected device as Writable,
the mounted FAT32 partition at `/mnt/disk1-part1`, and `Persistence: Flush
required`. The root backing device was visibly marked protected; no root
operation was attempted.

Notepad saved `DM9/PROOF.BIN` through the existing production VFS write path,
overwriting it with 57 bytes:
`guideXOS DM9 QEMU proof 001\r\nDM37 durability accepted 001`.
The expected payload SHA-256 is
`99AEF80C20E27A68CDF31EE0B4C59CECA7581D985D983CA37D76C00A134F6AB0`. Guest
serial records `LFPASTE_WRITE_END ... actual=0x00000039`,
`LFPASTE_FLUSH_END status=0x00`, and
`VFS_WRITE_FILE path=/mnt/disk1-part1/DM9/PROOF.BIN operation=overwrite
bytes=...0039 fatStatus=FAT_FILE_WRITE_OK blockStatus=0x00 vfsStatus=OK`.
Notepad was closed normally after saving. These records prove the application
save path completed its write and Flush; they do not prove the later final
Dismount Flush.

### Dismount, shutdown, and independent media inspection

Disk Manager displayed the selected USB serial, GPT partition `DM9 QEMU
Proof`, writable access, and mounted path. The Dismount Volume control was
visible, but the confirmation did not open despite further guest keyboard and
pointer attempts. No confirmation identity, production Dismount result,
Unmounted transition, final Dismount Flush result, Remount, or fresh guest
read is claimed. No forced Dismount, device removal, format, erase, or repair
was attempted. The exact target was last observed Mounted at
`/mnt/disk1-part1` before the production Shell shutdown.

After QEMU had exited and QAC released ownership, the working image was opened
with an exclusive read handle and independently inspected. The repository's
read-only `scripts/verify-dm9-qemu-image.py` parser was run with the expected
57-byte payload for this authorized writable copy. It verified the
protective MBR; primary and backup GPT header and entry-array CRCs; matching
GPT copies; partition 1 geometry (LBA 2048–1,228,766); the leading canary
gap; FAT32 BPB and backup boot sector; primary/backup FSInfo signatures;
mirrored FATs; and the exact proof-file bytes. It reported SHA-256
`D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`, 57
payload bytes, and `result=PASS`. A read-only byte comparison against the
original fixture found zero changes in sectors `[0,2048)` and
`[1228767,1228800)`, so image changes are confined to the authorized GPT
partition. The source fixture remains at its original hash. No repair was
run.

The source image already reports FSInfo free-cluster hint 76,592 while the
actual FAT count is 76,590; the same values appear in the final copy. The
two-cluster hint discrepancy predates this run and is not represented as a
new change. Retained verification outputs are `media-verification-source.txt`,
`media-verification-after.txt`, `media-diff-outside-partition.txt`, and
`shutdown-cleanup.txt` under the Continuation 11 E: artifact root.

The production Shell `shutdown` completed with serial
`[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1`.
QAC observed child exit code 0 and released the reservation; QMP closed after
guest exit. No operation-scoped QMP blockstats snapshot was captured or used
to attribute writes. The guest write is attributed by the path-specific VFS
serial record instead.

### Remaining gates and repository state

Busy-volume Gate B was not exercised in this run because no safe supported
guest path was established for retaining an open FAT32 handle while using
Disk Manager; the accepted production-linked host tests remain the available
evidence. Gate C only re-confirmed the visible root-device protection. Boot,
`/system`, and unknown-provenance admission were not guest-tested here.

For Gate H, Tab reached the Disk Manager action pane and Refresh focus was
visible, but this continuation did not capture a Dismount action dispatch or
confirmation transition. Notepad closed normally after its save, Disk
Manager closed before shutdown, and the guest used production Shell shutdown.
Close during an active synchronous Dismount, Escape/cancel of the writable
confirmation, delayed-key protection, busy-reference release/retry, and
repeated activation remain unqualified.

Production source is unchanged. No tests or builds were rerun; the accepted
1,491/1,491 storage baseline remains from the previous continuation. Gate A
and Gate E remain accepted as previously recorded. This continuation's
outcome is **Outcome B — `DM37_PARTIAL_GUEST_ACCEPTANCE`**, not
`DM37_DURABILITY_VERIFIED`. Keep `.phase` at `last_completed=DM36`,
`next_expected=DM37`; DM38 has not started. Remaining mandatory work includes
busy/protected admission, production Dismount settlement and final Flush,
explicit Remount plus guest reread, removal/replacement, Flush/I/O failure
behavior, and the remaining input/Close cases.

## DM37 Continuation 12 — writable mounted state reached; input boundary before Dismount dispatch

This run reused the accepted writable FAT32 save fixture rather than
repeating the save. A disposable 629,145,600-byte clone was created at
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation12\fixtures\DM37DURABLE01.cont12.raw`.
Its initial SHA-256 was
`D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`, matching
the accepted source image that already contained the expected 57-byte
`DM9/PROOF.BIN`. The source fixture was not opened for writing.

QAC admitted exclusive acceptance reservation
`57af6efd1913425899e6fe3c853736e0`, owned by PID 13268 with child QEMU PID
18036 and QMP port 53420. Running QAC snapshots showed one managed QEMU, zero
foreign QEMU processes, and `degraded=false`. The source-matched continuation
11 runtime was reused without a rebuild: kernel
`4BFD226CA145D0298A3C299DD44194B08606F6AAAD72F84085B3E271D52DD563`,
bootloader
`E65C0A2CBF50534CDB01602800F579446B27B2BE7597BDB461E1E83F94BC0DF4`, and
OVMF
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`.
`NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; no physical media was attached.

Disk Manager selected Disk 1 USB Mass Storage, serial `DM37DURABLE01`, GPT
partition 1 (`DM9 QEMU Proof`), FAT32, writable. The guest visibly reported
`Mounted at /mnt/disk1-part1 (Read/Write)`. Dismount Volume widget 6 was
visible. A stray Read-Only Verification Target page was dismissed without
starting verification; the guest returned to the same mounted status.

The activation boundary in this run is before Disk Manager widget dispatch.
QMP status and display queries showed QEMU still running with GTK, an implicit
`-vga std` device, and the QEMU PS/2 mouse as the current relative pointer.
QMP `send-key`, HMP `sendkey`, and mouse requests were acknowledged, but no
new `[DM37-IN] stage=dm-key`, `dm-action-attempt`, or `dm-action-result`
followed the last observed input trace (`seq=0000013E`). Host UI clicks also
did not open the Dismount confirmation. Explicit QMP display routes `VGA`,
`video0`, and the VGA QOM path were rejected as unknown devices. This
localizes the observed failure to input delivery in this QEMU session; it
does not show that the production Dismount handler is defective or that a
production admission gate disabled the action.

No Dismount confirmation was opened or accepted, no production
`vfs::dismount` result was observed, and no final FAT32 Flush, Unmounted
state, Remount, or fresh VFS read is claimed. QMP `system_powerdown` was
acknowledged, but QEMU remained Running after five seconds. Normal Shell
shutdown has not completed; the QAC reservation remains held, so no final
exclusive host access, image hash, or offline verification is claimed. The
current screen capture and input/routing record are
`current-disk-manager.ppm` and `current-input-blocker.txt` under the
Continuation 12 run directory on E:.

Production source is unchanged. No tests or builds were run; the accepted
1,491/1,491 storage baseline remains from the previous continuation. The
initial capture outcome was **Outcome B — `DM37_ACTIVATION_BOUNDARY_IDENTIFIED`**.
The later Continuation 12 follow-up below supersedes that initial input-path
description. Keep
`.phase` at `last_completed=DM36`, `next_expected=DM37`; DM38 has not started.
Resume only after guest input is reliably reaching the production UI, then
complete the writable confirmation, final Flush, Unmounted state, explicit
Remount and fresh file read, normal Shell shutdown, QAC release, and offline
media verification.

### DM37 Continuation 12 follow-up — delayed input; Dismount still unactivated

After the initial capture, QEMU was brought to the foreground. The user
selected Disk 1 and the `DM9 QEMU Proof` row. A fresh QMP display capture shows
the USB Mass Storage device `DM37DURABLE01`, Online GPT, writable, with one
mounted partition; the FAT32 partition row is selected and the **Dismount
Volume** button remains visible. The earlier retained mounted-partition
capture records `/mnt/disk1-part1 (Read/Write)` and `Persistence: Flush
required`. No confirmation dialog is visible in the follow-up captures.

The QEMU process remains responsive and the guest PIT heartbeat continues.
The user reports mouse movement can take about 20 seconds. Paced QMP keyboard
events returned successfully; delayed screenshots showed focus at Refresh,
Properties, Diagnostics, and Verify Partition. Later Tab cycling did not
produce a stable action-focus highlight. The boot-enabled `DM37-IN` trace had
already reached its 512-record cap (`trace=incomplete`, `dropped=0`), so later
input cannot be judged from missing trace rows. Current QMP captures are
`current-lag-state.png`, `focus-after-tab-lag20.png`,
`after-send-key-tab.png`, `pane-cycle-tab7.png`, and
`action-pane-after-tab.png` under
`E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation12\runs\qac-writable-dismount-remount-20261031\`.

No Dismount confirmation or result was observed. There is no production
`vfs::dismount`, final FAT32 Flush, Unmounted transition, explicit Remount,
fresh VFS read, normal Shell shutdown, or QAC release. The guest is still
running under reservation `57af6efd1913425899e6fe3c853736e0`; do not force
close it or claim final image verification while QAC owns the clone. This
follow-up narrows the current boundary to slow, unreliable GUI input before
confirmed Dismount activation; it does not demonstrate a production Dismount
handler defect.

The current run outcome is **Outcome B — `DM37_PARTIAL_GUEST_ACCEPTANCE`**.
Production source is unchanged; no tests or builds were run. Keep `.phase` at
DM36→DM37 and do not begin DM38. Resume the Gate D sequence only when keyboard
focus can be observed reliably, then complete the confirmation/cancel check,
final Flush, Unmounted state, explicit Remount and fresh read, safe shutdown,
QAC release, and independent media verification.

## DM37 Continuation 13 — selected-partition render path performance fix

**Outcome B — `DM37_SELECTION_PERFORMANCE_FIX_HOST_VERIFIED`.** This continuation started on `DISK_MANAGER_IMPROVEMENTS` at `1d4ade53f4de59fffd999e728c9cf3e5c2ba8dab`. `.phase` remains `last_completed=DM36`, `next_expected=DM37`; DM38 has not started. Gate A and the ordered read-only Gate E workflow remain accepted from earlier continuations. Writable Gate D durability and the remaining admission, failure, removal/replacement, and input/Close cases remain open.

The reported symptom was up to roughly 20 seconds of delayed pointer response while a mounted FAT32 partition was selected in Disk Manager; closing the app restored desktop responsiveness. Continuation 12 is historical symptom evidence only: its run metadata records source HEAD `c9a2b717f0c7e39180b5df8de22b0e60549027d6`, not this continuation’s starting HEAD. That guest remained visibly mounted read/write, the Dismount Volume control stayed visible, and no confirmation, VFS dismount, final Flush, or remount was observed. The guest heartbeat advanced while GUI input lagged. Its fixed 512-record `DM37-IN` trace had reached capacity, so missing later trace rows do not prove that an input event was absent.

Reviewing the synchronous UI path identified a repeated media-validation operation. Disk Manager calls `updateInitializeControls()` during drawing and again on selection changes. For a selected FAT32 partition with an active mount, that routine used `storage::query_mount_protection()`. The protection query checked each matching partition mount with `vfs::mount_identity_valid()`, which calls `find_current_partition()` and reparses the GPT from the block device. A valid 128-entry GPT validation reads the protective MBR, both GPT headers, both 32-sector entry arrays, and compares the arrays: 131 block-read callbacks for one identity validation. The synchronous render path cannot return to the normal input/render loop until those callbacks finish. The source explains how a slow USB-backed validation can delay visible input while interrupt-driven PIT heartbeat continues; this continuation did not reproduce a 20-second delay in a guest, so that exact wall-clock symptom remains unverified against the fix.

The UI preview now calls `storage::query_mount_presence()`. It revalidates the exact current device registration before and after a fixed-size `VFS_MAX_MOUNTS` metadata scan. The scan holds the VFS metadata lock only while checking active mount records and performs no partition parse or block read. This is a fresh live scan on every preview, not a cache. Its result deliberately does not prove partition identity and is used only for UI presentation/admission preview. The destructive and lifecycle paths retain their authoritative exact-target revalidation and mount-identity checks; no Dismount, Flush, or remount authorization was weakened.

Focused instrumentation now arms the bounded input checkpoint when a partition is first selected and records that transition. The trace keeps at most 512 records, drops and counts newest records after the cap, and emits loss summaries at bounded doubling thresholds; formatting remains deferred and limited to two lines per flush. The checkpoint makes a later guest capture begin at the relevant transition while avoiding boot and navigation noise. It does not turn trace silence into proof that no event was delivered.

The storage test measured 64 deep protection queries at 16,768 block reads and 39,377 microseconds, versus 64 presence previews at zero block reads and 16 microseconds. These timings use the host’s memory-backed GPT fixture. They demonstrate repeated I/O removal in the tested path and are not guest or USB latency measurements. The storage suite passed 1,492/1,492 checks, including 249 USB Mass Storage checks; focused widget, DM37 input diagnostics, input focus, PS/2 queue, DM37 Shell focus, compositor pointer-button, USB trace classifier, GPT verifier, and DM30 fixture verifier checks also passed.

The freestanding AMD64 kernel and UEFI x64 Release bootloader were rebuilt in continuation-local output directories with `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`. `build.bat` was run with its output redirected to the continuation-local `build-bat` directory so the pre-existing root `guideXOSServer.exe` was not overwritten. Exact paths, hashes, and final build results are recorded in the implementation summary and continuation artifact directory.

QAC and host ownership were inspected read-only. The QAC dashboard showed its coordinator online, zero managed sessions, zero foreign QEMU processes, zero queued or blocked reservations, and the acceptance lane available; its activity feed records the previous child PID 18036 exiting. The dashboard Refresh action was unavailable through the UI automation surface, so these displayed counts are recorded as an observed snapshot rather than a fresh coordinator API response. An independent host process inventory also found no QEMU system process. No QAC reservation, guest, QMP endpoint, or physical disk was contacted or changed, and no QEMU was launched in this continuation.

Continuation 12 evidence remains on `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation12\runs\qac-writable-dismount-remount-20261031\`. The historical `guest.serial.log` SHA-256 is `0F64B39966CF6A2FCE8BB7104030CDEF41A83CC1CDADD6771320505152FE9494`; `current-input-blocker.txt` is `511D262444C6B9DF2346D2D9461AF98F520AC81803842BDD09EE77CC8DF533C0`. These files were read for diagnosis and were not modified.

**Acceptance remains open.** This host-verified UI performance correction does not prove guest responsiveness after the change or complete writable Dismount durability. Keep `.phase` at DM36→DM37 and both NVMe proof flags at zero. A later owned QAC run should build from the exact reviewed source, measure injected-input-to-guest-receipt and guest-receipt-to-visible-compositor/widget response, sample render/update duration and block reads per update, record queue depth/overflow and heartbeat, compare the mounted-partition preview before and after the correction, then complete the remaining writable Flush, Unmounted, explicit Remount, fresh-read, shutdown, and offline image verification gates.

### Continuation 13 build and validation record

| Target | Result | SHA-256 |
| --- | --- | --- |
| Freestanding AMD64 kernel (`out/dm37/continuation13/kernel-build/bin/kernel.elf`) | PASS; `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; 3,179,720 bytes | `18D6E2AD8EF5DD89FCF22973288219930EA1873BF7A8D4B4D8D9718A6871F59B` |
| Native desktop `build.bat` output (`out/dm37/continuation13/build-bat/guideXOSServer.exe`) | PASS; 7,891,254 bytes | `FC67E30DE26A2AF71DF6CE92DDC148A8821DFC86A4E62E042F99368D594E36C6` |
| UEFI x64 Release bootloader (`out/dm37/continuation13/uefi-x64-release/bin/guideXOSBootLoader.exe`) | PASS; 59,904 bytes | `FBC2978BB2D3AE24B81CFAA273061F628C97262AE35BEE4D4EAB06BE7845B3A0` |

The kernel build used `mingw32-make -C kernel ARCH=amd64 BUILD_DIR=../out/dm37/continuation13/kernel-build NVME_WRITE_PROVEN=0 NVME_FLUSH_PROVEN=0 -j1`. The UEFI bootloader was rebuilt with MSBuild Release/x64 using task-local `OutDir` and `IntDir`. A one-line output redirection in a task-local copy of `build.bat` directed its desktop executable to the path above. The pre-existing root `guideXOSServer.exe` remains 7,891,254 bytes with its original 2026-10-24 timestamp; the task build output did not replace it. Build logs are `kernel-build.log`, `build-bat.log`, and `uefi-x64-release/msbuild.log` under `out/dm37/continuation13/`.

## DM37 Continuation 14 — mounted read/write guest responsiveness sample

Continuation 14 used the committed fix at source revision `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`. The existing source-matched private runtime was booted under QAC 2.0.0 in two sequential guests. The first guest mounted the `DM37WORKFLOW01` FAT32 partition read-only at `/mnt/disk1-part1`; the second mounted the disposable `DM37DURABLE01` FAT32 partition read/write at that path. The selected partition remained visible during refresh and keyboard navigation. The reported approximately 20-second input stall was not reproduced in the observed intervals, including a quiet observation exceeding 20 seconds with the mounted read/write partition selected.

Host-side QMP input-call-to-screenshot upper bounds in the RW guest were 216.1 ms for F5, 180.2 ms for Up, and 189.4 ms for Down. The QMP event-call portions were 127.1 ms, 123.5 ms, and 124.3 ms respectively. These bounds include QMP and screenshot work and are not guest input-latency measurements. F5 displayed `Disk state refreshed.`; Up/Down screenshots retained the selected `DM9 QEMU Proof` GPT partition. Screenshots and timing records are in `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation14\runs\guest-writable-responsiveness-20261101-r1\`.

The bounded serial trace records PS/2 mouse receipt, input mapping, compositor pointer dispatch, a Disk Manager widget press/activation, keyboard queue acceptance/dequeue, compositor focus, and Disk Manager key dispatch. Loss summaries report zero dropped diagnostic records, while pointer-motion records are omitted after the bounded trace limit. The trace has no per-event guest timestamps or render/update work counter. QMP cumulative statistics for the RW USB device report `account_invalid=true` and `account_failed=true`; they cannot attribute reads to the selected-partition preview. No guest measurement in this continuation proves that repeated preview rendering performed zero GPT sector reads. The host-side regression and benchmark from Continuation 13 remain the only render-preview I/O evidence.

The RW guest showed `DM9 QEMU Proof`, GPT partition 1, FAT32, `Access: Read/Write`, mounted at `/mnt/disk1-part1`. No Dismount control was activated. Production Dismount, final Flush, Unmounted VFS state, Remount, and fresh 57-byte file read were not attempted. This sample therefore does not pass the complete performance gate or authorize Gate D durability work.

### QAC ownership, runtime, and media record

Before each launch, QAC admission was clear. The read-only guest used reservation `5a1500b6e9ff45e48740dbaefd2af032`, owner PID 28608, child QEMU PID 24888, QMP `127.0.0.1:59305`. It exited after the production Shell `shutdown` command; serial records `[SHUTDOWN] final reason=system-shutdown exitCode=0`, and QAC released the reservation. Its one registered USB device was the read-only `DM37WORKFLOW01`; the other attached backing image was not registered by the guest.

The RW guest used reservation `d20a459368a44dbaae4b362a9a0d99b1`, owner PID 10140, child QEMU PID 15340, QMP `127.0.0.1:59306`, and workspace `D:\dev\guideXOSServer_DiskManagerImprovements`. Its command line attached only the disposable `DM37DURABLE01.cont14.raw` USB image and the private boot ESP; no physical media were attached. After Computer Use reported a physical Escape stop, the QEMU window was no longer present. QAC inspection showed the reservation `Released`, reason `Managed child exited`, `degraded=false`, and no remaining QEMU processes, sessions, or reservations. The RW serial log has no final production Shell shutdown marker; the child exit code was unavailable. Normal guest shutdown is not claimed. QMP port 59306 was closed after the child exited.

Both runs used source revision `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`, kernel SHA-256 `F4DF400045C9AF358C329E7FD29061410D4D6CE93AA169592EE9703DD3F68B05`, UEFI bootloader SHA-256 `FBC2978BB2D3AE24B81CFAA273061F628C97262AE35BEE4D4EAB06BE7845B3A0`, OVMF SHA-256 `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`, ramdisk SHA-256 `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`, and private ESP manifest SHA-256 `A24FE67578D92B9548E4A348D3C4C55C0E398E9562EF5144A2DB638157989379`. QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`, executable SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remained in force.

The disposable RW fixture's initial and post-exit SHA-256 both equal `D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`; the read-only reference image's initial and final SHA-256 both equal `C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`. The recorded expected 57-byte payload hash remains `99AEF80C20E27A68CDF31EE0B4C59CECA7581D985D983CA37D76C00A134F6AB0`; no new save or persistence read was performed. The full RW launch request, QAC command, serial log, QMP samples, and screenshots are retained in the Continuation 14 run directory above.

**Outcome B — `DM37_PARTIAL_GUEST_ACCEPTANCE`.** The user-visible multi-second stall was not reproduced in the observed mounted read/write state, but the guest evidence does not establish zero GPT reads during repeated preview rendering, and the final production shutdown was not observed. Real-guest responsiveness remains unaccepted; writable durability was not attempted. No automated tests or builds were run in this continuation, no production source changed, and `.phase` remains `last_completed=DM36` / `next_expected=DM37`. Gate A and previously recorded Gate E evidence are preserved. Busy-volume and protected-volume guest proof, writable Dismount durability, device removal/replacement, controlled I/O/Flush failure, remaining input/Close cases, and the complete per-scenario media-integrity campaign remain open. DM38 has not started.

## DM37 Continuation 15 — QAC guest preserved at input-automation boundary

**Outcome B — `BLOCKED_BY_QAC_OR_INPUT_AUTOMATION`.** This continuation started on branch `DISK_MANAGER_IMPROVEMENTS` at `b81c5dcef1a31ccb0104e062ec3bf53d72a8ce5e`. The source-matched runtime uses production source `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`. `.phase` remains `last_completed=DM36`, `next_expected=DM37`; DM38 has not started.

This section preserves the original live-boundary snapshot. Its pending QAC-release and final-hash statements describe the state at that observation; the subsequent **C15R1 recovery closeout** below records the later child exit and offline verification without changing the partial Gate D result.

Continuation 15 created a separate disposable image, `DM37DURABLE01.cont15.raw`, and passed its initial offline GPT/FAT32 verifier. The 629,145,600-byte image began at SHA-256 `D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`. It contains GPT partition 1 (`DM9 QEMU Proof`, LBAs 2048–1228766), FAT32 label `DM9PROOF`, and `DM9/PROOF.BIN`. The planned existing payload is 57 bytes, `guideXOS DM9 QEMU proof 001` followed by a CRLF and `DM37 durability accepted 001`, SHA-256 `99AEF80C20E27A68CDF31EE0B4C59CECA7581D985D983CA37D76C00A134F6AB0`. The image was attached only as removable read/write USB Mass Storage BOT through PIIX3 UHCI; no physical storage was attached. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remained in force.

The guest registered USB serial `DM37DURABLE15` as Disk 1 and mounted its FAT32 partition read/write at `/mnt/disk1-part1`. The last clear Disk Manager evidence, `rw-mounted-ready.png`, showed the selected mounted partition. The final QMP screenshot `input-automation-boundary-c15-final.png` shows File Explorer and the Start menu in front of Disk Manager; the Dismount Volume control remains visible behind them. Computer Use and paced QMP key/mouse injections produced intermittent desktop changes, while the intended File Explorer and Disk Manager actions could not be confirmed through a stable guest event trace. The bounded guest serial log contains no `DM37-IN` event record for this interaction window. No guest-side input-to-dispatch or input-to-render latency was established, and this run does not qualify the responsiveness correction or establish whether the prior severe stall recurs.

The menu interaction produced two rejected attempts to save `/.startmenu_recent`; both report `FAT_FILE_WRITE_INVALID_NAME`, `blockStatus=0x00`, and `vfsStatus=Invalid filesystem operation`. No `DM9/PROOF.BIN` save, save-path Flush, final Dismount Flush, or other authorized proof-file write was observed. QMP cumulative USB block statistics have `account_invalid=true` and `account_failed=true`; they cannot attribute media reads or Flush completion.

The QAC 2.0.0 reservation is `21bf0822b1b34a43831659693e04c65b`, project `guideXOSServer`, mode `acceptance`, owner PID `29648`, QEMU child PID `30576`, QMP `127.0.0.1:59307`. A fresh read-only QAC dashboard census showed the coordinator online, one managed session in `Running`, owner `Verified`, zero foreign QEMU processes, zero queued or blocked reservations, and the exclusive acceptance lane assigned to `guideXOSServer`. A final screenshot-backed click intended for the visible Shell Shut Down control landed in the File Explorer list; the Start menu remained open. The subsequent QMP snapshot still reported `running`, and no Shell shutdown marker or QAC exit was observed. The reservation remains active. Do not force-quit the guest or inspect the image offline while it remains guest-owned.

| Gate | Continuation 15 result |
| --- | --- |
| Mounted writable FAT32 state | Reached and recorded before the input boundary |
| Guest responsiveness qualification | Blocked; no attributable guest dispatch/render trace or latency evidence |
| Production VFS save of the 57-byte payload | Not attempted |
| Production Dismount confirmation and final Flush | Not attempted |
| Authoritative Unmounted transition | Not observed |
| Explicit Remount and fresh complete VFS read | Not attempted |
| Normal Shell shutdown and QAC release | Pending; reservation remains Running |
| Final image hash and independent offline verification | Pending; image remains guest-owned |

The run artifacts are in `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation15\runs\guest-writable-dismount-20261101-r1\`, including `launch-request.json`, `active-qac-list.txt`, `guest.serial.log`, `qmp-input-nav.log`, `input-automation-boundary-c15-final.json`, `input-automation-boundary-c15-final.png`, `shell-shutdown-not-activated.json`, and `shell-shutdown-not-activated.png`. The source-matched kernel SHA-256 is `F4DF400045C9AF358C329E7FD29061410D4D6CE93AA169592EE9703DD3F68B05`; UEFI bootloader `FBC2978BB2D3AE24B81CFAA273061F628C97262AE35BEE4D4EAB06BE7845B3A0`; OVMF firmware `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`; ramdisk `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`; ESP manifest `A24FE67578D92B9548E4A348D3C4C55C0E398E9562EF5144A2DB638157989379`; QEMU 11.0.0 build `v11.0.0-12122-ga4bb4b10c9`, executable SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

At the original live-boundary snapshot, the final media hash was unknown because the QAC-owned guest had not exited. No tests or builds were run and production source was unchanged. Gate A and the prior read-only Gate E acceptance remain preserved. Guest responsiveness, writable Dismount durability, final Flush, Remount persistence, normal shutdown, and independent final media verification were still open at that point. See the recovery closeout below for the later QAC release and offline image result.

## DM37 Continuation 15 recovery closeout

**Outcome B — `DM37_C15_SESSION_RELEASED_UNCLEAN`.** Recovery did not restart QEMU or reconnect to QMP. Supported QAC 2.0.0 `status`, `list`, and `inspect 21bf0822b1b34a43831659693e04c65b` calls reported the reservation `Released`, reason `Registered child process exited.`, owner PID `29648` exited, child PID `30576` exited, and `retainReservationOnChildExit=false`. The coordinator PID was `24548`; QAC reported zero reservations, managed sessions, queued or blocked requests, and QEMU processes (zero foreign). A host process census found no QEMU child, and `127.0.0.1:59307` had no listening endpoint. The QAC owner/child lifecycle and the closed endpoint were checked again after media verification. No process was forcibly stopped by this recovery.

Normal guest OS shutdown is **not** claimed. The retained `guest.serial.log` contains no `[SHUTDOWN]` marker, and QAC's generic child-exit reason does not identify a production Shell shutdown command or guest exit code. The guest therefore exited without proven clean OS shutdown; the cause of the child exit is unknown.

The first verifiable input-targeting boundary remains the post-attempt guest screenshot, not a guest event receipt. `shell-shutdown-not-activated.png` (1280×800) shows the Start menu open over Disk Manager, File Explorer in front, and the pointer over a File Explorer row near `(146,586)` while the Shell **Shut Down** control is near `(372,742)`. The paired `shell-shutdown-not-activated.json` records a QMP snapshot with the guest still `running` before and after; its `action` is `snapshot` and does not contain the attempted click coordinates or a click event. The QMP mouse inventory reports `QEMU PS/2 Mouse`, `absolute=false`, and `qmp-input-nav.log` records relative-axis injections. The screenshot establishes the target mismatch, but the captured evidence cannot distinguish screenshot scaling or coordinate mapping, stale coordinates, window/client-area offsets, focus, or delayed delivery as the cause. No stable guest `DM37-IN` receipt exists for this interaction window. This is an input-automation boundary, not a demonstrated production defect; no further input was sent after the guest had exited.

After QAC release, the fixture opened successfully with an exclusive `FileShare.None` read/write handle. The complete final SHA-256 is `D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`, identical to the initial hash recorded in `launch-request.json` and `initial-fixture-verification.txt`; size remains 629,145,600 bytes. The independent read-only `verify-dm9-qemu-image-cont15.py` run passed the protective MBR, primary/backup GPT headers and entry-array CRCs, matching GPT copies, partition geometry, zeroed 1,031,168-byte leading canary gap, FAT32 BPB, backup boot sector, FSInfo signatures, mirrored FATs, root and `DM9` directories, and `DM9/PROOF.BIN` contents. The complete image hash match establishes that no fixture bytes changed during C15.

`DM9/PROOF.BIN` already existed in the initial fixture and held the deterministic 57-byte payload (SHA-256 `99AEF80C20E27A68CDF31EE0B4C59CECA7581D985D983CA37D76C00A134F6AB0`) before the guest was launched; the final verifier reads that same pre-existing payload. C15 did not save it. The two rejected `/.startmenu_recent` writes remain separate `FAT_FILE_WRITE_INVALID_NAME` events with `blockStatus=0x00`; they are not evidence for or against a Dismount. The verifier reports an FSInfo free-count hint of 76,592 versus 76,590 actually free clusters, the same two-cluster discrepancy recorded by the initial verifier and unchanged with the byte-identical image; it is not a new C15 change.

The accepted 1,493/1,493 storage regression baseline is retained without rerun: the runtime's production source revision is `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`, and the repository diff from that revision through the recovery starting HEAD contains only `docs/DISKMANAGER_IMPLEMENTATION.md` and this phase report. No production source, build, or regression test changed or ran in this recovery. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain in force; `.phase` remains `last_completed=DM36`, `next_expected=DM37`; DM38 has not started.

Gate D remains incomplete: C15 performed no authorized proof-file save, production Dismount confirmation, final Flush, Unmounted transition, explicit Remount, or fresh guest read. The next step toward writable Dismount acceptance is a separately scoped QAC continuation that first demonstrates a reliable guest input route with a harmless action, then uses a fresh disposable fixture for the controlled save/Dismount/Flush/Remount sequence. The retained C15 fixture is verified and unchanged; it is not evidence that those lifecycle gates passed. Recovery evidence remains in `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation15\runs\guest-writable-dismount-20261101-r1\` and the parent `continuation15\` directory.

## DM37 Continuation 15R2 — writable Dismount lifecycle verified

**Outcome B — `DM37_WRITABLE_DISMOUNT_VERIFIED` for Gate D on the disposable USB BOT fixture.** This follow-up completes the production writable save, Dismount, final Flush, Unmounted, Remount, and fresh-read sequence that remained open after C15R1. It does not close the other DM37 gates. Starting HEAD was `dadd6a5ab1a7faaec84ca461ab02931c231cd420` on `DISK_MANAGER_IMPROVEMENTS`; production source revision remained `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`. `.phase` remains `last_completed=DM36`, `next_expected=DM37`; DM38 has not started. Production source was unchanged, so no tests or builds were rerun; retain the accepted 1,493/1,493 storage baseline.

### Runtime, ownership, and input route

The isolated runtime manifest at `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation15r2\runtime-manifest.json` has SHA-256 `D972383AACAECD2B2CC2CC2972C19F36872FC1449D011E96BEE2D7B9E68A9C4E`. Its source-matched artifacts are kernel `F4DF400045C9AF358C329E7FD29061410D4D6CE93AA169592EE9703DD3F68B05`, UEFI bootloader `FBC2978BB2D3AE24B81CFAA273061F628C97262AE35BEE4D4EAB06BE7845B3A0`, OVMF `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`, and ramdisk `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`. QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`, executable SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`, and `physicalHostStorageAttached=false` remained in force.

QAC 2.0.0 reservation `a3b93e2f144c4dc5ae60d4c6ead83e60` was owned by verified owner PID `12528`, with QEMU child PID `24124` and QMP `127.0.0.1:59308`. Before shutdown, the saved QAC snapshot showed one managed process, zero foreign QEMU processes, and `degraded=false`. After shutdown, QAC reported zero QEMU processes, reservations, and managed sessions; `inspect` reported `Released` with reason `Managed child exited`. The QMP endpoint closed with the guest. The pre-shutdown QAC snapshot preceded several minutes of Shell focus recovery; no additional QAC refresh was taken immediately before pressing Return. No ownership change was observed, and the post-shutdown QAC snapshot confirmed release.

The new disposable image `DM37DURABLE01.cont15r2.raw` was cloned from the preserved C15 fixture and started at SHA-256 `D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`. The guest registered removable USB Mass Storage BOT VID:PID `46F4:0001` as Disk 1, serial `DM37DURABLE15`, with GPT partition 1 `DM9 QEMU Proof` (LBAs 2048–1228766), FAT32, mounted at `/mnt/disk1-part1` read/write. No physical media were attached.

Input was calibrated against the guest's 1280×800 framebuffer. QMP reported a relative `QEMU PS/2 Mouse` (`absolute=false`); the paced helper limited each relative-axis packet to 40 pixels with a 200 ms delay. The host Computer Use capture includes a 56-pixel window frame above the guest image, so cursor and target coordinates were compared in guest coordinates before clicking. The action path used individually paced key-down/key-up events. Initial menu mis-targets opened Clock; each was closed without a storage action, then the pointer was visibly positioned over the intended Console row and allowed to settle before activation. Disk Manager was launched through Start Menu, Disk 1 and its partition were visibly selected, and the action pane was navigated with keyboard input. The mounted partition remained selected for at least 50 seconds before the next recorded navigation, with no observed recurrence of the earlier multi-second stall. This is a bounded visual observation, not a guest latency measurement.

The retained serial log contains no `[DM37-IN]` input trace rows for this run, so QMP acknowledgements are not presented as guest dispatch proof. GUI state was checked from screenshots; the later production VFS create, successful final Dismount Flush, and Dismount result are independently present in guest serial output. A distinct F5/Refresh widget dispatch was not retained for this C15R2 run. Before shutdown, Shell focus was checked by typing a harmless character and removing it; a separate harmless shell command was not submitted before `shutdown`.

### Production save, Dismount, Remount, and fresh read

Notepad's ordinary Save As flow created `/mnt/disk1-part1/DM9/R2TEST.txt` with the exact 36-byte payload `GUIDEXOS DM37 C15R2 DURABLE 20261101` (SHA-256 `A0F4E39985ED7B5A12BC984E2BDEB6D4409BC849A1C9B21CECC1EE420CCE0901`). Guest serial records allocation to cluster 5, `FAT_FILE_CREATE_WRITE ... status=BLOCK_OK`, publication as `FILE_WRITE_OK`, save-path `LFPASTE_FLUSH_END status=0x00`, and `[VFS_WRITE_FILE] ... operation=create ... vfsStatus=OK` (around lines 43821–43839). The existing 57-byte `DM9/PROOF.BIN` was preserved.

The production Dismount confirmation captured device `usb-46F4:0001-p0-i0-10`, registration 2 / registry generation 5, GPT partition 1, the mounted path, and LBAs 2048–1228766. It showed zero open files, zero open directories, zero active I/O, and required Flush supported. The normal action-pane confirmation completed; the result screen showed `Volume Dismounted` and authoritative `Unmounted`, while retaining the registered block device and partition. Guest serial lines 56798–56800 record `LFPASTE_FLUSH_BEGIN volume=0x01`, `LFPASTE_FLUSH_END status=0x00`, and `[VFS] Dismounted '/mnt/disk1-part1'`. This final Dismount Flush is distinct from the successful save-path Flush.

Disk Manager then explicitly mounted the same partition read/write at `/mnt/disk1-part1`. The view showed registration 2 and partition-view generation 3. A new Notepad open selected `R2TEST.TXT` from `/mnt/disk1-part1/DM9`; the fresh title bar showed `/mnt/disk1-part1/DM9/R2TEST.TXT` and the full expected payload. Serial line 84558 records `[VFS] Opened: /mnt/disk1-part1/DM9/R2TEST.TXT`. The screenshot verifies displayed bytes; the guest log does not report a read byte count. Notepad was closed normally before shutdown.

With the Disk Manager window closed and the production Shell active, the `shutdown` command was entered and submitted. Guest serial line 113578 records `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`. QMP closed, QAC observed the child exit, and the reservation was released without a forced stop.

### Independent final media verification

After QAC confirmed exclusive fixture access, read-only offline verification passed protective MBR, primary and backup GPT header CRCs and entry-array agreement, partition bounds and the 1,031,168-byte leading canary gap, FAT32 BPB, backup boot sector and FSInfo signatures, both matching FATs, root/DM9 directory traversal, the original 57-byte proof file, and `R2TEST.TXT` exact bytes and SHA-256. Final image size is 629,145,600 bytes and SHA-256 is `C56D2B07EDC4DCFC936EF7F2C66AFEA265F5E5B47D0CA1347345B559A954B231`.

A sector-by-sector comparison against the preserved initial C15 image found four changed sectors and 58 changed bytes, all inside partition 1: FAT copies at LBAs 2080 and 2679, the existing DM9 directory cluster at LBA 3294, and the new file's data cluster at LBA 3326. No bytes outside the target partition changed. The original FSInfo hint remains 76,592; actual free clusters moved from 76,590 before the run to 76,589 after allocation of the new file's cluster. The pre-existing hint discrepancy was not repaired. Detailed results are in `final-offline-verification.txt`, `final-image-hash.txt`, and `media-delta-report.json` under `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation15r2\runs\guest-writable-dismount-20261101-r2\`.

Gate D's writable save/final-Flush/Unmounted/Remount/fresh-read lifecycle is verified for this disposable USB BOT configuration. Gate B (busy volume), Gate C (protected-volume scope), Gate F (device removal/replacement), Gate G (controlled I/O/Flush failure), Gate H (delayed input and close-during-operation), and the remaining per-scenario Gate I media/ownership qualifications remain open. Gate A and Gate E remain accepted as previously recorded. Keep `.phase` at DM36→DM37 and do not start DM38.

The serial log also contains several rejected `/.startmenu_recent` and desktop-preference create attempts (`FAT_FILE_WRITE_INVALID_NAME`), plus successful `/desktop.cfg` overwrites during UI activity. They are not classified as target-media writes: the independent full-image comparison found only the four authorized FAT32 sectors listed above, with no other fixture byte changes. QMP cumulative USB counters were not used to attribute those events.

## DM37 Continuation 16 — busy-volume and protected-mount qualification

**Outcome B — `DM37_BUSY_REFERENCE_GUEST_UNAVAILABLE`.** C16 completed production-linked host qualification for busy-volume and protected-target admission. Gates A, D (USB BOT), and E remain accepted. A safe guest inventory observation also confirmed that the running root device is shown as protected, but no actual busy-reference guest workflow exists in the current production UI/Shell surface. The guest Gate B result therefore remains unavailable; the host results are not represented as a guest PASS. C16 did not start a Gate F/G campaign, change production source, repeat the writable Gate D workflow, advance `.phase`, or start DM38.

### Repository and production audit

The continuation started at `84bb4f06a7338a330a6bd90665c743e49741337a` on `DISK_MANAGER_IMPROVEMENTS`; `.phase` remained `last_completed=DM36`, `next_expected=DM37`. The QEMU runtime's production source revision is `349e85a836a0c3735f84bac5a0cf827e4f2a60c7`, matching the repository production source. C16 changed only `tests/storage_manager_test.cpp` and these two reports; no production build result is claimed. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain in force.

The production busy state is VFS mount ownership: `MountPoint.openFileReferences`, `openDirectoryReferences`, and `activeOperations`, backed by the real `FileHandle` and `DirIterator` tables. Open/admission reserves a mount operation before publishing a handle; actual file and directory references are accounted against the exact mount identity. Reads, writes, enumeration, and close settlement retain an active-operation reference. Dismount captures the mount identity, obtains the relevant storage-operation lease/pin, rechecks identity and protection at final admission, blocks new admissions while quiescing, then checks active operations and live handle tables. A busy result restores the mounted lifecycle before filesystem teardown or Dismount Flush. It does not invalidate another owner's handle or report Unmounted. After the last reference and operation settle, a normal retry can proceed.

The lightweight UI preview does not authorize Dismount. Final VFS admission validates the current registration and registry generation, sector geometry, partition-table identity and bounds, partition-view slot/generation, boot provenance, alias conflicts, and storage-operation ownership before Flush or filesystem teardown. The host coverage includes changed MBR identity, a GPT unique partition GUID changed with valid recalculated GPT CRCs, stale mount generations, root/`/boot`/`/system` protections and aliases, same-capacity replacement in a reused registry slot with changed registration ID and generation, active storage-operation conflict, known boot protection, and unknown boot provenance. Unsupported durability leaves the mount read-only. The unknown-provenance VFS request returns `VFS_ERR_PROTECTED`; stale and replaced targets fail closed without I/O to the replacement. These checks preserve the separate render-preview and authoritative final-action paths.

### Production-linked host regressions

The full `scripts/run-storage-manager-tests.ps1` run passed **1,503/1,503 storage checks**, extending the retained 1,493-check baseline by ten checks. Added cases use the real VFS lifecycle with bounded fake block callbacks, not test-only production counters. They cover two simultaneous open files plus an open directory; snapshots show two file references and one directory reference. Dismount stays busy as one file and then the other references are closed, and ordinary retry succeeds only after all owners close. Invalid-handle close does not release ownership. These busy attempts add no Dismount Flush or write.

Path-level active write, active enumeration, and a two-thread held block read each keep a real production VFS operation in flight while Dismount is attempted. Every attempt returns busy; dismount-attributable Flush is absent, the underlying operation settles, and normal retry succeeds. The concurrent-close test blocks the close owner's own Flush: Dismount still observes one live file reference plus an active operation and returns busy without adding a Flush or write beyond the close already in progress. Releasing that callback completes close, drops the references, and permits retry. A failed close Flush reports I/O failure while releasing only that handle's ownership. A reentrant active-I/O case also observes nonzero `activeOperations` and busy rejection.

The confirmation-to-activation protection regression captures an exact mounted ATA target while UEFI provenance resolves definitely-not-boot, then changes only the boot-source descriptor so it identifies that same registration as boot backing. The captured mount and target binding still match, but fresh GUI admission reports protected and production `vfs::dismount` returns `VFS_ERR_PROTECTED`; the mount remains live with no added write or Flush. The test then takes the fake device offline only to perform stale cleanup. This exercises a protection change without replacing the disk, changing its capacity, or altering the captured mount generation.

The same full run reported USB Mass Storage 249 checks included in the storage total; DM34 wipe 17/17, whole-device erase 34/34, DM35 read-only surface engine 48/48, DM36 FAT32 integrity metadata 259/259, DM36 GUI 31/31, DM34 wipe UI 31/31, DM34 whole-device erase UI 39/39, and DM35 surface GUI 34/34. Widget activation, DM37 input diagnostic/focus, PS/2 legacy keyboard queue, and DM37 Shell focus suites all passed with zero failures. USB classifier passed 7/7, GPT verifier 1/1, and DM30 fixture verifier 1/1. No production source changed, so no kernel, `build.bat`, or UEFI build was run or claimed.

### Gate B guest capability and Gate C observation

Production workflow audit found that Shell `cat` opens, reads, and closes before returning; `ls`/`ll`, File Explorer refresh, and normal Notepad dialogs enumerate and close their iterators. No supported production app or Shell behavior retains a file or directory reference while the user switches to Disk Manager. Accordingly no hidden command, fabricated reference, or test-only handle was added, and no fake guest Busy result is claimed.

For a safe Gate C observation, QAC 2.0.0 admitted one fresh source-matched guest under reservation `0f690ad12b6242cd83eb7f9ea02cc02a` (owner PID 18048, QEMU child PID 22304). Preflight had zero QEMU processes, foreign processes, sessions, or reservations; QMP port 59308 was free. The guest used QEMU `v11.0.0-12122-ga4bb4b10c9`, executable SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. The runtime manifest hash was `D972383AACAECD2B2CC2CC2972C19F36872FC1449D011E96BEE2D7B9E68A9C4E`; the production kernel artifact hash was `F4DF400045C9AF358C329E7FD29061410D4D6CE93AA169592EE9703DD3F68B05`. No physical media was attached.

Disk Manager was launched through the normal Start Menu, corroborated by production `[LaunchDispatch]` selecting `DiskManager`. Disk 0 was visibly the running root backing device; its properties showed `Boot provenance: Boot device: protected` and mount state `/`. With its single MBR partition selected, the action pane showed Wipe, Verify, and Delete controls but no Dismount control. Classification: **action absent from the observed view**. No Dismount confirmation was opened and no final VFS admission was attempted. The separate USB BOT fixture was attached read-only and was not selected. This observation does not claim disabled-action, confirmation-rejection, or engine-rejection guest evidence, and the root/system/boot volume was never taken offline.

QMP reported a relative PS/2 mouse (`absolute=false`); input used calibrated, paced relative movements capped at 40 pixels per event with 200 ms spacing and individually paced key transitions. Serial output records PS/2 IRQs and normal app dispatch but contains no `[DM37-IN]` rows, so no guest-side confirmation/input trace is claimed. The shell `shutdown` command was visibly entered and the guest recorded `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`. QAC observed child exit code 0, released the reservation, and returned to zero QEMU processes, sessions, and reservations; QMP port 59308 was closed. No forced termination or foreign-guest/QMP interaction occurred.

### Fixture and isolated guest state

The read-only data fixture `DM37GATEC.cont16.raw` is a distinct clone under `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\continuation16\gatec-protected-inventory-20261101\`. Its 629,145,600-byte SHA-256 was `C56D2B07EDC4DCFC936EF7F2C66AFEA265F5E5B47D0CA1347345B559A954B231` both before and after the guest. Independent offline verification passed PMBR, primary/backup GPT CRCs and agreement, partition bounds, the 1,031,168-byte leading gap, FAT32 BPB and backup boot/FSInfo structures, mirrored FATs, the DM9 directory, and the original 57-byte proof file. The existing FSInfo hint discrepancy (76,592 versus 76,589 actual free clusters) was preserved. No data-fixture write was enabled or observed.

UEFI writable state used a private cloned ESP. Its `NvVars` file changed from SHA-256 `ADF486698ED8EDDB372B098E4E20D753223D37AFE99F15D5701442822593AF2D` to `4B0C50C6A6C30AAEC36F3C14A546D8F86445BAD84BD7EB3369DD4A8FF9873348` during guest startup; no files were missing and no other ESP file changed. This isolated firmware-variable change is separate from the read-only data fixture. Runtime captures, QAC pre/post state, serial/QMP logs, screenshots, hashes, and offline verification are retained under the C16 E: directory above.

Gate B host reference-lifetime admission and Gate C host fail-closed identity/protection checks are verified for the covered production paths. Gate B remains incomplete because guest workflow capability is absent. Gate C has a safe protected-root inventory observation only; final guest VFS rejection was intentionally not attempted. Preserve accepted A/D/E, keep `.phase` DM36→DM37, and do not start DM38. Recommended next continuation: pursue only a newly available safe production guest workflow for a retained VFS reference or document Gate B's explicit exclusion in the final DM37 acceptance contract; do not manufacture a reference.

## DM37 Continuation 17R1 — Gate F stopped at Mount input dispatch

**Outcome B — `BLOCKED_BY_QAC_OR_INPUT_AUTOMATION`.** Gate F did not reach a production VFS mount or a media-removal transition. QAC admission and runtime ownership were available; the first missing transition was activation of the visible read-only Mount confirmation. This is an input/dispatch blocker, not evidence of a storage-lifetime defect. Gates A, D (USB BOT), and E remain accepted. Gate B retains its guest-workflow limitation, and Gate C remains a protected-root inventory observation only.

### Repository baseline and retained host evidence

The continuation started at `c37a4ace53754ce1aba74e1e225180d982965938` on `DISK_MANAGER_IMPROVEMENTS`, with a clean worktree and index. The owner's maintenance commit since `9970d1c74089035371a01f53f299b8ced099f0d3` removes generated build/proof/wallpaper outputs, updates `.gitignore`, and includes two tracked Python bytecode files. The 128-file diff contains no production source, test, tool, or documentation changes. Direct comparison of current `kernel/core` and `kernel/arch` production files with runtime source revision `349e85a836a0c3735f84bac5a0cf827e4f2a60c7` found no differences. The C16 1,503/1,503 storage baseline therefore remains source-matched. No tests or builds were rerun; no new Gate F test was introduced because the existing host suite already exercises the relevant removal and identity paths.

The production lifetime rules remain those recorded in Continuation 16. A captured Dismount target binds the device registration and registry generation, sector geometry, partition identity and extent, partition-view identity/generation, and VFS mount identity/generation. The lightweight render preview does not authorize an action. Final admission rechecks current identity and protection, mount state, and storage-operation ownership before filesystem teardown or Flush. Removal invalidates the old registration and mount; stale endpoint, handle, and unmount operations fail with device-removed status. A replacement in a reused slot receives fresh registration/generation identity and cannot inherit the old mount or captured confirmation.

Existing production-linked host coverage in `tests/storage_manager_test.cpp` includes mounted stale VFS access and cleanup after device removal; open file and directory behavior; removal during writes and during Unmount Flush; exact DM37 captured-target matching; same-capacity replacement with changed registration identity; stale final Dismount rejection without reads, writes, or Flushes on the replacement; registry-slot reuse with strictly increasing identities; and GPT partition-identity validation. Existing DM14 hotplug cases cover idle and open-reference removal, BOT data-out, Synchronize Cache, VFS write and Unmount Flush, and removal during initialization/create/format. These cases were reused rather than duplicated. The retained 1,503/1,503 total does not include any tests added by this continuation.

### Disposable media and QAC-owned runtime

The new evidence directory is `E:\CodexArtifacts\guideXOSServer_DiskManagerImprovements\out\dm37\gatef-removal-replacement-20261102\`. The original and replacement images are separate 71,303,168-byte (68 MiB) raw FAT32/GPT fixtures with 512-byte logical sectors and 139,264 sectors. Both passed independent GPT/FAT32 checks before and after the QEMU session, remained read-only at the host and QEMU block layers, and have byte-identical final hashes to their prelaunch hashes. No physical storage was attached; `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` remain unchanged.

| Fixture | SHA-256 | GPT disk GUID | GPT partition GUID | Proof content |
| --- | --- | --- | --- | --- |
| `DM37F-original.raw` | `7B4D3911CD982215308B2562E6DC4A409D6EA3F5B1A2422E249850D38099AFB5` | `0b1fe6dc-26d4-424f-9449-5c962e1dd135` | `9c308050-013a-4aa2-b897-b14548763cd3` | `guideXOS DM13 USB lifecycle proof 001` |
| `DM37F-replacement.raw` | `1506E05D57B283BEB4D57686FA1C75B0FF83AE6820E71C0683259019FBFA0288` | `d37f0001-2026-4f37-9001-000000000037` | `d37f0002-2026-4f37-9002-000000000037` | `guideXOS DM14 USB revisionB proof 002` |

Both partitions span LBAs 2048–139230. The replacement has the same capacity and geometry but distinct GPT disk/partition GUIDs and proof contents. Its intentionally differing sectors are the primary GPT header and entry sector, proof payload, and backup entry/header sectors (LBAs 1, 2, 3148, 139231, and 139263). `fixture-preparation.json`, `fixture-verification.txt`, `final-media-verification.json`, and `final-offline-verification.txt` preserve provenance and verification details.

Fresh QAC 2.0.0 prelaunch status showed the coordinator online with zero QEMU processes, foreign processes, sessions, or reservations. QAC admitted project `guideXOSServer` in acceptance mode under reservation `2a88ba01d1a64dbba8e3add8ae6a045b`; verified owner PID was 30508, QEMU child PID 8172, and QMP endpoint `127.0.0.1:59308`. The managed process was not degraded. QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`, SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`) used the source-matched production kernel and private ESP. Kernel SHA-256 was `F4DF400045C9AF358C329E7FD29061410D4D6CE93AA169592EE9703DD3F68B05`; bootloader `FBC2978BB2D3AE24B81CFAA273061F628C97262AE35BEE4D4EAB06BE7845B3A0`; OVMF `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`; ramdisk `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD`. The runtime manifest hash was `D972383AACAECD2B2CC2CC2972C19F36872FC1449D011E96BEE2D7B9E68A9C4E`.

QEMU attached only the original test image as a read-only removable USB Mass Storage device through PIIX3 UHCI port 1 (qdev `dm37fusbid`, serial `DM37FORIG01`). `query-block` reported `ro=true`, the expected 71,303,168-byte raw image, and zero observed block writes. The replacement image was verified but never attached; no hotplug command was issued.

### Guest observation and exact stopping point

The production desktop booted and Disk Manager was launched through the normal Start Menu dispatch, recorded at serial line 949 as `[LaunchDispatch] ... target=DiskManager`. Disk Manager reported its Mount/Dismount widget IDs. The original USB device and its GPT partition were selected, and the visible Mount dialog specified `Access: Read Only` and `/mnt/disk1-part1`.

The QEMU PS/2 mouse was relative (`absolute=false`). Bounded, paced QMP input first produced visible focus, Start Menu, application-launch, disk-selection, and partition-selection changes. A click on the visible Mount action and one Return-key attempt were acknowledged by QMP, but the confirmation remained on screen and no VFS mount or mount-related serial transition followed. The evidence has no `[DM37-IN]` input receipt for the activation. Following the first missing dispatch, no repeated clicks or speculative QMP actions were sent. The desktop and Disk Manager had responded to earlier input; this run does not measure general responsiveness or prove a post-removal UI state.

Accordingly, the guest did not mount the original, read its proof file, capture a mounted target, remove the USB device, attempt stale VFS access or stale Dismount, attach/enumerate the replacement, or explicitly mount/read replacement contents. There is no guest evidence yet for F1/F2/F3/F6/F7, nor an attempted hotplug transition to classify as unsupported. The host qualification remains useful but does not substitute for these mandatory guest transitions.

### Session release, media, and remaining work

The guest could not be shut down through the production Shell because reliable input had not been established. QMP `quit` was issued only to the QAC-owned guest after the blocker; this is **not** claimed as normal Shell shutdown. QAC observed child exit code 0, marked the reservation `Released` (`Managed child exited`, `degraded=false`), and post-run census returned to zero QEMU processes, sessions, and reservations. QMP closed. The private ESP changed only `NvVars` (SHA-256 `4B0C50C6A6C30AAEC36F3C14A546D8F86445BAD84BD7EB3369DD4A8FF9873348` before to `76EECC781CA0219DDD88F8B21D0929A0EF63E3F7BBC17330B0705BEF4EA408A1` after); all other ESP entries were unchanged.

The independent final verifiers passed GPT CRC/copy agreement, partition geometry, FAT32 metadata, FAT mirror, and proof-file checks for both images. Original and replacement final hashes match their initial hashes exactly. `final-media-verification.json` records `hotplug_executed: false`; no read-only image bytes changed.

Keep `.phase` at `last_completed=DM36`, `next_expected=DM37`; DM38 has not started. Gate F remains open. The next continuation should establish a reliable guest input/dispatch route using harmless actions, then resume the mounted-idle removal and same-capacity replacement sequence from fresh verification. Gate G remains the separately scoped controlled I/O/Flush fault-injection campaign. Preserve accepted A/D/E and the B/C classifications above.

## DM37 Continuation 17R2 — input dispatch and Gate F removal/replacement verified

**Outcome B — `DM37_REMOVAL_REPLACEMENT_VERIFIED` for the prepared read-only USB scenario.** C17R2 resumed from full HEAD `c23dea85cb4e2c6e09ee47f4fb6163afc1bd0a63` on `DISK_MANAGER_IMPROVEMENTS`; `.phase` stayed at `last_completed=DM36`, `next_expected=DM37`. The earlier C17R1 input boundary is superseded by this source-matched run's guest input, production Mount, removal, replacement, VFS proof read, and clean Shell shutdown evidence. Gate G and the other separately scoped DM37 scenarios remain open. No DM38 work or Outcome A is claimed.

### Production input path and Mount

The production Mount confirmation now starts with Cancel focused. Tab toggles Cancel/Mount focus, Escape cancels, and Enter/Space dispatch only the focused button through the normal widget activation path. Cancel and confirmation both use `onWidgetClick`; modal dismissal restores the partition-pane focus. The bounded DM37 trace records keyboard/mouse receipt, queue admission/dequeue, compositor and window focus, modal/widget identity, action dispatch, Mount handler entry, VFS result, and trace loss/omission counters. The partition-selection checkpoint is armed from the actual partition list selection as well as the direct selection path.

The production-linked widget regression covers default Cancel, Tab then Enter on Mount, focused Space, Escape, paired key release, repeated activation, no neighboring destructive action, and focus restoration. It does not call the Mount handler directly. The guest used QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`, SHA-256 `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`) under verified QAC acceptance reservation `1dfbea52681b4e59aea5f7bca305b0e7`, owner PID `19376`, child PID `33264`, QMP `127.0.0.1:56751`. QEMU reported a relative `QEMU PS/2 Mouse` (`absolute=false`). The diagnostic kernel SHA-256 is `DF05613B454B861B052546319607B4903FBC5EF1A43AF24CC7ACB2E25932DC23`, bootloader SHA-256 `A0F1E0A46D166D1C99828AE07A80C2ECDAF8A2517442C07CB74BC316D5A455C1`, and private runtime manifest SHA-256 `F761AE4A5B1A9A7149D74C9A63E10237A4343FA6F8F42B46C49821F7457BB01F`.

Disk Manager launched through Start Menu. The original USB was Disk 1, read-only, GPT partition 1, registration ID 2, partition-view slot 1/generation 2, GUID `9c308050-013a-4aa2-b897-b14548763cd3`. Its normal Mount dialog proposed `/mnt/disk1-part1`. The default Cancel control was exercised with Enter and the dialog reported `Mount canceled` with the device still unmounted. After reopening, Tab visibly focused Mount and Enter reached the production route. Serial records the PS/2 key, accepted queue event, dequeue, Disk Manager focus, focused Mount widget activation, `mount-handler-entry` for device 1/registration 2, and `mount-vfs-result` success at mount index 1. Disk Manager then reported `Mounted at /mnt/disk1-part1 (Read Only)`.

### Original removal and replacement

Before removal, the selected original disk, partition, registration, generation, mount path, and read-only state were captured in `guest-pre-remove.png`. Only the supported, verified QEMU `device_del dm37fusbid` path was used for the removable USB data device. QMP returned the matching `DEVICE_DELETED` event; `query-block` dropped the original image and `info usb` became empty. Guest serial then recorded one in-flight READ10 ending in a CSW failure during the detach (LBA `0xC48`, transfer status `0x09`, caller buffer still classified as untouched sentinel), followed by `[USB-MSC] disconnected`. No old-device USB I/O was recorded after that disconnect. Disk Manager's cached mounted row remained visible until one normal F5 rescan; afterward the USB row disappeared and the selected-device details were cleared. The root, OVMF, and unrelated devices were not removed.

Only after the original disconnect settled, the prepared same-capacity replacement was added read-only at the same supported UHCI port. Guest serial recorded a fresh USB MSC registration; Disk Manager showed a new read-only GPT Disk 1 in `Unmounted` state. The replacement's current registration ID was 5 and its partition view was slot 2/generation 3. It retained the same geometry but has disk GUID `d37f0001-2026-4f37-9001-000000000037`, partition GUID `d37f0002-2026-4f37-9002-000000000037`, and distinct proof bytes. No mount state was inherited. The ordinary confirmation was explicitly opened for this selected partition, Tab focused Mount, and the production input trace records Mount widget activation, handler entry for device 1/registration 5, and a successful VFS mount at index 2. The new read-only path was `/mnt/disk1-part1-2`; the original path was not reused.

After the replacement mounted, the Shell read `/mnt/disk1-part1-2/DM13/PROOF.BIN` through VFS and displayed the exact 39-byte payload `guideXOS DM14 USB revisionB proof 002` (CRLF). Serial records a successful open (handle `0x114`), 39 bytes read, and successful close. A subsequent read of the captured original path `/mnt/disk1-part1/DM13/PROOF.BIN` failed with `Cannot open file` and `shell-vfscat-open-failed`, so the stale original mount path remained unusable after replacement. The Shell's harmless `help` input was verified after restoring Terminal focus.

### Shutdown, media integrity, host coverage, and remaining gates

The production Shell `shutdown` command produced `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`. QEMU child PID 33264 exited, its QMP endpoint refused a new connection, and QAC status/list returned zero QEMU processes, reservations, sessions, queued requests, and blocked requests. No forced termination or QMP `quit` was used. After exit, both read-only image files passed an exclusive `FileShare.None` host read-open check.

| Image | Final SHA-256 | GPT disk GUID | GPT partition GUID | Proof payload |
| --- | --- | --- | --- | --- |
| `DM37F-original.raw` | `7B4D3911CD982215308B2562E6DC4A409D6EA3F5B1A2422E249850D38099AFB5` | `0b1fe6dc-26d4-424f-9449-5c962e1dd135` | `9c308050-013a-4aa2-b897-b14548763cd3` | `guideXOS DM13 USB lifecycle proof 001` |
| `DM37F-replacement.raw` | `1506E05D57B283BEB4D57686FA1C75B0FF83AE6820E71C0683259019FBFA0288` | `d37f0001-2026-4f37-9001-000000000037` | `d37f0002-2026-4f37-9002-000000000037` | `guideXOS DM14 USB revisionB proof 002` |

After guest exit, independent read-only verification passed protective MBR, primary/backup GPT CRC and copy agreement, partition geometry, FAT32 BPB, FSInfo and backup boot metadata, mirrored FATs, DM13 directory and 39-byte proof file, canary gap, and metadata/data isolation. The replacement structure check allows its intentionally distinct payload; the separate final-media verifier checked its exact expected proof bytes and prelaunch hash. Both files remained 71,303,168 bytes, host-read-only, and byte-identical to their prelaunch hashes. Evidence is in `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gatef-input-dispatch-20261102/` and the preserved fixture directory `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gatef-removal-replacement-20261102/`. The guest serial log, QMP input/hotplug transcripts, screenshots, QAC post-shutdown census, build logs, and `dm37-final-media-verification.json` are retained there. `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`; no physical media was attached.

The full `scripts/run-storage-manager-tests.ps1` qualification passed **1,503/1,503 storage checks**, including the existing USB Mass Storage 249 checks. Widget activation, DM37 input focus/diagnostics, PS/2 legacy keyboard FIFO, and Shell focus suites passed with zero failures; the USB trace classifier passed 7/7, the GPT verifier 1/1, and the DM30 fixture verifier 1/1. Existing production-linked DM37 coverage in `tests/storage_manager_test.cpp` exercises captured dismount identity, changed MBR/GPT identity, stale mount generations, same-capacity registration replacement, stale dismount rejection without replacement I/O, active-operation settlement, and no inherited mount during replacement. The retained USB/DM14 cases cover disconnect and same-port replacement behavior. The diagnostic AMD64 freestanding kernel build, `build.bat`, and UEFI x64 Release build all succeeded. `git diff --check` passed.

Gates A, D (USB BOT), and E remain accepted; this C17R2 qualifies the prepared read-only Gate F removal/replacement lifecycle. Preserve the existing Gate B/C classifications. Gate G controlled I/O/Flush fault injection, Gate H delayed-input/Close scenarios, and remaining Gate I scenarios stay open. Keep `.phase` at DM36→DM37 and do not start DM38.

## DM37 Continuation 18 — Gate G controlled I/O and Flush failure qualification

**Outcome B — `DM37_FAILURE_ROLLBACK_HOST_VERIFIED`.** C18 started from `3db54b43f93bb379589efb86725bb837309021d8` (`feat(dm37): verify mount dispatch and USB replacement`) on `DISK_MANAGER_IMPROVEMENTS`. Preflight found a clean worktree, `.phase` at `last_completed=DM36` / `next_expected=DM37`, and no later Gate G acceptance. Continuation 17R2 remains the latest prior phase entry. This run qualifies the production-linked hosted failure paths; the final-Flush guest extension remains unqualified because the installed QEMU path did not yield a verified, isolated Flush fault.

### Fault fixture and production paths

The existing `FakeDisk` callbacks already supported selected read/write failures, partial writes, removal, and Flush failures. C18 reused those callbacks and the existing block registry, mount-generation, operation-reference, partition-view, and storage-operation-lease infrastructure. One bounded one-shot read selector, `failReadOnceAtLba`, was added to target a single exact LBA and clear itself on activation. No production fault switch or second fault framework was added.

The fixture is an in-memory 512-byte-sector MBR disk with a disposable writable FAT32 partition. The production formatter, partition mount, FAT implementation, VFS handles, block callbacks, and ordinary `vfs::dismount(MountIdentity)` path were used. Each fault's LBA or callback count was chosen before the operation and cleared immediately afterward. No physical disk was used.

### Gate G results

| Scenario | Controlled trigger and production result | Observed recovery and integrity |
| --- | --- | --- |
| G1 directory metadata | Fail the root-directory sector read; `vfs::stat` returns `VFS_ERR_IO`, preserving the distinction from `VFS_ERR_NOT_FOUND`. | Clearing the fault lets the same path return the original file and size. |
| G1 file data | Fail the first file data LBA; `vfs::read` returns `VFS_ERR_IO`. | The caller buffer remains at its sentinel bytes. Seeking the same open handle to offset zero and reading after clearing the fault returns the exact original data; close succeeds. |
| G1 FAT chain | Fail the FAT-sector read after one data cluster has been copied. | The API returns the valid one-cluster byte count, with the unread suffix unchanged; the failed LBA is present in the read trace and close succeeds. This preserves the VFS partial-read contract. |
| G1 final admission | A one-shot failure on MBR LBA 0 occurs during Dismount's current partition-identity check. | Dismount returns `VFS_ERR_IO`, skips Flush, then restores the exact same still-valid mount to `MOUNTED`; active operations and open references are zero and the storage-operation lease is released. The UI admission helper classifies the read as I/O failure. |
| G2 before first sector | Fail the first selected data-sector write callback. | `vfs::write` returns `VFS_ERR_IO`, the accepted-write count remains zero, and `vfs::close` settles the handle successfully without changing the failed write result. |
| G2 partial data write | Allow the first 512-byte sector callback and fail the second sector callback. | `vfs::write` returns the exact 512-byte short count. The first sector changed on the fake backing disk; the second attempted LBA is recorded as failed. No atomic rollback is claimed. |
| G2 file-size metadata | Allow the data-sector write and fail the following root-directory metadata write. | The write returns `VFS_ERR_IO`; the data sector remains changed, the directory sector remains unchanged, and a later `stat` reports the old one-byte file size. Close succeeds. No FAT repair is attempted. |
| G3 required final Flush | Capture the live target and mount identity, then fail exactly the next Flush callback while calling ordinary production Dismount. | Dismount returns `VFS_ERR_IO` after one Flush attempt. The exact original registration and mount generation still validate, so the mount returns to `MOUNTED`; backing status is `VFS_OK`, active operations and references are zero, and the lease is released. UI failure text names synchronization/Flush failure, and a missing mount after an I/O error maps to “Mount unavailable; durability not confirmed,” never “Unmounted.” |
| G4 transient retry | Clear that single Flush fault and retry Dismount normally. | The retry makes one fresh successful Flush, reaches actual Unmounted state, then explicitly Remounts. A fresh VFS handle reads the expected bytes, including the earlier accepted partial data sector; the failed metadata update still has its original size. |
| G5 device loss/replacement | Reuse existing DM14/DM37 removal regressions: remove the registration during Flush and attach a same-capacity replacement. | The operation returns `VFS_ERR_DEVICE_REMOVED`, invalidates the stale mount, and performs no later read/write/Flush or mount inheritance on the new registration. Existing tests also reject stale captured confirmation by registration ID and registry generation. |

The exact dismount sequence is: verify the captured mount generation, parent registration, geometry, partition-view identity, and Mounted lifecycle; check boot protection; acquire and pin the shared storage-operation lease; revalidate under the VFS metadata lock; reject in-flight admission, aliases, open handles, and active operations; enter Quiescing; revalidate backing partition identity; then perform one required Flush for a writable FAT32/exFAT mount. Success proceeds to filesystem detach, partition-view release, and mount-slot release. A busy failure before teardown restores Mounted. A Flush or backing failure restores Mounted only when a fresh backing-identity check still succeeds and the original quiescing mount has no remaining references. Device removal or changed identity proceeds to stale filesystem/mount invalidation and returns the device/identity error without targeting a replacement. RAII lease settlement releases the operation lease on each return path. There is no automatic Flush retry, forced Dismount, or disk-write rollback.

A failed final Flush leaves the persistence of preceding writes uncertain. The host fixture proves status, callback order, and memory-backed sector effects; it cannot establish physical durability. A partial sector or metadata write that the backing callback accepted remains visible, even when the file operation fails.

### UI and regression qualification

The Dismount test captures the target from the same production-linked `disk_manager_dismount_ui::capture_target` helper used by the app. The final-action revalidation now propagates backing-table read errors as `DISMOUNT_ADMISSION_IO_FAILURE`, instead of rendering them as a stale target. Failed synchronization shows the shared Flush error text. The result-state helper will not label a missing mount Unmounted when the operation failed. The lightweight selected-partition preview remains separate from final authoritative identity checks.

The full storage runner passed **1,514/1,514 checks**, including the new **11/11 DM37 Gate G checks** and all 249 USB checks. Existing busy-reference/protected-mount, DM14 hotplug, same-capacity replacement, stale-handle, and DM36 integrity regressions were included. Widget activation, DM37 input/focus diagnostics, PS/2 keyboard FIFO, compositor pointer activation, Shell focus, USB trace classifier (**7/7**), GPT verifier (**1/1**), and DM30 fixture verifier (**1/1**) passed. The tests exercise production-linked helpers and widget routing; C18 did not claim a live guest screenshot of the resulting error dialog or a guest GUI responsiveness observation.

### QAC and QEMU guest-fault preflight

Read-only QAC 2.0.0 status/list/queue reported zero QEMU processes (zero foreign), managed sessions, reservations, queued reservations, or blocked reservations. The installed QEMU and `qemu-io` are 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`). The QEMU `blkdebug` documentation lists controlled read, write, and Flush events ([official QEMU blkdebug documentation](https://www.qemu.org/docs/master/devel/testing/blkdebug.html)). A host-only smoke on a new 64 KiB raw scratch file confirmed an injected `read_aio` error returns EIO. On this exact Windows raw-file graph, `flush_to_disk` did not fail an explicit `qemu-io flush`; `flush_to_os` failed the write itself. These observations do not establish an isolated guest USB SYNCHRONIZE CACHE failure at the final Dismount boundary.

The safe guest-injection prerequisite is therefore missing for this installed path. No guest was launched, no QMP error-injection command was attempted, and no QAC reservation was taken. No guest runtime, input trace, shutdown, guest-media hash, or guest-media integrity result is claimed. QAC remained clear after preflight. The raw host smoke file is only a tool probe, not a disposable guest fixture; its final SHA-256 is `ABC3DA22414BA3E7625E73EEBF4BD748B2EE5C062DF81E3D4890A4DCB2BF2630`.

### Builds, hashes, and retained evidence

The freestanding AMD64 kernel, isolated `build.bat` desktop build, and UEFI x64 Release build all succeeded. Kernel flags remained `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`. The builds emitted existing compiler warnings but no build errors. `git diff --check` passed after the source changes.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `kernel-build/bin/kernel.elf` | 3,183,704 | `A2A431BD070489A671211F42EDD0F5FC4789D1272BB5F9E3222E6E5D20196923` |
| `kernel-build/bin/kernel.pe` | 3,308,964 | `5FCE634F42B2B3010DA7F32063E350B12506F87832E19EC1EB62896CB2505E82` |
| `build-bat/guideXOSServer.exe` | 7,891,254 | `067A04A03F491E0355702E6EF8600B216312CCA3CB104D05FBAD048F9A938377` |
| `uefi-build/bin/guideXOSBootLoader.exe` | 59,904 | `C6F2F42C946CABA4C2F49B9FC822A2A0E8EE5E7FDB7488B896DFC0BDB2E0AE90` |

SHA-256 values below identify the validated source/test files at C18; the final Git commit records the complete source and documentation set.

| Source/test file | SHA-256 |
| --- | --- |
| `kernel/core/fs_fat.cpp` | `80F6FA707BF86678F795CB4829C27817876C945BD47119B6EA24B2781134CCA7` |
| `kernel/core/vfs.cpp` | `CCEA275A819550BDF8B8CC96F06822B45F9535C7B7A6BBBF7211504B7C0B567D` |
| `kernel/core/kernel_apps.cpp` | `608BC047808ECD9947AD07ADDE314E39E69918E587A99A1C957D31D9B35F4015` |
| `kernel/core/include/kernel/disk_manager_dismount_ui.h` | `CDD366DA5D824998CFFE7607545D5E2DB270E4BE76767F4CB361DE1E2BFB8C20` |
| `kernel/core/include/kernel/kernel_apps.h` | `188BF58BEB282DA9B710ADC8D367865B7DF083AA7AFC0FDAD3CB95EB59B3FE22` |
| `tests/storage_manager_test.cpp` | `0C9A41D1D21272EACCBDA6C77D5AA41CFF03DDFFB89F4ABEE4A4749CD24972A3` |

Build logs and QEMU capability observations are retained under `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gateg-host-20261102-c18/`. The hosted FAT32 fixture is memory-backed, and its test assertions preserve the exact accepted and rejected sectors around each injected write.

Gate G host behavior is qualified. Its guest final-Flush failure and on-screen failure-state observation remain open until a supported mechanism can trigger and clear a fault specifically at the guest's final Flush boundary. Gate H delayed-input/Close, remaining Gate I work, Gate B/C limitations, and final DM37 review also remain. Preserve accepted Gates A, D (USB BOT), E, and F. Keep `.phase` at DM36→DM37; do not declare Outcome A or begin DM38. For final DM37 closure, first qualify the guest final-Flush boundary and then complete the outstanding H/I and B/C acceptance review without changing the accepted gate distinctions.

## DM37 Continuation 19 — QEMU final-Dismount Flush targeting investigation

**Outcome B — `DM37_FAULT_INJECTION_PREREQUISITE_MISSING`.** C19 started from the requested clean checkpoint `4d7ede7c8a7ed09547e3e8e3aa56dc281b7fa5f8` (`feat(dm37): qualify controlled I/O and flush rollback`) on `DISK_MANAGER_IMPROVEMENTS`. `.phase` remains `last_completed=DM36`, `next_expected=DM37`. The accepted C18 hosted Gate G result and 1,514/1,514 storage-suite baseline remain in force. C19 made no production, test, or build changes.

### Installed QEMU and production block graph

The installed `qemu-system-x86_64`, `qemu-io`, and `qemu-img` are QEMU 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`). Their SHA-256 hashes are respectively `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`, `E735D9BD1B5254F908CA8EB197F6EEF5BCF3B760B595F05DEA45CC91AD677C32`, and `6D484FD8CD6E90150772529160347A52A8F48EB50F132ACC1228D8A4E7BD2D75`. The C15R2 writable launch request confirms the production test profile is raw storage with `cache=writeback`, exposed as a removable USB Mass Storage device.

The tested fault-filter placement is `USB Mass Storage BOT → SCSI disk → BlockBackend → raw format → blkdebug protocol filter → file protocol → image`. The offline scratch graph used the lower four block nodes: `driver=raw,file.driver=blkdebug,file.config=<rule.ini>,file.image.driver=file,file.image.filename=<scratch.raw>`. Placing `blkdebug` above `raw` did not see the child's `read_aio` event; placing it below `raw` did. QEMU's [blkdebug documentation](https://www.qemu.org/docs/master/devel/testing/blkdebug.html) describes configuration-time event rules and its `qemu-io` breakpoint commands. The QEMU 11.0.0 [block flush path](https://gitlab.com/qemu-project/qemu/-/blob/v11.0.0/block/io.c), [blkdebug filter](https://gitlab.com/qemu-project/qemu/-/blob/v11.0.0/block/blkdebug.c), and [USB storage SCSI path](https://gitlab.com/qemu-project/qemu/-/blob/v11.0.0/hw/usb/dev-storage.c) were reviewed alongside the installed QEMU 11.0.0 documentation. The reviewed source is the v11.0.0 tag; the installed binary's longer build revision is recorded above.

### Offline scratch matrix and finding

All probes used newly created 1 MiB raw scratch images in `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gateg-host-20261103-c19/`. No accepted fixture, guest, QAC reservation, or physical disk was used. The complete commands, rule files, exit codes, pre/post hashes, and first-sector bytes are in `offline-matrix-final2.txt` in that directory.

| Probe | Result |
| --- | --- |
| `read_aio`, raw → blkdebug → file | The selected read returned EIO (exit 1); the unchanged scratch hash confirms the read fault rule was reached. |
| Default `qemu-io` cache mode with `flush_to_os` | The write command itself reported EIO (exit 1), though the host-visible scratch bytes changed. This does not prove durability. |
| Default `qemu-io` cache mode with `flush_to_disk` | The write and command sequence succeeded (exit 0); this event did not produce a fault. |
| `qemu-io -t writeback`, `flush_to_os` or `flush_to_disk` | The write reported 512/512 bytes written; the following explicit Flush made the overall command exit 1. Generic explicit-Flush injection works in the production cache profile. |
| `qemu-io -t unsafe`, either Flush event | The write and explicit Flush returned success (exit 0); this unsafe mode suppresses the disk Flush path and is not the production profile. |
| Write, successful Flush, then clean Flush, state rule armed for second Flush | Exit 0 for both `flush_to_disk` and `flush_to_os` rule variants. The first successful Flush leaves no dirty generation for QEMU's disk-flush callback; a subsequent clean Flush cannot be isolated by the tested event rule. |
| `flush_to_os` state rule with a later second write and Flush | Both write commands reported success, then the overall sequence exited 1: the armed error was delivered on a later dirty Flush, not the intervening clean Flush. |
| One-shot `read_aio` control | First read failed and second read succeeded, confirming `once=on` is consumed after a match. |

C18's contradictory smoke behavior is explained by using `qemu-io` defaults: the utility defaults to writethrough, whereas the production guest uses writeback. Explicit `-t writeback` proves that the generic Flush error can be raised after a successful write. It does not solve the final-Dismount boundary. In the production FAT32/VFS path, a file save performs a volume Sync Cache after its data and metadata writes; closing a write-open FAT32 handle also calls the filesystem Flush path. These successful save/close Flushes occur before the later ordinary Dismount Flush.

QEMU's block flush code emits `flush_to_os`, then can skip the physical/disk callback when the current write generation is already recorded as flushed. The `flush_to_disk` event and blkdebug driver Flush callback therefore do not identify a later clean Dismount Flush after successful earlier Flushes. The state-rule probes likewise could not cause the clean second Flush to fail; delaying a `flush_to_os` error made it hit only after another write dirtied the image. The installed documentation and reviewed source expose configuration-time blkdebug rules and qemu-io breakpoints, but no documented runtime QMP operation for safely rearming a rule at the final guest Dismount boundary was found. C19 therefore did not establish a deterministic, isolated final-Dismount Flush failure while preserving the save-path Flush.

### Stop boundary and retained gates

Because the final-Dismount fault prerequisite remains missing, C19 launched no guest, made no QAC reservation, and claims no guest UI, rollback, post-Dismount state, shutdown, or guest-media result. The host proof also does not imply durable media state: QEMU may expose bytes in the scratch image even when Flush reports EIO. No physical media was used. The accepted C18 host Gate G qualification, 1,514/1,514 storage baseline, and C18 build provenance remain unchanged; no tests or builds were rerun because C19 changed only documentation. `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`, and `.phase` remains DM36→DM37. Preserve accepted Gates A, D (USB BOT), E, and F and existing B/C classifications. Gate G guest Flush injection, Gate H, remaining Gate I scenarios, and final B/C acceptance review remain open. Do not start DM38.

## DM37 Continuation 20 — delayed input, modal confirmation, and Close lifetime

**Outcome B — `DM37_INPUT_CLOSE_HOST_VERIFIED`.** Starting HEAD was `ecb2105132e709d32b5f911cc2347bc01cc791cb` (`docs(dm37): record C19 QEMU flush targeting blocker`) on `DISK_MANAGER_IMPROVEMENTS`. Preflight found a clean index/worktree and `.phase` at `last_completed=DM36`, `next_expected=DM37`. The hosted matrix and final-source builds pass. A QAC-owned guest completed the safe read-only Mount/Dismount modal sequence with the runtime source state recorded at launch. The final audit added a pointer-release capture guard after that guest run; this follow-up is covered by focused host regression and final-source build, but is not present in the guest kernel. Controlled delayed/double-input races and true guest Close-during-active-operation timing remain incomplete. No production safety defect was demonstrated. DM38 remains blocked pending DM37 review.

### Production input and lifetime rules

The desktop drains the existing PS/2 FIFO in order. Each queued legacy key-down carries its enqueue-time input-context generation and a repeat bit derived from make-code/release tracking. Key-up transitions remain in FIFO order, while the legacy desktop dispatcher consumes key-downs only. Before routing, the desktop drops a key-down whose generation is no longer current. The compositor advances the generation when focus changes or the last focused window goes away; Disk Manager advances it when a confirmation/result modal opens, changes focus, settles, or shuts down. Repeated modal Enter, Space, Tab, and Escape events are ignored. Tab updates focus and invalidates keys queued for the prior focused control. Enter and Space use the existing focused-button dispatcher; Escape follows the supported Cancel route.

Pointer activation captures window ID, widget ID, button, and current input-context generation on mouse-down. Mouse-up dispatch requires that capture, matching button, current context, same registered window, and a visible/enabled widget under the pointer. Release clears pressed marks and capture; a stale or mismatched release cannot activate a replacement or neighboring widget. A new mouse-down retires an incomplete prior capture. This is bounded state in the existing compositor, not a new asynchronous UI framework.

Mount and Dismount confirmations capture their device/partition/mount target. Rendering continues to use the lightweight preview. Final admission freshly checks captured identity, registration and generation, partition/view identity, mount generation, presence, protection, busy ownership, and lifecycle before VFS. The handler does not substitute the newly selected row for a stale capture. Existing C16–C18 production-linked cases cover changed protection, stale mount/partition identity, registration reuse, replacement identity, removal, busy state, and rejected-target no-I/O behavior.

The Dismount VFS call is synchronous on the desktop event-loop thread. The ordinary event loop cannot process Close while that call is executing. An admitted Dismount holds VFS ownership through active-I/O settlement and required final Flush; VFS owns rollback/invalidation and releases its lease before returning. `DiskManagerApp::onCloseRequested` records a reentrant Close while its Dismount handler is active and queues ordinary Close only after recording the actual VFS result/final mount state. It never forces VFS close, retries, repairs, or reports a failed Flush as success. Modal/app close advances input context and retires modal/widget state. Queued input from the retired context is rejected.

### Hosted qualification and limits

The final `scripts/run-storage-manager-tests.ps1` run passed **1,521/1,521 storage-manager checks**, including USB Mass Storage **249** and Gate G fault/rollback **11**. Seven named C20 combined GUI-to-VFS checks are included in that total: Mount Cancel made zero VFS calls/reads/writes/Flushes; deliberate focused Mount called production VFS once; Dismount Cancel preserved Mounted with no write/Flush; Close was requested while the fake device held final Flush; a failed Flush returned the VFS error, kept the valid mount Mounted, attempted one Flush, and settled active operations to zero; closing the terminal Flush-error result performed no retry/write/Flush; and one deliberate retry attempted exactly one further Flush and reached Unmounted. A focused final-source Gate H input/pointer test passed **24/24**, PS/2 legacy FIFO **9/9**, and the compositor pointer-button test passed. Widget activation, input diagnostics, input focus, Shell focus, USB trace classifier **7/7**, GPT verifier **1/1**, and DM30 fixture verifier **1/1** passed.

Coverage has three linked layers. **Production-linked GUI/input policy:** the 24-check lane exercises production queue/context/focus/dispatch helpers and pointer-capture policy with a bounded modal harness. It covers delayed Enter/Space/Tab/Escape, focus changes with queued input, release and repeat metadata, repeated Cancel, default-Cancel Enter, reopen after Cancel, a later deliberate Confirm, result-modal stale Confirm, stale pointer release, and Close deferral policy. **Production-linked VFS lifetime:** the 1,521-check storage executable uses production VFS and bounded fake callbacks for paused-final-Flush/failure/result-close/retry and reuses existing stale-identity, busy, removal, and replacement cases. **Combined GUI-to-VFS:** seven checks dispatch through production shared widget/button and modal-selection helpers into production VFS Mount/Dismount, including Close of a terminal Flush-error result with exact unchanged write/Flush counts. The host harness does not link full `DiskManagerApp::onWidgetClick`, `onCloseRequested`, or app/window destruction. It therefore does not prove an app-class callback while fake I/O is blocked; application synchronous ordering/deferred-close code was reviewed and built, while the fake validates shared defer policy and VFS settlement. Full app-class host coupling is unavailable.

A failed Flush leaves persistence of earlier writes uncertain. The valid backing mount remains Mounted and VFS active-operation ownership settles; no atomic rollback is claimed. Device removal invalidates the captured registration/mount instead of restoring it. The fake does not establish physical-media durability. C19's QEMU targeting blocker is unchanged and was not reopened.

### QAC guest: read-only modal workflow

A fresh QAC 2.0.0 coordinator/ownership census was clear before launch. Reservation `ba3190017dfe4090b5a13f453eac0e1e` ran verified owner PID `9284`, QEMU child PID `22512`, QMP `127.0.0.1:57090`; the process was not degraded. QEMU 11.0.0 SHA-256 was `A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. Diagnostic guest kernel SHA-256 was `40123F24D504ABA9DF4DF1AB075B60DF59D7B63B946A21191A41BD4480F703D2`, bootloader `7C608F24BB68AC3BB5DA55A5D33AD245E46D99458570EB302B89C7C33F5C9F4B`, OVMF `2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`, and runtime-manifest SHA-256 `1AAE797F7E00E219F9122DDF394354AC2D8B1C7E45D2FB700E8677C2061D3E21`. Exact source hashes are retained in the manifest.

Only the disposable 71,303,168-byte read-only USB data fixture `guest/media/DM37H-readonly.raw` was attached. SHA-256 `7B4D3911CD982215308B2562E6DC4A409D6EA3F5B1A2422E249850D38099AFB5` matched before and after. Disk 1 USB serial `DM37HREAD01` exposed GPT disk GUID `0b1fe6dc-26d4-424f-9449-5c962e1dd135`, partition GUID `9c308050-013a-4aa2-b897-b14548763cd3`, FAT32 partition 1 at LBAs 2048–139230, label `DM13PROOF`, and path `/mnt/disk1-part1`. QMP `query-block` reported `ro=true`; no physical disk was attached.

Real guest PS/2 input opened Disk Manager and selected the partition. Enter on default Cancel dismissed Mount confirmation with `Mount canceled`; no Mount handler entry followed and VFS remained Unmounted. Reopening, visually confirming Tab moved focus to Mount, and Enter produced one Mount handler entry and successful production VFS Mount. Dismount confirmation default Cancel + Enter reported `Dismount canceled` and kept the volume Mounted. The user deliberately focused Dismount and confirmed; production VFS succeeded and the UI reported `Unmounted`. This read-only target required no final Flush. Disk Manager closed normally; desktop remained live, Shell launched, `help` worked, and Shell `shutdown` produced `[SHUTDOWN] final reason=system-shutdown exitCode=0 caller=desktop expected=1 runtime=none app=none`.

QMP acknowledged a pointer click before the guest release/dispatch; serial later recorded the release when subsequent pointer movement pumped input, followed by exactly one widget activation. This shows host acknowledgement is not guest dispatch. It is **not** a controlled delayed-input race or double-activation test. No blind rapid sequence, physical Escape, hidden VFS command, or in-flight guest Close was used. Writable final-Flush failure and Close during active I/O were not exercised in the guest. The runtime matched source at launch; the pointer-release capture follow-up was added after shutdown and validated only by final host test/build.

After normal Shell shutdown, QEMU child exited with code 0, QMP closed, QAC released the reservation, and fresh status/list/queue plus process/port census found zero sessions, reservations, QEMU children, or foreign guests. Read-only image hash matched and independent PMBR/GPT/FAT32/FAT-mirror/proof verification passed. OVMF file hash was unchanged; only private ESP `NvVars` changed (`76EECC781CA0219DDD88F8B21D0929A0EF63E3F7BBC17330B0705BEF4EA408A1` to `5A65E8B784CF1D26BF1F025BCEC89BB642F449D4041567B4D1EE82AC6B6DCCEC`). This firmware-variable change is separate from the read-only data image. Serial, QMP, screenshots, runtime, QAC census, hashes, and verifier output are under `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gateh-host-20261104-c20/guest/`.

### Final-source build, Gate I inventory, and remaining acceptance

After the final pointer-release source change, `build-uefi.ps1 -Arch amd64` passed its UEFI x64 Release bootloader build, full-source AMD64 freestanding production-kernel build, and ESP setup. `ESP/kernel.elf` matches the freshly built kernel. Kernel ELF SHA-256 is `B8F177B6E8AA41A8AB9F4215DC1329EA72300447694AE3AB98A2079A2B3BAFD6` (3,189,768 bytes); kernel PE is `E3AC598784AD98FB4602802C6A42293121E04DF103AE7982DCC86AA149D5CB60` (3,316,851 bytes). UEFI x64 Release `BOOTX64.EFI` SHA-256 is `C5D46674C7A857E54B4F4EA41C51A61467FC57DEDB6910EDBE40E4CCDAD4ACEB` (59,904 bytes). `build.bat` passed and produced `guideXOSServer.exe` SHA-256 `C0842C7249AA54EC0B2FB3B69E6B48093E1A3E8959E4DC137DF26732D4C29B84` (7,891,254 bytes). The UEFI build checked QEMU prerequisites but did not launch a guest. `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; PacMan audio source was not modified. Logs and copies of all four final artifacts are under `E:/CodexArtifacts/guideXOSServer_DiskManagerImprovements/out/dm37/gateh-host-20261104-c20/source-change-rerun/`.

| Campaign | Retained Gate I evidence | Remaining work |
| --- | --- | --- |
| Gate A read-only Dismount (C5) | Initial/final fixture hashes, registration/mount identity, production trace, QAC ownership/release, shutdown classification, GPT/FAT32 verification, and no attributable data-media changes are retained in the C5 report/artifacts. | Final cross-campaign consolidation. |
| Gate D writable USB BOT (C15R2) | Initial `D4975D8F20ABE5E8B8A221408E79CEB151B3E8C15C7A04A16556A54CAB6AA3DF`, final `C56D2B07EDC4DCFC936EF7F2C66AFEA265F5E5B47D0CA1347345B559A954B231`; four authorized in-partition sectors, device/partition identity, normal Shell shutdown, QAC release, and GPT/FAT32 verification. | Final cross-campaign consolidation. |
| Gate E read-only maintenance (C10 Run 6) | Initial/final image hashes, Mount/Dismount/Check/Remount/fresh VFS read evidence, device identity, QAC lifecycle, normal Shell shutdown, and GPT/FAT32 checks are retained. | Final cross-campaign consolidation. |
| Gate F removal/replacement (C17R2) | Original/replacement initial/final hashes, distinct GPT/partition IDs and registration generations, production removal/mount/read traces, unchanged read-only data images, normal Shell shutdown, QAC release, and independent GPT/FAT32 verification. | Final cross-campaign consolidation. |
| Gate H C20 | Read-only fixture initial/final hash, exact device identities, QAC/runtime provenance, normal Shell shutdown, QAC release, independent GPT/FAT32 verification; private ESP `NvVars` delta is isolated. | Controlled delayed/double race and active-operation guest Close remain unavailable. |

Gate I needs one final consistent table/audit of each required campaign's initial/final image SHA-256, authorized sector differences, QAC owner/child/QMP lifecycle, shutdown class, GPT/FAT32 result, device identity, and source/runtime hashes. Do not start a new media campaign only to fill this table. Gate B remains host-qualified because ordinary guest workflows cannot retain a real VFS reference through Disk Manager. Gate C remains host-qualified; the guest showed protected running-root inventory but no final VFS rejection attempt. Gate G remains production-linked host-qualified; C19 cannot target the isolated guest final-Dismount Flush after earlier successful Flushes. Gate H is host-verified with safe guest Cancel/Confirm/Unmount/Close-after-settlement evidence; no guest delayed/double race or Close-during-active-I/O proof is claimed. Preserve A, D, E, and F. Keep `.phase` at DM36→DM37 and both NVMe proof flags zero. The next continuation should be formal DM37 acceptance-review consolidation of B/C/G/H and Gate I, not DM38 implementation.

# DM37 — Safe Volume Dismount Foundation

**Outcome B — `DM37_DISMOUNT_ENGINE_READY` for the host-qualified production
VFS lifecycle.** `.phase` remains `last_completed=DM36` and
`next_expected=DM37`. The Disk Manager GUI flow and guest/QEMU acceptance are
future work.

**Latest continuation status (2026-10-20): Outcome B —
`BLOCKED_BY_QAC_OR_FOREIGN_QEMU`.** Continuation 8 stopped before guest launch:
QAC had a verified live acceptance reservation, but its QEMU command line
pointed into the sibling `guideXOSServer_FONT_SUPPORT` project while the
coordinator labeled the reservation `guideXOSServer`. That ownership conflict
left the acceptance lane unavailable for safe DM37 use. Gate A remains
accepted; Gate E remains incomplete after continuation 7. See continuation 8
at the end of this document. The earlier continuation 6 and 7 censuses remain
historical evidence for their respective runs.

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

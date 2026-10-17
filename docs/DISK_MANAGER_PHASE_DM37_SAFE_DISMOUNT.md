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

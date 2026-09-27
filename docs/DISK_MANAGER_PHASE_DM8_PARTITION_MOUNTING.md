# Disk Manager DM8: Partition-Aware FAT32 Mounting

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `b489317f46daee1887d12476840132405eb6dc3d`
- The starting worktree was clean. The locally recorded branch was even with `origin/DISK_MANAGER_IMPROVEMENTS` (0 ahead / 0 behind). A fetch was attempted and failed with `git@github.com: Permission denied (publickey)`, so the remote state was not freshly verified.
- DM1 through DM7 and the current implementation report were reviewed before implementation. No ordinary physical disk was accessed.

## Previous mount path and limitation

At the starting revision, `vfs::mount_partition(const char*, uint8_t, uint8_t)` accepted a path, block-device index, and 8-bit partition number, ignored all three arguments, printed a TODO diagnostic, and returned `0xFF`.

The VFS mount record held only an active flag, path, filesystem type, block-device index, filesystem-volume index, read-only flag, and alias source. It could not retain a disk incarnation or identify the partition behind a mount. `mount_type()` mounted a filesystem from the whole-device index. `detect_fs_type()` read LBA 0 and recognized a FAT boot sector there or a FAT-family MBR primary entry. The FAT driver then scanned the first four MBR entries and used a `partitionOffset` added to each filesystem LBA. It did not retain the MBR partition length, so the FAT volume could extend beyond that partition while remaining inside the disk. GPT volumes were not discovered by this path. The ext readers likewise interpreted filesystem structures at fixed device-relative locations; they did not have a general partition endpoint.

## Partition endpoint and identity

`kernel/core/partition_block_view.cpp` provides 16 fixed view slots. Each view has an opaque slot/generation handle, exact partition identity, parent capacity and sector size, reference count, in-flight I/O count, and read-only state. Duplicate views for the same identity share a slot and increment its reference count. Closing slots are not reused while I/O is in flight; generations stop before wraparound.

The endpoint contains no durable pointer. It references the parent device by index and monotonic registration ID and references the view by slot and generation. The retained identity includes:

- GPT: parent incarnation, entry number, unique partition GUID, and exact start/end/count.
- MBR: parent incarnation, primary entry number, type, and exact start/end/count.
- The parent registry generation captured when the view is created.

The view inherits the parent logical sector size and supports power-of-two sectors from 512 through 4096 bytes. A 4Kn view test verifies inherited geometry. This does not claim 4Kn FAT VFS support.

Every view read/write rejects zero count, an invalid local range, a crossing range, a translated-LBA overflow, or a range outside the parent capacity before calling the parent driver. It pins and rechecks the exact parent registration, capacity, and sector size for each operation. Removal, offline registration, or slot reuse therefore fails with no-media and cannot redirect I/O to a replacement device. View reads/writes propagate parent errors. Flush uses the same exact parent identity.

## VFS and FAT integration

`mount_partition()` now delegates to a production partition mount path. The detailed API returns specific errors for invalid paths, occupied paths, unavailable devices, invalid tables, changed/missing partitions, duplicate mounts, registry exhaustion, unrecognized filesystems, and unsupported filesystems.

Mount preflight requires an absolute normalized path under the 256-byte VFS limit, safe ASCII path components, a valid non-hybrid MBR or agreeing GPT table, an exact partition number, and—when supplied by Disk Manager—the unchanged parent registration and partition snapshot. It creates the view, rejects duplicate identity and occupied path, mounts through `fs_fat::mount_endpoint()`, then reparses and matches the identity before publishing the VFS record. Partition mounts accept FAT32 only. A missing or invalid FAT boot signature, unsupported geometry, out-of-volume BPB range, invalid root cluster, or undersized FAT table fails closed. Mount does not format, repair, or modify media.

FAT sector operations use a `BlockEndpoint`; an explicit partition mount sees its boot sector as local LBA 0 and has no scattered base-LBA arithmetic. The compatibility whole-device MBR FAT probe also creates a bounded view and exposes the exact identity in its VFS record. A direct filesystem at device LBA 0 remains a whole-device endpoint. The legacy read-only BPB probe retains its explicit offset solely for DM7 compatibility tests.

VFS mount records now distinguish whole-device and partition-view mounts. They retain parent registration ID, exact partition identity, view handle, mount path, filesystem type, and read-only state. `mount_identity_valid()` checks the parent incarnation, view generation, and current parsed partition identity. Storage destructive-operation protection still treats any mount as protection for the entire parent disk; it reports partition identity as known only while the exact identity validates.

Aliases retain a view reference and the exact mount identity. The source mount cannot be unmounted while an alias exists. VFS exposes mounts through `mount_count()` and `get_mount_by_index()`; File Explorer already lists those paths in its Mounted drives pane and navigates to a selected mount path.

## Mount-point and access policy

Disk Manager proposes `/mnt/disk<device-index>-part<partition-number>`. If occupied, it chooses `-2`, `-3`, and subsequent bounded numeric suffixes deterministically. The path policy accepts normalized components containing ASCII letters, digits, `-`, `_`, or `.`; it rejects `/`, `.` and `..` components, repeated separators, and paths at or above `VFS_MAX_PATH`.

The contextual **Mount...** action appears for detected FAT32 partitions. Its dialog shows disk, partition number and scheme, FAT32, the detected volume label when available, proposed path, and read/write mode, with explicit Mount and Cancel buttons. The user starts mounting explicitly; successful format only rescans and leaves the partition unmounted. GPT and MBR use the same FAT/VFS path. The dialog passes the captured parent registration and exact partition snapshot back to mount preflight. No editable mount path or automatic repair/format is offered.

A mount is read/write only when the parent has a write callback and trusted synchronous-durable or explicit-flush semantics. Volatile RAM disks and unknown durability are read-only. The 512-byte FAT32 mount path is the only partition/filesystem combination advertised. DM7 formatting remains 512-byte-only; FAT16, exFAT, ext, and UFS partition mounts are not advertised.

FAT file operations synchronously flush through the partition endpoint after metadata publication. Unmount refuses `/`, open files, open directory iterators, and a source mount with aliases. A writable FAT mount gets a final flush before its VFS record and view are released. A failed flush leaves the mount active so the caller can retry. If the parent identity is gone, unmount still releases the filesystem and view for cleanup and returns I/O error rather than claiming a healthy flush.

## Verification

The deterministic in-memory fake-disk suite passed **312 checks, 0 failures**. It includes DM1–DM7 coverage, partition-view boundaries, and production VFS/FAT mount and file operations. New proof includes:

- First, offset, last, and multi-sector translations; zero-count, integer-overflow, one-past-end, huge-count, and partition-crossing rejection before a parent callback; equivalent write bounds and parent error propagation.
- Duplicate-view references, deterministic release/reuse, fixed registry capacity, read-only propagation, 4Kn endpoint geometry, and no I/O to a replacement device after parent removal and slot reuse.
- GPT: create and format two independent FAT32 partitions; verify format leaves them unmounted; mount exact unique-GUID identities; reject duplicate identity and path collision; verify deterministic mount-path fallback; block format/partition creation while mounted; enumerate a root, create a directory and a 1537-byte file through VFS, read exact bytes, and verify the second partition remains isolated.
- MBR primary: reject unformatted and corrupted-boot-signature media without writes; format and mount exact MBR identity; round-trip a 701-byte file; verify all writes stay in-range and preserve LBA 0 and adjacent guard sectors. The legacy whole-device MBR auto-mount is also verified to produce a bounded exact VFS partition mount.
- GPT entry arrays/headers, adjacent partition, guard sectors, and mirrored FAT copies remain intact. Parent flushes are observed after file writes. A failed final unmount flush keeps the mount active; retry succeeds. Open file and directory handles return busy. A fresh FAT/VFS remount reads persisted file bytes while another partition remains independently mounted.
- Read-only parents produce read-only mounts and reject VFS writes before a parent write callback. Parent replacement invalidates stale mount identity and I/O; cleanup succeeds without touching the replacement.

`git diff --check` is run for the final commit. MinGW C++14 freestanding syntax-only validation passed for the new view, FAT, storage manager, VFS, and Disk Manager translation units. The hosted `disk_manager.cpp` C++17 syntax-only check passed. The full desktop build was attempted: the stb reproducibility check passed, then the existing Mbed TLS profile check stopped it because `third_party/mbedtls` is absent. MSVC `cl` and `qemu-system-x86_64` are unavailable, so there is no QEMU/runtime proof. Tests use in-memory fake devices only.

The test build retains pre-existing warnings for an unused FAT helper and an always-false ext4 name-length check. Bare-metal syntax validation also reports existing kernel-app unused-parameter and signedness warnings.

## Outcome and remaining limits

**Outcome A — Partition-aware FAT32 VFS mounting, file I/O, flush, unmount, and remount persistence are proven on GPT and MBR fake disks.** The implementation also bounds the legacy MBR auto-mount path. No hardware runtime claim is made.

Remaining limits: partition-aware mount UI supports FAT32 on 512-byte sectors only; FAT free-space reporting is still unknown; no FAT repair or FSInfo correction exists; VFS operations remain synchronous; there is no QEMU hardware demonstration; and the normal full build awaits the existing Mbed TLS checkout. Suggested DM9 work is a QEMU secondary-disk end-to-end run plus removable-device lifecycle handling and user-visible I/O error reporting before advertising additional filesystem types or geometries.

# Disk Manager DM6: Safe Partition Creation

## Starting repository state

- Repository: `D:\dev\guideXOSServer_DiskManagerImprovements`
- Branch: `DISK_MANAGER_IMPROVEMENTS`
- Starting HEAD: `3362712113b0129f8581bb636510a278d76d83a1`
- The starting worktree was clean and the branch was even with `origin/DISK_MANAGER_IMPROVEMENTS`.
- DM1–DM5 audit, storage safety, initialization, boot provenance, and Disk Manager reports were reviewed before implementation.

## Architecture and request/result model

Partition metadata writes live in `kernel/core/partition_operations.cpp`, exposed through `kernel/core/include/kernel/partition_operations.h`. The bare-metal Disk Manager only prepares a bounded request, collects confirmation, invokes the service, and displays its structured result. Hosted image previews remain read-only.

`CreatePartitionRequest` captures the full target identity and registry generation, partition scheme, exact selected `UnallocatedRegion`, requested byte size or maximum-size choice, one supported partition type, and a bounded GPT name. `CreatePartitionResult` carries status and stage, resulting partition identity, zero-based table slot and one-based parser partition number/range, flush outcome, read-back and parser state, rollback outcome, uncertainty, and a diagnostic.

Creation acquires the existing exclusive storage-operation lease, pins and revalidates the target, checks geometry/write/durability/flush gates and boot/root/mount protection, parses the current table, recomputes unallocated regions, and matches the selected gap by all bounded region fields. It derives the final range from that fresh model and confirms the requested size still fits. It does not rely on UI row positions or cached map coordinates. The lease remains held through snapshots, writes, flush, read-back, parser verification, rescan, and cleanup.

The parser now retains validated GPT entry size, entry-array sector count, and primary/backup array LBAs. That metadata lets the writer update existing GPT geometry without moving or expanding the arrays.

## Range, alignment, size, and supported types

The service uses checked 64-bit LBA and byte arithmetic. The default boundary is 1 MiB, converted from bytes using the current logical-sector size: 2048 sectors at 512 bytes and 256 sectors at 4096 bytes. The minimum partition size is 1 MiB. `MAX` consumes the entire creatable portion of the selected gap after alignment; a numeric size is entered as an integer number of MiB and is floored to complete logical sectors. The service rejects any range that would exceed the request, selected gap, GPT usable range, or MBR 32-bit fields. The dialog shows the original gap capacity, maximum creatable size after alignment, proposed size, and exact proposed inclusive LBA range.

DM6 exposes only the conservative fixed default for each scheme:

- GPT Basic Data, on-disk type GUID bytes `A2 A0 D0 EB E5 B9 33 44 87 C0 68 B6 B7 26 99 C7` (standard GUID `EBD0A0A2-B9E5-4433-87C0-68B6B72699C7`).
- MBR FAT32 LBA, type `0x0C`, inactive by default.

The type is displayed in the confirmation flow; arbitrary GUIDs, EFI/system types, alternate MBR types, extended partitions, logical partitions, resize, move, delete, and repair are outside this phase. The MBR parser already rejects layouts with unsupported extended/logical structures for free-space creation.

The GPT name defaults to `New Volume`, may be empty, is bounded by the UI, and is encoded to UTF-16 with safe replacement and length bounds. It is partition metadata only, not a filesystem label. Production GUIDs come from the existing VirtIO RNG interface; zero values and collisions with the disk GUID or existing partition GUIDs are rejected. Test builds can inject deterministic values.

## GPT and MBR metadata writes

GPT creation requires two healthy, agreeing GPT copies. It chooses the first free entry without renumbering existing entries, writes the Basic Data type, unique GUID, aligned bounds, zero attributes, and encoded name, then updates both entry-array CRCs and both header CRCs while preserving disk GUID, usable range, geometry, and unrelated array bytes. It snapshots exactly the primary header and entry-array sectors plus the backup entry-array sectors and header. Write order is backup array, backup header, primary array, primary header, flush, then read-back. A degraded or conflicting GPT is rejected; DM6 does not repair GPT.

MBR creation supports only a free one of the four primary slots, requires a valid MBR with no extended/logical layout, and enforces start, end, and sector-count representation limits. It writes LBA-addressed FAT32-LBA metadata with conventional CHS compatibility fields; CHS does not determine correctness. It snapshots LBA 0 and changes only the chosen 16-byte entry. Bootstrap bytes, disk signature, unrelated entries, and `55 AA` signature are preserved.

Both paths flush and reread. The normal parser must report the expected valid scheme and locate the exact new partition with expected type, range, GUID/name where applicable; prior partition identities and counts must remain valid, and the recomputed unallocated-region count is returned. GPT byte-level verification independently checks entry bytes, GUID, bounds, type, UTF-16 name, both arrays, both CRCs, disk GUID, and unrelated bytes. MBR byte-level tests independently check its selected entry, inactive flag, type/range, signature, disk signature, unrelated entries, and bootstrap region.

On write, flush, verification, or rescan failure, the service attempts bounded restoration of only the snapshotted metadata while the target remains pinned and the durability policy permits it. It flushes and verifies restoration. Failed or unverifiable restoration reports uncertain state and never claims success. The UI forces a rescan after the operation and selects the new partition by identity when it can be matched.

## Disk Manager UX and non-goals

Selecting a validated eligible gap reveals the contextual **Create Partition...** action; it is not a general toolbar action. The options dialog shows disk identity, scheme/type, gap capacity, maximum after alignment, proposed size/range, size input, and GPT name input. It offers a separate Create confirmation and Cancel. `MAX` is the default. The result identifies the new range and table entry. Properties, the partition list, and map use the rescanned parser model, so remaining gaps are recomputed rather than manually reduced. The new entry is unformatted; no filesystem structures, boot sector, mount, label, or free-space statistics are created, and Format is not opened.

The operation retains DM2–DM5 identity, registry-generation, active-target pin, operation-lease, geometry, write, durability/flush, boot, root, and device-level mount protections. It does not modify a physical device during tests. Runtime/QEMU proof was not performed because QEMU is unavailable in this environment.

## Fake-media tests and validation

The storage suite includes GPT maximum-size, partial and sequential creates, 512-byte and 4096-byte sectors, 1 MiB alignment, entry exhaustion, healthy/degraded/conflicting tables, stale/changed gaps, overlap and size boundaries, GUID collision/injection, empty and named GPT entries, copy parity/CRCs, existing-entry preservation, MBR first/second/full primary slots, inactive FAT32-LBA type, extended layout rejection, 32-bit limits, 512/4096-byte sectors, protection/durability/lease checks, write/flush/read-back failures, rollback success/failure, and uncertain state. Tests use in-memory or sparse fake disks only and independently inspect resulting table bytes.

Validation commands and results for this phase:

- `scripts/run-storage-manager-tests.ps1`: **234 checks, 0 failures**.
- MinGW C++14 freestanding `-fsyntax-only` on `kernel/core/partition_operations.cpp`, `kernel/core/kernel_apps.cpp`, and `kernel/core/kernel_compositor.cpp` with `GXOS_BARE_METAL`: passed.
- MinGW C++17 `-fsyntax-only` on hosted `disk_manager.cpp`: passed with the existing `gui_protocol.h:96` indentation warning.
- `git diff --check`: passed.
- `cmd /c build.bat`: stopped in the existing dependency check before C++ compilation because `third_party/mbedtls` is absent. Dependency policy was not changed.
- `cl` and QEMU are unavailable. The installed `i686-elf-g++` lacks the C++ standard headers required by this translation unit.
- No physical disk or runtime virtual disk was modified.

## Remaining limitations and recommended DM7 scope

DM6 supports one fixed interoperable type per scheme, sizes expressed as integer MiB or `MAX`, GPT and supported primary MBR only, and no formatting or mounting. 4096-byte fake-media coverage does not establish support by every hardware transport or later filesystem driver. The UI cannot associate a VFS mount with an individual partition, so the established conservative whole-device protection remains in force.

DM7 should add a separately reviewed format flow for an already-created unformatted partition: bounded filesystem/type choices, target-and-partition identity revalidation, filesystem minimum/maximum geometry validation, format-only metadata snapshots where meaningful, durable write/read-back verification, rollback/uncertainty behavior, no implicit mount, and fake-media validation before any user-facing operation.

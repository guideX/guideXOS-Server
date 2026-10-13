# Disk Manager DM36 — Read-Only FAT32 Integrity Foundation

**Outcome B — `DM36_METADATA_ENGINE_READY`**
**Phase marker:** `last_completed=DM35`, `next_expected=DM36`
**Scope:** bounded host-tested FAT32 metadata verification foundation. This does not complete DM36 or claim full filesystem consistency.

## Roadmap and repository gate

The continuation began on branch `DISK_MANAGER_IMPROVEMENTS` at expected HEAD
`ed355846cc6cb89dee01b1a967f3d0be215536ce` (`disk-manager: close DM35 read-only
surface verification`). The tracked worktree was clean and `.phase` contained
`last_completed=DM35` and `next_expected=DM36`.

The final DM35 closeout names DM36 as next but does not assign a different
objective. `docs/ROADMAP.md`, `ROADMAP.txt`, and `docs/Development Priorities.txt`
are broader plans and do not define a DM36 Disk Manager scope. Repository
history and the full DM36 search showed no other approved DM36 scope or
competing implementation. The supplied proposal was therefore used as the
working scope. No branch or worktree changes were made, and the phase marker
was not advanced.

## Production audit

The audit covered `fat32_formatter.*`, `fs_fat.*`, `vfs.*`,
`partition_table.*`, `partition_block_view.*`, `storage_manager.*`,
`disk_initialization.*`, `block_device.*`, and the DM35
`surface_verification.*` engine.

Existing production helpers provide checked sector reads, checked LBA ranges,
geometry validation, exact device registration identity and pins, registry
generation checks, shared storage-operation leases, active VFS mount
enumeration, and parsed MBR/GPT partition snapshots. FAT32 formatting writes
and reads back primary/backup boot metadata, FSInfo, and FAT layout. The FAT32
mount parser validates core BPB geometry and root-cluster bounds, while treating
FSInfo as a hint; it does not validate backup boot consistency or FAT mirrors.
No production parser or target-lifetime defect was demonstrated, so existing
mount/format behavior was left unchanged.

## Production API and target contract

The engine is declared in `kernel/core/include/kernel/fat32_integrity.h` and
implemented in `kernel/core/fat32_integrity.cpp`:

- `capture_fat32_integrity_request` binds a selected partition to the caller's
  current parsed MBR/GPT table and target snapshot without doing I/O.
- `begin_fat32_integrity_job` admits an immutable request, acquires the shared
  storage-operation lease, pins the exact registration, and rejects active
  mounts on the target.
- `step_fat32_integrity_job` performs at most one checked read request per
  cooperative step and revalidates the lease, registration, and target.
- `request_fat32_integrity_stop` stops at a safe step boundary.
- `fat32_integrity_cluster_sector` exposes checked cluster-to-volume-sector
  mapping for later bounded traversal work.

The request captures target identity/incarnation, expected registry generation,
partition scheme and table identity (MBR disk signature or GPT disk GUID), and
the selected partition entry, start, and length. The job copies the request;
subsequent UI selection changes cannot redirect it. The engine does not reparse
the partition table during verification: the caller supplies the already parsed,
validated table snapshot, and all filesystem reads remain within the selected
partition and parent-device geometry. Overflow, invalid parent or partition
ranges, unsupported sector sizes, changed registrations, missing reads, and
identity mismatch fail closed. Same-capacity replacements do not inherit the
captured registration.

The target must be unmounted. The lease also prevents conflicting storage
operations and new mount admission during the job. Each terminal path releases
ownership once; the result records acquisition and release. The checker accepts
read-only media and never requests write, Flush, Trim, Discard, formatting,
repair, mount/unmount, or formatter KnownZero authority. It does not claim a
consistent snapshot of a concurrently changing mounted filesystem.

## Metadata coverage and diagnostics

The engine supports 512-byte and 4096-byte logical sectors. It checks primary
boot structure and signature, BPB sector-size consistency, cluster size,
reserved-sector count, FAT count/size and FAT32 field consistency, media byte,
total-sector bounds, FAT mirroring/active-FAT fields, root-cluster validity,
FSInfo and backup-boot locations, FAT capacity, cluster-count geometry, and
checked data/root mapping.

Backup boot metadata is read inside the volume and compared against the
relevant primary BPB/extended fields and boot signature; executable boot-code
and OEM padding bytes are not required to match. FSInfo signatures and structure
are checked. Only the primary FSInfo sector is checked in this continuation; a backup FSInfo copy is not read or compared. Unknown free-count/next-free values are accepted; stale or impossible hints are advisory rather than asserted corruption. FAT reserved
entries are checked. FAT copies are compared in fixed 32 KiB chunks, with no
whole-table comparison in one step. Divergent copies under enabled mirroring
are structural findings; when mirroring is disabled, divergence is advisory
because the BPB selects an active FAT. Cluster-chain, directory-entry, and
allocation walks are not performed.

Results distinguish pass, inconsistency, stopped, read error, invalid geometry,
target invalidation, and incomplete. Findings retain category, structure,
severity, LBA/range, and expected/observed values where meaningful. The fixed
32-finding result limit sets `additionalFindingsMayExist` on overflow. A failed
read retains its requested LBA/count and block status/diagnostic. The checker
reports filesystem metadata findings and does not label the physical device
defective.

## Deterministic host validation

The new focused group runs against the production checker with formatter-made
sparse in-memory FAT32 fixtures. It qualifies 512-byte and 4Kn valid volumes,
including primary/backup boot metadata and FSInfo, and covers invalid boot
signature/BPB/cluster geometry, oversized FAT, invalid root cluster, truncated
volume, backup mismatch, bad FSInfo signatures, stale and unknown hints, FAT
reserved entries, mirrored FAT mismatch, non-mirrored advisory mismatch,
request-range overflow, and bounded finding overflow. Lifecycle cases cover
Stop before first read and after progress, read failure, removal, conflicting
operation, mounted-target rejection, immutable request binding,
same-capacity replacement/invalidation, and exactly-once release. The fixture
asserts unchanged bytes and zero writes/Flushes for checker operations.

The focused DM36 group passed **80 checks, 0 failures**. The full storage suite
passed **1,272 checks, 0 failures**. DM35 surface engine passed 48/48; DM34
partition wipe 17/17; DM34 whole-disk erase 34/34; DM34 GUI policy 31/31; DM34
whole-erase GUI/operation 39/39; DM35 surface GUI 34/34; widget activation,
USB trace classifier (7 tests), GPT verifier, and DM30 fixture verifier all
passed. No QEMU or physical media was used.

## Builds and source state

The AMD64 kernel build used `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0` and succeeded. Root `build.bat` passed. UEFI x64 Release passed: the MSBuild x64 Release bootloader, AMD64 kernel, and ESP setup completed.

The additional `build.ps1 -Arch amd64` run stopped before the server build stages at the unrelated PacMan Native ELF link failure: `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)` are undefined. No PacMan source or staged package was changed. Three tracked PacMan intermediate object files rewritten by the failed attempt were restored to their original indexed contents.

The UEFI script regenerated tracked `ESP/build-identity.txt`; its original bytes were restored, and the SHA-256 matches its pre-build value. `git diff --check` passed.
## Deferred DM36 work

The next continuation should add bounded FAT32 cluster-chain and directory
traversal: free/allocated consistency, loops, cross-links, orphaned allocation,
file-size versus chain capacity, directory cluster references, and long-file-name
entry integrity. Traversal must bound both memory and work on malformed/cyclic
structures and reuse this request, geometry, checked read, finding, stop, and
ownership contract. The Disk Manager `Check FAT32 Filesystem` GUI and guest/QEMU
acceptance remain deferred. Until those checks exist, passing means only that
the covered metadata checks found no inconsistency; it must not be presented as
“Filesystem Healthy.”

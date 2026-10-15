# Disk Manager DM36 — Read-Only FAT32 Integrity

**Current outcome: Outcome B — `BLOCKED_BY_FOREIGN_QEMU`**
**Phase marker:** `last_completed=DM35`, `next_expected=DM36`
**Current scope:** bounded, host-tested FAT32 metadata, cluster-chain, directory, and allocation checks are integrated into the production Disk Manager GUI. Guest/QEMU acceptance remains incomplete because a sibling project owns an active QEMU guest; this does not claim full filesystem consistency or complete DM36.

## Continuation 1 — Metadata integrity foundation

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

## Metadata coverage and diagnostics — continuation 1

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
because the BPB selects an active FAT. At this continuation's outcome,
cluster-chain, directory-entry, and allocation walks were not performed;
continuation 2 below adds those checks.

Results distinguish pass, inconsistency, stopped, read error, invalid geometry,
target invalidation, and incomplete. Findings retain category, structure,
severity, LBA/range, and expected/observed values where meaningful. The fixed
32-finding result limit sets `additionalFindingsMayExist` on overflow. A failed
read retains its requested LBA/count and block status/diagnostic. The checker
reports filesystem metadata and structural findings and does not label the physical device
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
## Continuation 1 status and deferred gates

Continuation 2 adds bounded FAT32 cluster-chain and directory
traversal: free/allocated consistency, loops, cross-links, orphaned allocation,
file-size versus chain capacity, directory cluster references, and long-file-name
entry integrity. Traversal must bound both memory and work on malformed/cyclic
structures and reuse this request, geometry, checked read, finding, stop, and
ownership contract. The Disk Manager `Check FAT32 Filesystem` GUI and guest/QEMU
acceptance remained deferred after continuation 1. The covered metadata and
structural checks must not be presented as “Filesystem Healthy” until the GUI
and guest acceptance gates are met.

## Continuation 2 — FAT32 cluster-chain, directory, and allocation checks

The continuation began on branch `DISK_MANAGER_IMPROVEMENTS` at HEAD
`bd91e60af9cf8c660726de3c8c41fdbc220652e5`, with a clean worktree and
`.phase` still at `last_completed=DM35`, `next_expected=DM36`. The implementation
extends the existing read-only FAT32 integrity job; it does not add another
checker, scheduler, or UI path. It preserves exact target binding, the shared
storage-operation lease, registration pin, checked partition-bounded reads,
stop-at-step-boundary behavior, bounded findings, and exactly-once ownership
release.

After metadata and active-FAT selection, the cooperative state machine walks
the root and queued subdirectories and checks directory file chains. A fixed
owner map detects cycles, cross-links, and allocated clusters shared across
files or directories. It checks invalid/out-of-range references, free, reserved,
and bad-cluster links, directory self/parent references, file-size versus
allocated chain capacity, and groups otherwise-unreferenced allocated clusters
as orphan allocation only after directory coverage completes. It validates
VFAT sequence, checksum, type, and cluster fields without decoding or
normalizing long-name text. Deleted directory entries and `.`/`..` are handled
separately from ordinary file references. When mirroring is disabled, traversal
uses the BPB-selected active FAT; inactive-copy divergence remains advisory.

All memory and work are capped: 131,072 tracked clusters, 256 queued
directories, depth 64, 4,096 directories, 65,536 directory entries, 4,096 file
chains, 262,144 structural reads, 8 MiB per FAT comparison, 263,000 total
read requests, and 600 MiB total requested read bytes. Exceeding a structural
coverage bound returns an incomplete/resource-limit result instead of implying
that unvisited allocation is clean. Each cooperative step requests at most one
checked read, bounded by the same captured partition extent. The job never
issues a write, Flush, Trim, Discard, format, repair, mount/unmount, or
KnownZero operation.

Deterministic `FakeDisk` fixtures cover valid empty, single- and multi-cluster
files; nested directories and root-parent references; single- and multi-entry
LFNs; deleted entries; self and general chain loops; out-of-range and bad-cluster
links; shared and merged-tail cross-links; file/directory cross-links; short
and overallocated file chains; grouped orphans; reserved markers; directory
loops and invalid references; active FAT selection; queue exhaustion; read
failure; Stop; and 512-byte and 4Kn volumes. Fixtures assert one read request
per step, partition-contained reads, unchanged media, and zero writes/Flushes.
The focused DM36 suite passed **202/202** checks; the full storage suite passed
**1,394/1,394**. Existing DM35/DM34 suites, widget activation, USB classifier,
GPT verifier, and DM30 verifier also passed.

The AMD64 kernel build passed with `NVME_WRITE_PROVEN=0` and
`NVME_FLUSH_PROVEN=0`; `build.bat` produced `guideXOSServer.exe`; and UEFI x64
Release built the bootloader, AMD64 kernel, and ESP. The requested top-level
`build.ps1 -Arch amd64` attempt stopped at the known unrelated PacMan Native
ELF link errors for `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)`. PacMan source was not changed, and
the three tracked PacMan object files rewritten by the attempt were restored.
The UEFI staging run rewrote tracked `ESP/build-identity.txt`; its original
bytes were restored. No QEMU or physical storage was used. GUI integration and
guest acceptance remain outstanding, and `.phase` was not advanced.

## Continuation 3 — production Disk Manager FAT32 integrity GUI

This continuation began at `01b54dc65381c7f2380a73277377247a3492890e`
(`disk-manager: add DM36 structural checks`) on branch
`DISK_MANAGER_IMPROVEMENTS`. The worktree was clean, `.phase` remained
`last_completed=DM35` / `next_expected=DM36`, and no branch or worktree switch
was performed.

The Disk Manager now exposes **Check FAT32 Filesystem** for supported MBR
FAT32 partition types and GPT Microsoft Basic Data entries. Candidate
selection uses partition identity, not boot-sector validity, so a damaged
FAT32 boot sector can still be diagnosed. The confirmation view shows the
captured device registration, partition identity and bounds, sector size,
eligibility, and read-only scope. Start re-captures and compares device
capabilities, exact parsed partition table, selected entry, registration, and
mount/operation state before creating the production `fat32_integrity` job.
Mounted targets, unavailable reads, changed identity, invalid geometry, and
busy storage are rejected. The UI does not mount or unmount a volume.

The app advances at most one production checker step per update. Progress uses
the engine's current stage and actual metadata-read, byte, FAT-comparison,
chain, directory, entry, structural-read, and orphan-scan counters; it does not
invent a percentage. The result shows the engine's terminal state, read
failure location/status where available, finding category/severity/structure,
LBA and byte offset, expected/observed values, and stored-finding overflow.
Previous/Next navigates stored findings. Stop settles the in-flight checked
read and closes only after the engine releases its lease and registration pin.
Device removal or replacement remains associated with the captured target.
The app supports pointer and keyboard Start/Stop/finding navigation/Close.
The verified widget IDs are Check FAT32 **20**, Start Check **11**, Stop
Check **12**, Previous **21**, Next Finding **22**, and Close **12**; IDs are
checked against the creation order during app initialization.

The expanded DM36 metadata/structural group passes **259/259** checks,
including the previously accepted 202 checker cases and additional
production-result mapping coverage. The new production-linked GUI group passes
**31/31** checks for target
admission and identity, read-only devices, damaged metadata eligibility,
real engine findings, one-read-per-step progress, explicit Start, clean
terminal results, busy-operation exclusion, keyboard and pointer routing,
Stop/Close settlement, and disabled finding navigation. The complete
`scripts/run-storage-manager-tests.ps1` run passes **1,482 checks, 0
failures**, including the DM35/DM34 regressions, widget activation, 249 USB
checks, USB trace classifier, GPT verifier, and DM30 verifier. The generated
result log is `out/dm36-gui-storage-test.log`.

The isolated AMD64 kernel build passes with `NVME_WRITE_PROVEN=0` and
`NVME_FLUSH_PROVEN=0` (`out/dm36-gui-amd64/kernel-build.log`). The root
`build.bat` application build passed; its pre-existing ignored
`guideXOSServer.exe` was restored byte-for-byte (`out/dm36-gui-host/build-bat.log`).
The isolated MSBuild UEFI x64 Release bootloader target passed, alongside the
AMD64 kernel build (`out/dm36-uefi-x64-release.log`). The requested
`build.ps1 -Arch amd64` attempt stopped at the known unrelated PacMan Native
ELF undefined symbols `pacman_audio_load_resources(gx_app_context*)` and
`pacman_audio_submit(void*, PacManSoundId)` (`out/dm36-powershell-build.log`).
All seven tracked PacMan generated files were restored byte-for-byte; PacMan
sources were not changed. No QEMU guest or physical media was used.

### Deferred DM36 QEMU acceptance plan — not run in this continuation

The next DM36 guest campaign should use the previously qualified paced PS/2
input helper and manifest-verified disposable images. It should verify:

- **A — Clean FAT32:** a valid disposable partition completes through the real
  GUI with no structural findings.
- **B — Corrupt FAT32:** a purpose-built chain or directory inconsistency
  produces the corresponding actual engine finding in the GUI.
- **C — Stop Check:** normal widget dispatch requests Stop, the bounded read
  settles, the result is Stopped, and ownership is released.
- **D — Application Close:** Alt+F4 during a check stops the job and defers
  teardown until the active read and ownership release settle.
- **E — Removal/replacement:** the original target becomes terminal before a
  separately identified replacement is scanned; the old job does not resume.
- **F — Read failure:** supported QEMU blkdebug injection retains the exact
  failed request bounds and returned block status.
- **G — Media integrity:** each disposable image is byte-for-byte identical to
  its pristine preimage after QEMU exits and releases it.
- **H — Mounted target:** an actively mounted volume is rejected, with no
  automatic unmount or concurrent snapshot claim.

Do not repeat prior DM35 general Disk Manager launch qualification merely to
reprove app startup. No gate above was run here.

This is **Outcome B — `DM36_GUI_READY`** for host implementation and
validation. Guest/QEMU acceptance remains deferred, so DM36 is not closed and
`.phase` remains `last_completed=DM35`, `next_expected=DM36`. No repairs or
destructive writes were performed. `git diff --check` passed.

## Continuation 4 — exact-head guest preflight blocked by foreign QEMU

### Repository and stale-prompt gate

The continuation verified repository root
`D:\dev\guideXOSServer_DiskManagerImprovements`, branch
`DISK_MANAGER_IMPROVEMENTS`, starting HEAD
`6cd730dbe14e2103eb9d36a82f5aaad378244857` (`disk-manager: add DM36 FAT32
integrity GUI`), and `.phase` values `last_completed=DM35` and
`next_expected=DM36`. The tracked and untracked worktree was clean. The
authoritative report identified DM36 guest acceptance as deferred, and there
was no `out/dm36/fat32-integrity/` continuation artifact. DM36 was not already
accepted, so the stale-prompt gate passed. No Windows timestamps were used.

### QEMU ownership gate

The initial process census was empty. A repeat census before any guest launch
found PID **22092**, executable
`C:\Program Files\qemu\qemu-system-x86_64.exe`, with command line name
`guidexos-s12-20261014`. Its OVMF, ESP, serial log, and QEMU debug log all
reside under the separate project workspace
`D:\dev\guideXOSServer\out\validation\settings-s12-20261014\production\guest-acceptance\`.
The process owns a WHPX QEMU guest and QMP endpoint `127.0.0.1:58312`. Its
command line uses a GTK display and does not name a DM36 fixture. The owner is
the sibling `D:\dev\guideXOSServer` project, not this repository. The QMP
endpoint was not contacted; the process was not stopped, paused, or otherwise
modified. No DM36 QEMU guest was started.

The installed emulator reports QEMU **11.0.0** (`v11.0.0-12122-ga4bb4b10c9`);
the executable SHA-256 is
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. The
active foreign WHPX guest makes this continuation's required QEMU lane
unavailable under the foreign-owner stopping rule. The result is **Outcome B
— `BLOCKED_BY_FOREIGN_QEMU`**.

### Fixture reconnaissance and runtime provenance

No new fixture was created or attached. Read-only offline inspection identified
the existing DM30 source image `out/dm30-regression-dm9-ata/secondary-600-MiB.raw`
as a possible starting point for a later DM36 fixture. It is 629,145,600 bytes,
SHA-256 `C2E22CEA70DE015E6E551CBC31CAEA918135B0EEAC526AD411DEE7EE5392A30F2`.
Its GPT Basic Data partition is named `DM9 QEMU Proof`, spans LBA 2048 through
1228766 (1,226,719 sectors), and contains a 512-byte-sector FAT32 volume with
16 sectors per cluster, 32 reserved sectors, two 599-sector FATs, 76,593 data
clusters, root cluster 2, FSInfo sector 1, backup boot sector 6, and label
`DM9PROOF`. The inspected root tree contains `DM9/PROOF.BIN` (29 bytes). This
is fixture-source reconnaissance only; it is not a DM36 guest result or a
newly generated DM36 fixture. The source image was not changed.

The current source revision is the starting HEAD above. Source and candidate
artifact SHA-256 values are:

| Artifact | SHA-256 |
|---|---|
| `kernel/core/fat32_integrity.cpp` | `401E05E3AF38C74FF6264E248C172BD7CFE5F2E3E8632AED22A92A5494431331` |
| `kernel/core/include/kernel/fat32_integrity.h` | `85858039AE17548B4D737DC44F4FB6EDF39E5A276D684FA8B4BF99D4C4B1BE1C` |
| `kernel/core/kernel_apps.cpp` | `2A9E37B944B7725C41B5409637ADD5555D5411A8ABB1AB7552E34165462A8023` |
| `kernel/core/include/kernel/disk_manager_surface_verification_ui.h` | `C638F3DF497EF0BBF0E9CF31785EB14422857EE686A3D4B30FF337E6245014D0` |
| Existing isolated DM36 AMD64 `kernel.elf` | `96EC935C93BCB014D83419CE2C9A76B60521F89A8BBC148E28DC3A6A9136C870` |
| Current workspace `ESP/kernel.elf` file | `0045993829AECECBCDC2782D4C3C62B3B80FBC36E5C2A8082CD066F2550B7779` |
| Current workspace `ESP/EFI/BOOT/BOOTX64.EFI` file | `869D92E35AEF66C3AD8A4246C3A329BCD25A42C9FC6438A699BC80859125C50A` |
| Existing UEFI x64 Release bootloader output | `30CAF50C39B37E1298C9D246DF88B1C16798C85AE1F5A8B78BD2DCB3524238F7` |
| Current workspace `ESP/ramdisk.img` file | `5C9B3C5302E72F845448688A4C57DA91FD95F111946F5FCA4C30E452449555FD` |
| Paced PS/2 helper `scripts/dm34_qmp_gui_input.py` | `FD41BE33D4F5C9E0B363B41E6EA4206FA7427E3DA9736EA39691B58505641D02` |

The current ESP kernel and bootloader files do not match the isolated DM36 kernel
and UEFI Release bootloader outputs. A source-matched candidate ESP was not
assembled or booted because the QEMU ownership gate blocked guest launch. No
proof-only checker bypass or automatic check-start hook was added. The existing
build evidence retains `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`.

### Gate and regression status

Gates A–H were **not attempted**: no clean or corrupt DM36 guest scan, findings
navigation, Stop Check, application Close, removal/replacement, guest read
failure, mounted-target guest test, guest I/O counters, screenshots, or
before/after DM36 fixture hashes exist. No physical host disk or USB device was
attached. Existing host evidence remains source-matched and was not rerun:
DM36 checker **259/259**, DM36 GUI **31/31**, full storage suite
**1,482/1,482**, AMD64 kernel build PASS, `build.bat` PASS, and UEFI x64
Release PASS. The previously recorded `build.ps1 -Arch amd64` failure at the
unrelated PacMan Native ELF audio symbols remains unchanged; no PacMan files
were modified.

The smallest next continuation is to wait until the sibling project releases
its QEMU guest, repeat the process census without touching foreign processes,
assemble and hash a source-matched runtime ESP, prepare and independently
validate the DM36 fixture set under `out/dm36/fat32-integrity/`, then run the
guest gates A–H in the prescribed order. `.phase` remains
`last_completed=DM35`, `next_expected=DM36`; DM37 is not ready. The report is
not a filesystem-health certification and claims no repairs.

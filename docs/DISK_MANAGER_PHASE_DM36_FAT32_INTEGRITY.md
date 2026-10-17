# Disk Manager DM36 — Read-Only FAT32 Integrity

**Current outcome: Outcome B — `BLOCKED_BY_FOREIGN_QEMU`**
**Phase marker:** `last_completed=DM35`, `next_expected=DM36`
**Current scope:** bounded, host-tested FAT32 metadata, cluster-chain, directory, and allocation checks are integrated into the production Disk Manager GUI. The source-matched DM36 guest passed the clean FAT32 partition check, but the remaining mandatory guest gates are incomplete because a sibling project owns an active QEMU guest. This does not claim complete DM36 or general filesystem-health certification.

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

## Continuation 5 — source-matched guest and clean FAT32 pass; foreign owner blocks remaining gates

### Repository and runtime provenance

The repository was verified on `DISK_MANAGER_IMPROVEMENTS` at starting HEAD
`4a729fadea61ded7ef82c0a5d45cab8c282c0510` (`disk-manager: record DM36 guest
blocker`), with a clean worktree and `.phase` still at
`last_completed=DM35`, `next_expected=DM36`. No Git topology operation was
performed and no production source changed.

An initial QEMU census found the lane free. The isolated source-matched runtime
was assembled under the ignored
`out/dm36/fat32-integrity/runtime/esp/` directory. The AMD64 kernel and staged
ESP kernel both hash to
`96EC935C93BCB014D83419CE2C9A76B60521F89A8BBC148E28DC3A6A9136C870`; the UEFI
x64 Release bootloader and staged ESP bootloader both hash to
`30CAF50C39B37E1298C9D246DF88B1C16798C85AE1F5A8B78BD2DCB3524238F7`. The workspace ESP retains a non-matching kernel
(`0045993829AECECBCDC2782D4C3C62B3B80FBC36E5C2A8082CD066F2550B7779`) and
bootloader (`869D92E35AEF66C3AD8A4246C3A329BCD25A42C9FC6438A699BC80859125C50A`);
only the private staging directory uses the isolated build outputs. OVMF is
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`; QEMU is
11.0.0 (`v11.0.0-12122-ga4bb4b10c9`) with executable hash
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`. The
staged candidate retains normal production startup, has no automatic checker
launcher or automatic target selection, attaches no physical host storage,
and keeps `NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`.

### Disposable fixture evidence

The unchanged 600 MiB DM30 source image has SHA-256
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`, size
629,145,600 bytes, and a valid GPT/FAT32 layout. Its Microsoft Basic Data
partition starts at LBA 2,048, has 1,226,719 sectors, and ends at LBA
1,228,766 inclusive. FAT32 uses 512-byte sectors, 16 sectors per cluster,
32 reserved sectors, two 599-sector FATs, 76,593 data clusters, root cluster
2, FSInfo sector 1, backup boot sector 6, and label `DM9PROOF`. The source was
not modified.

Ten independent disposable images and their serials, device IDs, starting
hashes, and geometry are recorded in
`out/dm36/fat32-integrity/fixtures/fixture-manifest.json`. The clean,
pristine, Stop, Close, removal, replacement, read-error, and mounted-target
fixtures retain the source hash. The loop fixture
(`DM36LOOP01`, SHA-256
`25F764F83C60D3C0208092C1E2A0CBF945675F32132B4590795A8853F7B6B316`) changes
only cluster 4 in both FAT copies from EOC to a self-reference. The orphan
fixture (`DM36ORPHAN01`, SHA-256
`0E8944010169B8ACE8FE509492982AFDE8F5714DC9A5F70C092540F067AEF6A1`) changes
only cluster 76,594 from free to allocated in both FAT copies. Offline checks
confirmed the intended byte differences and preserved partition geometry.

### Gate A — clean partition passed

The clean guest used QEMU 11.0.0, WHPX, `pc,usb=off`, UHCI, standard headless
VGA, and the paced PS/2 helper with input routed to the named VGA device. The
production Start Menu opened Disk Manager. The selected USB disk and its sole
GPT FAT32 partition were visibly selected in screenshots. The production
confirmation dialog captured serial `DM36CLEAN01`, device 2, partition 1,
GPT identity, start LBA 2,048, count 1,226,719, and 512-byte logical sectors;
it identified the check as read-only and did not unmount the target.

The clean check reached `Verification Passed`. The result screen recorded 40
metadata reads, 925,184 bytes read, both FAT copies compared over 306,688
bytes, three chains/clusters, two directories, seven entries, 15 structural
reads, and all 76,593 orphan clusters scanned with zero orphan bytes. It
reported no FAT32 structural inconsistencies, no repairs, and released
storage ownership. Screenshots and QMP snapshots are retained in
`out/dm36/fat32-integrity/runs/A-clean-partition-routed/`.

The operation-scoped trace analysis in `A-read-coverage.json` isolates the
checker's contiguous SCSI tags 664–703: 40 READ(10) requests, 925,184 bytes,
all within the selected partition (LBA 2,048–3,294). This matches the result
screen. The whole pre-operation-to-result trace window contains 434 reads and
1,126,912 bytes. The other 394 requests account for 201,728 bytes and are not
attributed to the checker: 393 are outside the selected partition and one is
at LBA 2,048. QMP target block counters show zero writes, zero write bytes,
zero unmaps, and zero failed reads, writes, unmaps, or Flushes. The scan added
no Flush. One Flush was already present at the pre-check
baseline and is not attributed to the checker. After the result and ownership
release, QEMU exited, its endpoint closed, exclusive image access succeeded,
and the full fixture hash remained
`C2E22CEA70DE015E6E551CBC31CAE918135B0EEAC526AD411DEE7EE5392A30F2`. The
accepted run was owned by QEMU PID 5648 on QMP `127.0.0.1:61432`; the earlier
input calibration used PID 18904 on `127.0.0.1:61431`. Both were DM36-owned
processes, shut down after their guest work, and released the image.

One earlier input-routing calibration boot did not start a check; it was
closed with zero writes, unmaps, or failures and an unchanged clean-fixture
hash. It is retained separately as `A-clean-partition/`. The routed run above
is the accepted Gate A result.

### Ownership recheck and remaining gates

Before launching the loop fixture, the launch helper took a new process census
and refused to start because it found QEMU PID **20084**, parent PID **22432**,
from the sibling workspace `D:\dev\guideXOSServer`. At census time the
process was active, with executable
`C:\Program Files\qemu\qemu-system-x86_64.exe`; its command line showed a
WHPX guest on QMP `127.0.0.1:58413` using that sibling workspace's
`out/validation/settings-s13-20261014/production/guest-acceptance-vgaid/`
ESP. The owner command line identified a separate `-display gtk` guest with
no network. This census was recorded at `2026-10-15T02:18:02Z` in
`out/dm36/fat32-integrity/runs/B-loop-partition/prelaunch-qemu-ownership.json`.
The loop launch was refused before QEMU creation. The foreign guest was not
contacted, paused, modified, or stopped; its files and port were not reused.
Guest-related work ended at this ownership gate.

Gate A passed. Gate B (loop and independent corruption findings, including
finding navigation), C (dedicated Stop), D (Alt+F4 Close), E
(removal/replacement), F (blkdebug read failure), the remaining cross-run
requirements in G, and guest H (mounted-target rejection) remain unqualified.
The accepted host admission tests remain the evidence for mounted-target
policy; no guest-mounted target was fabricated. The pre-existing 259/259
checker, 31/31 GUI, 1,482/1,482 storage, AMD64, `build.bat`, and UEFI x64
Release results remain valid because production source did not change. The
unrelated PacMan Native ELF linker issue remains unchanged. This is **Outcome
B — `BLOCKED_BY_FOREIGN_QEMU`**; it is not DM36 acceptance. `.phase` remains
`last_completed=DM35`, `next_expected=DM36`; DM37 is not ready.

## Continuation 7 — B1 loop finding passed; between-guest census blocks B2 onward

### Repository, runtime, and ownership

The continuation began at HEAD
`da8e6ddedf931102eb070dd383460821e31bd139` on branch
`DISK_MANAGER_IMPROVEMENTS`; the tracked worktree was clean and `.phase`
remained `last_completed=DM35`, `next_expected=DM36`. No production source
changed. The saved source-matched runtime manifest was checked against the
built and staged artifacts: the private ESP kernel hashes to
`96EC935C93BCB014D83419CE2C9A76B60521F89A8BBC148E28DC3A6A9136C870`, and the
UEFI x64 Release bootloader hashes to
`30CAF50C39B37E1298C9D246DF88B1C16798C85AE1F5A8B78BD2DCB3524238F7`.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; no physical host storage or
automatic checker launcher was used.

The fresh prelaunch census found no QEMU guest. Exactly one DM36-owned QEMU
11.0.0 process (PID 5680, QMP `127.0.0.1:61433`) was launched for
`DM36LOOP01`, using the private ESP and the paced PS/2 helper. After the check
settled and the owned guest exited, the required between-guest census found a
foreign WHPX guest, PID 26464, from sibling workspace
`D:\dev\guideXOSServer`. Its command line uses that project's
`out/validation/settings-s15-20261016/production/guest-image-wallpaper/ESP`
and QMP `127.0.0.1:58441`. Its endpoint was not contacted; the guest was not
controlled, paused, or stopped. No second DM36 guest was started. B2 and the
remaining guest gates therefore stop at this ownership boundary.

### Gate B1 — production GUI detected the cluster-chain loop

Before attachment, the actual `DM36LOOP01` image was rechecked against the
fixture manifest and pristine baseline. It is 629,145,600 bytes, SHA-256
`25F764F83C60D3C0208092C1E2A0CBF945675F32132B4590795A8853F7B6B316`, with
GPT partition LBA 2048–1228766 (1,226,719 sectors) and a 512-byte-sector FAT32
volume. The only eight changed bytes are the two documented four-byte FAT
entries: cluster 4 changes from EOC `0x0FFFFFF8` to self-reference
`0x00000004` at FAT1 LBA 2080 / byte offset 1,064,976 and FAT2 LBA 2679 / byte
offset 1,371,664. The intended finding is a file cluster-chain loop at
cluster 4.

Disk Manager was launched through normal production UI interaction. The
captured-target confirmation identified serial `DM36LOOP01`, device 2,
partition 1, the selected GPT partition bounds, and read-only checking with no
repair or filesystem writes. Start Check was explicitly activated. The GUI
showed the checker running through Finalization and then settled at
**Structural inconsistency detected**. It reported 40 metadata reads,
925,184 bytes read, 306,688 bytes compared across both FATs, three
chains/clusters, two directories, seven entries, 15 structural reads, and all
76,593 orphan clusters scanned. Exactly one finding was stored. The finding
view showed **Cluster-chain loop**, **Structural inconsistency**, structure
**FAT**, location LBA **3310**, byte offset **0**, expected **3**, observed
**4**. LBA 3310 is the data-cluster location for cluster 4. The result stated
that no repair was performed and storage ownership was released. The single
finding means Previous/Next could not navigate to another finding; no
multi-finding navigation case was present. Captures, QMP snapshots, serial
output, and the trace are in
`out/dm36/fat32-integrity/runs/B-loop-retry-20261016/`.

### Gate B1 — I/O attribution and media integrity

The operation trace window is byte offsets 92,858–150,341. READ(10) tags
664–703 are the 40 checker requests, totaling 925,184 bytes; every request is
within selected partition LBAs 2048–3294. This matches the GUI counter. The
whole window contains 434 reads / 1,126,912 bytes because it also includes
startup and registration traffic: 394 other requests total 201,728 bytes,
with 393 outside the selected partition and one at LBA 2048. QMP target
counters moved from 568 reads / 578,048 bytes to 1,002 reads / 1,704,960
bytes. Writes, write bytes, unmaps, failed I/O, and flushes added during the
window were all zero; one Flush was already present at the pre-check baseline.
After QEMU exited, the full fixture SHA-256 still matched its initial value.
The machine-readable attribution is
`out/dm36/fat32-integrity/runs/B-loop-retry-20261016/B-read-coverage.json`.

### Gate B2 and remaining acceptance

The separate orphan fixture remains unchanged at initial SHA-256
`0E8944010169B8ACE8FE509492982AFDE8F5714DC9A5F70C092540F067AEF6A1`. Its
only documented mutations allocate cluster 76594 in both FAT copies; the
intended finding is an unreferenced allocated cluster at 76594. It was
verified offline but not attached or checked in the GUI because the
between-guest census found the sibling guest.

Gate B1 passed. B2, findings navigation across multiple findings, C (dedicated
Stop), D (Alt+F4 Close during active work), E (removal/replacement), F
(blkdebug read failure), remaining per-run media/I/O checks in G, and guest H
(mounted-target rejection) remain open. The accepted clean Gate A result and
host suites/builds remain valid; production source did not change, so they
were not rerun. This remains **Outcome B — evidence incomplete / guest lane
blocked by foreign QEMU**. `.phase` remains DM35→DM36; DM37 is not ready.

## Visible retry — B1 and B2 run through the production GUI

The retry began at HEAD `da8e6ddedf931102eb070dd383460821e31bd139` on
`DISK_MANAGER_IMPROVEMENTS`; `.phase` remained DM35→DM36. The earlier B1
capture showed the `Cluster-chain loop` category and LBA but omitted the
observed number. The UI had formatted the expected and observed values into a
clipped line. The renderer now draws the labels and numeric values as separate
text segments. Orphan findings use the checker's `orphanClusters` result for
the observed value; the focused host regression verifies expected 0, observed
2, and the renderer helper returns 2.

The source-matched private runtime was rebuilt and checked against its
manifest: kernel SHA-256
`EAFA950DBA3D38265976D0BFD73ED553D1A665D5BCE421DC847CC53C2795F075`, and UEFI
x64 Release bootloader SHA-256
`51C7BD0E16DC54FF843EFFA615738E2F4208F8D8729111D8C4C99BA4732889A9`.
`NVME_WRITE_PROVEN=0` and `NVME_FLUSH_PROVEN=0`; the private ESP was used, no
physical host storage was attached, and the automatic check launcher was not
enabled. The host test suite, freestanding AMD64 kernel build, `build.bat`,
UEFI x64 Release build, and final `git diff --check` passed.

Fresh ownership checks found the QEMU lane available. B1 and B2 each ran as a
single visible GTK QEMU guest, one at a time, against only their disposable
fixture. The fixture hashes were recorded before attachment and rechecked
after each guest exited.

### B1 retry — loop detection after the renderer change

The production Disk Manager checked `DM36LOOP01` on GPT partition LBA
2048–1228766. The result showed **Structural inconsistency detected**, one
stored finding, 40 metadata reads / 925,184 bytes, 306,688 FAT bytes compared,
three chain clusters, two directories, seven entries, 15 structural reads,
and all 76,593 clusters scanned. It stated that no repair was performed and
storage ownership was released. The earlier pre-fix result capture identifies
the intended finding as a cluster-chain loop at LBA 3310, expected 3; the
renderer regression now supplies the observed number from the finding. The
post-fix compact-window screenshot does not include the lower finding-detail
rows, so the numeric framebuffer output still needs a maximized-result
capture.

Checker READ(10) tags 795–834 are 40 contiguous requests / 925,184 bytes, all
within partition LBAs 2048–3294. The scan caused no writes, unmaps, failed I/O,
or additional flushes. After QEMU exited, the image matched its original
SHA-256 `25F764F83C60D3C0208092C1E2A0CBF945675F32132B4590795A8853F7B6B316`.
The visible run evidence is in
`out/dm36/fat32-integrity/runs/B-loop-observed-render-fix-visible-20261016/`.

### B2 retry — orphaned allocation detection

The production Disk Manager checked the independent `DM36ORPHAN01` fixture
using the same partition geometry. The result showed **Structural
inconsistency detected**, one stored finding, 40 metadata reads / 925,184
bytes, 306,688 FAT bytes compared, three chain clusters, two directories,
seven entries, 15 structural reads, and all 76,593 clusters scanned. It
stated that no repair was performed and storage ownership was released. The
fixture mutation allocates cluster 76594 in both FAT copies, so the intended
finding is an unreferenced allocated cluster at LBA 1228750, expected 0,
observed 1. The compact-window result screenshot shows the completed scan and
finding count but not the lower category/location/value rows; those visible
values remain to be captured after expanding the Disk Manager window.

Checker READ(10) tags 795–834 are 40 contiguous requests / 925,184 bytes, all
within partition LBAs 2048–3294. In the baseline-to-result QEMU window there
were 434 READ(10) requests / 1,126,912 bytes, including startup and background
registration traffic; 394 requests were outside the checker tag range. QMP
target counters increased from 699 reads / 645,120 bytes to 1,133 reads /
1,772,032 bytes. Writes, write bytes, unmaps, failed I/O, and additional
flushes were zero. After QEMU exited, the image matched its original SHA-256
`0E8944010169B8ACE8FE509492982AFDE8F5714DC9A5F70C092540F067AEF6A1`. The
visible run evidence is in
`out/dm36/fat32-integrity/runs/B-orphan-observed-render-fix-visible-20261016/`.

After both evidence guests exited, a separate visible GTK QEMU process was
started and brought to the foreground at PID 14792, QMP `127.0.0.1:61438`.
It is still running at the desktop in
`out/dm36/fat32-integrity/runs/user-visible-open-20261016/` so the guest is
visible to the user.

The numeric-value renderer defect has a narrow source fix and focused host
regression; B1 and B2 checker execution passed, but the detailed numeric rows
were not recaptured in the maximized GUI, and multi-finding navigation is not
covered by these single-finding fixtures. Dedicated Stop (C), application
Close during active work (D), removal/replacement (E), blkdebug read failure
(F), the remaining cross-run media/I/O checks (G), and mounted-target guest
rejection (H) also remain open. This is **Outcome C — production defect
repaired; DM36 acceptance evidence remains incomplete**. `.phase` remains
`last_completed=DM35`, `next_expected=DM36`; DM37 is not ready.

## Continuation 8 — numeric findings qualified; foreign QEMU blocks lifecycle gates

### Repository and QEMU ownership

This continuation began at full HEAD
`f7f42c2752dd90610b76b062787242a60e4bd144` on
`DISK_MANAGER_IMPROVEMENTS`. `.phase` remained
`last_completed=DM35`, `next_expected=DM36`. No authoritative DM36 Outcome A
was present. The tracked worktree was clean at entry; the existing unrelated
untracked `scripts/__pycache__/dm34_qmp_gui_input.cpython-312.pyc` was left
untouched and is not part of the result.

The initial fresh QEMU census found no active guest; historical PID 14792 had
already exited, so there was no retained DM36 guest to reuse. The B1 and B2
qualification guests were launched and then safely exited after their checks.
Before the dedicated Stop guest could launch, the helper's fresh ownership
gate found QEMU PID **5036**, parent PID **10864**, executable
`C:\Program Files\qemu\qemu-system-x86_64.exe`. Its command line identifies a
WHPX/TCG guest from sibling workspace
`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`, its image under
`out/dotnet/c174-shared-managed-resize/settings-ui-proof-runs/`, and QMP
`127.0.0.1:49825`. The launch was refused before contacting QEMU. The foreign
process, QMP endpoint, and its files were not contacted or modified, and no
additional guest was launched. This leaves the requested lifecycle lane
blocked under the foreign-owner rule.

### Gate B — numeric finding presentation

The first expanded B2 capture exposed a production rendering defect. The
`DM36ORPHAN01` engine finding correctly represented one grouped orphan finding
for allocated cluster **76594** at LBA **1228750**, but the UI's Observed
field displayed **1** (the orphan finding/count) instead of the first orphan
cluster ID. The initial failed capture is retained at
`out/dm36/fat32-integrity/runs/C8-B2-rendered-detail-20261016/C8-B2-maximized.png`.

The narrow correction in
`kernel/core/include/kernel/disk_manager_fat32_integrity_ui.h` makes the
orphan-finding display helper return `result.firstOrphanCluster`. The engine's
finding count and integrity validation were not changed. The focused
production-result regression in `tests/storage_manager_test.cpp` now uses two
orphan clusters beginning at cluster 5 and expects displayed value **5**;
the engine still reports two orphan clusters. The full suite remained at
1,482 checks because this replaces the earlier count assertion.

For B1, the expanded visible `DM36LOOP01` run displayed **Cluster-chain loop**,
**Structural inconsistency**, structure **FAT**, location LBA **3310** / byte
offset **0**, expected **3**, observed **4**. The location identifies the
cluster-4 data reference involved in the loop; no filename or fabricated
metadata was shown. The screenshot is
`out/dm36/fat32-integrity/runs/C8-B1-renderer-detail-2-20261016/C8-B1-maximize-click-2.png`.
This display-only B1 guest used kernel
`EAFA950DBA3D38265976D0BFD73ED553D1A665D5BCE421DC847CC53C2795F075`, before
the orphan-specific correction; the loop value path is unchanged by that
correction. Its run manifest records source HEAD f7f42c27, bootloader
`51C7BD0E16DC54FF843EFFA615738E2F4208F8D8729111D8C4C99BA4732889A9`, OVMF
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`, and
QEMU 11.0.0 SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`.

B2 was rerun with a fresh kernel built from the corrected working tree. The
maximized GUI now displays **Orphaned allocation**, **Structural
inconsistency**, structure **FAT**, location LBA **1228750** / byte offset
**0**, expected **0**, observed **76594**. The captured output is
`out/dm36/fat32-integrity/runs/C8-B2-rendered-detail-fixed-20261016/C8-B2-fixed-maximized.png`.
The actual finding corresponds to the fixture's allocated, unreferenced
cluster 76594 in both FAT copies. The run's `owner-manifest.json` binds source
HEAD f7f42c27, kernel
`B7DFB46C2C15F9D1B0BA39B4025C47940AA07E8F23EE48837F7779A00A67B079`, bootloader
`51C7BD0E16DC54FF843EFFA615738E2F4208F8D8729111D8C4C99BA4732889A9`, OVMF
`2DF617A6FD1BAE41EC7C0E3BCECA96C928A5A88D8DC198468D34733393AB2792`, QEMU
11.0.0 SHA-256
`A930E028F93D0FA47E4D58BDAD2432F7466DC2B6AF0AE376F77EF7A298FFDD02`, and
QMP `127.0.0.1:61442` (guest PID 19792). Kernel and bootloader staged in the
private runtime ESP matched those hashes. Both runs used read-only disposable
images, `NVME_WRITE_PROVEN=0`, `NVME_FLUSH_PROVEN=0`, and no physical host
storage.

Previous and Next were activated in the B1 GUI; the view remained at Finding
1 of 1. This verifies activation and stable single-finding behavior, not
movement between different findings. Existing production-linked host tests
cover the no-finding and multiple-finding navigation logic. No two-finding
guest fixture was run, so cross-finding guest navigation remains unqualified.

### Guest I/O and image integrity

Both qualified fixtures are 629,145,600-byte disposable images. The loop
fixture `DM36LOOP01` had initial and final SHA-256
`25F764F83C60D3C0208092C1E2A0CBF945675F32132B4590795A8853F7B6B316`; the
orphan fixture `DM36ORPHAN01` had initial and final SHA-256
`0E8944010169B8ACE8FE509492982AFDE8F5714DC9A5F70C092540F067AEF6A1`.
Each production UI reported 40 checker metadata reads / 925,184 bytes.

For B1, QMP target counters moved from 438 reads / 511,488 bytes to 741 /
1,571,328; the 303-read / 1,059,840-byte delta includes the 40 checker reads
and 263 other startup/registration reads (134,656 bytes). QMP writes and
write bytes, unmaps, and failed reads were zero; its single Flush predated the
check and did not increase. For corrected B2, QMP moved from 306 reads /
443,904 bytes to 609 / 1,503,744, again a delta of 303 reads / 1,059,840
bytes. The trace analysis in
`out/dm36/fat32-integrity/runs/C8-B2-rendered-detail-fixed-20261016/C8-B2-fixed-trace-analysis.json`
attributes 40 READ(10) requests / 925,184 bytes to checker tags 402–441,
within partition LBAs 2048–3294; the other 263 requests account for 134,656
bytes. The trace contains no Sync Cache command. The single QMP Flush was
already present at baseline and did not increase. Writes, write bytes,
unmaps, and failed reads were zero. QEMU exited safely for both runs and both
whole-image hashes remained unchanged. These per-run results support G for
B1/B2 only; they do not establish the remaining lifecycle gates.

### Host/build qualification and remaining gates

The updated `scripts/run-storage-manager-tests.ps1` run passed: storage
manager **1,482/1,482**, DM36 metadata/integrity **259/259**, DM36 GUI
**31/31**, DM35 surface **48/48**, DM35 GUI **34/34**, DM34 wipe **17/17**,
erase **34/34**, wipe UI **31/31**, erase UI **39/39**, USB Mass Storage
**249 checks included**, widget activation, USB trace classifier **7/7**, GPT
verifier **1/1**, and DM30 fixture verifier **1/1**. Log:
`out/dm36-continuation8/storage-suite.log`. The compositor pointer-buttons
script also passed (`out/dm36-continuation8/compositor-pointer-buttons.log`).

The source-matched freestanding AMD64 kernel build passed with both NVMe
write/Flush proof flags zero; its log is
`out/dm36-continuation8/kernel-build-after-orphan-display-fix.log` and kernel
SHA-256 is the B7DF hash above. `build.bat` passed
(`out/dm36-continuation8/build-bat.log`). UEFI x64 Release bootloader build
passed with the already-built matching kernel intentionally skipped by the
script; the bootloader SHA-256 is the 51C7 hash above
(`out/dm36-continuation8/uefi-x64-release-build.log`). The unrelated
`build.ps1 -Arch amd64` PacMan Native ELF undefined symbols remain a known
separate issue and PacMan sources were not modified.

The B2 corrected result, builds, and host tests qualify the numeric-rendering
fix. Dedicated Stop (C), active-check Alt+F4 Close (D), USB removal and
same-capacity replacement (E), an actual checker read error (F), and the
remaining per-lifecycle portions of media-integrity gate G were not run
because the fresh launch was blocked at foreign PID 5036. Gate H has the
existing production-linked mounted-target host admission coverage, but no
guest-mounted volume was tested; the guest test is unperformed. The foreign
guest was not disturbed, and no attempt was made to launch another guest.

This is **Outcome B — `BLOCKED_BY_FOREIGN_QEMU`**: numeric B1/B2 finding
details are qualified, the narrow B2 renderer defect is fixed and
requalified, and B1/B2 images remained unchanged, but mandatory lifecycle
acceptance remains incomplete. `.phase` remains
`last_completed=DM35`, `next_expected=DM36`. No DM37 work is ready or included.

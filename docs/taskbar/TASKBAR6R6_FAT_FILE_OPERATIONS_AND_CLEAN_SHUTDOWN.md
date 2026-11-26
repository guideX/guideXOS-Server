# TASKBAR6R6 — FAT File Operations and Clean Shutdown

**Outcome: `TASKBAR6R6_NATIVE_PARTIAL`**

TASKBAR6R6 traced and removed the native auxiliary-settings invalid-name writes, resolved the R5 file-open misdiagnosis, and completed four QAC-managed guest lifetimes with guest-initiated shutdown and verified storage sync. Taskbar settings and the existing Calculator pin survived clean restarts. The full native create/update/rename/delete sequence was not completed, so this report does not claim complete native file-operation qualification.

## 1. Initial repository state

- Repository: `D:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`
- Branch: `TASKBAR_IMPROVEMENTS`
- Starting HEAD: `1331f90288d972cd9b15653ef9b8be3d36b3caf8`
- Starting worktree: clean; no user modifications were present.
- QAC CLI and coordinator were available. Before Boot A, the coordinator was online with no task-owned reservation, guest, or queued request.
- QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. QAC launched every guest as its direct child.

## 2. TASKBAR6R5 evidence baseline

R5 recorded 36 successful native `ata0m` cache flushes and confirmed that Boot B loaded `/desktop.cfg`. Its read-only raw-image inspection found the exact test-file bytes and matching FAT copies. The clean-fixture FAT suite passed 64 checks; the ATA state machine passed 80; hosted, production AMD64, and diagnostic AMD64 builds passed.

R5 did not demonstrate guest file reopen: it called `desktop.launch r5test.txt`, which dispatches applications and is not a VFS file-open operation. R5 also did not deliver a shutdown request to the guest; no shutdown marker appeared before host QMP quit. Those are independent findings. R5's raw image remains untouched with SHA-256 `8B278A581D7D52861FF2EE5A834AB16AAF93DDE606647613ABC8B19267797AA0`.

## 3. `FAT_FILE_WRITE_INVALID_NAME` analysis

R5 logs identify `bare_metal_save_display_options` as the source of four redundant auxiliary writes, with Start Menu recent-program persistence as a fifth failing writer:

| Caller | Absolute write path | FAT component rejected |
|---|---|---|
| Legacy display-options save | `/display-options.cfg` | `display-options.cfg` |
| Taskbar position sidecar | `/desktop.taskbar.position` | `desktop.taskbar.position` |
| Desktop icon sidecar | `/desktop.system.icons` | `desktop.system.icons` |
| Background-scale sidecar | `/desktop.background.scale` | `desktop.background.scale` |
| Start Menu recent list | `/.startmenu_recent` | `.startmenu_recent` |

The first four paths were written by the desktop settings component after the primary desktop configuration save. The recent-list path came from a separate desktop component. The paths were absolute, normalized root-level paths with no separator or traversal issue. R5's diagnostic records show parent cluster `0x00000002` (the mounted FAT32 root), so parent lookup succeeded. FAT rejected the final component at short-name generation. The first four names contain overlong/multiple-dot components; `.startmenu_recent` has a leading dot and exceeds the supported short-name shape. None was an LFN creation attempt routed through the LFN writer: this create/rename path did not generate arbitrary LFNs. VFS path parsing and FAT validation differed in scope, not in the final error status: VFS passed the component to FAT, and FAT returned `FAT_FILE_WRITE_INVALID_NAME`.

The failure was not caused by a missing parent, mount selection, temporary filename, rename, or replacement recovery. The canonical `/desktop.cfg` update already used valid 8.3 names (`DESKTOP.TMP`, `DESKTOP.BAK`, and `DESKTOP.CFG`) and completed its transactional replacement. The R5 error came from additional direct writes to redundant sidecars. Read-only migration reads of old sidecars remain supported.

## 4. Filename rules and targeted repair

`kernel/core/fs_fat.cpp` now validates the supported ASCII FAT 8.3 character set and shape in the production `make_short_name` path. It rejects multiple dots, empty/overlong base or extension components, leading/trailing dots, separators, and unsupported characters. The actual FAT create path records the complete supplied path, final component, and `short-name-generation` stage before returning the original invalid-name status. `vfs::last_open_status()` continues to expose `VFS_ERR_INVALID` to the caller.

New file and directory entries are intentionally limited to this ASCII 8.3 subset. The current create/rename path does not generate arbitrary VFAT long-name entries. Lookup and rename association for existing checksum-valid LFNs remain in place, including stale-checksum protections. No arbitrary filename rewriting was added.

The settings repair makes `/desktop.cfg` the single writable record for the preferences previously duplicated into sidecars. Its existing versioned format already contains taskbar position, size, presentation, auto-hide, previews, activity, and desktop icon settings. Start Menu recent-program writes now use `/STARTMNU.REC`; reads still fall back to the former legacy path. Native R6 writes use these supported names and no longer emit the R5 sidecar invalid-name failures.

The same limited-creation rule exposed one more invalid caller: startup tried to create `/Documents` even though that final component is not an 8.3 name. New user folders now use `/DOCS`; the Start Menu still selects an existing `/Documents` directory when a checksum-valid imported LFN is present. Boot D on the rebuilt diagnostic kernel logged `/DOCS` with `FAT_CREATE_OK` and `VFS_OK`.

Focused production FAT/VFS regressions cover all five exact legacy sidecar components, invalid-name propagation, normalized separators and `.` components, short-name boundaries, reserved characters, missing parents, temporary replacement, repeated updates, rename/readback/delete, and imported LFN association. The suite also retains the stale-LFN checksum tests from earlier milestones.

## 5. Guest file reopening investigation

R6 created `/r6test.txt` on the writable FAT32 root volume mounted at `/`, attached to the PIIX3 IDE primary master (`ata0m`). Boot B used the same raw image and volume. File Explorer enumerated the root entry as `R6TEST.TXT` (27 bytes, Text document); pressing Enter opened it in the associated Notepad application. The guest reported `Opened text file in Notepad` and displayed the text. Independently, Terminal's production `vfscat /r6test.txt` opened the path through VFS/FAT and read 27 bytes.

This is result **D** from the R5 alternatives: the R5 test used application dispatch for an arbitrary filename, not the File Explorer/VFS open path. In R6, path lookup, open, read, File Explorer visibility, and associated text presentation all succeeded. Boot C repeated the production VFS read after another clean shutdown and cold start.

## 6. Guest readback and raw-image comparison

The exact guest-visible contents were:

```text
r6 native at peristencev1sf
```

Length: **27 bytes**. SHA-256: `98d6756d676fcda515ca3682931bfad012eb9d82dbc4553f9a2cd21888c575db`.

Boot A saved the file through the guest desktop/Notepad path. Boot B and Boot C both read it through `vfscat`; Boot B also opened it from File Explorer in Notepad. The final read-only host parser reported the same length, bytes, and SHA-256. This raw-image check is corroboration, not a substitute for the guest reads.

## 7. Shutdown failure analysis and repair

R5's first failing stage was before guest shutdown: its attempted UI/request path did not produce a kernel shutdown marker. Inspection of the production power-off routine also found two correctness gaps: it wrote ACPI `0x2000` (S5 type without `SLP_EN`) and issued the debug-exit path before any filesystem synchronization.

`perform_shutdown(caller)` now logs request receipt, synchronizes storage, and stops on sync failure before asking the platform to power off. It writes ACPI S5 as `0x3400` (type 5 plus `SLP_EN`) and retains the Bochs-compatible fallback. The shell and desktop dialog both use the same checked shutdown path. A failed sync returns to the running guest and reports the error; shutdown is not logged as complete before sync succeeds.

Boots A, B, C, and D each received the guest shell `shutdown` command. Each serial trace shows `request_received`, `storage_sync_begin`, successful ATA flush, `[VFS] sync complete mount=/ fs=FAT32`, `storage_sync_complete`, and the ACPI S5 request. QAC reported exit code 0 for each QEMU process and released each reservation. No host QMP quit or forced process termination was used to satisfy shutdown acceptance.

## 8. Filesystem synchronization

The new `vfs::sync_all()` closes any remaining writable VFS handles, flushes each mounted writable FAT/exFAT volume once, attempts the remaining mounted volumes after a failure, and returns the first error. Read-only and RAM mounts do not trigger a device flush. A writable filesystem without a supported sync operation reports `VFS_ERR_NOT_SUPPORTED`.

The hosted production VFS regression injects a close-time flush timeout, verifies that `sync_all()` returns `VFS_ERR_IO_TIMEOUT` and closes the handle, then verifies a subsequent sync succeeds when the injected failure clears. Native shutdown traces verify successful FAT/ATA synchronization on all four boots. No native ATA failure was injected.

Native diagnostics recorded **71 successful cache flushes** across the four boots: A 46, B 16, C 4, D 5. Every command selected `ata0m`, issued opcode `0xEA`, and returned `BLOCK_OK`; each boot's final flush preceded VFS sync completion and the ACPI S5 request. The historical TASKBAR6R2 timeout root cause remains unknown because no failure recurred.

## 9. Native FAT and taskbar workload

The native workload established:

- Guest creation and content write of `/r6test.txt`.
- Successful device flushes during file/configuration writes.
- Guest reopen and exact read through VFS on Boot A, B, and C.
- File Explorer enumeration and Notepad open on Boot A and B.
- Five canonical `/desktop.cfg` temporary-to-primary replacements in Boot A while taskbar options were changed.
- Clean guest shutdown and a subsequent cold boot after each Boot A, B, and C.

The planned Boot B edit of the test contents was not completed. Native rename and delete of a disposable test file were not exercised. Those operations are covered by the hosted production FAT/VFS suite, but that does not satisfy the remaining native-operation portion of this workload. R6 therefore leaves the native file-operation gate partial.

## 10. Native taskbar settings and auxiliary persistence

In Boot A, genuine guest input changed the taskbar from its starting Bottom/Standard/Icons/Auto-hide Off state to:

| Requested change | Guest UI observation | Stored result | Filesystem/flush evidence |
|---|---|---|---|
| Docking position: Left | Dock moved to the left edge | `taskbarPosition=left` | `/desktop.cfg` replacement; successful `ata0m` flushes |
| Size: Expanded | Expanded taskbar controls shown | `taskbarSize=expanded` | Same canonical file path and successful flushes |
| Presentation: Icons and labels | Labels appeared in taskbar buttons | `taskbarPresentation=icons-labels` | Same canonical file path and successful flushes |
| Auto-hide: On | Auto-hide control showed enabled | `taskbarAutoHide=1` | Same canonical file path and successful flushes |

The stored file also retained `taskbarShowPreviews=1` and `taskbarShowActivity=1`. Boot B, Boot C, and Boot D logs report `loaded taskbar position=left source=primary`; their initial screenshots show the left dock and one Calculator button. The `TASKBAR.PIN` record contains exactly one `appId=gxos.builtin.calculator` entry. That pin was already present in the clean R2 source image; R6 verified it loaded and did not duplicate it, but did not create a new pin.

Start Menu recent-list saves used `/STARTMNU.REC` and reported the chosen path. The R6 raw image contains that supported short-name record. No R6 sidecar write returned `FAT_FILE_WRITE_INVALID_NAME`.

## 11. Cold-restart acceptance

| Boot | Starting evidence | Guest work and readback | Shutdown result |
|---|---|---|---|
| A | Fresh isolated R2 copy with staged diagnostic kernel | Changed taskbar settings; created `/r6test.txt`; Explorer and VFS paths used | Guest shell shutdown; sync complete; QEMU exit 0 |
| B | Same writable R6 volume, fresh QEMU process | Restored left/expanded/labeled/auto-hide settings and Calculator button; Explorer opened test file; `vfscat` read 27 bytes | Guest shell shutdown; sync complete; QEMU exit 0 |
| C | Same writable volume after Boot B clean shutdown | Settings and Calculator button remained; `vfscat` read the same 27 bytes | Guest shell shutdown; sync complete; QEMU exit 0 |
| D | Same volume after restaging the updated diagnostic kernel | Reconfirmed settings/pin load; `/DOCS` was created with a valid FAT short name | Guest shell shutdown; sync complete; QEMU exit 0 |

Boot B did not make the additional file/settings change requested for the full updated-state variant. Boot C verified the latest state actually saved in Boot A. Thus repeated clean cold-restart recovery is established for that saved workload, but the second-generation update/readback case remains open.

## 12. FAT integrity and failure safety

After Boot D, the read-only image parser found matching FAT copies, 17 root entries, zero LFN association errors, the expected `DOCS` directory, the exact test-file bytes, and no `DESKTOP.TMP` or `DESKTOP.BAK`. `TASKBAR.PIN` retained one Calculator record. The hosted injected rename-flush failure preserved the original file bytes and returned the original timeout for retry; repeated temp replacement and delete left the directory consistent. The existing checksum-valid LFN rename/read/enumerate/delete cases and stale-checksum rejection passed.

No destructive power-loss test was performed. These results qualify the tested workload and do not establish universal power-loss atomicity.

## 13. Regression and build results

| Suite or build | R6 result |
|---|---:|
| Production FAT/VFS persistence, clean fixture | **77 checks, 0 failures** |
| ATA PIO flush state machine | **80 checks, 0 failures** |
| Taskbar application model | **113 checks, 0 failures** |
| Native settings runtime | **908/908 passed** |
| Hosted application build (`build.bat`) | **Passed** |
| Production AMD64 kernel | **Passed** |
| Diagnostic AMD64 kernel with `ATA_QUALIFICATION_DIAGNOSTICS` | **Passed** |
| `git diff --check` | **Passed** |

App Model identity (25/25) and actions (55/55), compositor pointer, Window UI, and resize remained at their R5 passing baseline; these files were not changed and those unrelated suites were not rerun in R6.

## 14. Gate-by-gate outcome

| Gate | Status | Evidence / limit |
|---|---|---|
| R6-G1 — QAC admission and guest candidate verified | **PASS** | Exclusive acceptance reservations; candidate was an isolated R2 fresh-image copy; QEMU, OVMF, and disk configuration recorded. |
| R6-G2 — Original invalid-name failure traced | **PASS** | Exact paths, callers, root mount/parent cluster, and short-name-generation failure identified from R5 logs and production code. |
| R6-G3 — Auxiliary settings write failure corrected | **PASS** | Redundant invalid sidecar writes removed; `/desktop.cfg` and `/STARTMNU.REC` succeeded natively; exact old path components preserve invalid status in tests. |
| R6-G4 — Native FAT file create/open/read/write operations | **PARTIAL** | Native create/write/flush/reopen/read passed; native update/rename/delete were not exercised. Hosted production FAT/VFS covers those operations. |
| R6-G5 — Guest file-reopening problem resolved or characterized | **PASS** | File Explorer enumerated/opened the text file; guest VFS reads matched the raw image. R5 used application dispatch instead of file opening. |
| R6-G6 — Guest-initiated clean shutdown | **PASS** | Shell shutdown request reached the kernel and all four QEMU guests exited with code 0. |
| R6-G7 — Required shutdown storage sync verified | **PASS** | Successful `ata0m` cache flush and VFS FAT32 sync precede ACPI S5 on every boot. |
| R6-G8 — Taskbar settings and pins survive clean cold restarts | **PARTIAL** | Boot A settings and the existing Calculator pin loaded on Boots B–D with no duplicate pin. R6 did not save a new Calculator pin or make a second taskbar change in Boot B before Boot C. |
| R6-G9 — Test-file contents survive and read inside guest | **PASS** | Boot B and C production VFS reads each returned the exact 27 bytes saved in Boot A. |
| R6-G10 — Configuration/FAT safeguards remain intact | **PASS** | Injected replacement failure preserves prior bytes; raw image has identical FAT copies, no stale LFN, and no temp/backup files. No ATA failure was injected. |
| R6-G11 — Focused regressions and builds pass | **PASS** | FAT 77/77, ATA 80/80, taskbar 113/113, settings 908/908, hosted and both AMD64 builds. |
| R6-G12 — QAC ownership and cleanup verified | **PASS** | Four QAC-owned guests released; final status/list/queue show zero QEMU processes, reservations, sessions, or requests. |
| R6-G13 — Mainline readiness and graphical gaps reassessed | **PASS** | Integration remains not ready; storage and taskbar overlap risks and next milestone are documented below. |

## 15. QAC resource accounting and candidate evidence

The four QAC acceptance reservations were:

| Boot | Reservation | QEMU PID | QMP port | Result |
|---|---|---:|---:|---|
| A | `0e67363c4453403ea844834b0c0b176b` | 18436 | 49312 | Guest S5 shutdown; exit 0; released |
| B | `445ebd18d6e74e8abea5bbb75e7183c1` | 10752 | 49313 | Guest S5 shutdown; exit 0; released |
| C | `9de804cedb334c03bade73672e948a10` | 20356 | 49314 | Guest S5 shutdown; exit 0; released |
| D | `72a0ba63df844a3b902d49e4b4e91004` | 23256 | 49315 | Guest S5 shutdown; exit 0; released |

Boot D used the rebuilt diagnostic kernel after offline restaging through the production FAT/VFS image adapter. The R5 raw image was never mounted or modified. `candidate-manifest.json` records source, firmware, kernel, disk hashes, and the observed disk hashes across the run. `qac-post-clean.log` is the final coordinator status/list/queue capture.

## 16. Evidence locations

All generated native evidence is under `out/taskbar/TASKBAR6R6/`:

- Serial and QAC records: `boot-a-serial.log` through `boot-d-serial.log`, `qac-run-boot-a.log` through `qac-run-boot-d.log`, and `qac-post-clean.log`.
- File creation/open/read screenshots: `boot-a-saved-file.bmp`, `boot-a-explorer-opened-file.bmp`, `boot-b-opened-r6test.bmp`, `boot-b-vfscat-read.bmp`, `boot-c-vfscat-result.bmp`.
- Taskbar state screenshots: `boot-a-after-position.bmp`, `boot-a-after-size.bmp`, `boot-a-after-autohide.bmp`, `boot-a-after-labels.bmp`, `boot-b-initial.bmp`, and `boot-c-initial.bmp`.
- Host raw verification: `raw-fat-readonly-after-boot-a.txt`, `raw-fat-readonly-after-boot-c.txt`, and `raw-fat-readonly-final.txt`; parser `inspect_raw_fat_r6.py` is read-only.
- Regression and build logs: `tests/fat-final-run.log`, `tests/ata-pio-flush-test.log`, `tests/taskbar-model.log`, `tests/settings-native-runtime.log`, `hosted-build.log`, `native-amd64-build.log`, and `diagnostic-amd64-build.log`.
- Candidate/QEMU record: `candidate-manifest.json`, `qemu-config-r6.txt`, `candidate-stage.log`, and `candidate-restage-docs.log`.

Large disk and firmware files remain generated evidence and are not part of the source commit.

## 17. Mainline integration recommendation

The current branch is 11 commits ahead of the locally available `origin/main` ref and 0 behind it. The branch contains overlap in `kernel/core/ata.cpp`, `kernel/core/fs_fat.cpp`, `kernel/core/vfs.cpp`, desktop/taskbar code, App Model files, compositor/Window Manager files, and bare-metal interfaces. R6 adds more storage-sync and FAT behavior to shared files. These are ordinary integration risks, but the storage and VFS overlap needs a deliberate comparison on the merge candidate.

Keep `TASKBAR_IMPROVEMENTS` local for now. Do not merge or rebase as part of this milestone. Frozen v0.3 release candidates remain untouched.

## 18. Recommended next milestone

Run a narrow final graphical persistence acceptance: perform a fresh Calculator pin action and a second distinct taskbar preference save, update then rename/delete a disposable file through production guest APIs, cleanly shut down, and verify all latest values and contents after the next cold boot. Include the remaining graphical checks for grouping, previews, overflow, auto-hide, and File Explorer progress. Leave the historical TASKBAR6R2 ATA timeout root cause as a separate open question unless a new native failure reproduces it.

# TASKBAR-RECOVERY2R1 — Native Requalification and Mainline Reconciliation

- **Recorded:** 2026-11-27
- **Outcome:** `TASKBAR_RECOVERY2R1_NATIVE_PARTIAL`
- **Candidate branch:** `integration/taskbar-recovery2`
- **Candidate worktree:** `E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERY2`
- **Qualification profile:** `ServerV03PacManOnly` / AMD64 UEFI
- **Actual main worktree:** `E:\dev\guideXOSServer`

## 1. Result

The reconstructed candidate was booted through QAC on an earlier build. UEFI, the kernel, and the desktop event loop started, and the taskbar was visible. That run exposed a real RAMDISK virtual-address collision: the kernel found the image in BootInfo but could not mount `/`, so `/system`, packaged applications, and the wallpaper were unavailable. A high virtual alias and a shared 1,024-page page-table bound were then implemented, regression-tested, and included in a new production AMD64 release build.

The corrected build has **not yet received its own QAC guest run**. QAC is online, but an identified mainline S22R28R3R3 acceptance session currently owns the QAC lane. It was left untouched. Native taskbar behavior, writable FAT32 UI saves, S5 shutdown, and cold-restart persistence therefore remain unqualified. No claim is made that the reconstructed candidate is ready to merge.

The narrow next step is to wait for that QAC reservation to release, then boot the final ISO and verify the high alias, `/`, `/system`, wallpaper, application launch, and taskbar with retained screenshots and serial evidence. If that passes, continue with the separate disposable FAT32 save, shutdown, and cold-restart run.

## 2. Git identity, integrity, and synchronization

At preflight, the candidate was clean on `integration/taskbar-recovery2` at `e81dfbf82e65e8a88199aa21d0aafcdb75153bb6`, tracking `origin/integration/taskbar-recovery2`. `git fsck --full --no-reflogs` completed without reported object errors. The configured origin is `git@github.com:guideX/guideXOS-Server.git`.

The final tested source was `7686e5135ed9cd367154e52316a87500c4d3b9b5`; the worktree was clean and `git diff --check` passed before this report was added. Three candidate commits follow the pushed baseline:

| Commit | Change |
|---|---|
| `ab51ffb6` | Raise the UEFI page-table allocation bound after the native mount failure was isolated |
| `34965649` | Map the release RAMDISK at a stable high kernel virtual alias |
| `7686e513` | Share the 1,024-page limit between loader and kernel |

Read-only HTTPS `git ls-remote` confirmed the GitHub candidate branch still pointed to `e81dfbf82e65e8a88199aa21d0aafcdb75153bb6`. GitHub `main` was `5f8720519a5b4cce735525016da9853640fd361f`. SSH authentication had failed with `Permission denied (publickey)` on the earlier push attempt. No force update was attempted. The candidate was three commits ahead of GitHub before this report commit; safe branch delivery remains subject to a final remote check and available push authentication.

The archived personal review ISO was rehashed and still matches `b5a638f134c949ddede9658596009df4c716112f4d200fde0ca3f4c86ada2906`. It was not overwritten. The recovery bundle, earlier reports, generated images, and prior build outputs remain preserved.

## 3. Recovery evidence reviewed

Reviewed the reconstruction report, Recovery1 storage relocation report, TASKBAR6R1–TASKBAR6R6 acceptance and storage reports, and the current mainline S22R28R3R3 corrected RAMDISK guest report. The recovered repository did not contain historical TASKBAR-INTEGRATION2R1, TASKBAR-INTEGRATION2R2, TASKBAR-ISO1, or TASKBAR6R7/R8 source reports; their contents and commits were not assumed.

The reconstruction remains a new implementation with new Git provenance. Historical results were used as behavioral context, not as native acceptance for this candidate.

## 4. Final source and build artifacts

The final production release was created from a clean source tree at `7686e5135ed9cd367154e52316a87500c4d3b9b5` using `ServerV03PacManOnly`. Build ID: `4b5f45cb-4e8d-47f8-87c8-cb380b70d15d`. Bootloader, kernel, symbol audit, ramdisk, and PacMan stages passed. Developer Studio was not selected; its signed-off package hashes were not changed or bypassed. The candidate ISO is UEFI El Torito bootable (platform `0xEF`, no emulation).

ISO:

`out/release-profile/4b5f45cb-4e8d-47f8-87c8-cb380b70d15d/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`

| Artifact | Size | SHA-256 |
|---|---:|---|
| Candidate ISO | 90,245,120 bytes | `294def446d557197577160cea42fb94d04742066f926cf9f5522c3dd6c094499` |
| `ESP/EFI/BOOT/BOOTX64.EFI` | 58,368 bytes | `348974926ad00ba5994b7b8f5a826dec0fab87a90698e78b6e4bc2160c127bd8` |
| `ESP/kernel.elf` | 2,893,696 bytes | `cc5b2fb0caadf6ebea31f8339aa82d6fc5f84f3b95a4f3385ae635eba12e79b9` |
| `ESP/ramdisk.img` | 67,108,864 bytes | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |

The manifest and `ESP/build-identity.txt` record the source commit, clean-at-build-start status, stage results, and artifact hashes. A distinct diagnostic AMD64 release profile was not available from the release script; no separate diagnostic build is claimed.

## 5. Native QAC run and defect repair

QAC 2.0.0 was online. The first candidate run used QAC acceptance reservation `4fa74fd5c9db453683a9efbfd4c69cba`, owner process PID `15968`, and QEMU child PID `23592`. Its QAC record was later inspected as **Released**, reason `Managed child exited`; elapsed time was 831 seconds. The run was bounded to ten minutes of guest observation plus controlled closeout. The archived review ISO was not used.

That ISO was built at commit `ab51ffb6b45d9b1e8feda832dc5da8e30590991b` (before the high-alias mapping change), build ID `0af9f73e-3aba-4d84-b729-7b5497133888`, SHA-256 `8e33b6ed618d47d1f24d86bab31ac3884a57e355ae10c664e716fa5e8da94bae`. QEMU, kernel, RAMDISK, and firmware hashes are recorded in `out/taskbar/RECOVERY2R1/native-iso/preflight.txt`.

Observed serial evidence in `out/taskbar/RECOVERY2R1/native-iso/serial.log` confirms UEFI handoff, a valid desktop framebuffer, keyboard/mouse capability, and an active desktop loop. The kernel found the RAMDISK pack but logged `Filesystem mount failed`, `failed to mount boot runtime image at /`, `/Apps` absent, `/system` absent, and wallpaper fallback to a gradient. The native taskbar rendered. A genuine PS/2 keyboard event sent through QMP opened the Start Menu. The QEMU GTK pointer was grabbed, so the available desktop automation could not perform a reliable guest pointer click. File Explorer launch and taskbar actions were not verified.

The serial log and empty QEMU guest-error log were retained. A screenshot was inspected during the run but no screenshot file was retained; this is an evidence gap. QMP `system_powerdown` produced a powerdown request without guest S5. QEMU was then ended through QMP `quit`; its event was `guest:false, reason:host-qmp-quit`. This was **not** a clean guest shutdown, and no writable disk was attached.

Source inspection localized the mount failure to a low virtual-address collision: `BootInfo.RamdiskBase` is a physical address, while the loader's low kernel virtual range can occupy the same numeric address. The candidate now maps the full RAMDISK at `GUIDEXOS_RAMDISK_ALIAS_BASE` (`0x0000400000000000`) after kernel mapping, preserves the physical BootInfo range for ownership, and has the kernel consume the alias. The loader and kernel now share `GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES=1024`. The UEFI alias regression passes on the final source, covers the complete 16,385-page test extent, and observes a 525-table tree. The 525-page-tree memory-foundation test also passes. These are source/hosted checks; they do not replace a post-fix QAC boot.

At the latest coordinator check, QAC remained online and reported another verified acceptance reservation: `15b5e322548148b09cde5fabdfec58ae`, owner CLI PID `2036`, QEMU PID `14508`, project `guideXOSServer`. The current mainline S22R28R3R3 report identifies this as its in-progress corrected RAMDISK guest run. It was left running without interaction. No second QEMU was started.

## 6. Native executable test plan

The plan below is prepared for the final ISO after QAC releases the current session. First boot is ISO-only and read-only; writable persistence uses a separate fresh disposable FAT32 image. Inputs use the QEMU PS/2 keyboard and mouse path. QMP is used for supported virtual input, screenshots, serial capture, and bounded shutdown; every action must be confirmed by a visible guest result, not merely by an input acknowledgement.

The selected `ServerV03PacManOnly` receipt has `kernelProfileFlags=none`. In this profile, `mount_persistent_storage()` mounts the first writable non-transient device at `/`; the release RAMDISK is then mounted read-only at `/system`. Therefore the writable test image is seeded with the **verified `/Apps` package files from the final ESP** so native App Model discovery still has its production inputs. No settings or pin records are pre-seeded. The production taskbar presentation store is `/desktop.cfg` (`kernel/core/desktop.cpp`); the primary taskbar pin store is `/taskbar.pin` (`kernel/core/kernel_compositor.cpp`; `/desktop.taskbar.pins` is read only as a legacy fallback). This profile does not compile the `GXOS_PUBLIC_PREVIEW_SECURITY_PROFILE` path that mounts `/Documents` and `/AppData`, so those alternate user-data paths are not part of this qualification.

| Scenario | Input | Expected result | Evidence |
|---|---|---|---|
| UEFI boot | QAC-managed QEMU, OVMF, final ISO | Loader exits boot services; kernel entry proceeds | QAC reservation/child identity, serial log, boot screenshot |
| BootInfo and mounts | Observe boot serial and desktop | Valid memory data; RAMDISK alias resolves; `/` and `/system` mount | Serial checkpoints and screenshot |
| Desktop and input | PS/2 key and pointer events | Desktop renders and responds to both devices | Before/after screenshots and serial capability lines |
| Start Menu | PS/2 Start key, then navigate/select | Menu opens and a real registered item launches | Screenshots, serial launch result, visible window |
| File Explorer | Select its actual Start Menu/taskbar entry | Explorer opens as a real window with expected content | Screenshot, title/icon and launch serial evidence |
| Taskbar activation | Select running Explorer taskbar button | Correct window receives focus | Screenshots and window/focus evidence |
| Minimize/restore/close | Use real window controls and taskbar button | Same window minimizes, restores, then closes; stale entry disappears | Sequential screenshots and serial state |
| Context menu and pins | Right-click real taskbar entry; pin/unpin through UI | Menu actions affect the selected application only | Screenshots plus saved pin state after sync |
| Hover preview | Hover over real running taskbar entry; enter and select preview | Title/icon or supported title-state fallback remains accessible and activates same window | Screenshots and window identity |
| Docking, size, labels, auto-hide | Change supported settings through Settings UI | Geometry, labels, edge reveal, popup transfer, dismissal, and restore match selection | Screenshots for each changed mode |
| Grouping | Launch multiple windows with same canonical application identity | Grouped entry/member selection and close cleanup work | App/window identities and screenshots |
| Overflow | Use genuine supported running-app population | Overflow control, paging, selection and restoration work | Screenshots; no limit changes to manufacture overflow |
| Copy progress | Run a safe real Explorer copy | Real progress appears under correct taskbar owner and clears at completion | Source/destination hashes, screenshots and serial progress |
| Settings and pin save | With the disposable FAT32 image as `/`, change docking, size, auto-hide, another presentation option, and a pin through UI; save | `/desktop.cfg` and `/taskbar.pin` report successful writes | Serial write/sync logs, screenshots, and read-only post-exit FAT inspection |
| Clean shutdown | Use native supported shutdown | VFS sync and required ATA flush succeed, ACPI S5 is issued, QEMU exits, QAC releases | Serial sync/flush/S5, QMP event, QAC inspect/list, disk hash |
| Cold restart | Reattach same disposable disk to a fresh QAC run | Settings and pins restore once; unrelated settings remain; second save also survives if time allows | Screenshots, serial logs, settings/pin file contents and FAT mirror checks |

The hosted regression fixture is `out/taskbar/RECOVERY2R1/validation/fat-persistence/disposable-fat32.img` (128 MiB, label `GXOSDATA`, creation hash `6ad9b4a76b6f98ee108370b483f50449e358ef4ff56609fb165e7cd472813a90`). It remains detached from QEMU. A separate native guest image was created at `out/taskbar/RECOVERY2R1/validation/fat-persistence/native-guest-20261127/guideXOS-native-data.img` (128 MiB, FAT32, label `GXOSDATA`, marker `GXOSDAT.ID`, `APPDATA/`, and the final release `/Apps` tree). Its initial pre-QEMU SHA-256 after staging `/Apps` is `a64e70081ff23da4f28adf932ff3e3e9c2be3c4b9a9452c84d9bce151b23866d`. All 11 staged application files were copied from the final ESP and their individual SHA-256 values were recorded in the adjacent staging output. This disk has not been attached to QEMU; settings and pin files will be created only by guest UI actions.

Read-only preflight verified the FAT32 boot signature, 512-byte sectors, one sector per cluster, 32 reserved sectors, two identical FAT mirrors (2,033 sectors each), 262,144 total sectors, expected marker content, and exact `/Apps` file hashes. It also confirmed that neither `/desktop.cfg` nor `/taskbar.pin` was pre-seeded. The final QAC run should use the installed x86_64 EDK2 code image (SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`) and a task-owned copy of the installed disposable variables template (SHA-256 `5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e`). The QEMU executable SHA-256 is `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f77ef7a298ffdd02`. These exact firmware and QEMU paths/hashes are staged in the same native guest evidence directory.

## 7. Hosted and storage regression results

Focused regressions on the final source passed. Logs and executables are under `out/taskbar/RECOVERY2R1/validation/`.

| Area | Result |
|---|---|
| Taskbar model | 118/118 |
| App Model identity / actions | 25/25; 55/55 |
| File / folder activation | 60/60; 26/26 |
| URI/protocol activation | 41/41; persistence/activation/no-handler checks 100 each, 0 failures |
| Default handlers | 54/54; persistence reload, Open, Open With checks 100 each |
| Settings runtime / layout | 908/908; 1,037/1,037 |
| FAT/VFS persistence and directory growth | 82 checks, 0 failures; mutation was limited to a separate hosted image copy |
| ATA PIO flush | 80 checks, 0 failures |
| Release RAMDISK mount | 12 checks, 0 failures on final profile image |
| UEFI RAMDISK alias / page-table regression | PASS; 16,385 pages covered, 525 page tables observed |
| FAT mount diagnostics | enabled 15/15; disabled 7/7 |
| User data volume policy | 9 passed, 0 failed |
| Native ELF security policy | PASS, 0 failures, 4 native-entry calls |
| AMD64 memory foundation | PASS; hosted only, no hardware CPL3/page-fault transition |
| Protected task contract | PASS, process exit 0; harness emits no assertion total |
| Compositor pointer buttons, window UI, resize notifications | PASS; resize and window UI regressions passed |

The hosted desktop build passed during the reconstruction. These final recovery commits change UEFI RAMDISK mapping and the AMD64 page-table audit bound; the final `ServerV03PacManOnly` production AMD64 build passed after those changes. The build generated the UEFI bootloader, kernel, symbol audit, runtime RAMDISK, and PacMan package from pinned source. No distinct diagnostic AMD64 profile was available.

The FAT/VFS and ATA regressions support storage contracts but do not prove native write durability, FAT mirror consistency after guest use, ACPI S5, or cold-restart settings restoration. The historical intermittent ATA timeout is not claimed permanently resolved; none recurred in these focused runs.

## 8. Current mainline state and compatibility

The actual main checkout was inspected read-only and left unchanged. At inspection it was on `main`, HEAD `5f8720519a5b4cce735525016da9853640fd361f`, equal to `origin/main`, with 0/0 ahead/behind. It had three modified tracked files and four untracked files:

- Modified: `guideXOSBootLoader/paging.cpp`, `kernel/core/amd64_memory_foundation.cpp`, `tests/amd64_memory_foundation_test.cpp`.
- Untracked: `docs/RELEASE_S22R28R3R3_CORRECTED_RAMDISK_GUEST_ACCEPTANCE.md`, `kernel/core/include/kernel/boot_page_table_validation.h`, `scripts/run-uefi-paging-alias-test.ps1`, `tests/uefi_paging_alias_test.cpp`.

This live state differs from the 2026-11-26 snapshot, which held seven modified tracked files and three untracked files at `aad1bce8…`. The snapshot remains untouched. Current main has one committed step beyond the candidate's common base (`aad1bce8…`) and the candidate has 21 commits beyond that base. No candidate refresh, rebase, main checkout, or main mutation was performed.

Reconciliation findings:

| Classification | Evidence and next handling |
|---|---|
| **Semantic overlap — RAMDISK alias** | Current main's committed `guideXOSBootLoader/main.cpp` maps the RAMDISK at the same `0x0000400000000000` virtual base and current main's untracked `ramdisk_integrity_diagnostic.h` supplies that base. The candidate adds the alias constant to shared `guidexOSBootInfo.h` and uses it for loader mapping, kernel reads, and fingerprinting. The core mapping intent is compatible, but the shared constant and diagnostic APIs need one deliberate reconciliation. Main's report records that `/` and `/Apps` mounted in its earlier corrected run; this is not candidate acceptance. |
| **Semantic overlap — page-table bound** | Candidate shares `GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES=1024` across loader and kernel and passes an actual 525-table alias regression. Main's uncommitted `paging.cpp` raises `MAX_PT_PAGES` to 1,024; its uncommitted `amd64_memory_foundation.cpp` extracts `boot_page_table_tree_avoids_range` and emits page-count diagnostics, with a synthetic 525-tree regression. These approaches validate the same boundary but use different shared headers and should be reconciled together. |
| **Uncommitted mainline dependency** | The current main commit includes bootloader diagnostic references to `ramdisk_integrity_diagnostic.h`, while the header is still untracked in the live main worktree. The active owner must finish and account for this dependency before integration; it was not copied into the candidate. |
| **Textual/semantic conflict** | Commit `5f872051` changes `guideXOSBootLoader/main.cpp`, `trampoline_msvc.cpp`, `kernel/Makefile`, `kernel/arch/amd64/boot.asm`, `kernel/core/main.cpp`, and `kernel/core/desktop.cpp` for S22R28R3 diagnostics. The candidate also changes bootloader, kernel build, entry, desktop, and memory paths for taskbar/recovery behavior. These overlapping files require a three-way review after mainline work is finalized. |
| **Already compatible at a contract level** | Physical `BootInfo.RamdiskBase` remains the ownership value; the runtime needs a supervisor alias for dereferencing. Both branches preserve that separation and use `/` plus `/system` as the intended mounts. This shared contract does not by itself prove source-level merge compatibility. |
| **Separate future integration** | No NATIVE_DB_STORAGE source was changed. Shrink/reclamation, the native database file bridge, and database durability ordering remain separate work; no direct taskbar dependency was established. The page-table ownership validation is a platform contract that any later NDB execution will also depend on. |

The current main report identifies its in-progress QAC reservation and records separate RAMDISK alias and 525-table findings. Those results inform compatibility but do not qualify this candidate. The uncommitted main work and all its new files remain owned by that workflow.

## 9. Acceptance gate matrix

| Gate | Status | Evidence / reason |
|---|---|---|
| R2R1-G1 Git baseline, integrity, remote synchronization checked | **PASS** | Clean starting point, `fsck`, upstream, remote candidate and main refs checked read-only. Candidate is ahead of the remote with safe delivery pending. |
| R2R1-G2 Reconstructed candidate builds | **PASS** | Final production AMD64 UEFI/profile build, symbol audit, packages and ISO passed. |
| R2R1-G3 QAC-managed native UEFI boot | **PARTIAL** | Earlier candidate reached kernel and desktop; corrected final ISO has not yet booted in QAC. |
| R2R1-G4 Root and `/system` mounts | **PARTIAL** | Earlier boot failed both mounts; alias and bound fixes pass hosted tests, final native retest pending. |
| R2R1-G5 Start Menu and File Explorer activation | **PARTIAL** | Start Menu visibly opened with PS/2 keyboard input; File Explorer launch was not achieved. |
| R2R1-G6 Native taskbar controls and context menus | **PARTIAL** | Taskbar rendered; no real taskbar control, window action, or context-menu operation completed. |
| R2R1-G7 Hover previews and auto-hide interaction | **BLOCKED** | Requires native pointer interaction and running application; not exercised. |
| R2R1-G8 AppID, grouping, overflow, progress | **BLOCKED** | Hosted identity/action tests pass; no genuine native launch or multi-window/copy scenario was completed. |
| R2R1-G9 Native UI configuration changes saved | **BLOCKED** | No writable FAT32 volume was attached and no native settings were changed. |
| R2R1-G10 VFS sync and clean ACPI S5 shutdown | **BLOCKED** | Earlier QMP powerdown did not reach S5; host QMP `quit` was not guest shutdown. |
| R2R1-G11 Cold-restart settings and pin persistence | **BLOCKED** | No guest disk boot/restart cycle. |
| R2R1-G12 FAT/ATA integrity and regressions | **PARTIAL** | Hosted FAT/VFS 82 and ATA flush 80 checks pass; final native root alias, guest writes and FAT mirror state remain unverified. |
| R2R1-G13 Current mainline work preserved and assessed | **PASS** | Live main HEAD/status and overlaps inspected read-only; snapshot retained; no main files modified. |
| R2R1-G14 Hosted/native builds and relevant regressions | **PARTIAL** | Final AMD64 build and focused hosted regressions pass; final ISO QAC retest and separate diagnostic target are outstanding. |
| R2R1-G15 QAC cleanup and Git delivery | **PARTIAL** | This task's reservation is verified released. Another verified mainline reservation remains active; safe GitHub push has not been completed. |
| R2R1-G16 Controlled INTEGRATION3 readiness | **NOT READY** | Candidate's final native boot, mounts, UI, persistence, and shutdown are not demonstrated; mainline overlap is understood but still owned/in progress. |

## 10. QAC and artifact cleanup

QAC inspect verified this task's reservation `4fa74fd5c9db453683a9efbfd4c69cba` as Released. The coordinator's latest `list` showed the separate active reservation `15b5e322548148b09cde5fabdfec58ae`; therefore global QAC cleanup is not claimed. No foreign or unmanaged QEMU was started, and the active mainline QEMU was not touched.

All generated build/test evidence stays under ignored `out/` paths. The existing review ISO, recovery bundle, prior reports and disk artifacts were preserved. The fresh 128 MiB FAT32 fixture remains detached from QEMU. No files were removed as cleanup.

## 11. Readiness decision and exact next action

**Outcome B — `TASKBAR_RECOVERY2R1_NATIVE_PARTIAL`.** The reconstructed source and production build are in good shape for continued qualification, and focused hosted/storage regressions pass. The first actual guest run exposed and helped localize a boot-time RAMDISK alias defect. The source fix now passes an alias/page-table regression, but the final built ISO still needs a native QAC run. Essential taskbar, persistence, clean shutdown, and cold-restart gates are still open.

Do not start TASKBAR-INTEGRATION3 yet. The immediate next action is: **wait for QAC reservation `15b5e322548148b09cde5fabdfec58ae` to be released, then request one exclusive QAC acceptance run of the final ISO identified in §4 and verify RAMDISK alias, `/`, `/system`, wallpaper, Start Menu, and File Explorer.** If that passes, use a newly created disposable FAT32 disk for UI saves, VFS/ATA sync, ACPI S5, and cold-restart verification. Reconcile the alias constant and page-table validator with the mainline owner's completed changes in a separate controlled integration review. No merge into `main` is authorized or performed in this milestone.

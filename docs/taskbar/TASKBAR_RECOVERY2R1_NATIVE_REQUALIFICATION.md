# TASKBAR-RECOVERY2R1 — Native Requalification and Mainline Reconciliation

- **Recorded:** 2026-11-27
- **Outcome:** `TASKBAR_RECOVERY2R1_NATIVE_REQUALIFIED`
- **Candidate branch:** `integration/taskbar-recovery2`
- **Candidate worktree:** `E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERY2`
- **Qualification profile:** `ServerV03PacManOnly` / AMD64 UEFI
- **Actual main worktree:** `E:\dev\guideXOSServer`

## 1. Result

The reconstructed production AMD64 candidate completed a QAC-managed ISO-only UEFI boot and three QAC-managed boots with a disposable writable FAT32 volume. The ISO-only run confirmed the high RAMDISK alias, read-only FAT32 root and `/system` mounts, wallpaper, Start Menu, real File Explorer activation, taskbar minimize/restore, the title/state preview fallback, context menu, and a real `com.guidexos.pacman` native ELF launch.

On the writable volume, native UI changes to taskbar position, size, auto-hide and presentation settings were saved and restored after cold restart. A real File Explorer taskbar context-menu action saved its pin; the next cold boot loaded one pinned Files entry and activated the real Explorer window. The guest synchronized storage, selected ACPI S5, exited QEMU with code 0, and QAC released each reservation. Read-only host inspection found matching FAT mirrors and no orphan clusters after each boot. Start Menu shutdown confirmation and S5 were observed on Boot A; Boots B and C used the native shell `shutdown` command to exercise the same production shutdown implementation.

This qualifies the essential native boot, launch, taskbar, writable settings/pin persistence, storage synchronization, clean shutdown, and cold-restart gates. Grouping, overflow and real copy-progress behavior remain partial because suitable genuine scenarios were not available. Some taskbar details, including unpin/close cleanup and full hover-popup pointer transfer, also remain unqualified. There is no established material taskbar integration defect. Recommend controlled TASKBAR-INTEGRATION3 preparation after the focused mainline reconciliation described below; no merge is performed in this milestone.

## 2. Git identity, integrity, and synchronization

At preflight, the candidate was clean on `integration/taskbar-recovery2` at `e81dfbf82e65e8a88199aa21d0aafcdb75153bb6`, tracking `origin/integration/taskbar-recovery2`. `git fsck --full --no-reflogs` completed without reported object errors. The configured origin is `git@github.com:guideX/guideXOS-Server.git`.

The final tested production source was `7686e5135ed9cd367154e52316a87500c4d3b9b5`; later commits on the candidate branch contain qualification documentation only. Three implementation commits followed the reconstructed baseline:

| Commit | Change |
|---|---|
| `ab51ffb6` | Raise the UEFI page-table allocation bound after the native mount failure was isolated |
| `34965649` | Map the release RAMDISK at a stable high kernel virtual alias |
| `7686e513` | Share the 1,024-page limit between loader and kernel |

The final read-only HTTPS `git ls-remote` check found GitHub `integration/taskbar-recovery2` at `cc09851a385ff53761928adf14469c9d672470e2`, matching the local branch before the report commit. GitHub `main` was `02422a86d44319e6e6473342af7e8b1e29c55ff9`. The report commit `015bae136e75781ff2d05348157bb7b7390daeed` and its delivery-status correction are local commits after that remote tip. SSH push failed with `Permission denied (publickey)`; a noninteractive HTTPS push using Git Credential Manager was authenticated as `guideX224` but GitHub returned HTTP 403 (`Permission to guideX/guideXOS-Server.git denied`). The remote branch remains at `cc09851a`; the local branch is ahead by two documentation commits. No force update or main push was attempted. Repository synchronization for the qualification report is incomplete until an account with write access pushes those commits as a fast-forward.

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

Reservation `15b5e322548148b09cde5fabdfec58ae` later reached Released. A free QAC census was recorded, but a different acceptance reservation was admitted before this task's next request. Reservation `823f14635ac9445b99074c16b165f554` was a mainline S22R28R3R3 CPL3-on final candidate run (verified owner CLI PID `23164`, QEMU child PID `25800`, ISO and data image under `E:\CodexArtifacts\S22R28R3R3`). Process command-line and QAC inspect confirmed this was not the Recovery2R1 launch; it later reached Released before the final ISO was admitted. The local prelaunch guard had rechecked QAC during that session and exited before invoking `qac run`; no QEMU was started for Recovery2R1 by that guard. The mainline session was left untouched.

After the final ISO was built and verified, Recovery2R1 received an exclusive QAC acceptance reservation `e1b98acf06df4c1e8a94829ad302e081` (owner CLI PID `18876`, QEMU child PID `8840`, granted 2026-11-27 21:13:50 UTC). The final ISO SHA-256 was checked as `294def446d557197577160cea42fb94d04742066f926cf9f5522c3dd6c094499`; the command line attached only the read-only CD-ROM, with no user disk. The session ran for 616 seconds. QAC recorded it **Released**, reason `Managed child exited`, after QMP `quit`; QEMU exited 0 at 2026-11-27 21:24:07 UTC. Serial log and framebuffer screenshots are retained under `out/taskbar/RECOVERY2R1/native-iso-final-20261127/`.

The final run serial log records `Mapping RAMDISK alias: 0x0000400000000000 -> 0x00000000150C9000 size 67108864`, `vfs.root_mount=/`, `vfs.root_fs_type=FAT32`, `vfs.root_block_device=1`, and `vfs.system_mount=/system`. The storage probe found **zero ATA devices**; the kernel logged `No writable persistent storage filesystem found`, mounted the release `wallimg` as device 1, mounted `/` from its FAT32 image, and mounted `/system` as an alias of that read-only root. The kernel loaded `/system/wall/blueflwr.gxi` and rendered the blue-flower wallpaper. The packaged `/Apps/PacMan` manifest was discovered with app ID `com.guidexos.pacman`; QMP `F12` launched the real native ELF path and serial recorded `window PASS`, a presented initial frame, runtime cleanup PASS, and shortcut result PASS. The later Escape was an intentional guest key used to end that app, not an app crash.

The PS/2 Start-button click opened the Start Menu; selecting its real File Explorer item produced a visible `File Explorer` window rooted at `/` with `/system` in its navigation. Its taskbar button was visible as `Files`. Clicking the native minimize control hid the window and preserved the button; clicking `Files` restored the same window. Hovering that entry showed a preview containing `File Explorer` and `Active`, an accepted title/state fallback rather than a fabricated thumbnail. Right-click displayed `Activate`, `Minimize`, `Maximize`, `Close Window`, and `Pin to Taskbar`; pin storage was reported unavailable on this read-only image. No pin action was taken. The shutdown item was clicked once, but no confirmation dialog or shutdown/sync/S5 marker was observed; a managed QMP stop closed QEMU and QAC verified release. This was not a clean guest shutdown.

Screenshots, including the initial desktop, Start Menu, Explorer launch, minimize/restore, preview and context menu, are in `out/taskbar/RECOVERY2R1/native-iso-final-20261127/screenshots/`. Those were the ISO-only observations; that run had no writable disk, so settings, pin edits, grouping, overflow, copy progress and persistence were not exercised there. They were later tested on the writable FAT32 boots as recorded below.

### Writable FAT32 persistence and shutdown boots

A separate disposable 128 MiB FAT32 image was used for all writable guest tests:
`out/taskbar/RECOVERY2R1/validation/fat-persistence/native-guest-20261127/guideXOS-native-data.img`.
Its initial staged hash was `a64e70081ff23da4f28adf932ff3e3e9c2be3c4b9a9452c84d9bce151b23866d`. It was not a user disk, mainline disk, or historical qualification image. QAC admitted one Recovery2R1 guest at a time; each run used the final candidate ISO and the same image. The QEMU process and reservation identities were verified before and after every boot.

| Boot | QAC reservation / QEMU | Guest actions and result | Image SHA-256 after boot |
|---|---|---|---|
| A | `44440fc85a1342fbbc49f67682c67c29` / PID `19032` | Writable ATA root mounted; `/system`, desktop and Explorer worked. Through native taskbar settings, changed position to top, size to expanded, auto-hide off, labels off, previews on and activity indicators on. Start Menu shutdown confirmation was accepted; serial showed `caller=desktop-ui`, `storage_sync_complete`, and `poweroff_request method=acpi-s5 pm1a=0x3400`. QEMU exited 0 and QAC released the reservation. | `74f18ce88f2833a2dec570383f3c6937b50974dbdbedffb30bee0b0af923aa9` |
| B | `4957c3a4d7914fdc8a9eae6b1364127b` / PID `23444` | Cold boot restored the top/expanded/auto-hide-off/labels-off/previews-on/activity-on settings in the native menu. The real Explorer context menu saved a pin; serial reported `path=/taskbar.pin pins=00000001`. Native `shutdown` invoked production VFS sync and ACPI S5; QEMU exited 0 and QAC released the reservation. | `87f2796791696f39f33735af31b33e200f9bff158c5e2208ec0e6c2d2ebff5f3` |
| C | `8b08ab34d4f9498282a4c8aa0ab3ce1d` / PID `5032` | Second cold boot logged `pin persistence=loaded` and restored position `top`. Exactly one pinned Files icon appeared; selecting it launched the real Explorer window rooted at `/`. Native `shutdown` again completed VFS sync and ACPI S5; QEMU exited 0 and QAC released the reservation. | `87f2796791696f39f33735af31b33e200f9bff158c5e2208ec0e6c2d2ebff5f3` |

After each guest had released the disk, the host-side FAT32 inspector ran read-only. It found identical FAT mirrors, zero orphan clusters, and all allocated clusters reachable. After Boot A, 2,929 clusters were allocated and `/DESKTOP.CFG` contained the selected presentation values; `/TASKBAR.PIN` did not yet exist because the attempted pin action in that boot did not save. After Boot B, 2,930 clusters were allocated and `/TASKBAR.PIN` contained `version=1` plus `appId=gxos.builtin.fileexplorer`; `/DESKTOP.CFG` retained the exact settings. Boot C left the image hash and both configuration files unchanged, with the pin loaded once. The exact post-boot inspection output, serial logs, QEMU logs, preflight records and screenshots are retained in each `native-fat32-bootA/B/C-20261127/` evidence directory. The guest image's final hash is `87f2796791696f39f33735af31b33e200f9bff158c5e2208ec0e6c2d2ebff5f3`. The verified settings content was `taskbarPosition=top`, `taskbarSize=expanded`, `taskbarPresentation=icons-only`, `taskbarAutoHide=0`, `taskbarShowPreviews=1`, and `taskbarShowActivity=1`; unrelated desktop settings remained present. The saved pin content was `version=1` and `appId=gxos.builtin.fileexplorer`.

The actual guest shutdown path completed the requested storage synchronization and ACPI S5. Boot A demonstrated the desktop UI confirmation flow. The shell command was used on Boots B and C after repeated GUI-menu interaction did not reliably produce a second confirmation; this exercises the same production shutdown routine but does not expand the UI confirmation evidence beyond Boot A. No native Explorer copy/progress scenario, grouping scenario, overflow scenario, or pin-unpin/close-cleanup cycle was demonstrated.

## 6. Native executable test plan

This plan was prepared before QAC admission and was executed across the ISO-only run and three writable FAT32 boots. Inputs used the QEMU PS/2 keyboard and mouse path. QMP was used for supported virtual input, screenshots and serial capture; shutdown acceptance required guest synchronization/S5 markers, QEMU exit and QAC release rather than an input acknowledgement alone. The ISO-only profile tested read-only `wallimg` root and `/system`; the fresh FAT32 image tested writable settings and pins.

The selected `ServerV03PacManOnly` receipt has `kernelProfileFlags=none`. In this profile, `mount_persistent_storage()` mounts the first writable non-transient device at `/`; the release RAMDISK is then mounted read-only at `/system`. Therefore the writable test image is seeded with the **verified `/Apps` package files from the final ESP** so native App Model discovery still has its production inputs. No settings or pin records are pre-seeded. The production taskbar presentation store is `/desktop.cfg` (`kernel/core/desktop.cpp`); the primary taskbar pin store is `/taskbar.pin` (`kernel/core/kernel_compositor.cpp`; `/desktop.taskbar.pins` is read only as a legacy fallback). This profile does not compile the `GXOS_PUBLIC_PREVIEW_SECURITY_PROFILE` path that mounts `/Documents` and `/AppData`, so those alternate user-data paths are not part of this qualification.

| Scenario | Input | Expected result | Evidence / result |
|---|---|---|---|
| UEFI boot | QAC-managed QEMU, OVMF, final ISO | Loader exits boot services; kernel entry proceeds | Passed on ISO-only and FAT32 boots; QAC reservations, serial and screenshots retained per run |
| BootInfo and mounts | Observe boot serial and desktop | Valid memory data; RAMDISK alias resolves; `/` and `/system` mount | Passed; ISO root is read-only, FAT32 root is writable ATA; serial checkpoints retained |
| Desktop and input | PS/2 key and pointer events | Desktop renders and responds to both devices | Passed; desktop screenshots and serial capability lines retained |
| Start Menu | PS/2 Start key, then navigate/select | Menu opens and a real registered item launches | Passed; File Explorer launched from the actual menu and visible in the guest |
| File Explorer | Select its actual Start Menu/taskbar entry | Explorer opens as a real window with expected content | Passed; real `/` window with `/system` visible, and Files entry activated after pin restore |
| Taskbar activation | Select running Explorer taskbar button | Correct window receives focus | Passed for minimize/restore and pinned Files activation; broad focus transition coverage remains limited |
| Minimize/restore/close | Use real window controls and taskbar button | Same window minimizes, restores, then closes; stale entry disappears | Minimize/restore passed; close cleanup and stale-entry removal remain unqualified |
| Context menu and pins | Right-click real taskbar entry; pin/unpin through UI | Menu actions affect the selected application only | Menu display and real Explorer pin save/restore passed; unpin was not tested |
| Hover preview | Hover over real running taskbar entry; enter and select preview | Title/icon or supported title-state fallback remains accessible and activates same window | Title/state fallback showed `File Explorer` / `Active`; popup pointer transfer and preview selection remain partial |
| Docking, size, labels, auto-hide | Right-click empty taskbar space to open the native taskbar settings menu, then select its position, size, auto-hide, or presentation rows | Geometry, labels, edge reveal, popup transfer, dismissal, and restore match selection; each row persists through `/desktop.cfg` | Top docking, expanded size, auto-hide off, labels off, previews and activity on saved/restored. Top-edge reveal was observed; popup transfer/dismissal was not fully qualified |
| Grouping | Launch multiple windows with same canonical application identity | Grouped entry/member selection and close cleanup work | Not qualified; no genuine multi-window same-app scenario was available |
| Overflow | Use genuine supported running-app population | Overflow control, paging, selection and restoration work | Not qualified; no production overflow population was available |
| Copy progress | Run a safe real Explorer copy | Real progress appears under correct taskbar owner and clears at completion | Not qualified; no real copy/progress operation was run |
| Settings and pin save | With the disposable FAT32 image as `/`, change docking, size, auto-hide, another presentation option, and a pin through UI; save | `/desktop.cfg` and `/taskbar.pin` report successful writes | Passed across Boot A/B; serial save logs and read-only post-exit file/FAT inspection retained |
| Clean shutdown | Use native supported shutdown | VFS sync and required ATA flush succeed, ACPI S5 is issued, QEMU exits, QAC releases | Passed on all writable boots: desktop confirmation in A; native shell shutdown in B/C; sync/S5, QEMU exit 0 and QAC release confirmed |
| Cold restart | Reattach same disposable disk to a fresh QAC run | Settings and pins restore once; unrelated settings remain; second save also survives if time allows | Passed across Boots A→B→C; settings and exactly one pin restored; FAT mirrors matched and no orphan clusters found |

The hosted regression fixture is `out/taskbar/RECOVERY2R1/validation/fat-persistence/disposable-fat32.img` (128 MiB, label `GXOSDATA`, creation hash `6ad9b4a76b6f98ee108370b483f50449e358ef4ff56609fb165e7cd472813a90`). It remains detached from QEMU. The separate native guest image at `out/taskbar/RECOVERY2R1/validation/fat-persistence/native-guest-20261127/guideXOS-native-data.img` (128 MiB, FAT32, label `GXOSDATA`, marker `GXOSDAT.ID`, `APPDATA/`, and the final release `/Apps` tree) was attached only for the three Recovery2R1 FAT32 boots above. Its initial pre-QEMU SHA-256 after staging `/Apps` was `a64e70081ff23da4f28adf932ff3e3e9c2be3c4b9a9452c84d9bce151b23866d`; the final post-Boot-C hash is recorded above. All 11 staged application files were copied from the final ESP, with their individual hashes in the adjacent staging output. `/DESKTOP.CFG` and `/TASKBAR.PIN` were created or changed only through the native guest UI and production save path.

Read-only preflight verified the FAT32 boot signature, 512-byte sectors, one sector per cluster, 32 reserved sectors, two identical FAT mirrors (2,033 sectors each), 262,144 total sectors, expected marker content, and exact `/Apps` file hashes. It also confirmed that neither `/desktop.cfg` nor `/taskbar.pin` was pre-seeded. The ISO-only run used the installed x86_64 EDK2 code image (SHA-256 `33090cc07675baa5190d9f1e84bf5176b33bcbfa9bacac522961150cdb6dbb2a`) and task-owned disposable OVMF variables (initial SHA-256 `5d2ac383371b408398accee7ec27c8c09ea5b74a0de0ceea6513388b15be5d1e`). Fresh variable-store copies were staged for the writable-volume boots. The QEMU executable SHA-256 is `a930e028f93d0fa47e4d58bdad2432f7466dc2b6af0ae376f7f7a298ffdd02`; the exact firmware and QEMU paths/hashes are staged in the native guest evidence directory. The ISO-only and FAT32 boot runs each have separate preflight, serial and debug records. An earlier QAC launch guard prevented a QEMU start when another mainline reservation appeared; it did not start an unmanaged guest.

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

The hosted FAT/VFS and ATA regressions support storage contracts but do not, on their own, prove native write durability, FAT mirror consistency after guest use, ACPI S5, or cold-restart settings restoration. The native FAT32 boots and read-only host inspections above provide that guest evidence for this image and these tested operations. The historical intermittent ATA timeout is not claimed permanently resolved; it did not recur in the 80 focused ATA checks or the bounded native shutdown flushes.


## 8. Current mainline state and compatibility

The actual main checkout was inspected read-only and left unchanged. It is on `main` at `02422a86d44319e6e6473342af7e8b1e29c55ff9`, equal to `origin/main` with 0 ahead and 0 behind; `git status --porcelain=v2` reports a clean tree. A fresh read-only GitHub `ls-remote` check returned the same main SHA. The earlier `TASKBAR_RECOVERY2_MAIN_WORKTREE_SNAPSHOT_20261126` remains preserved. Its listed local changes have since been committed in the mainline workflow; they were not copied, staged, or otherwise altered in this candidate task.

The two lines share base `aad1bce8a8e42890e9bfd49a2f397f04e19f3eef`. Current main has two commits after that base (`5f872051` and `02422a86`); the candidate has 24. These are diverged histories. The candidate repository's local `origin/main` ref is stale at the common base, so the verified main checkout and read-only remote SHA above are the authority for current main. No candidate refresh, rebase, main checkout, or main mutation was performed.

Commit `02422a86` records the mainline RAMDISK/page-table qualification work across seven files: `guideXOSBootLoader/paging.cpp`, `kernel/core/amd64_memory_foundation.cpp`, `kernel/core/include/kernel/boot_page_table_validation.h`, `scripts/run-uefi-paging-alias-test.ps1`, `tests/amd64_memory_foundation_test.cpp`, `tests/uefi_paging_alias_test.cpp`, and `docs/RELEASE_S22R28R3R3_CORRECTED_RAMDISK_GUEST_ACCEPTANCE.md`. The earlier mainline `5f872051` also changes shared bootloader, kernel entry, `kernel/Makefile`, `kernel/core/main.cpp`, and `kernel/core/desktop.cpp` paths.

| Classification | Evidence and next handling |
|---|---|
| **Semantic overlap — RAMDISK alias** | The candidate exports `GUIDEXOS_RAMDISK_ALIAS_BASE` (`0x0000400000000000`) from `guideXOSBootLoader/guidexOSBootInfo.h`; its loader maps there and the kernel reads/fingerprints the alias while retaining physical `BootInfo.RamdiskBase`. Main uses the same address as `guideXOS::ramdisk_diagnostic::kKernelRamdiskVirtualBase` in `kernel/core/include/kernel/ramdisk_integrity_diagnostic.h` and validates the mapping in its diagnostic path. The values and physical-versus-virtual contract agree, but the duplicate definitions and diagnostic integration need one deliberate reconciliation. |
| **Semantic overlap — page-table bound and audit** | Both loaders permit 1,024 tracked tables. The candidate shares `GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES` between loader and kernel and tests a real alias mapping with a 525-table tree. Main has a loader-local 1,024 limit and `boot_page_table_validation.h` with a bounded kernel tree walk, reason reporting, and synthetic 525-tree regression. The behavior aligns; ownership of the shared limit and validator API should be reconciled together. |
| **Textual and semantic overlap — kernel/desktop build paths** | The two histories both touch the bootloader entry/paging, `kernel/core/main.cpp`, `kernel/core/desktop.cpp`, `kernel/core/amd64_memory_foundation.cpp`, and `kernel/Makefile`. The taskbar model/settings and release RAMDISK diagnostics have separate purposes but share initialization, package discovery, and build ordering. Integration3 needs a three-way source review against base `aad1bce8` and current main `02422a86`; a mechanical cherry-pick or wholesale file replacement is not safe. |
| **Uncommitted mainline dependency** | None at inspection. The previously outstanding main files are now committed and the main worktree is clean/equal to its remote. Their workflow ownership is respected; future integration should coordinate with that owner before reconciling those commits. |
| **Already compatible at a contract level** | Both lines preserve physical `BootInfo.RamdiskBase` as the RAMDISK allocation/ownership value and use a supervisor virtual alias for kernel access. The qualified candidate mounts writable `/` from ATA and read-only `/system` from the release RAMDISK; main's report records its own corrected-mapping acceptance. These shared contracts do not establish source-level integration by themselves. |
| **Separate future integration** | No NATIVE_DB_STORAGE source was changed and no direct taskbar dependency was found. FAT/VFS shrink, the native database file bridge, ATA serialization ownership, and database durability ordering remain separate integration work. The page-table ownership validation contract is a platform dependency for any later NDB execution. |

## 9. Acceptance gate matrix

| Gate | Status | Evidence / reason |
|---|---|---|
| R2R1-G1 Git baseline, integrity, remote synchronization checked | **PARTIAL** | Baseline, integrity, upstream, and remote refs were checked read-only. GitHub remains at `cc09851a`; the local report commits are safely ahead but unsynchronized because both SSH and HTTPS authentication paths lack write permission. |
| R2R1-G2 Reconstructed candidate builds | **PASS** | Final production AMD64 UEFI/profile build, symbol audit, packages and ISO passed from the verified source. |
| R2R1-G3 QAC-managed native UEFI boot | **PASS** | Final ISO-only and three writable FAT32 boots ran under exclusive QAC reservations; all managed children exited and QAC released the reservations. Writable-boot guest shutdown results are recorded under G10. |
| R2R1-G4 Root and `/system` mounts | **PASS** | ISO-only boot mounted read-only release `/` plus `/system`; FAT32 boots mounted the intended writable ATA root with `/system` available. |
| R2R1-G5 Start Menu and File Explorer activation | **PASS** | Genuine PS/2 input opened Start and launched the real Explorer window; the restored pin also activated Explorer after cold restart. |
| R2R1-G6 Native taskbar controls and context menus | **PARTIAL** | Running/pinned distinction, activation, minimize/restore, context-menu display, and a real pin save/load passed. Unpin, close cleanup and exhaustive focus transitions remain unqualified. |
| R2R1-G7 Hover previews and auto-hide interaction | **PARTIAL** | Title/state preview fallback, top docking, expanded size, auto-hide setting changes and top-edge reveal were observed. Full preview pointer transfer, popup dismissal and every restore transition remain unqualified. |
| R2R1-G8 AppID, grouping, overflow, progress | **PARTIAL** | Real native ELF app ID `com.guidexos.pacman` and launch passed. Genuine multi-window grouping, overflow pagination and Explorer copy-progress behavior were not demonstrated. |
| R2R1-G9 Native UI configuration changes saved | **PASS** | Native UI saved top docking, expanded size, auto-hide off, labels off, previews on and activity on to `/DESKTOP.CFG`; Boot B restored the values. |
| R2R1-G10 VFS sync and clean ACPI S5 shutdown | **PASS** | Serial showed storage sync completion and ACPI S5 on Boot A/B/C. QEMU exited 0 and QAC released each reservation; Boot A used desktop confirmation, Boots B/C used native shell shutdown. |
| R2R1-G11 Cold-restart settings and pin persistence | **PASS** | Boot B restored Boot A settings and saved the Explorer pin; Boot C loaded that pin once and activated Explorer. Read-only FAT inspection confirmed both files. |
| R2R1-G12 FAT/ATA integrity and regressions | **PASS** | FAT mirrors matched and there were zero orphan clusters after all guest boots; FAT/VFS (82) and ATA flush (80) focused regressions passed. No real Explorer copy operation was run, and the historical timeout is not claimed permanently resolved. |
| R2R1-G13 Current mainline work preserved and assessed | **PASS** | Main is clean and equal to GitHub at `02422a86`; its committed changes and overlaps were reviewed read-only; the earlier snapshot remains preserved. |
| R2R1-G14 Hosted/native builds and relevant regressions | **PASS** | Final production AMD64 and all applicable focused regressions passed. No separate diagnostic AMD64 release target exists in the build script; none is claimed. |
| R2R1-G15 QAC cleanup and Git delivery | **PARTIAL** | All Recovery2R1 reservations released and the final QAC census was empty. Two report commits remain ahead of GitHub; SSH public-key auth failed and HTTPS returned 403 for account `guideX224`. No main push occurred. |
| R2R1-G16 Controlled INTEGRATION3 readiness | **PASS** | Essential native gates pass and the remaining partial features are identified. Recommend controlled Integration3 preparation for three-way reconciliation; no merge into main is performed here. |

## 10. QAC and artifact cleanup

Recovery2R1's final ISO reservation `e1b98acf06df4c1e8a94829ad302e081`, Boot A `44440fc85a1342fbbc49f67682c67c29`, Boot B `4957c3a4d7914fdc8a9eae6b1364127b`, and Boot C `8b08ab34d4f9498282a4c8aa0ab3ce1d` were each inspected as **Released** after their managed QEMU child exited. The post-Boot-C coordinator census was Online with zero QEMU processes, zero foreign processes, zero managed sessions, zero queued or blocked requests, and zero reservations. No foreign or unmanaged QEMU was started or interrupted.

Generated build/test evidence, QAC serial logs, debug logs, FAT inspection output, and screenshots remain under ignored `out/taskbar/RECOVERY2R1/` paths. Root-level screenshots created during the writable guest sessions were moved into the matching Boot A/B/C evidence folders after verifying their PNG signatures; captured P6 frames were encoded as PNG. The existing review ISO, recovery bundle, prior reports, and disk artifacts were preserved. The disposable native image remains available with its final SHA-256 for repeatable inspection; no cleanup deleted it.

## 11. Readiness decision and exact next action

**Outcome A — `TASKBAR_RECOVERY2R1_NATIVE_REQUALIFIED`.** The reconstructed candidate has demonstrated native AMD64 UEFI boot, root and `/system` mounts, Start Menu and real application activation, essential enhanced-taskbar interaction, native settings/pin saves, VFS synchronization, ACPI S5, and restoration after cold restart. Hosted storage and subsystem regressions and the final production build passed. The guest disk passed read-only FAT mirror, reachability and content inspection. Mainline changes are preserved, current main is synchronized and clean, and the source/API overlaps are understood. No significant unresolved taskbar integration defect was reproduced.

The narrow remaining delivery blocker is GitHub write access for the two local report commits; the production candidate source and native evidence are already present on the synchronized branch through `cc09851a`. After those documentation commits are pushed safely, proceed with **controlled TASKBAR-INTEGRATION3 preparation**: use common base `aad1bce8a8e42890e9bfd49a2f397f04e19f3eef`, candidate branch `integration/taskbar-recovery2`, and current main `02422a86d44319e6e6473342af7e8b1e29c55ff9` for a three-way source/API reconciliation. Reconcile the duplicate RAMDISK alias constants and loader/kernel page-table bound/validation APIs, then review shared bootloader, entry, desktop and Makefile changes with the mainline owner. Keep grouping, overflow, real copy progress, unpin/close cleanup and complete preview-popup interaction as explicit partial gates for follow-up qualification. Do not merge into actual `main` as part of this milestone; no main files or refs were changed here.

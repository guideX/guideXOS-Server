# TASKBAR-INTEGRATION3 — Mainline Reconciliation

**Outcome:** `TASKBAR_INTEGRATION3_PARTIAL`

**Candidate branch:** `integration/taskbar-integration3`

**Candidate source commit:** `c334d04f3c2dd7ccc57807ee513741b268218327`
**Report prepared:** 2026-11-27

The integrated source builds and its hosted regression suite passes. This is not a native-qualified candidate: QAC-managed boot, interactive taskbar use, cold-boot settings persistence, FAT integrity after guest writes, and ACPI S5 remain unverified on the combined source. Do not recommend a final merge to `main` until those gates are completed.

## 1. Starting Git state and synchronization

The isolated candidate was created at the verified GitHub `main` commit. HTTPS `git ls-remote` confirmed:

| Ref | Verified commit |
| --- | --- |
| `main` | `02422a86d44319e6e6473342af7e8b1e29c55ff9` |
| `integration/taskbar-recovery2` | `15e16d8c15038717741eb8135eb897a2b792acf0` |
| `integration/taskbar-integration3` before creation | absent |

The recovery worktree was on `integration/taskbar-recovery2` at `15e16d8c…`, with no tracked modifications. The protected checkout `E:\dev\guideXOSServer` was on `main` at `02422a86…`. At final inspection its HEAD was unchanged, and it had uncommitted AMD64 exception-recovery changes in `kernel/Makefile`, `kernel/arch/amd64/protected_task.asm`, `kernel/core/include/kernel/native_elf_fault.h`, `kernel/core/include/kernel/protected_task.h`, `kernel/core/interrupts.cpp`, `kernel/core/main.cpp`, `kernel/core/native_elf_baremetal.cpp`, `kernel/core/protected_task.cpp`, and related new tests/header files. These changes were left untouched. The ignored build and evidence artifacts in both source worktrees were preserved.

Both relevant repositories passed `git fsck --full` without integrity errors (dangling objects were reported). SSH fetch was unavailable (`Permission denied (publickey)`); ref verification and fetch of the recovery branch used HTTPS. The old `origin/main` in the recovery clone was stale, so it was not used as the current mainline reference. The documented Recovery2 branch was confirmed on GitHub. The candidate branch has not yet been pushed; see §18.

## 2. Ancestry and merge-base analysis

The actual merge base of the recovered branch and current main is `aad1bce8a8e42890e9bfd49a2f397f04e19f3eef`. Mainline then added `5f8720519a5b4cce735525016da9853640fd361f` and `02422a86…`. Recovery2 contains a reconstruction merge `25239b9d756a603f0b7328d655b767ca63e5b436`, whose parents are the base and surviving historical taskbar tip `63034c507093a9f2d34c503312893245db98839b`.

The integration was recorded as a real, non-fast-forward merge, not a squash. Merge commit `c334d04f3c2dd7ccc57807ee513741b268218327` has parents in this order:

1. Main: `02422a86d44319e6e6473342af7e8b1e29c55ff9`
2. Recovery2: `15e16d8c15038717741eb8135eb897a2b792acf0`

The Recovery2 reconstruction does not recreate the unavailable original qualified-candidate source history. The archived Recovery2R1 ISO and its guest results remain evidence for that original source only; they are not proof of this merged source or this ISO.

## 3. Change inventory and reconciliation

The three-way review found taskbar and supporting changes in `desktop.cpp`, `kernel_compositor.*`, `compositor.*`, `desktop_config.h`, `right_click_menu.*`, `taskbar_*`, App Model sources/headers, native ELF launch paths, FAT/VFS/ATA sources, and UEFI paging/BootInfo code. The Recovery2 branch also adds its taskbar regression scripts, focused alias and persistence tests, and Recovery2 history documents under `docs/taskbar/`. Current main independently had evolved RAMDISK diagnostics and page-table validation. No `NATIVE_DB_STORAGE` work was imported.

Text conflicts were resolved and semantically reviewed in:

| File | Resolution |
| --- | --- |
| `guideXOSBootLoader/main.cpp` | Kept current mainline initialization/diagnostic flow and added Recovery2 RAMDISK alias setup using the shared BootInfo contract. |
| `guideXOSBootLoader/paging.cpp` | Retained mainline paging validation and added guarded mapping behavior plus the shared page-table limit. |
| `kernel/core/amd64_memory_foundation.cpp` | Kept mainline page-table tree validation and removed the duplicate Recovery2 limit in favor of the shared constant. |
| `kernel/core/main.cpp` | Combined mainline RAMDISK integrity/page-table diagnostics with Recovery2 desktop/taskbar initialization. |

`kernel/Makefile` and `kernel/core/desktop.cpp` auto-merged; their taskbar changes and mainline diagnostic hooks were reviewed in the integrated source. Mainline diagnostic helpers and reason reporting were retained. The application model propagates the existing AppID through launch/instance/window/taskbar activation and cleanup; the v0.3 bundled-application model remains in place. No independent download/install model or second settings/pinning path was introduced.

## 4. RAMDISK alias and page-table decisions

`BootInfo.RamdiskBase` and size continue to describe physical ownership. The loader maps the RAMDISK once at `GUIDEXOS_RAMDISK_ALIAS_BASE` (`0x0000400000000000`); kernel consumers use that high virtual alias. Mainline's `ramdisk_diagnostic::kKernelRamdiskVirtualBase` now derives from the shared BootInfo constant, preventing two independent alias definitions. The maximum boot page-table allocation is likewise shared as `GUIDEXOS_MAX_BOOT_PAGE_TABLE_PAGES` (1024) between loader and kernel validation.

The newer mainline tree validator, range checks, diagnostics, and fail-closed error reporting were preserved. `MapRange` now checks 48-bit canonical virtual endpoints and that the range stays in one canonical half, arithmetic overflow, physical-address bounds, matching virtual/physical page offsets, and valid root/system-table inputs. It preflights the whole range before writing entries and rejects overlap backed by a different physical range. Large-page parents fail closed. The RAMDISK alias call uses the default no-replacement policy; an explicit replacement option is used only for the known kernel virtual remap that intentionally overlaps the low identity mapping.

The existing access contract is retained: present, writable, supervisor-only, executable mappings (P=1, W=1, U=0, NX=0). The implementation does not claim stronger permissions than the code establishes. Hosted tests exercised invalid canonical/overflow/offset/conflicting mappings and verified rejection before mutation. The alias regression mapped 16,385 pages, including a partial final page; the observed page-table tree used 525 pages, below the shared 1,024-page cap. These results validate hosted mapping logic, not actual hardware boot or translation behavior.

## 5. Storage and shutdown compatibility

The integrated tree retains the Recovery2 FAT allocation rollback and directory-growth paths, ATA PIO flush capability/error handling, and VFS synchronization ordering. Hosted persistence and FAT32 growth tests passed. This does not requalify write durability on an actual guest disk: taskbar settings and pin changes were not made in a guest, cold-booted, or checked with host-side FAT verification. The historical intermittent ATA flush timeout remains an observed issue and is not declared fixed by hosted tests.

## 6. Build and artifact identity

The canonical build ran from a clean tracked worktree at source commit `c334d04f3c2dd7ccc57807ee513741b268218327` using `ServerV03PacManOnly`, version `0.3.0`, AMD64. Build ID: `545ed247-9f12-4973-8df3-0cf11da8178e`. Kernel, RAMDISK, and application-package stages passed. The kernel symbol audit reported 52 undefined-symbol rows representing 22 distinct `.symtab` entries, with no relocations/imports. The ISO structure was verified.

| Artifact | SHA-256 |
| --- | --- |
| ISO (90,245,120 bytes) | `139cce7c95517c97ff92dfb310440a8ce1ca24a76348202d809ea95d332ea0bf` |
| Kernel | `55342060360b18cfedd092edeabdb90f1c6e9edcc4893ad2510e8df93bf931c7` |
| `BootX64.EFI` | `0d14b5a166fcc788bf681da965257f7923e06c7f0547c757100adc795fc8d68a` |
| RAMDISK | `ead5105aa131dee575e0021fac1650a28330df422bd211ca6baccdd267179f05` |
| PacMan ELF | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` |
| Package manifest | `3f3592e6c76053e323f16d48073e98c67d7f5702b8e47f0b507d6bfb3d126449` |

ISO: `out/release-profile/545ed247-9f12-4973-8df3-0cf11da8178e/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`. It contains the intended single `ServerV03PacManOnly` PacMan package plus system assets. `run-release-ramdisk-mount-test.ps1` passed 9/9 on this exact image, including root read-only block ownership, `/Apps`, and PacMan identity. The canonical source pin for PacMan was verified as `8fc2aedd603ddc57e492bdc02c99442bb165109b`; no stale kernel binary was used.

## 7. Hosted regression matrix

All listed hosted regressions exited successfully. Counts are the test suite's reported checks/assertions where available; “pass” is used where the runner did not report a total.

| Area | Result |
| --- | --- |
| Taskbar application model | Pass; 119 `check()` assertions in the test source, all reported passing |
| App Model identity | 25/25 |
| App Model file activation | 60/60 |
| ATA PIO flush | 80 checks, 0 failures |
| FAT/VFS persistence and FAT32 growth | 82 checks, 0 failures; disposable copy used for hosted data-volume test |
| AMD64 memory foundation | Pass; hosted only, no hardware CPL3/page-fault claim |
| RAMDISK diagnostic helper | 17 test cases passed |
| UEFI paging alias validation | Pass; invalid ranges/overlaps rejected before mutation |
| UEFI RAMDISK alias regression | Pass; 16,385 pages mapped, 525 page-table pages observed |
| Settings native-runtime logic | 908/908 |
| VFS mount-table integrity | 19/19 |
| Hosted VFS read | 4/4 |
| User data-volume policy | 9 passed |
| FAT mount diagnostics | Enabled: 15/15; disabled: 7/7 |
| Compositor pointer, window UI, resize notification | Pass |
| Release RAMDISK mount/profile check | 9/9 |

`git diff --check` passed for the integrated source changes. The taskbar regression count above is based on the 119 assertion calls in the test source; Recovery2R1's separately reported 118/118 applies only to its historical source.

## 8. Native guest gates and QAC cleanup

Native qualification was **not run**. The expected QAC CLI at `E:\dev\guideXOS_QAC\src\GuideXOS.QAC.Cli\bin\Debug\net10.0-windows\GuideXOS.QAC.Cli.exe` is absent, `GUIDEXOS_QAC_CLI` is unset, and `qac.exe` is not on PATH. No QAC reservation or session ID exists. No QEMU process was present at final inspection, and no unmanaged emulator was started. Without the QAC coordinator, reservation admission and final coordinator inventory could not be inspected.

| Guest gate | Status |
| --- | --- |
| UEFI boot, kernel init, actual RAMDISK mapping and mounts | Not run |
| Desktop, keyboard/mouse, Start Menu, real File Explorer | Not run |
| Taskbar activation, minimize/restore, menus, previews, docking/autohide | Not run |
| Writable FAT32 settings/pin update and cold-boot persistence | Not run |
| Host-side FAT integrity after guest writes | Not run |
| Clean guest sync and ACPI S5 | Not run |
| QAC-owned resource release and coordinator inventory | No task-owned resources were created; coordinator inventory unavailable |

Same-AppID grouping, overflow/pagination, real File Explorer copy progress, unpin/close cleanup, and complete preview-pointer interaction also remain unqualified on the integrated guest. No result is inferred from RECOVERY2R1 for these newly merged binaries.

## 9. Regression assessment and decision

No hosted regression was observed in the tested taskbar/App Model, memory helper, FAT/VFS, ATA, settings, compositor, or release packaging paths. Actual boot stability, root and `/system` mounts under firmware, real input and taskbar interaction, guest write durability, ACPI S5, and post-write FAT integrity remain unknown for this merge. These are material gates, so this branch is **not ready for the final mainline merge**.

**Next milestone:** provide the QAC CLI/coordinator environment, reserve exclusive admission, and qualify this exact ISO and source merge commit. Perform ISO-only boot and interaction checks, then writable-FAT32 settings/pin persistence across a cold boot, host-side FAT verification, and clean S5. Record coordinator IDs and cleanup inventory. Revisit the remaining partial taskbar features and decide merge readiness from that evidence.

## 10. Candidate and delivery state

The source integration commit is `c334d04f3c2dd7ccc57807ee513741b268218327`. This report is a separate documentation commit on `integration/taskbar-integration3`; the build and ISO above identify the source merge commit, not the documentation-only tip. Immediately before push, the GitHub candidate ref was absent, while GitHub `main` and Recovery2 still matched the commits listed in §1. A normal HTTPS push was attempted and failed before authentication because Git could not prompt for a username; the GitHub CLI is not installed. The exact Git error was:

```text
bash: line 1: /dev/tty: No such device or address
error: failed to execute prompt script (exit code 1)
fatal: could not read Username for 'https://github.com': No such file or directory
```

The candidate source merge and this report commit therefore remain unpushed, and the GitHub candidate ref remains absent on the final read-only check. Synchronization is incomplete. No force push was attempted and no changes were made to GitHub `main` or the protected checkout.

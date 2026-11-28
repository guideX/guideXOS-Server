# TASKBAR-RECOVERY1 — Storage Relocation and Integration Recovery

**Date:** 2026-11-26
**Disposition:** `TASKBAR_RECOVERY1_BLOCKED_CANDIDATE_HISTORY`
**Working directory:** `E:\dev\guideXOSServer_TASKBAR_IMPROVEMENTS`

## 1. Repository health

The E: directory is a valid, non-shallow, standalone clone of `git@github.com:guideX/guideXOS-Server.git`. It is not the relocated D: worktree and is not a copy of the integration candidate. Its reflog records a clone on 2026-11-24.

Before this report was added, the working tree had no modified or untracked files. `git fsck --full` completed with exit code 0. No missing or corrupt objects were reported in this clone.

## 2. Git directory configuration and worktrees

- Git directory: `.git`
- Common directory: `.git`
- `core.bare=false`; no `core.worktree`, `commondir`, or linked-worktree `.git` file is configured.
- `origin`: `git@github.com:guideX/guideXOS-Server.git`
- `git worktree list` contains only this E: checkout. No D: worktree reference was found, so no worktree repair or pruning was needed.
- `D:\dev` and the prior candidate path `D:\dev\guideXOSServer_TASKBAR_INTEGRATION1_CANDIDATE_20261122` are unavailable. No writes were made to D:.

## 3. Branches and known commits

The checked-out local branch is `TASKBAR_IMPROVEMENTS` at `8dce98cb69d295bf825ec7fc6a74e74ea062333e` (`TASKBAR6R6: record remaining taskbar pin gate`). It tracks `origin/TASKBAR_IMPROVEMENTS` at the same commit. The directory name did not determine this branch; Git metadata did.

The only local branch is `TASKBAR_IMPROVEMENTS`. Available remote-tracking refs are:

| Ref | Head |
| --- | --- |
| `origin/AARCH64_SUPPORT` | `d22400cd` |
| `origin/CPU_IMPROVEMENTS` | `b5c840bf` |
| `origin/DEVELOPER_STUDIO_DEBUGGING` | `e16a0026` |
| `origin/DEVELOPER_STUDIO_TOOLS` | `460a3864` |
| `origin/DISK_MANAGER_IMPROVEMENTS` | `723046ff` |
| `origin/FONT_SUPPORT` | `341a4baa` |
| `origin/HEAD` → `origin/main` | `d1b2b2ce` |
| `origin/IA64_SUPPORT` | `fbc767ca` |
| `origin/INPUT_RELIABILITY` | `92512f7e` |
| `origin/MODERN_INPUT` | `843ab3f8` |
| `origin/NATIVE_DB_STORAGE` | `f29850c9` |
| `origin/NAVIGATOR_GENERAL_IMPROVEMENTS` | `a938771f` |
| `origin/NAVIGATOR_JAVASCRIPT_SUPPORT` | `86ee961d` |
| `origin/REXX_SUPPORT` | `911abeea` |
| `origin/SECURITY_SETTINGS` | `cc8d9d0d` |
| `origin/STELLAR_NAVIGATOR` | `8251ff98` |
| `origin/TASKBAR_IMPROVEMENTS` | `8dce98cb` |
| `origin/backup/main-pre-v02-merge` | `ce515df8` |
| `origin/backup/v0.2-pre-main-sync` | `aaee8a77` |
| `origin/main` | `d1b2b2ce` |
| `origin/v0.2` | `7141dfc5` |
| `origin/v0.3_SCI_FI_THEME` | `4a027908` |
| `origin/v0.5_DEVELOPER_STUDIO` | `801b4763` |
| `origin/v1.1_DOTNET_SUPPORT` | `97dc7b0d` |

The `v0.2` tag is also present. There is no local or remote-tracking `integration/taskbar-prep` ref.

The previously verified baseline `d1b2b2ce9bf7df71e21abbfcead411ce5b12d4c9` is present. The historical taskbar source `0afe07d57353f4ce16a452c00aff1e612b4bc0c0` and candidate `d3ef13caefcdfc2738b71d568badfb833b3f1b4b` are absent from this object database. Prior Codex task records additionally identify an earlier integration merge `457fc283651cc1195ab7878b03b075098cda578b`, initial candidate `72b40c9f92dd944da8046bc222ca4eda337bf5c1`, and later ISO build source `91eba69246cb9c0b7a7be3f0ee9898e46ee014f7`; those objects are absent as well. These historical records do not establish ancestry or tree equality, so no ancestry claim is made.

The other visible E: Server checkouts (`guideXOSServer` at main and `guideXOSServer_CPU_IMPROVEMENTS`) also lack the taskbar and integration objects. The configured SSH origin rejects this shell with `Permission denied (publickey)`. The connected GitHub API lists the published branches above; it has no `integration/taskbar-prep` branch, and direct commit lookups for `d3ef13ca…` and `91eba692…` return “No commit found.” The all-state pull-request listing was empty. No authorized remote copy of the missing commits was found.

## 4. Backup and preserved files

Before making repository changes, a full-ref bundle of the available E: clone was created and verified:

- `E:\dev\TASKBAR_RECOVERY1_BACKUP_20261126\repository-all-refs.bundle`
- Size: 721,571,235 bytes
- `git bundle verify`: successful; the bundle reports complete history for its 28 captured refs.
- E: had approximately 5.67 TB free when the bundle was created.

This bundle protects the current clone’s refs and objects; it does **not** contain the missing candidate history. There were no untracked source files or local source edits to preserve. Existing generated files and the review ISO were left untouched. No cleanup, reset, reinitialization, push, or merge was performed.

## 5. Build-environment findings

The reported `/usr/bin/sh` error matches the checked-in Windows recipe shape: `kernel/makefile` selects `if not exist ... mkdir ...` and `if exist ... rmdir ...` when `OS=Windows_NT`, but does not pin the recipe shell. If GNU Make selects Git for Windows `sh.exe`, those CMD `if` statements are parsed as incomplete POSIX shell `if` commands, producing the repeated “unexpected end of file” error at directory-creation recipes such as the reported Mbed TLS object target.

On this machine, `mingw32-make` is GNU Make 4.4.1 and `gcc`/`g++` are MinGW-W64 15.2.0 targeting `x86_64-w64-mingw32`, all under `C:\mingw64\bin`. `sh.exe` resolves to `C:\Program Files\Git\usr\bin\sh.exe`; `SHELL` and `MSYSTEM` are unset in this PowerShell process. `build-uefi.ps1` accepts `mingw32-make` from `PATH` and does not explicitly select or normalize the shell. `process_environment.ps1` normalizes duplicate case-insensitive `Path` entries but does not normalize `SHELL` or shell order.

The archived TASKBAR-ISO1 completion record says the integration candidate received a shell-aware filesystem-command fix, its regression passed under both Git `sh.exe` and `cmd.exe`, and the production AMD64 kernel then built. The same record identifies `91eba69246cb9c0b7a7be3f0ee9898e46ee014f7` as that ISO’s source. The fix and regression are not present in this clone, and their commit object is unavailable, so they could not be verified or ported here.

This clone cannot currently complete a kernel build: `third_party/mbedtls` is absent, and a read-only Make invocation stops at `makefile:77` because the Mbed TLS profile/layout is incomplete. No dependency bootstrap, kernel build, or ISO rebuild was attempted. Mbed TLS was not disabled and no integrity check was bypassed.

## 6. Review ISO and prior qualification evidence

An existing review ISO is present at:

`E:\CodexArtifacts\TaskbarReview\guideXOS-Taskbar-Review-AMD64.iso`

- Size: 90,245,120 bytes
- SHA-256: `b5a638f134c949ddede9658596009df4c716112f4d200fde0ca3f4c86ada2906`
- The checksum matches the archived TASKBAR-ISO1 completion record.
- Read-only structure inspection found ISO 9660 descriptors and an El Torito EFI platform entry (`platform_id=0xEF`, bootable entry `0x88`).
- The archived record identifies profile `ServerV03PacManOnly` and source `91eba69246cb9c0b7a7be3f0ee9898e46ee014f7`. It describes this as an internal PacMan-only candidate, not the signed-off public v0.3 preview.

The ISO was not rebuilt in this recovery. The missing commit objects prevent independent proof that its source tree equals the prompt-referenced `d3ef13ca…` candidate. It remains a previously built review artifact, not a substitute integration branch.

The archived QAC-managed QEMU run reports successful UEFI boot, kernel startup, read-only `wallimg` root, `/system` mount, wallpaper, Start Menu, File Explorer, minimize/restore preview, and auto-hide/reveal. It also reports that the Start Menu shutdown confirmation did not appear and graceful ACPI powerdown did not stop the guest; QAC’s owner stopped its managed child. Context-menu behavior is not recorded as verified. That run was QEMU-only; VirtualBox qualification remains pending.

The first QAC state-file snapshot showed an active acceptance reservation (`dc3bd0ce6d984ebe9245f066da65fa63`) owned by process 12128 with live QEMU child 21956. No QAC request or guest was started by this recovery. At the final recheck, the reservation list was empty and no `qemu-system` process was running. This is a point-in-time availability snapshot; the previous archived QAC result is not a new qualification run for this recovery.

The previous report’s VirtualBox starting settings were: **Other/Unknown (64-bit)**, 2 CPUs, 2 GiB RAM, EFI enabled, optical ISO only with no hard disk, VBoxVGA, 128 MiB video memory, 3D acceleration off, and PS/2 keyboard and mouse. Avoid USB Tablet input. These are setup suggestions, not evidence of VirtualBox compatibility. The ISO-only `wallimg` boot is read-only, so taskbar settings and user files do not persist between boots.

## 7. Worktree and ISO status

No E: integration worktree was created. Doing so from the older `TASKBAR_IMPROVEMENTS` branch would silently replace the missing qualified candidate, which is specifically prohibited. The exact blocker is the absence of a healthy copy of the candidate branch and its Git objects (`integration/taskbar-prep`, including the recorded `d3ef13ca…` candidate and subsequent ISO source history). The D: candidate location is unavailable and the configured origin does not contain those objects.

The review ISO itself already exists and its byte-level checksum and UEFI boot catalog were verified. No new ISO was produced, and no new guest qualification was run. The primary blocker to a recovered, reproducible candidate and a fresh candidate-bound ISO is the missing source history. QAC is also temporarily occupied for any new guest run.

## 8. Changes and final worktree state

This report is the only change in this recovery. It is committed locally on the existing `TASKBAR_IMPROVEMENTS` branch. No source files, other worktrees, mainline checkout, frozen release artifacts, or NATIVE_DB_STORAGE changes were modified. Nothing was pushed or merged.

## 9. Exact next steps toward INTEGRATION3

1. Recover a healthy copy of the prior candidate repository or a Git bundle containing the `integration/taskbar-prep` ref and its full ancestry. If the old storage is ever inspected, mount it read-only and copy to healthy E: storage; do not write to it. The known IDs above are checkpoints, not permission to recreate history.
2. Verify the recovered bundle with `git bundle verify`, run `git fsck --full`, and confirm the candidate, taskbar source, baseline, and latest ISO-build commits with `git cat-file`. Then verify candidate ancestry against `d1b2b2ce…` and the recorded taskbar source before creating any worktree.
3. Create `E:\dev\guideXOSServer_TASKBAR_INTEGRATION_RECOVERED` only from that verified candidate. Preserve any recovered untracked files separately and verify the candidate’s reports and build inputs. Do not derive it from the older E: taskbar branch.
4. Check whether the recovered candidate contains the archived shell-portability fix and regression. If not, reapply the fix on the recovered candidate, test both supported shells, restore the pinned Mbed TLS dependency without weakening checks, and obtain a fresh AMD64 kernel build under `ServerV03PacManOnly`.
5. Reuse the existing ISO only if the recovered source/build identity proves it matches the candidate. Otherwise create a new, uniquely named internal ISO without overwriting the existing artifact or frozen release images.
6. Wait for QAC’s current acceptance owner to release its reservation. Then request a QAC-managed run and record fresh evidence for UEFI boot, mounts, desktop, Start Menu, Explorer, taskbar controls, context menus, previews, auto-hide/reveal, and supported shutdown. Keep VirtualBox user validation separate from QEMU evidence.
7. Use that verified candidate and evidence for the INTEGRATION3 review. Keep main unchanged and do not push.

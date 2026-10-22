# guideXOS Server v0.3 — S22R11 Apps Discovery Lifecycle

**Outcome C — `ROOT_CAUSE_UNRESOLVED`.** S22R11 diagnostics confirm that the first observed failure is in VFS mount resolution: the root mount that serves `/Apps` is absent before Start-menu refresh, while the registered block devices retain their generations. Native ELF discovery then correctly fails to open `/Apps` and clears the previously registered package set. The diagnostics do not yet establish why the root mount entry becomes unavailable. No speculative storage or App Model repair was made. PacMan launch and gameplay remain untested.

This is a runtime investigation report, not release acceptance or publication authorization.

## 1. Repository and candidate provenance

- Repository: `D:\dev\guideXOSServer`
- Branch at investigation start: `main`
- Starting HEAD: `77cd7dbe4b19ce33f3eac9309da97fefeac3488`
- The requested S22R10 HEAD, `2e691b99b1f750954ac24b3f91ecabf84c272f5c`, is the parent of that starting commit. The additional local commit is `feat: route prebuilt x86 launcher through QAC`; it was present before S22R11 edits and was preserved.
- S22R10 (`7279a6d75c4327a1369d8a6e0340ed7a4f7449be`) is an ancestor of the starting HEAD.
- The tracked worktree was clean at start; `.phase` was absent. Existing untracked files and generated outputs were preserved.
- Candidate source commit in its receipt: `43f4725a6d7e9ae9949edccddc055c595cd93cb5`; build ID: `95cf3e2d-778b-4b31-8948-4acdb7cc6aaa`.
- Original candidate: `out/release-profile/95cf3e2d-778b-4b31-8948-4acdb7cc6aaa/artifacts/guideXOS-Server-v0.3.0-amd64-pacman-only-candidate.iso`.
- Expected and rechecked SHA-256: `e98bf087efc071ddf82db4dce9d79335aacbf38310de43b032744165792d712b`.

The candidate ISO, source ESP, `dist`, and release readiness markers were not changed. S22R11 produced diagnostic-only derivatives under `out/s22r11-diagnostic-20261021/`; they are not candidate images and do not qualify the original ISO. The v2 diagnostic ISO SHA-256 is `7452503430b2c17da63b24b5bcdf9d4b940556d30dde096fe10e3954348ae933`.

## 2. S22R10 failure reconstruction

The S22R10 report and original guest logs record three cold boots:

1. The ISO-only boot initialized firmware, kernel, and desktop, but had no writable persistent volume; `/Apps` discovery could not find PacMan.
2. The first storage-backed boot attached a writable QEMU FAT directory copy of the candidate ESP. Startup mounted the storage and read `Apps/PacMan/app.json`, then registered `PacMan` (`com.guidexos.pacman`, display name `Nexgen PacMan`). Opening Start caused another discovery attempt, which logged `[NATIVE-ELF] discovery result=NO_APPS_DIRECTORY`; PacMan was omitted from the refreshed menu. The F12 launch attempt produced no Native ELF launch marker.
3. The second storage-backed boot used an independent fresh copy of the candidate ESP. It again reached the desktop, registered PacMan at startup, then logged `NO_APPS_DIRECTORY` on later discovery and displayed a Start menu without PacMan.

The S22R10 logs did not capture the VFS operation or mount table at the transition, so they could not distinguish a storage, mount, FAT, discovery, or presentation failure. Neither storage-backed run launched PacMan.

Full S22R10 context and source evidence remain in [the S22R10 report](RELEASE_S22R10_PACMAN_GUEST_QUALIFICATION.md) and `docs/evidence/s22r10-pacman-guest-qualification-20261020/`.

## 3. Storage, VFS, App Model, and menu call chain

The candidate package path established by its manifest is `Apps/PacMan/app.json`, with executable `/Apps/PacMan/bin/amd64/pacman.elf` (63,640 bytes). The startup chain mounts the candidate FAT volume at VFS `/`, so `/Apps` resolves through that root mount. `native_elf::discover()` opens `/Apps`, enumerates directories, and validates each package manifest before adding it to the fixed App Model table. Start-menu refresh calls discovery, reads the table, and rebuilds package-backed entries from valid, menu-visible packages.

The relevant source chain is:

1. Persistent block device registration and generation (`block`/ATA startup).
2. FAT volume initialization and mount, followed by `vfs::mount_type("/", ...)`.
3. `vfs::find_mount_for_path("/Apps")` selects the active longest matching mount.
4. `vfs::opendir("/Apps")` opens the FAT directory; `vfs::readdir()` reports entry, end, or filesystem error.
5. `native_elf::discover()` validates manifests and builds the registry.
6. `refresh_start_menu_list()` reconstructs visible application rows from that registry.

Static source inspection found `vfs::init()` is idempotent, and the desktop's explicit VFS unmount is for `/Desktop`, an alias. No regular root-unmount call was found.

## 4. First observed failure and App Model effect

Two QAC-managed S22R11 diagnostic boots used the original candidate as an unchanged source and a writable FAT directory copy of its ESP. Both reached the desktop and reproduced the failure. The first v1 diagnostic run established A–D filesystem and registry state. The second v2 run added raw mount-table and root lifecycle events. At A in v2, `/` was a FAT32 mount on `ata0m`, device generation 1; `/Apps` stat and enumeration succeeded, three entries were read, `PacMan` was present, and `app.json` was read completely (956/956 bytes). At B, `PacMan` was registered and the same root mount and device generation were still reported. The ramdisk wallpaper device had also been registered, and the table contained five active mounts: `/`, `/system`, `/certs`, `/config`, and `/config/navigator`.

At C, immediately before Start-menu refresh, `ata0m`, `ram0`, and `wallimg` remained registered at generation 1, but VFS reported no mount for `/Apps`. The first failing operation was `vfs::opendir("/Apps")`, which returned `0xFF` with `VFS_DIR_OPEN_NO_MOUNT`. FAT lookup, block I/O, and traversal were correctly reported as `NOT_ATTEMPTED` because the VFS had no mount to dispatch to.

The raw table narrows the failure to VFS state: `mount_count()` still returned 5, but only slots 1 (`/system`) and 2 (`/certs`) were active in the table dump. The root slot 0 and aliases `/config` and `/config/navigator` were absent. Slot 2's `sourcePrefix`, previously `/certs`, now contained `p://guidexos.test:8080/navigator-smoke/typography-phase7a.html`. This text matches the URL passed by the earlier Navigator typography smoke (`http://guidexos.test:8080/navigator-smoke/typography-phase7a.html`). The v2 serial log contains one fresh VFS initialization and one root mount event, with no root-unmount request or result. These observations show table state corruption or an equivalent untracked mutation after B; the URL match is a correlation, not proof that the Navigator smoke wrote the table.

`native_elf::discover()` clears `s_packageCount` before opening `/Apps`. At C the registry still contained PacMan (`registeredPacMan=yes`, count 1); when `opendir` failed, discovery left retry enabled and returned `NO_APPS_DIRECTORY`. The subsequent `package_count()` retry failed as well. D then reported zero registered packages and `menuPacManAvailable=no`; the screenshot shows PacMan absent. This explains the immediate transition from a registered package to an absent menu row while preserving the filesystem error. It does not identify the writer that corrupted the mount table.

The v1 diagnostic log printed old FAT/block status values after VFS had already returned `NO_MOUNT`; those values were stale and are not evidence of a FAT or block failure. The v2 diagnostics reset per-operation status and label filesystem state `NOT_ATTEMPTED` when no mount exists. The v1 menu metric also consulted a static compatibility row and could say `yes` when the validated registry no longer contained PacMan. The v2 metric reads the current registered package table without triggering a new discovery; the v2 screenshot confirms PacMan is absent from Start.

The first observed failure is therefore in VFS mount resolution (category B). The table was already inconsistent at C: the active count and entries disagreed, and a stored alias path contained unrelated URL text. No explicit root unmount or second initialization was logged. The evidence supports VFS mount-table corruption or equivalent state mutation, but does not identify the code path that wrote it or prove the Navigator smoke is responsible. No storage failure was observed at the device layer in these diagnostic boots, but that does not establish device health for every access path.

## 5. Diagnostic changes and repair decision

The source changes add operation-result tracking for FAT path lookups and VFS directory open/read calls; bounded A–D lifecycle checkpoints; mount-table, device-generation, FAT-volume, `/Apps`, package-manifest, registry, and menu observations; and root-mount initialization/mount/unmount event records. Checkpoints use fixed buffers, inspect at most 32 directory entries, and stop after 16 checkpoints. Discovery failure events are emitted once until a later successful open. The menu diagnostic no longer triggers lazy discovery or counts the static PacMan placeholder as a validated package.

These edits do not retain a package after a failed discovery, hardcode PacMan into the menu, change package validation, change storage ownership, or alter Native ELF security/ABI behavior. No root-cause repair or failure-specific regression test was added because the mount-loss mechanism has not been isolated.

## 6. Hosted and freestanding validation

- `scripts/run-vfs-hosted-read-test.ps1`: PASS, 4/4 cases; executable retained at `out/s22r11-diagnostic-20261021/tests/vfs-hosted-read-final.exe`.
- `scripts/run-block-device-generation-test.ps1`: PASS, 5/5 cases; executable retained at `out/s22r11-diagnostic-20261021/tests/block-device-generation-final.exe`.
- `tests/pacman_source_registration_test.ps1`: PASS for pre-audio, audio-bearing, missing implementation, and missing definitions cases. Fixture evidence is under `out/s22r11-diagnostic-20261021/tests/pacman-source-registration-final/`.
- `scripts/run-native-filesystem-contract-test.ps1`: PASS after correcting the runner's missing TF-PSA built-in include path, `settings_presentation_core.cpp`, and `-lwinmm`. The executable is `out/s22r11-diagnostic-20261021/tests/native-filesystem-contract-test-fixed.exe`.
- Clean isolated freestanding AMD64 build: `out/s22r11-diagnostic-20261021/kernel-v4/`. After the final bounded-status and field-printing edits, an incremental rebuild of that isolated tree also succeeded. A local compiler wrapper added `wchar.h` only to `native_elf_baremetal.cpp` and `system_font.cpp` to work around the existing MinGW `_wcsicmp` declaration issue; no source workaround was added for the unrelated C translation units.
- `scripts/audit-kernel-symbols.ps1`: PASS on the rebuilt kernel — 45 undefined symbol rows, 21 distinct `.symtab` entries, no relocations or imports.
- `git diff --check`: PASS.

These checks validate the diagnostics and existing focused contracts. They do not prove the root-mount lifecycle or qualify a release candidate.

## 7. QAC guest results and limitations

The v1 S22R11 diagnostic-only storage-backed guest was closed under QAC acceptance reservation `844ca563d93249878a0368490bf0b9f8`; QAC reported exit code 0 and released the reservation. The v2 diagnostic-only guest was also closed cleanly under reservation `6493c1e0645644fd96102340382751b3`; its QAC launcher exited 0. Serial output, QMP records, and screenshots for the raw mount-table run are retained in `out/s22r11-diagnostic-20261021/guest-diagnostic-storage-backed-4/`.

Before the v2 launch, QAC reported zero processes, reservations, or queued sessions. During evidence review, a separate acceptance run queued behind this guest; the v2 guest was then quit through QMP and its QAC reservation released. No other QEMU process was interrupted.

No S22R11 run launched PacMan. Its window, input, gameplay progression, exit behavior, and post-launch desktop stability remain untested. The S22R10 Settings and offline Navigator smoke results remain as recorded in the S22R10 report; S22R11 did not repeat them.

## 8. Security, release boundary, and next phase

S22R6–S22R10 boundaries remain in effect: Native ELF applications execute with kernel privilege; process isolation and runtime ABI/package compatibility are not fully enforceable; writable ESP application discovery remains an exposure. A diagnostic result or a future `/Apps` fix would not qualify release publication.

The next Main Integration phase should add a targeted watchpoint or equivalent write trace around the VFS mount table and reproduce through the Navigator startup smoke, which ran between B and C and contains the matching URL. Confirm the exact writer before changing production behavior, then add a focused regression. Build a new candidate only under the phase's release authorization. Acceptance still requires independent storage-backed cold boots, PacMan remaining visible after refresh, actual launch and gameplay input, clean exit to desktop, and repeatable built-in application smoke. Keep release readiness and publication pending until those checks and the outstanding security decisions are complete.

**Disposition:** `ROOT_CAUSE_UNRESOLVED`; VFS mount-table state corruption is reproduced, but the writer is not isolated. Original candidate preserved. No repair, candidate qualification, PacMan launch, or gameplay validation was performed.

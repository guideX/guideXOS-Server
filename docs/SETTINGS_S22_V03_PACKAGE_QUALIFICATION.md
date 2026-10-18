# guideXOS Server — S22 Native Settings v0.3 Package Qualification

**Date:** 2026-10-18

**Outcome:** **B — Partial; native Settings is not qualified for inclusion in Server v0.3.**

The fresh AMD64 package was assembled, its provenance recorded, and the exact ISO booted in QEMU. Native Settings launched with the expected built-in identity and its four implemented pages rendered. Package-level acceptance did not pass: an appearance Apply from the shipped default configuration returns a conflict without saving, and the production build log reports that the new DeveloperStudio ELF did not pass its signed-off staging check even though the wrapper exits zero. These findings leave the Settings feature and the assembled package unqualified.

This is a component/package qualification result. It is not approval of the whole Server v0.3 release. Nothing was published or pushed.

## 1. Repository state and provenance

- Authorized repository and branch: `D:\dev\guideXOSServer`, `main`.
- Starting HEAD: `6e15a4f584d6e1e4cd3e2892f3f0e25fed8a1b2c` (the accepted S21 baseline).
- Tracked files were clean at preflight. Approximately 676 pre-existing untracked paths were inventoried and preserved; existing S10–S21 evidence and older candidate directories were not overwritten.
- Source commit packaged: `eac07e130f439b91d328558f286fa51266b9c7a0` (`main`). The build used an isolated, clean source snapshot at that commit.
- During S22, a Windows PowerShell 5.1 defect in PacMan source registration was reproduced: `Group-Object -Property Name` on the source hashtables collapsed distinct names into one blank group. The expression was corrected to `Group-Object -Property { $_.Name }` in `scripts/pacman-source-registration.psm1`; `tests/pacman_source_registration_test.ps1` passed under Windows PowerShell 5.1 and PowerShell 7.6.5. This narrow build fix is included in the packaged source commit.
- The documentation/evidence commit is made locally after qualification. Its SHA and final HEAD are recorded in the S22 completion handoff; neither was part of the package input.

## 2. Production packaging pipeline

The established production entry point is `scripts/create-release-iso.ps1`. It invokes the repository `build.ps1 -Arch amd64`, validates the ESP inputs and freshness against source files, builds a FAT32 UEFI image, then creates and verifies a UEFI no-emulation ISO with PyCdlib. The canonical build assembles the bootloader, AMD64 kernel, application packages, and wallpaper runtime image. Native Settings is a built-in kernel application registered by the application manager; it is not substituted with a hosted app or a separate Settings ELF.

For this architecture, `kernel/Makefile` links a PE-format kernel output and converts it to ELF64. The release ESP consumes `kernel.elf`; `kernel.pe` is a build artifact and is not included in the ISO. UEFI starts `EFI/BOOT/BOOTX64.EFI`, which loads the ELF kernel and `ramdisk.img`. The RAM image contains the wallpaper runtime assets and native app resources. The ESP also includes `desktop.cfg`, `startup.nsh`, build identity, app manifests and application files. The release manifest marks the FAT image as FAT32 and the ISO as UEFI bootable; it records no legacy BIOS El Torito entry.

The actual command, run from the isolated source snapshot, was:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/create-release-iso.ps1 `
  -Version 0.3.0-s22.20261018 -Arch amd64 `
  -PythonPath D:\dev\guideXOSServer\out\release-tools\venv\Scripts\python.exe `
  -RequireCleanWorktree
```

`-Clean` was not passed. The isolated snapshot started clean and the canonical build reported `Kernel built successfully`; the wrapper also checked that release inputs were current. The manifest records Git 2.54.0, Windows PowerShell 5.1.26100.9711, Python 3.12.14, pyfatfs 1.1.0, fs 2.4.16, and PyCdlib 1.16.0. The AMD64 kernel build uses the MinGW-W64 GCC toolchain (host `g++` 15.2.0, `x86_64-w64-mingw32`); PacMan uses LLVM clang 22.1.8 with `--target=x86_64-unknown-elf` and freestanding/no-exceptions/no-RTTI flags. The release build log is retained with the manifest.

## 3. Historical candidates and new package identity

S21 identified the preserved October 4–5 v0.3 candidates as pre-native-Settings artifacts. Their recorded kernel ELF hash was `1c0fa4325b1da9d8220f08e16e404b70c6d8f6ca414acd1e053d53747388d086`; their ramdisk hash was `a497b234c41ee936ae402555ec067f2775db3765fa612e41b23eb3d80b6f02f`. Those candidates were not used as S22 input and were left intact.

| S22 artifact | Size | SHA-256 | Package role |
|---|---:|---|---|
| `guideXOS-Server-v0.3.0-s22.20261018-amd64.iso` | 91,293,696 bytes | `b303c4c1a1cd31e500353cbd2dd7e11a8fd438b81ac9306285a6cd20ea1b1a3f` | New UEFI ISO candidate |
| `UEFI_BOOT.IMG` | 91,226,112 bytes | `950537cb16f8589535a96fbf9bdb0df7e3b5a3753259d66a2144938da99984ea` | FAT32 boot image inside ISO |
| `ESP/kernel.elf` | 2,771,240 bytes | `f5aa964da871dead7259d766dbaa4f67be5485af9df3afcd7812fe559eca9aef` | Kernel consumed by package; same ELF hash as accepted S20 |
| `kernel.pe` | 2,888,634 bytes | `f9d7d13e177d9bc728e2529148d9f5e1ab19195e69cb73f153547285eec113b3` | Current build output; not packaged. S20's historical kernel PE hash was `159d82dade3b2bf3eee607fa6c286b561bc9128d4048f1a4096c6dae17552589` |
| `ESP/EFI/BOOT/BOOTX64.EFI` | 56,832 bytes | `041dd60ed036528bb392e6539fd1843f4a6f6ffe0283d4dcc939d2ee04a1e2cd` | UEFI bootloader PE image |
| `ESP/ramdisk.img` | 67,108,864 bytes | `1603ed0e7606476e21e3e05038f937beae311a95c93933257e99b610f0af92b3` | Wallpaper/runtime image |
| `ESP/desktop.cfg` | 257 bytes | `95b274e39e9dde431286f4eb00fd2175888430744e60ffdec3ba49375ba52947` | Default appearance and desktop configuration |
| `ESP/Apps/PacMan/bin/amd64/pacman.elf` | 63,640 bytes | `eeb544bbb2e2574eeaed7bb7f0bad61ea24f6945231b7757223a225b30b8e1cc` | Native PacMan package, including its seven WAV resources |

The full manifest records source commit, clean-at-package-start status, all enumerated ESP inputs, sizes and hashes, ISO layout, and tool versions. It is copied to `docs/evidence/settings-s22-v03-package-20261018/build/manifest.json`; the package remains at `out/s22-v03-candidate-20261018/`. No pre-existing output directory was used as a destination.

## 4. Build result and package integrity

The canonical build produced the kernel and candidate ISO and the ISO structural verifier passed. `release-build.summary.txt` reports exit code 0. However, `release-build.stdout.log` contains:

```text
ERROR: DeveloperStudio signed-off ELF size/hash changed; refusing to stage it
```

The current source binary was 1,104,652 bytes with SHA-256 `f07270eacbd62f9ab67f1f38856a326981280e43d0a4d44f8edd17c777f01a6`. The ESP packaged the prior signed-off 1,015,064-byte binary with SHA-256 `5343f5df0edfe423542348c0af8c9f8690ce339d197deb4737dd05a27348cac7`. The package manifest confirms the older hash was staged. Thus the wrapper emitted a candidate despite a required-component staging error. This is an independent production package blocker, not a Settings failure, and the exit code does not clear it.

The captured build logs contain 232 compiler-warning lines and no compiler `error:` lines. They do include the DeveloperStudio staging error above. The package contains the required UEFI bootloader, ELF kernel, and ramdisk, and the ISO verifier found the UEFI boot record and `UEFI_BOOT.IMG` at the manifest's recorded locations.

The AMD64 kernel makefile uses C++14, `-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-exceptions`, `-fno-rtti`, and `-fno-builtin`. On the emitted `kernel.elf`, `readelf -r` reports no relocations; `nm -u` nevertheless reports 21 distinct undefined symbol names, including `mainCRTStartup`, serial routines, `KernelApp` callbacks, and two settings/network validators. The kernel boots and the tested Settings route works, so these entries did not prevent the observed execution. S22 did not establish a documented allowlist or explain every undefined symbol; this is an open audit item, not a claim that the unresolved-symbol check passed. The audit output is in `build/kernel-symbol-audit.txt`.

## 5. QEMU package checks

QEMU was 11.0.0. Both runs used read-only OVMF firmware (SHA-256 `2df617a6fd1bae41ec7c0e3bceca96c928a5a88d8dc198468d34733393ab2792`), TCG, 2048 MiB RAM, no network, and private QMP sockets. The ISO run attached the exact ISO as a read-only CD-ROM. The writable run used a private raw IDE copy of `UEFI_BOOT.IMG`; the copy hash matched the candidate before boot. QEMU was shut down through each run's private QMP endpoint. Foreign QEMU processes were left untouched.

The exact ISO booted through UEFI, loaded the 64 MiB ramdisk, reached the desktop event loop, and showed no `FATAL` or `PANIC` in the captured serial log. Start → Settings dispatched `gxos.builtin.settings`; the native adapter reported a ready provider snapshot and acquired compositor focus. The matching launch records and logs are under `guest/iso/` and `guest/writable/`.

| Native page / route | Package result |
|---|---|
| System | Product `guideXOS`, kernel `0.1.0`, architecture `AMD64 (x86-64)` shown. |
| Display | `1280×800`, 32 bpp, `B8G8R8A8`, pitch 5120. Kernel serial reports the same primary UEFI framebuffer values; topology is unavailable. |
| Personalization | Active wallpaper shown as Blue Flower. Eight built-in gradient presets and sixteen wallpapers were available; all four wallpaper pages rendered. |
| About | Correct product, kernel, and architecture. OS release version correctly shows “Not defined by this build.” |
| Deferred categories | Network & Internet, Devices, Storage, Apps, Users, Date & Time, Accessibility, and Developer were dimmed, with “Dim pages are deferred.” The captured S22 screenshot demonstrates the visible deferred state. |

Settings accepted keyboard navigation through Display, Personalization, About, and back to System; Tab and Shift+Tab focus paths were exercised in the package sessions. Alt+F4 closed Settings and returned to a stable desktop. The legacy Start → Control Panel route remained independent; Display Options opened and rendered the Backgrounds gallery. Its applied Blue Flower marker agreed with native Settings. The legacy windows closed normally. A full resize sweep was not repeated; S19/S20 acceptance remains the resize evidence.

## 6. Appearance persistence smoke and release-blocking defect

The isolated writable copy began with the exact candidate ESP payload hash. Its `/desktop.cfg` was 257 bytes and contained a single canonical but empty `wallpaperId=` field; the runtime resolved that implicit default to `legacy_blue_flower` (Blue Flower).

- Selecting Dinos changed only the pending selection. The before/after `/desktop.cfg` files were byte-identical (SHA-256 `95b274…52947`).
- Cancel reported “Pending selection cancelled.” The file remained byte-identical.
- Selecting Dinos again and applying via the keyboard failed. Serial reported `[settings-native] background.apply status=5 active=legacy_blue_flower persisted=(unavailable)`. The UI reported “Saved configuration changed. Select again to retry.” The wallpaper remained Blue Flower and `/desktop.cfg` remained byte-identical; runtime readback confirmed no saved change.

The current code explains this result: `apply_builtin_background()` passes the active default ID `legacy_blue_flower` to `persistBuiltInBackgroundId()`. The persistence helper sees one `wallpaperId` key whose value is empty, and treats the empty string as a conflicting current ID. That turns an ordinary Apply from the fresh package default into a conflict. The preserved config proves no unrelated field was changed, but the supported mutation smoke failed. This is a release-blocking Settings defect for this candidate. Fix the implicit-default comparison and add regression coverage before claiming Limited/Alpha package qualification.

The exact before, pending, cancel, and failed-Apply snapshots and hash comparisons are retained under `guest/writable/`. The failed status and UI are captured in `applied-dinos-settings.png` and `serial.log`.

## 7. Decision and next release-integration step

**S22 Outcome B — Partial. Native Settings is not qualified for inclusion in guideXOS Server v0.3 as Limited/Alpha.** The fresh package and exact-ISO boot provide meaningful integration evidence, but the default-configuration Apply conflict and DeveloperStudio source/package mismatch leave required gates open. The undefined symbol entries also need an explicit policy audit. The candidate must not be represented as a passing production build merely because the wrapper returned exit code 0.

Next: correct and regression-test default appearance persistence; make the DeveloperStudio source binary match an approved/signed-off artifact or otherwise close its staging qualification; clarify the kernel undefined-symbol policy; then build a new isolated AMD64 candidate with passing component gates. Re-run exact-ISO boot, the writable Apply/readback smoke, and legacy compatibility against that exact package. Overall Server v0.3 release readiness remains conditional on all other release gates. Do not publish this candidate.

## 8. Curated evidence index

Evidence directory: `docs/evidence/settings-s22-v03-package-20261018/`.

- Build manifest, ISO hash, build summary/stdout/stderr, and ELF symbol audit: `build/`.
- Exact ISO launch, QMP transcript, serial log, and QEMU debug log: `guest/iso/`.
- Hash-matched writable-copy launch and logs: `guest/writable/launch.json`, `copy-identity.json`, `serial.log`, and `qmp.log`.
- Settings pages: `guest/iso/settings-initial.png`, `settings-display.png`, `settings-personalization.png`, `settings-about.png`, `settings-gradients.png`, and `settings-wallpapers-page1.png` through `settings-wallpapers-page4.png`.
- Legacy compatibility: `guest/iso/legacy-control-panel-open.png` and `display-options-open.png`.
- Persistence smoke: `guest/writable/desktop.cfg.before`, `desktop.cfg.after-pending-selection`, `desktop.cfg.after-cancel`, `desktop.cfg.after-apply`, their JSON comparisons, and `applied-dinos-settings.png`.

The ISO and ESP image are not committed; their exact paths and hashes are recorded in the report and manifest. The qualification report and curated evidence are committed locally on `main`; nothing was pushed.

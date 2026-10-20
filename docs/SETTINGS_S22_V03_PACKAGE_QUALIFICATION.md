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

---

# S22R — Default Appearance Conflict Repair and v0.3 Build Integrity

**Date:** 2026-10-19

**Outcome:** **B — Partial.** The shipped-default appearance conflict is repaired and covered by the native runtime regression suite. Release-stage failure propagation and artifact checks now fail closed. The 21 kernel undefined names are classified against a fresh isolated AMD64 kernel build. A fresh v0.3 production package and exact-package guest qualification could not proceed because the current DeveloperStudio source package has no matching signed-off manifest+ELF identity. Native Settings therefore remains **unqualified for v0.3 Limited/Alpha inclusion** until a release owner approves the exact package to ship and the package-level gates pass.

S22R does not accept Server v0.3 as a whole, publish a release, begin S23, accept S9 or Settings 1.0, or change a sibling repository.

## 1. Starting state and preserved artifacts

- Repository: `D:\dev\guideXOSServer`; branch: `main`.
- Starting HEAD: `0513a65643f98b8c78ef709dfa7d6ea771677517`; its parent is the S22 report commit `655fe1f67c1d79d2b2c97aae01dd6267609ba84f`.
- No accepted S22R repair existed at preflight. S22 defects remained applicable.
- The tracked worktree was clean before S22R edits. The preflight inventoried 851 pre-existing untracked paths and retained their exact status/path lists under [preflight evidence](evidence/settings-s22r-v03-package-20261019/preflight/). No unrelated path was staged or removed.
- The original S22 ISO remains unchanged at `out/s22-v03-candidate-20261018/guideXOS-Server-v0.3.0-s22.20261018-amd64.iso`: 91,293,696 bytes, SHA-256 `b303c4c1a1cd31e500353cbd2dd7e11a8fd438b81ac9306285a6cd20ea1b1a3f`.
- The tracked ESP contains the old signed-off ELF but a different historical manifest. The preflight verified both file hashes before and after the build check; neither ESP file was used as a new package source.

## 2. Appearance failure and correction

The shipped `/desktop.cfg` contains `wallpaperId=` as an intentional legacy representation of the default. Desktop loading resolves the empty value to the built-in `legacy_blue_flower` wallpaper (Blue Flower). The native Settings provider reads the desktop’s effective state and reports Blue Flower, while the persistence helper compared the expected effective ID against the literal empty stored value. That comparison returned `Conflict` on a normal Dinos Apply from the fresh default. The S22 guest had already shown Blue Flower before Apply and preserved the original 257-byte config after the failed attempt.

The fix shares the Blue Flower default ID between desktop initialization/fallback and persistence. Persistence now considers the empty value equal to the expected current value only when that expected value is exactly `legacy_blue_flower`. Revision checks still run first; an explicit unknown stored ID remains a conflict. Reading the literal empty default stays byte-identical and performs no write. Desktop fallback resolves the named default from the wallpaper catalog instead of assuming array element zero.

The comparison is in the shared wallpaper persistence helper. It also covers valid gradient Apply from the default because that path uses the same background ID writer. Icon visibility uses a separate persistence helper and is unaffected. Legacy Display Options continues to use the same desktop default interpretation; its repaired behavior was not rechecked in a new guest because there is no S22R ISO.

Before the fix, the focused runtime test reproduced the failure: **1,422/1,423** checks passed and `Dinos Apply commits from the untouched shipped empty wallpaper default` failed. After the fix, the full affected native runtime suite passed **1,426/1,426**, including lifecycle repetitions, provider refresh cycles, configuration preservation, malformed/write/readback/rollback cases, and existing revision conflict protection. The regression log also records 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions.

The exact shipped default bytes and the expected unit-test bytes after changing only the wallpaper field are in [appearance evidence](evidence/settings-s22r-v03-package-20261019/appearance/). The post-Apply file is explicitly a unit-test fixture, not guest evidence. The earlier S16–S18 raw-FAT durability evidence remains historical; no crash-atomic persistence claim is made.

## 3. DeveloperStudio package contract and build failure

The existing Server build has a signed-off DeveloperStudio ELF hash pin, but does not pin the corresponding manifest. The Phase 25 DeveloperStudio release record identifies the accepted pair as:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Phase 25 `app.json` | 1,112 | `d59c12267d360c4e31b70dec7f9c1b0689fb5816011d681b38ee9cdef0d2f53b` |
| Phase 25 AMD64 ELF | 1,015,064 | `5343f5df0edfe423542348c0af8c9f8690ce339d197deb4737dd05a27348cac7` |

The Phase 25 status document prints a 63-hex-character manifest digest, which is not a complete SHA-256 value. The manifest blob in the paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02` is 1,112 bytes; its exact bytes independently hash to the 64-character value above. S22R pins those verified bytes and records the transcription discrepancy.

The current `Apps/DeveloperStudio` package is a legitimate later source change but has no matching release acceptance in the inspected record: its manifest is 2,011 bytes / `ab1db7c63a3dc49c06098b7a9341c33dbcf6fecbd8163460f62bcf907e7488e9`, and its ELF is 1,104,652 bytes / `f07270eacbd62f9ab67f1f38856a326981280e43d0a4d44f8ed1d17c777f01a6`. The ESP contains the old signed-off ELF but a different 1,176-byte manifest (`7af95d35f068c44f4529420922e0f4bec5f705568514688d160e2790022acb09`); that ESP directory is not the exact Phase 25 package pair and is not an authorized replacement source. Restoring its manifest into a new package would also roll back document-activation metadata without an owner decision.

The S22 log proves the required DeveloperStudio stage emitted an error, then later ISO packaging proceeded with stale ESP files. The retained combined log does not identify the exact PowerShell process boundary that caused the nonzero stage result to be lost. The observable wrapper defect was that a required app-stage receipt/provenance postcondition was absent: later packaging checked for bootloader, kernel, ramdisk, and timestamps, then accepted the older app files. S22R closes that path with an early exact manifest+ELF preflight, terminating stage errors, child-output error scanning, a build-ID-bound stage receipt, exact artifact hash/size checks, and required-stage validation before ISO input enumeration.

The new `build.ps1 -Arch amd64` preflight was exercised against the actual current source. It returned **exit code 1** on the DeveloperStudio mismatch before cleanup or output replacement. The original S22 ISO and existing ESP bootloader, kernel, ramdisk, and DeveloperStudio file hashes were unchanged. The ESP's old ELF plus mismatched manifest were not copied into `Apps` or a new ramdisk. A full canonical build and `create-release-iso.ps1` therefore did not run.

The release integrity harness passed **13 assertions** using isolated fixtures. It rejects a required command returning nonzero, a child reporting `ERROR:` while returning zero, a missing/incomplete or earlier build receipt, wrong source/build identity, absent required files including either signed-off DeveloperStudio file, post-receipt hash mutation, and a newer unapproved DeveloperStudio pair even when its receipt matches. Its passing fixture uses the exact 1,112-byte Phase 25 manifest from paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02` and the signed-off ELF; the historical ESP manifest is not used as the accepted fixture. The wrapper now requires a current successful receipt and records the actual DeveloperStudio hashes and kernel audit in any future release manifest.

The next release step is an explicit DeveloperStudio owner decision: either qualify and accept the current manifest+ELF pair, or select the complete prior signed-off pair as the intended Server dependency, including the capability rollback decision. Until that identity and provenance are authorized, the package gate stays closed.

## 4. Kernel symbol audit

A fresh non-instrumented freestanding AMD64 build was produced in isolated `kernel/build/s22r-20261019/` using `mingw32-make -j2 ARCH=amd64 BUILD_DIR=build/s22r-20261019`. MinGW-W64 g++ was 15.2.0; GNU ld, nm, readelf, and objdump were Binutils 2.46.0.20260210. The build log contains 232 compiler warning lines and zero compiler/linker error lines. A subsequent no-op make returned 0. The new kernel files were not copied into ESP or packaged.

The complete audit in [undefined-symbol-classification.md](evidence/settings-s22r-v03-package-20261019/kernel/undefined-symbol-classification.md) covers all 21 distinct names (45 ELF `.symtab` rows) and all 159 input objects. Nineteen strong names have final PE definitions; those COFF `UND` rows are linker-resolved table entries with no remaining relocation. `__dso_handle` is weak in its only input-object reference and maps to MinGW weak ABI guard storage. `mainCRTStartup` is absent from input-object undefined references; the custom linker script selects `_start`, making the row unused/non-loadable. The ELF has no dynamic table/segment or relocation records; the PE has no imports or COFF symbol relocations. No required unresolved production dependency remains for this exact ELF/PE/object set. This is a symbol classification, not guest boot evidence.

## 5. Production package and guest qualification

The full production build stopped at the DeveloperStudio source pin preflight. No new bootloader, ramdisk, package manifest, release ISO, firmware copy, QMP transcript, serial log, or screenshot was produced. The isolated kernel build is not a substitute for the full build. No QEMU process was launched, so no ownership inventory or guest smoke was needed for this attempt.

Consequently, S22R does not claim a fresh ISO boot, Blue Flower guest default, Settings UI Apply, Dinos desktop change, provider guest readback, cold-boot persistence, reopen state, or legacy Display Options guest check. The historical S22 UI and legacy Control Panel results remain S22 evidence only. The exact-package QEMU gate remains open.

## 6. Focused validation and evidence

- Native appearance/provider runtime: **1,426/1,426 passed**; the pre-fix reproduction is also retained.
- Build-integrity fixture harness: **13 assertions passed**.
- Fresh normal-target freestanding AMD64 kernel ELF/PE build: passed; 232 warning lines, zero error lines. Follow-up no-op make exit code 0.
- Undefined-symbol audit: 21/21 distinct names classified; no unexplained required unresolved symbol.
- PowerShell parser checks for `build.ps1`, `create-release-iso.ps1`, and the new integrity scripts passed. `git diff --check` reported no whitespace errors; Git printed only configured line-ending normalization warnings.
- Actual DeveloperStudio preflight: exit code 1, correct owner-boundary diagnostic, protected S22/ESP hashes unchanged.
- New S22R evidence index and SHA-256 list: [README](evidence/settings-s22r-v03-package-20261019/README.md), [manifest](evidence/settings-s22r-v03-package-20261019/manifest.json), and [checksum manifest](evidence/settings-s22r-v03-package-20261019/evidence-manifest.sha256).

## 7. Qualification decision and next step

**Outcome B — Partial. Native Settings is not qualified for inclusion in guideXOS Server v0.3 at this checkpoint.** The repaired default-to-Dinos persistence contract passes the native runtime suite, and the build and kernel audits now fail closed or classify their findings. The exact DeveloperStudio package for a release candidate remains unapproved, so no new production package exists to boot or qualify. The required production-stage and exact-package QEMU gates remain open.

After the DeveloperStudio owner approves exact manifest and ELF hashes and package provenance, rerun the canonical AMD64 build into isolated output paths. Require every receipt stage to pass, verify the package manifest and embedded App Model bytes, then boot that exact new ISO on isolated writable media and perform the default-to-Dinos Apply/readback/reopen and legacy Display Options smoke. Only after those gates pass can the component be recorded as **Native Settings — Qualified for guideXOS Server v0.3 Limited/Alpha inclusion**. This remains component-level qualification only and does not accept the whole Server release.

S22R changes and curated evidence are committed locally on `main`; nothing was pushed or published. The final local commit SHA is included in the task closeout.

## S22R2 — DeveloperStudio release contract resolution

**Date:** 2026-10-19
**Starting source HEAD:** `bce4145843a8c93a4b253116e374341b51f11655` on `main`.
**Outcome:** **C — Blocked at the release-policy and compatibility boundary.**

S22R Outcome B remained unresolved at the start: its source repair and integrity work passed, but the exact DeveloperStudio package for a new production ISO was not authorized. No S22R2 acceptance record or evidence directory already existed. The starting worktree had 0 staged paths, 0 tracked unstaged paths, and 853 pre-existing untracked paths. The separate unstaged DeveloperStudio compatibility note and all prior evidence/output were preserved.

### DeveloperStudio artifact decision

The exact Phase 25 pair was verified:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Phase 25 `app.json` | 1,112 | `D59C12267D360C4E31B70DEC7F9C1B0689FB5816011D681B38EE9CDEF0D2F53B` |
| Phase 25 AMD64 ELF | 1,015,064 | `5343F5DF0EDFE423542348C0AF8C9F8690CE339D197DEB4737DD05A27348CAC7` |

The pair is present in paired Server commit `fb71e36c77866cdabf20873e6ad3de0ddc3bbe02`; the original Phase 25 sign-off record is the DeveloperStudio release-candidate status dated 2026-08-23, sign-off commit `bb66e11c38dffae21514384f73b1920b53ac64fe`. That record documents a successful distribution rehearsal for DeveloperStudio 0.1.0 in its paired Server v0.5 checkout. It does not authorize that pair for Server v0.3. The exact manifest declares `minGuideXOSVersion: 0.5.0`, so its stated minimum exceeds the v0.3 target. The Phase 25 status text has an incomplete 63-character digest; the full manifest digest above was measured from the paired Server commit bytes and matches the 1,112-byte fixture.

The current source pair was measured as:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| Current `Apps/DeveloperStudio/app.json` | 2,011 | `AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9` |
| Current AMD64 ELF | 1,104,652 | `F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6` |

No matching release approval was found for this newer candidate. The source/package pairing is reconstructed from DeveloperStudio commit `18c093dcb9c4fc0daf5a019d82283aba0ac319a4` and Server package update commit `81565ee227ed784862ecc04fc692cbd1904deef4`; the ELF has no immutable build identity. Its manifest enables document activation and nine source/text associations that are absent from Phase 25. Server can boot without DeveloperStudio, but selecting Phase 25 removes those currently advertised document routes. The preserved ESP contains the Phase 25 ELF but a different 1,176-byte manifest (`7AF95D35F068C44F4529420922E0F4BEC5F705568514688D160E2790022ACB09`); it is not the accepted Phase 25 pair.

### Compatibility and selection policy

The Phase 25 package matches the current loader's basic AMD64 Native ELF identity: package ID `com.guidexos.developerstudio`, entry point `gx_main`, and base ABI `guidexos-c-abi-v1`. Its ELF is static and the historical host-call offsets remain stable. This is structural ABI compatibility only. Static review found that the current Server v0.3 runtime lacks the workspace filesystem, build, Run, and debugger host services required by Phase 25 DeveloperStudio. The minimum-version declaration is also unresolved. A package discovery or window launch would not establish compatibility for the app's intended development workflows.

The existing build pin and preflight select from `Apps/DeveloperStudio` and verify the exact Phase 25 pair before cleanup. There is no independent selector for a pinned artifact. The preserved S22R preflight against the current source pair exited 1 with the owner-acceptance diagnostic before replacing outputs. The 13-assertion S22R integrity harness passed historically; its exact Phase 25 fixture proves identity verification, not Server v0.3 authorization. No preflight check was weakened and no selection logic was changed.

The separate compatibility assessment recommends deferring DeveloperStudio from v0.3. It states that exact Phase 25 inclusion requires approval by both the DeveloperStudio and Server v0.3 release owners, resolution of the 0.5.0 minimum-version contract, and a runtime-owner decision on the absent services. The current pair also remains unapproved. Therefore Gate A did not authorize either candidate. No build, staging correction, ISO assembly, or QEMU launch was performed.

### Package, kernel, and Settings status

The preserved S22 ISO was rehashed at 91,293,696 bytes and SHA-256 `B303C4C1A1CD31E500353CBD2DD7E11A8FD438B81AC9306285A6CD20EA1B1A3F`, matching its recorded identity. S22R's source repair and fail-closed integrity changes are in starting commit `bce4145843a8c93a4b253116e374341b51f11655`; no S22R2 code or test changes were made. S22R historical checks remain: native appearance/provider runtime 1,426/1,426, release-integrity harness 13 assertions, and a successful isolated AMD64 kernel build with 232 warning lines and zero compiler/linker errors. The symbol report covers 21 distinct names (45 ELF symbol-table rows): 19 strong names have final PE definitions, __dso_handle is only a weak ABI guard reference, and mainCRTStartup is absent from input-object undefined references while the linker selects _start. The ELF had no dynamic segment or relocations; the PE had no imports or COFF symbol relocations. This classification applies to that exact S22R kernel; no new kernel was built, so there is no new symbol comparison. No new manifest, kernel, bootloader, ramdisk, ISO, package-content audit, QEMU logs, screenshots, or guest configuration readback exists for S22R2.

The empty shipped wallpaper default repair remains in source and its historical runtime suite passed. Settings page, legacy Control Panel, and DeveloperStudio guest checks were not repeated; S22/S22R records remain historical only.

### S22R2 closeout

The exact decision packet and validation boundary are recorded in [S22R2 evidence](evidence/settings-s22r2-v03-package-20261019/README.md), including the Phase 25 acceptance provenance, both candidate identities, compatibility findings, preflight result, and required owner decision.

**Final Outcome C — Blocked. Native Settings remains unqualified for Server v0.3 Limited/Alpha inclusion.** The required production ISO and exact-package guest acceptance were not performed because neither the current source pair nor the Phase 25 pair is authorized and compatible for the intended v0.3 package on the evidence available. This component result does not determine overall Server v0.3 release readiness.

**Recommended next action:** the Server v0.3 release owner should authorize an explicit package decision. The compatibility evidence recommends deferring DeveloperStudio from v0.3; that requires a narrow package-inventory change and validation. Retaining Phase 25 instead requires the stated owner approvals, resolution of its minimum-version and runtime-service gaps, and a bounded current-Server smoke before package assembly.

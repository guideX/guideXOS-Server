# FONT13R2 — Packaging Closeout & External Blocker Isolation

- **Date:** 2026-10-18
- **Outcome:** **B — Partial**
- **Branch / starting and ending HEAD:** `FONT_SUPPORT` / `89a58151c19da3d6532d838ae3a29a2c0ab4711a`
- **Release phase:** integration preparation only; no phase advance
- **Minimum-size marker:** `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open

## 1. Decision

The approved Roboto package, its provenance and license, and the independent runtime-image staging path are sound. The package was read back from the FAT32 runtime image with all three approved TTF hashes plus the complete license, notice, and manifest. The upstream archive and generated subset hashes were rechecked.

Outcome A is not met. The normal top-level build stops before runtime-image generation because the PacMan Native ELF build omits the object that provides its two audio symbols. The independently built fresh image booted to the desktop, but the supported guest input path could not reach Settings. Real Settings enumeration, preview, Apply, visible shell change, guest persistence, Restore Defaults, and guest missing-font fallback remain unqualified. The TTF Fonts page at 560×360 also remains unqualified.

The font engine remains qualified by the previously accepted renderer, cache, selection, persistence, and fallback tests. The font **asset package** is independently verified. The complete normal build and combined v0.3 release qualification remain incomplete.

## 2. Repository state and preservation

- The checkout began on the required `FONT_SUPPORT` branch at the expected HEAD. Ending branch and HEAD are unchanged.
- The worktree already contained legitimate FONT12R, FONT13, and FONT13R changes, package assets, evidence, and output artifacts. Those files were preserved. No reset, stash, clean, checkout, branch operation, worktree, merge, rebase, cherry-pick, or push was performed.
- The initial D: volume had 18,021,978,112 bytes free. No full or artifact-heavy system build was run.
- R2 changed only [the FONT13 integration handoff](FONT13_V03_INTEGRATION_HANDOFF.md) and added this closeout and the [PacMan build handoff](FONT13R2_PACMAN_BUILD_HANDOFF.md). Package validation and diagnostic evidence was written below the existing `out/font13-validation/` tree.
- The sibling `D:\dev\guideXOSServer_MODERN_INPUT` checkout was read only. It was on `MODERN_INPUT` at `27980591f302c5d64f4dc223d168262e1d78e298` and already had unrelated changes in `ESP/build-identity.txt` plus `out/mbedtls-bootstrap/`; neither was touched. The PacMan source checkout was also read only.

## 3. Approved package, provenance, and license

The package is derived from Google's official static Roboto release `v2.138`, tag commit `ab4876f650a4aeb9b661c83fee47f908c739bb70`, archive `roboto-unhinted.zip`. The retained archive is 4,324,398 bytes with SHA-256 `70f64c718510a601fbcf752aafe644314dacaeb85474dc689c89787c4a72a728`. The archive's `LICENSE` member matches the packaged complete Apache License 2.0 byte for byte: 11,357 bytes, SHA-256 `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`.

`NOTICE.txt` preserves Google Fonts / Google publisher attribution, copyright and designer attribution, the subset modification notice, and the no-endorsement statement. Its SHA-256 is `3a6665917a713a3762caf4c63385853eb1fa9fdbf5068eab93279d6ffa863222`. The manifest SHA-256 is `5a5862dcd4110c0e9c9bddd45d917e195f8804de8d55d03c65f5529c28ee2fd2`.

The generator `tools/generate_roboto_v2_138_package.py` uses the pinned development dependency `fontTools==4.60.1` in `tools/requirements-font13.txt`. The retained regeneration output was compared to the approved assets and produced the same three hashes below; see [font13r2-provenance-recheck.log](../../out/font13-validation/font13r2-provenance-recheck.log). The manifest marks each derivative `modified: true`; all outputs use the declared 922 Unicode mappings and carry the source metadata. The pre-existing fixture only supplied the mapping set; its bytes are not represented as upstream binaries.

| Approved file | Bytes | SHA-256 |
|---|---:|---|
| `Roboto-v2.138-Regular.ttf` | 88,780 | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| `Roboto-v2.138-Bold.ttf` | 90,556 | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| `Roboto-v2.138-Italic.ttf` | 94,736 | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |

These are exactly three registered faces and 274,072 font bytes. Each is within 192 KiB, and the total is within the existing 576 KiB limit. No fourth font is approved.

## 4. Packaging path and independent FAT readback

The modified top-level `build.ps1` passes `assets/Fonts/Roboto-v2.138-package` to the existing `scripts/generate-wallpaper-pack.ps1` packer. The packer stages the allowlisted package at the runtime image root `/fonts/`; that image is mounted at `/system`, where the font selection service discovers `/system/fonts/`. The package is self-contained: normal image generation reads repository assets and does not download the archive or require Python/fontTools at build or runtime.

The independent runtime-image build produced a 64 MiB FAT32 image with SHA-256 `a7cd4dd2943d733505835b747297937eceea394f219f01189dc5bb631f176dae`. Read-only FAT access found exactly these six entries under `/fonts/`, with no hidden or fourth TTF:

- The three approved TTFs with the exact hashes in Section 3.
- `LICENSE.txt`, `NOTICE.txt`, and `ApprovedFontManifest.json`, each byte-identical to its approved asset and hash listed above (manifest hash: `5a5862dcd4110c0e9c9bddd45d917e195f8804de8d55d03c65f5529c28ee2fd2`).

The private fresh-install ESP contains a `ramdisk.img` with the same 64 MiB SHA-256. The readback record is [font13r2-image-readback.log](../../out/font13-validation/font13r2-image-readback.log); its FAT library emitted an advisory type-detection warning and selected FAT32, while the image builder and guest both identify the volume as FAT32 and all six files read back successfully.

The 21-check package runner passed again with 0 failures: [font13r2-package-validation.log](../../out/font13-validation/font13r2-package-validation.log). It enforces the exact three-face set, bounded sizes, license presence, copy hashes, safe missing-optional-face behavior, strict missing-file rejection, and rejection of extra/hidden faces, unsafe names, path traversal, collisions, and unsupported data.

The normal packer handles absent or invalid optional font assets by warning, clearing the font staging directory, and continuing with bitmap fallback. This behavior is safe for boot but does not satisfy release package completeness. A qualified release image must show that the package was accepted and staged; the validated R2 packer log and FAT readback do so. The currently approved asset directory is untracked working-tree content because Outcome B does not authorize a commit, but the package is not sourced from untracked `out/` artifacts. It must be included in a later accepted commit before a clean checkout can build the font package.

## 5. PacMan link prerequisite

The normal `build.ps1 -Arch amd64` run stops at `[1c/6]`, before the `[1b/6]` runtime-image packer step. The exact symbols, source list, link command, current-source reproduction, root cause, historical pre-FONT13 failure, and owner acceptance are recorded in [FONT13R2_PACMAN_BUILD_HANDOFF.md](FONT13R2_PACMAN_BUILD_HANDOFF.md).

The classification is a missing provider object caused by PacMan package source registration: `main.cpp` calls `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)`; matching declarations and definitions exist in `pacman_audio_runtime.h` and `pacman_audio.cpp`; the six-object package link omits `pacman_audio.cpp`. It is not a font-induced regression, function-signature mismatch, missing library, feature-flag mismatch, or runtime-image build-order issue. The same two-symbol failure is present in the committed pre-FONT13 App Model Phase 13 build record.

The separate forced AMD64 freestanding kernel build passed previously, with a zero PE Import Directory. The independent runtime packer and FAT image readback also passed. Neither result is described as a passing normal top-level build. The full top-level build was not repeated because the current-source failure was freshly reproduced and no PacMan code was changed.

## 6. Fresh image and guest boot evidence

The successful isolated fresh-image boot already recorded for FONT13R was rechecked rather than duplicated. Its private 128 MiB writable FAT32 ESP was built through the existing release helper from the normal production boot files, the forced-AMD64 production kernel, and the independent runtime image. The source image had no font preference records (`noPreferencesInSource=true`). The recorded fresh runtime image hash is the same `a7cd…` hash in Section 4; the kernel hash is `9b8d1a86b5f2721f6a386fb40630aba85f842239d72b050a3f0a855fe2a668f3`. See [the launch and image records](../../out/font13-validation/fresh-clean-image-20261018/qemu-first-boot-retry-launch.json) and [the FAT32 ESP file report](../../out/font13-validation/fresh-clean-image-20261018/image-report.json).

Guest serial reports `[FONT6] system selection init=Ok active-font fallback-available=yes`, root and system VFS mount reports as FAT32 on block 0, `[NAVIGATOR-SMOKE] result=PASS`, and entry to the main input loop. The first desktop capture is [first-boot-desktop.bmp](../../out/font13-validation/fresh-clean-image-20261018/first-boot-desktop.bmp). These records establish the fresh image reached the desktop. Existing FONT6 selection tests establish Built-in Bitmap as the no-preference default. This guest did not open the Fonts page, so it does not provide a direct UI selection or package-enumeration result.

The readback confirms the three files were present before boot. Guest-side font discovery of those individual files was not logged, and the Settings Fonts page was not reached. No runtime download is involved or required.

## 7. Guest input feasibility and Settings acceptance

The QEMU launch record used a `pc` machine with the normal emulated PS/2 devices, plus `qemu-xhci` and `usb-tablet`. The guest identified PS/2 keyboard and mouse as available, selected PS/2 as the active mouse source, and reported USB HID as `N/A`. The USB tablet therefore does not qualify as an input path. The read-only Modern Input handoff at `D:\dev\guideXOSServer_MODERN_INPUT\docs\MODERN_INPUT_INPUT3D.md` says the current xHCI path has no HID interface discovery, interrupt-report delivery, report decoding, or routing to the input manager.

Keyboard input was delivered: Super toggled the Start Menu, confirmed by serial. A source audit found no keyboard handler that selects the Start Menu's Settings item; the key path handles desktop icon navigation, while the visible Settings entry in [settings-launch.bmp](../../out/font13-validation/fresh-clean-image-20261018/settings-launch.bmp) remained in the open menu. The serial log contains the two Start Menu toggles but no Settings launch. Previous PS/2 and USB-tablet pointer attempts did not reliably reach/click Settings. FONT12R3 documents the measured pointer underdelivery and its stop rule in [FONT12R3_RESULTS.md](../evidence/font12r3/FONT12R3_RESULTS.md).

R2 sent no new guest input and launched no new QEMU process. The bounded rule was one keyboard-only route to Settings using the existing device and source evidence, with no repeated mouse movements after the recorded underdelivery. The existing keyboard event reached the Start Menu, but the source audit and resulting screenshot showed no key path to select Settings, so interaction stopped before a Settings action. For a future input-qualified run, retain the existing limit of at most two deliberate resize drags and stop on the first cursor undershoot; do not issue a drag unless telemetry confirms the grip was reached.

| Acceptance item | R2 status |
|---|---|
| Fresh install with no preference selects Built-in Bitmap | **Engine/service default retained from FONT6 tests;** clean guest had no preference and no selection change, but Settings was not opened for a live page confirmation. |
| Fonts page lists the three packaged faces | **Unqualified in guest.** FAT image contains exactly the three faces; no live enumeration was observed. |
| Preview shows genuine packaged Roboto text | **Unqualified in guest.** |
| Apply completes and shell surfaces visibly change | **Unqualified in guest.** |
| Fresh QEMU process restores selected font | **Unqualified in guest.** |
| Restore Defaults returns to bitmap | **Unqualified in guest.** |
| Removing a configured font safely falls back | **Engine fallback tests remain accepted; guest removal/fallback is unqualified.** |
| Settings Fonts page at 560×360 with TTF selected | **Unqualified;** marker remains open. FONT8 is bitmap-only at that size and FONT12R3 did not reach the live resize grip. |

## 8. Validation and preserved evidence

Current R2 checks:

- Approved package safety: **21 checks, 0 failures**.
- Upstream archive, source members, and Apache license bytes/hashes matched the manifest.
- FAT readback: exactly six expected files, all notice/manifest/TTF contents matched the package assets, and no extra TTF.
- Current-source PacMan link reproduction: both expected audio symbols unresolved when the six registered objects are linked.

Previously accepted results remain preserved and applicable because R2 changed no production or test source:

- **6,681 FONT2–FONT11 checks** across eight standalone runners.
- Existing parser/raster tests for the three packaged faces, Settings/native regression suites, ABI and filesystem contract runs.
- Forced AMD64 freestanding kernel build and zero PE imports.
- Existing FONT12R/R2/R3 interaction and minimum-size evidence, including its explicit limitation.

Those broader suites and the forced kernel build were not rerun to duplicate unchanged evidence. The separate full top-level build remains blocked at PacMan.

## 9. Integration decision and owner work

| Qualification state | Result |
|---|---|
| Font engine | Previously accepted renderer, cache, identity, persistence, and fallback contracts remain sound; no FONT13R2 production change. Guest Settings visual acceptance and the minimum-size gate remain open. |
| Font package | Approved bytes, upstream identity, Apache 2.0 license, attribution, manifest, reproducible subset outputs, three-face bounds, independent FAT staging, and clean-image inclusion are verified. The ordinary top-level build and guest enumeration are still incomplete. |
| Combined guideXOS Server v0.3 release | **Not qualified.** The top-level PacMan prerequisite, real Settings flow, guest restart/fallback, and 560×360 TTF acceptance remain open. |

Minimal next acceptance by owner:

1. **PacMan Native ELF workstream:** compile/link the missing audio provider object from a fresh object directory, validate any required audio resource staging separately, and rerun the complete top-level AMD64 build through runtime image and ESP creation.
2. **Modern Input workstream:** qualify a supported keyboard or pointer route from QEMU device through the real desktop action into Settings; record the event and Settings launch, then open the actual Fonts page. The INPUT3D diagnostic report alone does not qualify USB HID.
3. **Settings/compositor acceptance:** on that integrated input path, prove the same live TTF Settings window reaches 560×360, handles a below-minimum clamp, and retains readable/hit-testable controls without changing the selection unintentionally.

Keep FONT_SUPPORT conditionally qualified for controlled integration only. Do not advance phase tracking, merge, commit, push, start FONT14, or declare the combined v0.3 release accepted. No commit SHA was created.

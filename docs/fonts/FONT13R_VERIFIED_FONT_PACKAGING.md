# FONT13R — Verified TrueType Packaging & Clean-Install Qualification

- **Date:** 2026-10-18
- **Phase:** FONT13 continuation; integration preparation only
- **Outcome:** **B — Partial**
- **Branch / HEAD:** `FONT_SUPPORT` / `89a58151c19da3d6532d838ae3a29a2c0ab4711a`
- **Minimum-size gate:** `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open

FONT13R resolves the upstream identity of a suitable Roboto source and adds a bounded, license-complete package to the existing runtime-image packer. The package and staging path pass automated checks. Outcome A is not claimed: the full `build.ps1` flow stopped at an unrelated PacMan link error, and the fresh guest could not be driven through the Fonts page because mouse movement was not reliably delivered. The pointer limitation and Settings acceptance gap remain with the input workstream.

This document supplements the historical FONT13 report; it does not start FONT14, advance phase tracking, authorize integration, or close the desktop minimum-size gate.

## 1. Repository and phase safety

- The requested starting branch and HEAD matched. Ending branch and HEAD are unchanged.
- Existing uncommitted FONT12R changes and all pre-existing evidence were preserved. New package and FONT13R implementation files are uncommitted working-tree additions because Outcome A was not reached. All generated evidence is under `out/`.
- No reset, clean, stash, checkout, branch operation, worktree, integration, or push was performed. No other guideXOS checkout or foreign QEMU guest was changed.
- The existing two-slot FONT9 preference format and rollback code were not changed. No kernel input, mouse, HID, or window-manager implementation was changed.
- The pre-existing marker `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` remains open. FONT8 showed the bitmap page at 560×360, but it did not establish a same-instance minimum clamp or hit-test trace. FONT12R3 later reached only about `(501,301)` while targeting the resize grip at `(850,565)`, so its minimum-size acceptance remained open. FONT13R's pointer behavior is consistent with that known input limitation; it does not close the gate.

## 2. Authoritative source and license

The package is derived from Google's official static Roboto release, not from the four development fixture binaries already in `assets/Fonts/roboto/`.

| Field | Verified value |
|---|---|
| Publisher / repository | Google Fonts / Google; [googlefonts/roboto-2](https://github.com/googlefonts/roboto-2) |
| Release | `v2.138`, dated 2017-08-02 |
| Tag commit | `ab4876f650a4aeb9b661c83fee47f908c739bb70` |
| Selected archive | `roboto-unhinted.zip` |
| Pinned download | [v2.138/roboto-unhinted.zip](https://github.com/googlefonts/roboto-2/releases/download/v2.138/roboto-unhinted.zip) |
| Archive size / SHA-256 | `4,324,398` bytes / `70f64c718510a601fbcf752aafe644314dacaeb85474dc689c89787c4a72a728` |
| Applicable license | Apache License 2.0, complete text shipped as `LICENSE.txt` |
| Upstream license path | [googlefonts/roboto-2 v2.138 LICENSE](https://github.com/googlefonts/roboto-2/blob/v2.138/LICENSE) |
| License size / SHA-256 | `11,357` bytes / `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4` |

The exact extracted source members are `Roboto-Regular.ttf` (`349,400` bytes; SHA-256 `f3edb8058e523f5612bfd99d0745e661568ad85e1b6217bc62f786fabae624c6`), `Roboto-Bold.ttf` (`351,108` bytes; SHA-256 `2ca2b3bfc2c2d43fa8f2b7227982d735fd537ecf113a4c56cc2d292bcc3106c8`), and `Roboto-Italic.ttf` (`375,092` bytes; SHA-256 `2b4076dd3f5a7fadfca760ef45557a0d437af983284652fc149535c5bd93d493`). The generator validates the archive SHA-256, member names, sizes, and member hashes before subsetting.

The `google/fonts` Roboto `OFL.txt` linked by FONT13's request is a current SIL OFL 1.1 reference. It is **not** the applicable license for these 2017 v2.138 source binaries. The release archive itself supplies Apache 2.0, and that complete license text accompanies the distributed files. Font metadata in each source and output reports family `Roboto`, the corresponding `Regular`, `Bold`, or `Italic` style, version `2.138`, copyright `Copyright 2011 Google Inc. All Rights Reserved.`, designer `Christian Robertson`, and embedded license text `Licensed under the Apache License, Version 2.0` with `http://www.apache.org/licenses/LICENSE-2.0`.

The output fonts are modified derivatives: `fontTools==4.60.1` subsets each upstream face to the 922 Unicode mappings used by the existing tracked Roboto regular fixture. That fixture supplies only the mapping set; none of its font bytes are copied into the package. The upstream archive files are unhinted, and the output's outlines remain standalone `glyf`. `ApprovedFontManifest.json` records `modified: true`, all source/output hashes, metadata, license hashes, and limits.

The existing four files under `assets/Fonts/roboto/` have not been proven to come from v2.138. Their embedded family or license text is not provenance evidence. No such relationship is claimed, and the files were left unchanged.

## 3. Approved package

The proposed distributable asset directory is `assets/Fonts/Roboto-v2.138-package/`. Only these three fonts are approved:

| File | Family / style | Bytes | SHA-256 | Format qualification |
|---|---|---:|---|---|
| `Roboto-v2.138-Regular.ttf` | Roboto / Regular | 88,780 | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` | `glyf`, cmap 4, 922 mappings, 1,019 glyphs |
| `Roboto-v2.138-Bold.ttf` | Roboto / Bold | 90,556 | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` | `glyf`, cmap 4, 922 mappings, 1,019 glyphs |
| `Roboto-v2.138-Italic.ttf` | Roboto / Italic | 94,736 | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` | `glyf`, cmap 4, 922 mappings, 1,019 glyphs |

Total font payload is **274,072 bytes**. Each face is below 192 KiB; three registered faces are within the three-face limit; total source bytes are below 576 KiB (589,824 bytes). No CFF, TTC, variable, color, or extra font file is present. The package also contains:

- `LICENSE.txt`: complete Apache License 2.0; SHA-256 `c71d239df91726fc519c6eb72d318ec65820627232b2f796219e87dcf35d0ab4`.
- `NOTICE.txt`: Google attribution and modification notice; 1,423 bytes; SHA-256 `3a6665917a713a3762caf4c63385853eb1fa9fdbf5068eab93279d6ffa863222`.
- `ApprovedFontManifest.json`: pinned source and output metadata; 5,965 bytes; SHA-256 `5a5862dcd4110c0e9c9bddd45d917e195f8804de8d55d03c65f5529c28ee2fd2`.

The longest filename is 25 characters including `.ttf` (Bold 22, Italic 24, Regular 25). The VFS accepts FAT long names up to 128 characters and performs case-insensitive FAT lookup; its enumerated canonical paths are uppercase. The exact package filenames were read back from the FAT image. The auxiliary `pyfatfs` readback tool preserves display case and is case-sensitive for lookup, so its failed lowercase lookup is not treated as a production VFS failure. The production-side behavior is covered by the font identity test and FAT name-matching implementation.

## 4. Reproducibility and packaging safety

`tools/generate_roboto_v2_138_package.py` regenerates the approved subset from the pinned archive. The development-only dependency is pinned in `tools/requirements-font13.txt` (`fonttools==4.60.1`). A regeneration from the recorded archive reproduced all three output hashes. The source ZIP is retained only as development evidence below `out/font13-validation/`; it is not needed by normal builds or runtime.

The chosen release model is repository assets, not a dependency package fetched during build: the three exact TTFs, complete license, notice, and approved manifest live in the repository asset directory (currently uncommitted additions that would need to be committed before integration). The normal build consumes those approved files. It does not download from GitHub, extract an archive, depend on developer-specific `out/` files, or require `fontTools`. No second installer or parallel image pipeline was introduced.

`build.ps1` passes the approved package directory into the existing `scripts/generate-wallpaper-pack.ps1` flow. That packer stages the allowlisted package under its existing `fonts/` image directory. The runtime image is mounted at `/system`, so those direct children are discovered at `/system/fonts/`. Validation rejects reparse points, unexpected names/directories, case-insensitive destination collisions, traversal-like names, unsupported TTF signatures, duplicate family/style identities, oversized assets, totals above the configured bound, excess faces, and missing license/notice files. Enumeration is capped at six package entries. The packer verifies size and SHA-256 before copying and verifies copied bytes; it never extracts source archives. Missing optional TTF files are omitted with a warning while image creation continues; strict qualification requires the full three-face package. A fourth or hidden font cannot be staged.

The existing runtime image builder produced a 64 MiB FAT32 `ramdisk-rebuilt.img` using the approved package. Readback found exactly the manifest, license, notice, and three font files under `/fonts/`, with exact hashes and no other font. A second image built through the same packer with one optional TTF removed contained only the two remaining faces and notices. This verifies omission/staging behavior, not guest fallback from a saved font preference.

The full top-level `build.ps1 -Arch amd64` run did not complete. It stopped in the PacMan package step with unresolved `pacman_audio_load_resources(gx_app_context*)` and `pacman_audio_submit(void*, PacManSoundId)` symbols in the configured sibling PacMan source. This is why the existing normal image packer was validated directly and a fresh private image was assembled using the existing ESP release helper; the complete normal clean-install build gate remains incomplete.

## 5. Identity and upgrade compatibility

The FONT6 identity contains the canonical source path, validated family/style, file length, and content fingerprint. A matching family name alone cannot select or restore a different binary. The packaged canonical FAT paths are distinct versioned names such as `/system/fonts/ROBOTO-V2.138-REGULAR.TTF`.

- A fresh install with no saved font selection starts on Built-in Bitmap. The package does not write a preference or change the default.
- An explicit persisted bitmap selection remains bitmap when packaged TTFs are present.
- An existing preference for a binary with the same path, family/style, length, and fingerprint can restore that same identity. The new versioned paths/hashes do not impersonate old Roboto files.
- A preference for an old/different custom binary is not silently rewritten or matched by family name. Existing custom source files are not replaced by packaging. If the configured file is missing, unreadable, corrupt, or has a changed fingerprint, the existing validated behavior activates bitmap fallback.
- Corrupt or unsupported preference versions are rejected and resolve through the existing fallback policy. A valid missing-font record may remain configured for diagnosis while bitmap is active, as specified by FONT6/FONT9.
- FONT9's two-slot transactional persistence and rollback contract is unchanged.

`tests/system_font_selection_test.cpp` now adds an exact package fixture check for all three identities, clean bitmap default, explicit bitmap persistence, applying Regular at 18 px, drawing, fresh selection-service restart restoration, and reset to bitmap. It does not replace the guest restart qualification required for Outcome A.

## 6. Clean-install and guest evidence

A private 128 MiB writable FAT32 block image was created with `tools/release_esp.py` from tracked ESP contents, the existing release bootloader, the forced-AMD64 kernel output, and the freshly packaged runtime image. It was not seeded from FONT5–FONT12 proof images and contained no `/fontsel0.cfg` or `/fontsel1.cfg` preference. Private OVMF, serial, QMP, and VNC paths were used. A fresh QEMU ownership census was captured before launch; the two foreign guests observed in that census were left untouched.

The guest reached the desktop and logged `[FONT6] system selection init=Ok active-font fallback-available=yes` and `[NAVIGATOR-SMOKE] result=PASS`. The captured first desktop screenshot is `out/font13-validation/fresh-clean-image-20261018/first-boot-desktop.bmp`; it shows the clean desktop before any TrueType selection. The boot included the packaged runtime filesystem. The launch and hash record is `qemu-first-boot-retry-launch.json`, with the corresponding prelaunch ownership census, image report, and serial/debug logs beside it. The package tests and selection tests establish the clean bitmap default; this guest run did not visually open the Fonts page.

QEMU keyboard input opened the Start Menu. Mouse movement did not provide reliable two-axis pointer delivery: USB absolute pointer events did not move the visible pointer, and PS/2 input was only partially effective. Settings could not be selected reliably. Therefore the following FONT13R guest acceptance items remain **unqualified**:

1. Real Settings Fonts page enumeration and exact family/style display.
2. Packaged font preview and Apply, and visible TrueType shell rendering.
3. Preference restoration after a fresh guest process restart, then Restore Defaults to bitmap.
4. Guest fallback after removing/corrupting a configured packaged face.

FONT8 and FONT10/FONT11/FONT12 prove earlier guest behavior with development fixtures, but do not substitute for these checks with this v2.138 package. The pointer limitation is handed to the owning input workstream; see the existing FONT12R3 result and the pointer note added to the FONT13 integration handoff. No input code was modified. The minimum-size acceptance remains open.

## 7. Validation record

Automated package validation passed **21 checks, 0 failures**, including exact contents/hashes, byte-preserving copy, optional missing face, strict missing-file rejection, oversize input, unsupported OTTO signature, duplicate family/style, fourth-face capacity, traversal path, hidden extra font, directory-entry bound, missing license, and case-insensitive collision. Packer readback confirmed exact bytes for the full and missing-face images.

The exact three approved derivatives each passed the FONT3 parser and FONT4 raster suites:

| Face | FONT3 checks | FONT4 checks | Failures |
|---|---:|---:|---:|
| Regular | 1,152 | 4,400 | 0 |
| Bold | 1,135 | 4,377 | 0 |
| Italic | 1,145 | 4,400 | 0 |

The complete available FONT2–FONT11 focused host suite passed **6,681 checks, 0 failures across eight standalone runners**: FONT2 contract 196; historical FONT3 parser fixture 1,150; historical FONT4 raster fixture 4,456; FONT5 cache 277; FONT6 discovery/identity/persistence 420; FONT8 text layout 46; FONT10 shell typography 88; FONT11 cache workload 48. There are no dedicated FONT7 or FONT9 standalone runners; FONT7's native Settings behavior has earlier guest evidence, while current Settings model/presentation checks and FONT9 persistence are covered by the related runs below and by the FONT6 selection suite.

The added Settings runs passed: Settings Center 89/89, S6 model 42/42, S7 model 58/58 (21 accessibility, 23 developer, 14 navigation/search), presentation core 2,651/2,651, and native runtime 828/828 with 100 lifecycle repetitions, 250 refreshes, and 1,000 state transitions. Native presentation, ABI layout, and filesystem contract tests also passed. No new hosted PE import was introduced.

The forced AMD64 freestanding kernel build succeeded in the new output directory `out/font13-validation/forced-amd64-20261018/`. `kernel.elf` SHA-256 is `9b8d1a86b5f2721f6a386fb40630aba85f842239d72b050a3f0a855fe2a668f3`; `kernel.pe` SHA-256 is `ae11d162bf40526e52581ef29d226645a90e7bfc26c11b1718a15f9f8489f925`. `objdump -p` reports the PE Import Directory RVA and size as zero. The separate full top-level build result is the PacMan failure described above.

## 8. Integration manifest and remaining gates

Distribute the three TTFs listed in Section 3 together with `LICENSE.txt`, `NOTICE.txt`, and `ApprovedFontManifest.json`. These are uncommitted working-tree additions in this Outcome B result. Retain `tools/generate_roboto_v2_138_package.py` and the pinned dev dependency as provenance/rebuild tooling. The binary release package itself is self-contained and has no network dependency.

- **Storage:** 274,072 font bytes plus 18,745 bytes of manifest/license/notice, approximately 292,817 bytes in the 64 MiB runtime image.
- **Memory:** no font limit or static buffer changed. Existing bounded selection storage remains the limit: at most 192 KiB per external face, three registered faces, and 576 KiB total font source. FONT9 recorded about 223 KiB selection-service static BSS; this package adds no additional kernel-resident copy or allocation.
- **Filesystem:** FAT32 long filenames and case-insensitive VFS lookup; direct `/system/fonts/` child layout; current package names use at most 25 characters.
- **Build:** use the existing `build.ps1` to runtime-image packer path and tracked approved assets. Normal build completes only when its existing PacMan prerequisite is resolved. No runtime network or Python/fontTools requirement.
- **Preferences:** preserve explicit bitmap and compatible exact binary identity; different, missing, changed, corrupt, or unsupported values follow FONT6/FONT9 fallback behavior. No old custom file is overwritten.
- **Open acceptance:** finish the full normal clean-image build; complete real guest Settings enumeration/preview/Apply/shell use/restart/Restore Defaults and configured-font missing/corruption fallback; hand off and resolve the pointer-delivery limitation; retain `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN` until its owning acceptance passes.
- **Post-integration smoke:** verify installed hashes and notices on a clean release image, fresh bitmap default, Settings enumeration and Apply, shell redraw, fresh-process persistence, explicit bitmap preservation on upgrade, changed/missing-file fallback, and Restore Defaults.
- **Concurrent branches:** review Settings page lifecycle and active selection reflection together with current Settings/compositor branches. Recheck VFS casing, image mount timing, boot ordering, and input event delivery at their actual integration tips.

**Conclusion:** the FONT13R package has authoritative v2.138 provenance, a complete applicable license, bounded approved assets, normal runtime-image staging, deterministic package tests, and passing parser/raster and forced AMD64 build evidence. Clean-image packaging and guest boot were demonstrated in a private test image, but full normal build and real Settings interaction/persistence/fallback qualification remain incomplete. Outcome **B — Partial**; no phase advance, commit, merge, push, or release sign-off.

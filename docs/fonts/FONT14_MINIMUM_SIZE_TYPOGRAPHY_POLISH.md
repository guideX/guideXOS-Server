# guideXOS Server — FONT14 Minimum-Size Layout & Desktop Typography Polish

**Outcome: A — hosted typography qualification accepted.** The production Settings layout and resize clamp are exercised through shared production routines at the documented minimum and larger sizes. One minimum-size preview clipping defect, several draw/hit rectangle mismatches, and preview overscroll were corrected. The real-guest TTF minimum-size marker remains open; this result does not qualify the combined v0.3 release.

**Date:** 2026-10-20

**Checkout:** `D:\dev\guideXOSServer_FONT_SUPPORT`

**Branch:** `FONT_SUPPORT`
**Starting HEAD:** `89a58151c19da3d6532d838ae3a29a2c0ab4711a`

## 1. Repository state and scope

The requested branch and starting commit were present. The worktree already contained tracked FONT12 changes and untracked FONT11R/FONT12R/FONT13 reports, guest evidence, validation outputs, and the approved Roboto v2.138 package. These were preserved. No reset, clean, stash, branch operation, worktree, merge, rebase, cherry-pick, push, or change to another checkout was performed.

FONT14 changed only the native Settings/compositor presentation path, its shared production geometry helpers, a hosted regression test and runner, and this report. The approved font assets and packaging scripts were not changed. Build and regression output is in `out/font14-validation/`; it is not part of the FONT14 commit.

The top-level release build was not used as FONT14 evidence. The known separate PacMan Native ELF prerequisite remains documented by FONT13R2. DeveloperStudio gates and the other named checkouts were not touched.

## 2. Settings minimum-size qualification

The Settings window minimum is an **outer** size of 560×360. The compositor subtracts its 24 px title bar before invoking the app draw callback, so the production Settings client layout is 560×336. The test converts outer dimensions to that same client geometry, then calls `settings_fonts::CalculateLayout`, which is used by production drawing and input handling. Pointer resize and registration now call the shared `WindowDimensionAfterResize` / `ClampWindowDimensionToMinimum` functions in the production compositor.

| Requested outer size | Production client size | Fonts preview box | Inner preview clip | Result |
|---|---:|---:|---:|---|
| 560×360 | 560×336 | 306×33 | 302×29 | Minimum geometry passes |
| 561×361 | 561×337 | 307×33 | 303×29 | Pass |
| 640×400 | 640×376 | 386×45 | 382×41 | Pass |
| 760×500 | 760×476 | 506×145 | 502×141 | Pass |
| 480×300 resize request | Clamped to 560×360 outer, 560×336 client | 306×33 | 302×29 | Production minimum clamp passes |

At 560×360, the Settings navigation has no overlap with the content column. The four font choices use two columns when card height is compact: each visible and clickable row is 149×16 px, with a 4 px vertical and 8 px horizontal gap. The font list, current selection, selected source, size controls, preview, status, footer, Back button, and all three actions remain inside the client window and card.

The Back target is 88×24 px; size controls are 24×22 px; Apply, Cancel, and Restore Default are 60×22, 60×22, and 102×22 px. Production drawing and `HitTestFonts` use those same rectangles. Navigation fills, selected-focus outlines, and navigation hit regions likewise share the same 162×38 px rectangles. The compact layout preserves the existing 16 px font-choice target height.

The test checks both included corners of every font row, navigation boundary exclusion, representative size and Back targets, and both included corners of all three actions. Production drawing and input consume the same rectangles. Preview and clip rectangles are contained by the card and window. The below-minimum case goes through the same resize dimension helper used by the compositor; it does not substitute a test-only clamp or layout.

## 3. Clipping, preview, and Settings corrections

The previous single-column page placed its preview 162 px below the card top. With the 214 px card at minimum size, the preview was only 5 px high and its inner text clip was 1 px. That made the preview unusable.

The production layout now packs the four choices into two columns when the card height is below 240 px, then moves the source, size, and preview labels up while retaining their order and avoiding overlap. This creates a 33 px preview box and 29 px text clip at minimum size. At 18 and 24 px, each approved face's measured line height fits that minimum clip. At 48 px, the real glyphs are clipped within the preview rectangle and can be inspected by scrolling.

The previous preview scroll maximum was fixed at 320 px. At minimum geometry that could scroll all three 48 px samples completely out of the 33 px viewport. Scroll range now derives from the three sample steps and the actual preview height; the minimum-size 48 px maximum is 156 px. The draw path also clamps an existing scroll position after a font-size or preview-size change, so reducing the size cannot leave the preview blank.

The same page layout now drives drawing and pointer input. This fixes the prior row hit region extending 16 px beyond the drawn row, the Back/tile rectangles not matching their visible frames, and 1–4 px mismatches on navigation, size, and action targets. The compact font rows retain 16 px height. Arithmetic for pointer resize now uses 64-bit intermediate values and saturates before conversion, covering signed pointer-coordinate extremes without integer overflow.

The 496-check hosted test loads the actual approved Roboto Regular, Bold, and Italic files. At 8, 18, 24, and 48 px it measures and rasterizes representative preview text, tests the supported UTF-8 string `Café — Roboto`, verifies the missing-glyph fallback, checks that raster writes remain inside the preview clip and buffer guards, and verifies warm redraws do not rasterize again. It also confirms layout calculation does not change the font-manager generation. `Layout` is 484 bytes on the hosted target.

## 4. Desktop typography audit

The adopted desktop roles remain bounded by their measured rectangles and keep the existing bitmap fallback:

| Surface | Qualification and finding |
|---|---|
| Settings labels and navigation | The real Fonts page uses fitted text and bounded preview clipping. Selected navigation text and focus border use the same navigation rectangle as input. The min-size regression found and corrected geometry defects described above. |
| Window titlebars | FONT10's 88 checks pass for measured baselines, negative bearings, long titles with ellipsis, and active/inactive title colors. The compositor reserves the fixed window-control area before laying out title text. |
| Taskbar labels and clock/date | FONT10 passes narrow-label ellipsis, independent task-label alignment, horizontal clock/date rectangle and baseline checks, and minute-update generation stability. |
| Start Menu | FONT10 covers the Start Menu label role, fitted placement, and bitmap fallback for invalid UTF-8 or stale handles. Existing menu hit targets remain unchanged. |
| Control Panel | The existing measured-label path and bitmap fallback remain in place. FONT8's prior live Control Panel capture and accepted layout coverage remain applicable; FONT14 found no additional defect and made no Control Panel change. |

The bounded boundary strings include long titles, narrow taskbar labels, variable-width `W`/`i` cases, the full clock/date strings, supported accented UTF-8, a supported em dash, an absent glyph, and invalid UTF-8. FONT10 continues to cover negative bearings, vertical centering, bitmap fallback, and generation changes. No theme, shell redesign, or unrelated app-owned UI was changed.

## 5. Bitmap fallback and recovery

The FONT6 system-font-selection suite passed 420/420 checks. Its exercised paths include missing selected files, malformed and unsupported preference records, checksum-corrupt slots, explicit Built-in Bitmap selection, Restore Default, candidate and generation invalidation, transactional save failures, and preview isolation/bitmap pixels. FONT10 also confirms stale-handle and malformed-input fallback to bitmap. The previously accepted FONT8 guest captures show Bitmap 7 px restored and missing-font recovery on the real Settings page: [bitmap restored](../evidence/font8/settings-bitmap-restored.png), [missing-font recovery](../evidence/font8/missing-font-settings-recovery.png).

FONT14 did not change FONT9 persistence serialization, kernel boot font dependencies, bitmap-only recovery interfaces, or the built-in bitmap renderer. Missing or invalid external fonts continue to leave the system on the bitmap face without requiring bundled TTF files for boot.

## 6. Memory, cache, and bounded work

The layout helper has no static storage, heap use, font loading, or cached font handles. It uses fixed-size rectangles and fixed choice arrays. The new `settings_fonts_layout.o` has 0 bytes of `.bss`; the local `Layout` value is 484 bytes. The inspected font subsystem objects sum to 1,143,712 bytes of `.bss` (about 1.09 MiB), within the previously documented approximately 1.09 MiB budget. The fixed subsystem cache limits are unchanged.

FONT11's 48-check workload passes with 342 hits, 65 misses/rasterizations, zero evictions, 65 entries, and 4,038 peak coverage bytes. The new real-package test confirms immediate warm redraws add hits without adding rasterizations, keeps entry/coverage usage within the declared bounds, and unloads each temporary hosted face after its checks. Layout and resize do not reload source fonts or retain generation handles.

## 7. Regression results

| Suite | Result |
|---|---:|
| FONT2 shared contract | 196/196 |
| FONT3 TrueType parsing/loading | 1,150/1,150 |
| FONT4 rasterization | 4,456/4,456 |
| FONT5 glyph cache | 277/277 |
| FONT6 selection/persistence/fallback | 420/420 |
| FONT8 text layout | 46/46 |
| FONT10 shell typography | 88/88 |
| FONT11 cache workload | 48/48 |
| **FONT2–FONT11 total** | **6,681 checks, 0 failures** |
| FONT14 Settings geometry/package typography | 496/496 |
| Settings Center / Default Apps / Inventory | 89/89; 48 checks plus 100 lifecycle cycles; 45/45 |
| Settings presentation core / native runtime | 2,651/2,651; 828/828 plus 100 lifecycle repetitions, 250 provider refreshes, and 1,000 state/notification transitions |
| Settings S6 / S7 / Users | 42/42; 58/58; 32/32 |
| Settings freestanding harness | PASS; bounded link and unresolved-symbol checks |
| Native present validation | PASS |
| Compositor pointer buttons / resize notification | PASS / PASS |
| App Model application actions / folder activation | 55/55; 26/26 |
| App Model default handlers / URI protocol | 54/54; 41/41; each also passed its 100-cycle stress paths |
| Approved font package validation | 21/21 |

The existing hosted FONT-numbered regression executables are FONT2, FONT3, FONT4, FONT5, FONT6, FONT8, FONT10, and FONT11; there are no separate FONT7 or FONT9 hosted executables. FONT6 exercises system-font selection and persistence, while the earlier FONT7 and FONT9 guest evidence remains preserved.

Per-run logs are under [`out/font14-validation/logs`](../../out/font14-validation/logs), with test executables under `out/font14-validation/tests`.

## 8. Forced AMD64 build and freestanding review

The forced AMD64 freestanding kernel build completed with the repository's `-ffreestanding`, `-nostdlib`, `-nostdinc++`, `-fno-exceptions`, and `-fno-rtti` flags. The final PE is 2,944,724 bytes (SHA-256 `347f47839c73bc0ede874c4298c6fc70e28c1e67a02fee26e5d1d91379b123ed`); the ELF is 2,819,544 bytes (SHA-256 `f53974791a47aa491c3091df8e576e2107ff6dd39cdeaf269355972717bdcc04`). PE Import, Import Address Table, and Delay Import directories are all zero. The new `settings_fonts_layout.o` has zero `.bss`; the new production code uses no STL, exception/RTTI feature, hosted runtime, dynamic allocation, unbounded loop, or new persistent font storage.

This was a forced kernel build and link only. No top-level guideXOS Server release build was attempted or claimed.

## 9. Approved font package preservation

All six approved package files were hash-checked after FONT14. The three font hashes remain:

| Approved face | SHA-256 |
|---|---|
| Roboto v2.138 Regular | `1078dedd79c55859b102e3ea141d8a74e5fb1f8f28a15d155bf7fc9b5b628346` |
| Roboto v2.138 Bold | `76a6c0f614d0e3e66f111ba0c9dcf266bfe6b1cd80f67fdaa87b722a3642bbad` |
| Roboto v2.138 Italic | `3bbf16d61be1f6b764aa91e74ea29ae9d0eb5e90615d86312da699fa637fc982` |

The complete Apache 2.0 license, Google attribution notice, and approved manifest also retain their FONT13R2 hashes. The 21-check package runner confirms exact three-face membership, manifest identity and bounds, license bytes, strict missing-file behavior, and rejection of extra faces and unsafe package entries. Existing FAT staging behavior, bitmap clean-install default, three-face registry limit, and safe optional-font handling were not changed. Full hashes are in the FONT13R2 closeout and the package test log.

## 10. Guest boundary and v0.3 recommendation

No QEMU mouse attempt was made. The known relative-pointer underdelivery remains the only established guest route for this window, and no supported mechanism in this checkout directly resizes the live Settings instance through production validation. The existing FONT12R2 same-window resize evidence is preserved; it demonstrated 825×555, not the minimum.

Keep `TTF_MINIMUM_SETTINGS_SIZE_ACCEPTANCE_OPEN`. The hosted result proves the production minimum layout, hit targets, text metrics, clipping, and clamp; it does not claim a live 560×360 TTF Settings window or guest minimum acceptance.

Recommend FONT14 for controlled v0.3 integration as **hosted typography qualification**. Keep the real-guest minimum-size check and separate PacMan/top-level build prerequisite open. Do not upgrade the earlier conditional qualification reports or claim combined v0.3 release readiness from this phase.

## 11. FONT14 changes

- Shared Settings Fonts geometry and hit-test routines used by the real production draw and pointer paths.
- Shared overflow-safe minimum resize calculation used by the compositor.
- Responsive compact font selector, minimum preview sizing, and content-derived preview scrolling.
- Hosted Settings geometry and approved-package typography regression test and runner.
- This qualification report.

No approved font bytes, package manifests, licensing files, or release gates changed.

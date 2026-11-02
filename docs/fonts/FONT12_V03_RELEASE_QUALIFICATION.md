# guideXOS Server — FONT12: v0.3 Font Subsystem Release Qualification

**Recommendation: Outcome B — conditionally qualified.** FONT2–FONT11 functionality, the freestanding AMD64 build, and the exercised FAT32 guest Apply/recovery flows pass. No font-specific correctness or safety blocker was found. Full v0.3 signoff remains conditional on triaging two unrelated native regression build failures and closing the remaining live-desktop checks listed below.

## Repository and scope

- Checkout: D:/dev/guideXOSServer_FONT_SUPPORT
- Branch: FONT_SUPPORT throughout; no branch or Git topology changes.
- Starting HEAD: b99a9254d55630c1fbf5ca1ba6ca8721d6474075, as requested.
- No production source changes were made by FONT12. The initial worktree contained untracked artifacts from earlier font phases; they were preserved. FONT12 adds this report and selected evidence under docs/evidence/font12. Build/test outputs and private guest disks remain in untracked out/font12-validation or the task's isolated Codex evidence directory.
- This qualification stops at FONT12. No merge, push, or FONT13 work was started.

The audited history is [FONT1](FONT1_INFRASTRUCTURE_AUDIT.md) through [FONT11](FONT11_CACHE_AND_SHELL_POLISH.md), including FONT11R's actual Apply and restart continuation. Source links below identify the current FONT_SUPPORT implementation.

## Feature completeness matrix

| Area | v0.3 scope and qualification | Implementation references |
|---|---|---|
| Shared drawing contract and bitmap recovery | A bounded API covers font registration, metrics, glyph lookup, raster output, UTF-8 text layout, and the built-in bitmap backend. Bitmap remains the early-boot and failure fallback. FONT2 contract tests and Settings/desktop tests pass. | [contract](../../kernel/core/font_contract.cpp), [bitmap backend](../../kernel/core/font_bitmap_backend.cpp), [FONT2 report](FONT2_SHARED_FONT_CONTRACT.md) |
| TrueType parsing and discovery | Accepts single-face TrueType-outline sfnt files with required outline/metric/name/cmap tables and supported Unicode cmap formats 4/12. Bounds: 192 KiB/file, 64 tables, 16,384 glyphs, 3 external faces, 576 KiB total external source bytes, and 64 discovered entries. TTC, CFF, variable fonts, WOFF, color fonts, and hinting are unsupported. | [format limits](../../kernel/core/include/kernel/font_truetype.h#L17-L31), [parser checks](../../kernel/core/font_truetype.cpp#L327-L409), [FONT3 report](FONT3_TRUETYPE_LOADING_AND_VALIDATION.md) |
| Outline decoding and rasterization | Supports simple and composite TrueType outlines, affine component transforms, grayscale coverage, and bounded scan conversion. Limits include 1,024 points, 128 contours, 8,192 flattened edges, 256 composite components, depth 8, 128×128 output, and 32 million scan-work units. No hinting or broad-script shaping is claimed. | [raster bounds](../../kernel/core/font_truetype_raster.cpp#L20-L27), [composite decoding](../../kernel/core/font_truetype_raster.cpp#L611-L756), [FONT4 report](FONT4_SCALABLE_GLYPH_RASTERIZATION.md) |
| Ownership, handles, and cache | Loader records own fixed source copies; clients do not retain pointers into files. Manager locking serializes raster scratch use. Handles and prepared candidates are checked against slot, identity, and generation; cache keys include generation and are invalidated when a font is released. The LRU is fixed at 72 entries, 4 KiB metadata, and a 64 KiB coverage arena, with bounded eviction and uncached scratch fallback. | [loader](../../kernel/core/font_truetype_loader.cpp), [cache limits and lock contract](../../kernel/core/font_glyph_cache_internal.h#L8-L20), [cache invalidation](../../kernel/core/font_glyph_cache.cpp#L307-L327), [FONT5 report](FONT5_GLYPH_CACHE_AND_DESKTOP_RENDERING.md) |
| Selection and Settings | The catalog exposes at most 3 validated external faces and the bitmap face. TrueType UI sizes are 8–48 px; bitmap is fixed at 7 px. Preview is clipped and isolated from active state; Apply is transactional; Cancel and bitmap reset preserve a recovery path. FONT6 selection and FONT7 presentation tests pass, including stale candidates, missing sources, and persistence-failure rollback. | [selection limits](../../kernel/core/include/kernel/system_font_selection_types.h#L8-L12), [selection and transactions](../../kernel/core/system_font_selection.cpp#L822-L842), [native Settings UI](../../kernel/core/system_font_settings_ui.cpp), [FONT6](FONT6_SYSTEM_FONT_SELECTION.md), [FONT7](FONT7_SETTINGS_FONT_SELECTION.md) |
| Native desktop adoption | TrueType participates in Settings, participating shell labels, window titles, Start Menu labels, taskbar labels, and horizontal taskbar clock/date. The FONT11 layout caps and clipping rules remain bounded. Previously qualified multi-window screenshots show independent application windows over the taskbar. | [shell text roles](../../kernel/core/desktop_system_font.cpp#L27-L42), [window title rendering](../../kernel/core/kernel_compositor.cpp#L930-L948), [taskbar label rendering](../../kernel/core/kernel_compositor.cpp#L1254-L1266), [FONT8](FONT8_NATIVE_DESKTOP_ADOPTION.md), [FONT10](FONT10_SHELL_TYPOGRAPHY.md), [FONT11](FONT11_CACHE_AND_SHELL_POLISH.md) |
| Preference storage and recovery | Two 1 KiB checksummed sequence slots use inactive-slot write and readback before publishing the new active selection. Raw FAT32 block-image operation on the qualified PIIX IDE path is supported. VVFAT behavior and arbitrary power-loss atomicity are not claimed. FONT9 and FONT11R document the earlier ATA observation and its successful continuation on the supported raw-image path. | [slot serialization and commit](../../kernel/core/system_font_selection.cpp#L563-L601), [transactional write](../../kernel/core/system_font_selection.cpp#L822-L842), [FONT9](FONT9_PREFERENCE_STORAGE_RELIABILITY.md), [FONT11R closeout](FONT11_CACHE_AND_SHELL_POLISH.md#L182) |
| Deliberately unmigrated surfaces | Desktop icon captions, boot/recovery/console text, most app-owned UI/content, and document shaping remain bitmap or app-specific. Mixing bitmap icons/content with scalable shell text is intentional and remained legible in the guest. This is partial adoption, not universal system font support. | [desktop compositor](../../kernel/core/kernel_compositor.cpp), [desktop font adapter](../../kernel/core/desktop_system_font.cpp), [FONT8 scope](FONT8_NATIVE_DESKTOP_ADOPTION.md) |

The full Settings Fonts window has a 560×360 minimum and the text-size boundary tests cover 8 and 48 px. FONT12's interactive guest was 1280×800; it did not repeat a live drag-resize at the minimum dimensions or the two size extremes. Long-label clipping and bounded preview behavior are covered by presentation/layout tests, not by a new FONT12 long-string guest capture.

## Safety and freestanding review

The parser checks file/table ranges, required table structure, cmap relationships, glyph counts, and supported outline variants before exposing a source. Rasterization separately bounds table reads, point/contour/edge/component counts, recursion depth, output dimensions, arithmetic, and total scan work. Invalid glyph features and malformed records return bounded errors. Manager-owned source lifetime, generation validation, lock ordering, cache identity, and release invalidation are covered by FONT2–FONT6 tests.

Preference Apply validates the candidate, stages the active handle, serializes into fixed buffers, writes and verifies scratch data and the inactive slot, removes scratch best-effort, then publishes the selection. Failure paths leave or restore the prior active state; the FONT6/FONT9 selection suites inject save, readback, and rollback failures. Bitmap rendering does not need a valid external face and is used when both records are unusable or the configured source is missing.

A source scan of the production font, selection, Settings, and desktop font files found no std namespace, STL containers, heap allocation calls, new, exceptions, RTTI, or hosted-runtime constructs. The forced kernel build uses freestanding flags including -ffreestanding, -fno-exceptions, -fno-rtti, -nostdlib, and -nostdinc++. Font fingerprints and checksums detect accidental corruption; they do not authenticate hostile files. That limitation is documented in FONT6 and is not a release blocker for the defined v0.3 scope.

## Desktop and storage guest evidence

FONT12 used private OVMF firmware, private raw FAT32 media and qcow2 overlays, private serial/QMP/VNC endpoints, disabled networking, and fresh QEMU processes. The raw volume was cloned from the FONT9/FONT11R-qualified image and attached as PIIX IDE primary master. The tested guest used 1 GiB RAM, 2 vCPUs, and a 1280×800 framebuffer. Exact per-run QEMU arguments are preserved in the launch manifests under [docs/evidence/font12](../evidence/font12/). The isolated manifests, serial logs, offline preference records, and screenshots are committed; the 512 MiB raw disk/overlays and helper scripts remain in untracked validation output rather than this source commit.

| Exercise | Result and limits |
|---|---|
| Invalid newest saved preference | On a private clone, only the checksum of newer TTF sequence 31 was corrupted; the older bitmap sequence 30 was byte-identical. A fresh guest's real Settings Fonts page showed Bitmap 7 px as current and left the valid older choice usable. See [recovery screenshot](../evidence/font12/fonts-page-invalid-preference-recovery.png), [image manifest](../evidence/font12/invalid-preference-image-manifest.json), and [serial log](../evidence/font12/invalid-preference-boot.serial.log). |
| TrueType preview and Apply | The actual Fonts page previewed Roboto Bold 17 px. That capture is a pending preview, not a saved selection. A QMP input timing race made the first completed Apply select Roboto Italic 16 px; the serial trace confirms successful slot verification and Apply completion. The UI then applied bitmap and explicitly applied Roboto Regular 16 px. The final Settings page reported “Font applied and saved.” See [pending preview](../evidence/font12/fonts-page-roboto-bold-17-preview.png), [final Regular Apply](../evidence/font12/fonts-page-roboto-regular-16-applied.png), [Apply serial trace](../evidence/font12/settings-apply-session.serial.log), and [launch manifest](../evidence/font12/settings-apply-session.launch.json). |
| Bitmap reset and repeated slot use | The actual Settings UI applied Bitmap 7 px after TTF, then reapplied Regular 16 px. Offline inspection found valid sequence 33 Regular 16 px in FONTSEL0 and sequence 32 Bitmap 7 px in fontsel1; both records were 1 KiB with valid checksums. This shows successive slot reuse and a durable fallback record. See [bitmap capture](../evidence/font12/fonts-page-bitmap-7-applied.png) and [post-Apply preference snapshot](../evidence/font12/preferences-after-apply.json). |
| Fresh-process restoration | A fresh process booted the saved image, opened both preference slots and the configured Regular source, and initialized selection. Its native Start Menu opened and the desktop remained responsive. In this FONT12 boot the Settings page was not reopened, so the screenshot is not evidence of a visible post-restart Current line. FONT11R previously did verify that line after a fresh-process restart: [FONT11R restored Settings screenshot](../evidence/font11/font11r-fresh-process-restored-bold-16.png). FONT12's [fresh boot log](../evidence/font12/fresh-process-restore.serial.log), [manifest](../evidence/font12/fresh-process-restore.launch.json), and [desktop capture](../evidence/font12/fresh-process-start-menu.png) are included. |
| Missing selected source | A separate clone retained the valid sequence-33 Regular 16 px preference byte-for-byte and removed only Roboto-Regular.ttf. The guest opened the two remaining TTFs and both preference slots, logged selection initialization with fallback available, reached the input loop, and rendered its Start Menu. The actual Settings Current line was not reopened in that boot; FONT6/FONT9 selection tests exercise the precise missing-source-to-bitmap resolution. See [desktop capture](../evidence/font12/missing-selected-font-safe-desktop.png), [serial log](../evidence/font12/missing-font-session.serial.log), [launch manifest](../evidence/font12/missing-font-session.launch.json), and [mutation manifest](../evidence/font12/missing-font-image-manifest.json). |
| Mixed shell and legacy presentation | The FONT12 Settings page showed scalable headings and preview beside legacy bitmap icons. FONT11R's [multi-window capture](../evidence/font11/03-multiple-windows-menu-clock.png) shows Disk Manager and PacMan over the taskbar with titles, taskbar labels, and clock/date visible. FONT6 also captured a native Control Panel surface with TrueType selected: [FONT6 live selection](../evidence/font6/font6-live-selection.png). Desktop icon captions and application-owned drawing remain outside the adopted surface set. |

The FONT12 guest logs include the proof-enabled FONT11R storage probe, which wrote/read 20 bytes, matched the payload, and unlinked it successfully before UI interaction. These qualification-only startup probes delay early keyboard handling; do not use that boot delay as production startup or font-redraw timing. The previous raw-image ATA stall was not reproduced. The supported block-image path is qualified; VVFAT, AHCI, virtio storage, and arbitrary power-loss durability are outside this result.

FONT12 did not repeat a manual Settings window resize/focus-and-hit-test sequence after changing the font. Settings native-runtime lifecycle/focus tests, the compositor pointer-button test, and the compositor resize-notification test pass, and prior guest evidence shows multiple windows and shell text together. A fresh post-integration guest should still verify live click targeting, focus change, move/resize, and the 560×360 window floor with TTF selected before release signoff.

## Memory and performance

| Component | Static BSS / bound |
|---|---:|
| Shared outline/raster scratch | 238,880 bytes |
| Glyph cache metadata and coverage arena | 69,152 bytes total; 72 entries, 4 KiB metadata, 65,536 coverage bytes |
| Loader-owned external source records | 590,624 bytes; source payload ceiling is 3 × 192 KiB = 589,824 bytes |
| Discovery/selection records and scratch | 244,736 bytes |
| Shared contract, shell adapter, selection manager | 256 + 64 + 16 bytes |
| **Total measured font-related BSS** | **1,143,728 bytes (about 1.09 MiB)** |

The figures are BSS sizes from the forced AMD64 object files, summed across the font subsystem components above. They include resident fixed scratch/source/cache capacity, not just occupied bytes. Runtime font source capacity is 576 KiB; the guest used Regular, Bold, and Italic (146,004 + 146,768 + 152,208 = 444,980 bytes). A fourth Roboto BoldItalic asset exists in the repository but exceeds the current three-external-face registry if all four were to be loaded together.

The FONT11 deterministic shell workload covers 17 surfaces, 56 unique glyph/size keys, and 125 repeated-key occurrences. Its cold pass rasterized 56 keys with zero evictions; after adding the clock/date surfaces the final cache held 65 entries and 4,038 peak coverage bytes. Across the recorded warm and changed-clock passes totals were 342 hits, 65 rasterizations/misses, and zero evictions. The fixed cache reserves 69,152 bytes regardless of that low occupancy. Results and per-surface counts are in [cache workload evidence](../evidence/font11/cache-workload-results.md) and the rerun log [font11-cache-workload.log](../../out/font12-validation/tests/font11-cache-workload.log).

Successful guest Apply visibly redrew the Settings/shell selection, but no wall-clock or cycle timing for the font-change redraw was collected. Numeric redraw latency therefore remains unqualified. The 1 GiB guest booted and exercised the path. The full kernel ELF BSS is 580,180,576 bytes (553.30 MiB), dominated by non-font pools; the v0.3 minimum-RAM target is not stated in this qualification. Keep the whole-kernel target requirement separate from the font subsystem's approximately 1.09 MiB fixed BSS when defining supported hardware.

## Regression and build results

The complete available FONT2–FONT11 focused suite passed: 6,635 checks across the eight FONT2 contract, FONT3 parser, FONT4 raster, FONT5 cache, FONT6 selection, FONT8 text-layout, FONT10 shell typography, and FONT11 cache-workload runners. FONT7 UI integration is covered by Settings presentation tests and the actual guest.

| Regression group | Result |
|---|---|
| Settings presentation core | 2,651/2,651 passed |
| Settings native runtime | 828/828 passed, including 100 lifecycle repetitions, 250 provider refresh cycles, and 1,000 notification/state transitions |
| Settings center and feature models | Center 89; S6 42; S7 accessibility 21, developer 23, navigation/search 14; Users 11 + 8 + 13; Inventory 21 device + 24 storage; Default Apps model 39 + 9 interactions — all passed |
| GXApp/App Model | No GXApp-named test runner exists in scripts/tests. The four applicable App Model regression runners passed: application actions 55/55; folder activation 26/26; default handlers 54/54; URI/protocol 41/41. The prior FONT5 real guest proof exercised the production GXApp drawing surface and remains in [FONT5 guest evidence](../evidence/font5/font5-desktop.png); it was not relaunched in FONT12. |
| Settings freestanding presentation, native present, compositor pointer buttons, compositor resize notification | All builds/checks passed |
| Native ABI layout | **Failed:** current MinGW reports sizeof(gx_host_calls) = 256 while the existing assertion requires 248. This discrepancy predates FONT12 and is outside font sources. Log: [native ABI run](../../out/font12-validation/integration-tests/native-abi-layout-baseline.log). |
| Native filesystem contract | **Failed to build:** generated/dependency build_info.h includes absent mbedtls/private/crypto_adjust_config_enable_builtins.h. This broad native harness failure is unrelated to the font subsystem. Log: [native filesystem run](../../out/font12-validation/integration-tests/native-filesystem-contract.log). |

The forced command mingw32-make -B -C kernel ARCH=amd64 compiled and linked successfully. The freestanding ELF is 2,813,880 bytes, SHA-256 9B8D1A86B5F2721F6A386FB40630ABA85F842239D72B050A3F0A855FE2A668F3. The PE is 2,937,438 bytes, SHA-256 F740DF684C739060DE9E82A8D14FF40F04F6ED8BA0D30445FB40C1DC7F14ADB3. PE Import, IAT, and Delay Import directories are all zero; no hosted imports were found. Build log: [forced AMD64 build](../../out/font12-validation/amd64-build.log). Unit and integration summary files and their per-run outputs remain under [out/font12-validation](../../out/font12-validation/).

The two native failures keep the complete repository regression gate from being green. The ABI assertion must be reconciled with the intended existing ABI (do not alter it as a font fix); the missing generated mbedTLS header or harness setup must be corrected, or explicitly dispositioned by the v0.3 release owner, before claiming the full native suite passed.

## Release risks and disposition

| Item | Classification |
|---|---|
| ABI layout and native-filesystem regression failures | Blocking to full v0.3 regression signoff until fixed or formally dispositioned. Neither is a demonstrated font defect. |
| Live post-change focus, click targeting, move/resize, minimum-size guest pass | Blocking to Outcome A / final desktop signoff; deferrable to the v0.3 integration smoke run. Existing focused tests and earlier guest captures reduce risk but do not replace that live sequence. |
| FONT12 visual readback of the final selection after fresh boot | Deferrable to the same integration smoke. The saved records and fresh manager startup are verified here; FONT11R has the prior actual Settings readback. |
| Numeric font-change redraw latency and hardware below the tested 1 GiB guest | Deferrable until the v0.3 target list and a timing/RAM budget exist. No font-specific overwork was observed in the deterministic cache workload. |
| VVFAT writes, arbitrary power-loss guarantees, hostile-font authentication, unhinted shaping and unmigrated app/icon text | Explicitly unsupported or out of v0.3 scope; do not promise these behaviors. Continue using the qualified raw FAT32 block-image path. |
| Historical ATA stall | Unresolved exact historical trigger, but not reproduced on the qualified raw PIIX IDE FAT32 path. The FONT11R continuation and FONT12 actual Apply/readback both pass; no storage-driver change is justified by current evidence. |

There is no demonstrated font-specific release blocker. Outcome B reflects the incomplete full-repository gate and remaining live-desktop acceptance, not a defect in parsing, rasterization, selection, persistence, or bounded caching.

## Minimal v0.3 integration recommendations

1. Reconcile FONT_SUPPORT with the actual v0.3 source branch by hand. Other branches were not treated as synchronized or inspected. In particular, review overlapping edits in kernel/core/kernel_apps.cpp (native Settings route), kernel/core/desktop.cpp and kernel/core/kernel_compositor.cpp (shell/window text), kernel/core/main.cpp (selection initialization), and kernel/core/settings_presentation_core.cpp and its tests. Preserve changes from both sides rather than replacing whole files.
2. Integrate the font contract, bitmap backend, parser/loader/raster, cache, text layout, selection service/UI adapter, desktop adapter, public headers, corresponding tests, and scripts as a dependency-consistent set. Core .cpp sources are discovered by the kernel Makefile wildcard; Settings presentation core is a separate explicit source dependency. This checkout required no font-specific build target.
3. Retain the native Settings Personalization route and native service contracts. FontHandle, pointers, and source bytes remain kernel-private; no host ABI expansion is needed for this font selection path.
4. Package only the intended external faces under /system/fonts on the supported FAT32 image and include assets/Fonts/roboto/OFL.txt plus NOTICE.txt. The Roboto TrueType files are under SIL Open Font License 1.1. Ship three concurrent external faces unless the fixed registry/source budgets are deliberately revised and requalified.
5. Keep boot/recovery, bitmap fallback, selection persistence, and filesystem assumptions intact. Do not infer VVFAT compatibility, atomic rename, or power-loss guarantees from the dual-slot/checksum format.
6. Before v0.3 RC signoff, rerun the focused font and Settings/native/compositor/GXApp suites on the integrated branch; resolve or disposition the two native failures; then run a fresh isolated raw FAT32 guest through bitmap boot, TTF preview/Apply/size change, bitmap reset, TTF reapply, fresh-process visible Settings restoration, selected-file removal, and live window focus/hit-test/move/resize at the supported minimum size. Capture that guest's logs and manifests separately from unrelated QEMU processes.

## Evidence index

Committed screenshots, guest serial logs, exact launch manifests, offline preference snapshots, and image mutation manifests are under [docs/evidence/font12](../evidence/font12/). FONT12 test/build logs, full test summaries, helper scripts, private raw images/overlays, and QEMU ownership censuses remain under the untracked out/font12-validation tree. Existing FONT11 evidence is preserved and referenced above.

## FONT12R continuation — final v0.3 qualification

**FONT12R disposition: Outcome B — conditionally qualified.** This addendum records the final FONT12R result and supersedes the earlier statements above that the ABI and native filesystem harness failures were unresolved. The original FONT12 report and evidence remain intact.

FONT12R began on FONT_SUPPORT at the requested starting HEAD 89a58151c19da3d6532d838ae3a29a2c0ab4711a and ended at the same HEAD. The branch was already one commit ahead of origin/FONT_SUPPORT at the start. No branch or Git topology changes, merge, push, or commit occurred. Existing untracked and out/ data were preserved. The only tracked changes are the corrected ABI size/slot assertions in tests/native_abi_layout_test.cpp and the native filesystem runner include/link/source inputs in scripts/run-native-filesystem-contract-test.ps1. No production font implementation changed.

### Regression dispositions and build

The gx_host_calls discrepancy is a stale test assertion, not packing or a production ABI mismatch. sdk/include/guidexos/abi.h appends get_document_activation_path at offset 248 after play_pcm at 240; on AMD64 the current structure size is 256 bytes. The test still pinned the earlier 248-byte table from the audio-only era. The kernel and hosted runtime use matching declarations/offsets, and the bare-metal runtime reports sizeof its current table. The updated native ABI test passes. See the [FONT12R ABI disposition](../evidence/font12r/FONT12R_RESULTS.md#abi-layout-stale-assertion-no-abi-incompatibility) and [test log](../evidence/font12r/tests/native-abi-layout.log).

The missing Mbed TLS header is present in the pinned TF-PSA-Crypto source tree under drivers/builtin/include; it is not produced by the bootstrap generators. The profile verified at Mbed TLS 4.1.0 and TF-PSA-Crypto 1.1.0. The harness lacked that include root, then exposed missing -lwinmm and settings_presentation_core.cpp inputs. The corrected runner builds and passes the native filesystem contract test. No cryptographic source or generated placeholder changed. See the [harness disposition](../evidence/font12r/FONT12R_RESULTS.md#native-filesystem-harness-include-and-link-configuration-no-crypto-source-defect) and [final log](../evidence/font12r/tests/native-filesystem-contract-final.log).

The eight available focused FONT2–FONT11 suites pass 6,635 checks. Requested Settings presentation/runtime, Settings feature models, compositor/native presentation, App Model, native ABI, and native filesystem checks also pass. The forced AMD64 build and link pass: ELF 2,813,880 bytes, SHA-256 9B8D1A86B5F2721F6A386FB40630ABA85F842239D72B050A3F0A855FE2A668F3; PE 2,937,438 bytes, SHA-256 7530DE7677014FFAED93B0813F882528789AD76D8C35454B98613B04F2A113CD. PE Import, IAT, and Delay Import directories are zero. Detailed results are in the [FONT12R report](../evidence/font12r/FONT12R_RESULTS.md#regression-and-forced-build).

### Live guest acceptance

A real Settings Fonts guest Apply changed Roboto Bold 16 px to Bold 18 px and wrote sequence 32. A Cancel capture preserved the prior active selection. A fresh QEMU process booted the same private FAT32 image and launched Settings, but the captured System overview was obscured by the terminal; no post-reboot Fonts Current line or shell-surface comparison was captured. The preference record remained on disk, but per the mandatory gate it is not sufficient proof of visible restoration.

The disposable missing-Bold clone retained sequence 32 and removed only its selected file. It booted, initialized selection, reached the desktop main loop, opened Settings, and shut down cleanly. The built-in bitmap fallback branch is corroborated by source and the existing FONT6 test, but its active-font result was not visible in the Fonts page after recovery.

The PS/2 mouse reached the Start menu and Fonts card. Longer pointer movement and VNC absolute clicks were unreliable, so the bounded input diagnostic stopped. Two-window pointer focus/hit testing and live resize were not qualified; no guest window dimensions were recorded. Passing compositor and Settings automation tests do not substitute for these acceptance steps.

### Final status matrix

| Area | FONT12R result | v0.3 gate |
|---|---|---|
| Font parsing, rasterization, cache, selection, persistence, Settings tests | Pass; 6,635 focused FONT2–FONT11 checks and requested Settings suites pass | No demonstrated font-specific defect |
| ABI size discrepancy | Stale 248-byte assertion corrected; current append-only 256-byte ABI passes | Resolved |
| Native filesystem harness | Missing include/link/source inputs corrected; full harness passes | Resolved |
| Forced AMD64 build and imports | Build/link pass; no PE hosted imports | Pass |
| Apply/Cancel and pre-reboot state | Real Fonts UI Apply/Cancel; sequence 32 Bold 18 saved | Pass |
| Fresh-process visible font restoration | Fresh process and Settings launch pass, but Fonts page and selected shell text are not visible | Open; blocks Outcome A |
| Missing-font recovery | Private clone boots without crash; bitmap fallback branch and tests corroborate recovery, but no visible Current readback | Partial; blocks Outcome A |
| Pointer focus and hit testing | Some actual mouse input worked; two-window focus, identity, and all required targets were not established | Open; blocks Outcome A |
| Live resize | Not exercised after the bounded mouse diagnostic; no before/after geometry | Open; blocks Outcome A |
| Memory | Font BSS 1,143,728 bytes; 1 GiB guest boots; minimum product RAM is unspecified | No demonstrated font-specific blocker; total target budget remains undefined |

**Font feature readiness:** conditionally qualified; focused tests, real Apply/Cancel, persistence, recovery code path, and full AMD64 build are sound, while live UI acceptance remains incomplete.

**Integration readiness:** suitable for controlled integration with the live guest gates tracked, but not ready for unconditional desktop integration signoff. Close pointer focus/hit testing, supported resize, visible fresh-boot restoration, and visible fallback readback on the integrated branch.

**Combined v0.3 release readiness:** not qualified here. No merge was performed and the integrated release candidate has not been tested.

Outcome B does not advance phase status. No FONT13 work or local commit was started. The complete evidence package is under [docs/evidence/font12r](../evidence/font12r/FONT12R_RESULTS.md).

## FONT12R2 continuation — final guest interaction and visual restoration

**FONT12R2 disposition: Outcome B — conditionally qualified.** A fresh-process Settings Fonts screenshot now visibly restores Roboto Bold 18 px at preference sequence 32. Real pointer focus changes, Settings control selection, and one live Settings resize passed. The bounded relative-pointer check later failed to reach the grip for a minimum-size drag, so minimum-size behavior remains unverified and no Outcome A claim is made.

The run started and ended on FONT_SUPPORT at 89a58151c19da3d6532d838ae3a29a2c0ab4711a. The exact starting worktree state is recorded in [starting-worktree-status.txt](../evidence/font12r2/starting-worktree-status.txt). Existing ABI/filesystem runner corrections, FONT12R documentation, and all out/ artifacts were preserved. No production, test, or harness source changed; previously accepted ABI/filesystem, 6,635 focused font checks, Settings/native suites, forced AMD64 build, and no-PE-import results remain as recorded in [FONT12R_RESULTS.md](../evidence/font12r/FONT12R_RESULTS.md). No tests/build were rerun and no commit was made.

The single fresh QEMU process used a private raw FAT32 clone and private firmware copy after a census that found three unrelated guests. Its Fonts page visibly shows the selected family/style/size and preview alongside the desktop taskbar. Pointer focus moved Settings → File Explorer → the same Settings instance. Font-row selection and the size plus control changed pending preview state without applying it. A real Settings drag resized the same focused window from 760×500 to 825×555; the compositor redrew it and the Fonts controls/preview remained visible. The default PC PS/2 relative mouse was the active device.

The remaining limitation is coordinate delivery: a bounded fast move and one slower-paced move did not reach the requested resize grip. The 560×360 minimum clamp and layout at that bound therefore remain unqualified. Numeric window IDs and font selection generation are not printed in serial telemetry; source inspection confirms focus/resize retain the registered ID and no font selection was committed, while the visible active Bold 18 rendering remains unchanged. Full configuration, logs, traces, and screenshots are indexed in [FONT12R2_RESULTS.md](../evidence/font12r2/FONT12R2_RESULTS.md).

The owned guest stopped after I sent QMP quit to its verified PID 30864/private-image endpoint because its accepted ACPI powerdown request did not stop it. No command was sent to the other guests; the post-run census showed the external QEMU process set had changed during the run. The ending branch, HEAD, and worktree status are in [ending-worktree-status.txt](../evidence/font12r2/ending-worktree-status.txt).

## FONT12R3 continuation — minimum-size qualification and final closeout

**FONT12R3 disposition: Outcome B — conditionally qualified.** No font-specific correctness defect was demonstrated. The minimum-size acceptance remains inconclusive because the live PS/2 pointer again under-delivered before reaching the Settings resize grip, and no existing same-instance production resize diagnostic was available.

The production Settings constraint is 560×360 outer-window pixels, with 760×500 preferred and 1180×820 maximum. The compositor resize path binds the drag to the registered window ID, clamps width and height to the Settings minimum (the drag handler does not impose the Settings maximum values), marks that same window dirty, and redraws it with the new dimensions. Display-topology reconciliation separately clamps dimensions to the available work area. A native window-resize request API is not implemented; the `WindowResize` IPC enum is unused. Source review is in [FONT12R3_RESULTS.md](../evidence/font12r3/FONT12R3_RESULTS.md#production-minimum-and-resize-path).

FONT8 already shows the bitmap Fonts page at 560×360 and reports that the window was resized to that geometry. It does not provide a before/after identity or minimum hit-test trace. FONT12R2 established pointer focus, real control selection, and same-window live resize from 760×500 to 825×555, but its minimum approach also undershot. The R3 gap is TTF Fonts-page rendering, same-instance minimum clamping, and below-minimum behavior at 560×360.

One new isolated guest used a byte-identical private copy of the accepted raw FAT32/PIIX IDE image. The initial Fonts page showed Roboto Bold 18 px at 760×500. From the observed pointer position `(465,264)`, the calibrated relative PS/2 move targeted the resize grip at `(850,565)`; the visible pointer reached only about `(501,301)`. No button-down or resize drag occurred. Following the bounded-test rule, no repeat was attempted. No valid existing API could request the minimum on the same Settings instance, so minimum clamping and redraw were not guest-tested. The exact capture, QMP trace, serial log, ownership census, and image/preferences records are in [docs/evidence/font12r3](../evidence/font12r3/FONT12R3_RESULTS.md).

The active UI remained Roboto Bold 18 px, and saved preference sequences 31/32 were unchanged. Settings launched once and did not restart. The 560×360 bitmap screenshot from FONT8 remains prior evidence; this run makes no claim that the TTF page or its controls are usable at that bound. Previously accepted FONT12R regression/build evidence remains applicable because production and test source files were unchanged; broad tests and builds were not rerun.

**Final recommendation:** keep FONT_SUPPORT conditionally qualified, with no demonstrated font-specific blocker. For controlled integration, retain the existing conditional status and leave the minimum-size gate open; this is not v0.3 desktop signoff. Hand the persistent pointer automation limitation to the owning input project. Outcome B does not authorize a FONT12R3 closeout commit; no commit, push, merge, or FONT13 work was started. See [FONT12R3_RESULTS.md](../evidence/font12r3/FONT12R3_RESULTS.md) for the full disposition and ending worktree state.

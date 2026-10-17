# FONT8 — Native Desktop Font Adoption & Layout Hardening

**Outcome: A — Accepted**

- **Branch:** `FONT_SUPPORT`
- **Starting HEAD:** `283fe286b8e3d52ecefd2e5edb648ecb6917fb28`
- **Ending HEAD:** the local FONT8 commit; its exact SHA is included in the task closeout.
- **Remote:** unchanged; no push.
**Scope:** controlled rollout to Settings, the Start Menu profile label, and Control Panel labels. Boot, console, recovery, panic, and most application text remain on their existing renderers.

## Summary

FONT8 adds one bounded single-line layout helper on top of the FONT2–FONT7 contract, then uses it on three native surfaces. It measures variable advances, line metrics, bearings, and fallback through the shared font service. Labels fit their control rectangles with ellipsis or use the legacy bitmap renderer if scalable text cannot fit. The existing Settings preview stays pending until Apply succeeds.

The real Settings app enforces a 560 × 360 minimum; compositor registration, resize, and display-topology reconciliation respect the per-window minimum. The applied system-font generation is read at draw time. Settings invalidates and synchronizes its active/pending selection when the generation changes; existing window identity and input geometry remain in place.

All relevant hosted suites and the forced AMD64 build passed. An isolated QEMU desktop showed the applied font in Settings, the Start Menu, and Control Panel, verified persistence across a fresh process boot, reverted to bitmap, and recovered safely when the saved italic font file was absent.

## Native text-consumer inventory

| Consumer | Existing path and layout | FONT8 change | Risk / impact |
|---|---|---|---|
| Window decorations and compositor-owned captions | Kernel compositor has its own 5×7 bitmap table and fixed glyph metrics. | Not migrated. | High migration cost: independent drawing and hit-test ownership. |
| Desktop shell and taskbar | Most shell labels use desktop bitmap drawing with fixed row heights and baseline assumptions. | Only the Start Menu “User” profile label uses the applied system font. Other menu entries, taskbar labels, clock, title text, and toast labels remain bitmap. | Low-risk representative shell label; fixed menu geometry retained. |
| Settings navigation and controls | The native Settings app previously drew most labels through its bitmap helper. | Navigation, page headings, picker rows, size/status labels, preview caption, buttons, and keyboard hints now try the active system font. Preview samples use the pending face and size. | Medium: dense controls and small supported window size; covered by bounded fitting and fallback. |
| Control Panel | Header and item captions previously used desktop bitmap drawing; hit testing used icon/layout geometry. | “System Settings” and the three item captions use the active font when they fit. Their measured line height contributes to the label/hit geometry. | Medium: dynamic label height; icon geometry and separate hit bounds are retained. |
| File Explorer, context menus, popup menus, and desktop dialogs | Existing native bitmap drawing and component-specific fixed layout. | Not migrated. | Higher impact because these surfaces have distinct sizing, scroll, and interaction rules. |
| GXApp/native ELF text | FONT2 shared contract; FONT5 adds cached grayscale drawing for the FONT5 demonstration package. Other packages keep their bitmap/default behavior. | No FONT8 ABI or package-wide change. | Broad migration deferred; application-specific layout contracts vary. |
| Boot, framebuffer console, recovery, serial, and panic output | Dedicated early-boot/console/diagnostic renderers. | Not migrated and default behavior unchanged. | Reliability-sensitive paths remain independent from runtime font loading. |

The shared scalable stack remains FONT2 contract and metrics, FONT3 loading, FONT4 rasterization, FONT5 cache, FONT6 selection/persistence, and FONT7 preview. FONT8 adds layout and selective native UI adoption; it does not replace all desktop text renderers.

## Migrated production surfaces

1. **Settings:** active font is used for suitable navigation, labels, picker rows, size controls, buttons, and status text. If an active-font line does not fit its rectangle, it uses the existing bitmap fitted-text path. The preview is drawn with the selected pending identity/size into the preview clip and does not alter unrelated Settings text before Apply.
2. **Start Menu:** the “User” profile label uses the active face in its fixed header rectangle, with bitmap fallback. Menu row text and hit targets are unchanged.
3. **Control Panel:** the “System Settings” heading and category captions use active metrics and bounded fitting. If the scalable line is too tall for its available box, the renderer uses the legacy bitmap path. The icon remains the main target; label height is included in the existing item hit region so larger text does not make the target smaller.

No new application ABI, window identity, or second font-change notification channel was introduced.

## Shared layout rules and overflow policy

The new `FontTextLayoutSingleLine` helper is in [font_text_layout.h](../../kernel/core/include/kernel/font_text_layout.h) and [font_text_layout.cpp](../../kernel/core/font_text_layout.cpp). It uses the same glyph lookup and UTF-8/font fallback contract as rendering.

- Uses per-glyph advance, ascent, descent, line height, baseline, x/y bearings, and ink bounds. Negative left bearings are offset into the available rectangle.
- Supports one line only. CR/LF is explicitly rejected for this helper; it does not wrap or implement shaping.
- Strictly decodes bounded UTF-8 and returns the contract’s invalid-UTF-8 or missing-glyph fallback status.
- Accepts at most 4,096 source bytes per line. Desktop fitted-text callers use fixed 256-byte output buffers; bitmap fallback uses a fixed 192-byte clipped buffer. No heap allocation is added.
- Supports explicit Clip and Ellipsis policies. Ellipsis is three ASCII periods. Underlying family names, messages, and control values are never changed.
- The desktop rectangle helper centers by measured line height/baseline, checks framebuffer bounds, and draws through the existing grayscale cache path. If scalable text cannot safely fit the box or height, the caller falls back to bitmap text.

| Surface | Overflow behavior |
|---|---|
| Settings navigation, labels, button captions, status and hints | One line; ellipsis where the scalable line is too wide; bitmap fitted-text fallback if metrics/height or output bounds reject it. Fixed button/control bounds continue to own hit testing. |
| Font family/style rows and current-selection label | One line with bounded truncation; the stored identity and family/style values remain unchanged. |
| Font preview | Explicitly clipped to the preview rectangle; the existing preview scroll remains available. No wrap. |
| Start Menu profile label | One line in the fixed header; ellipsis or bitmap fallback; no hit-target change. |
| Control Panel heading and captions | One line in measured boxes; ellipsis or bitmap fallback. Caption height may enlarge the item’s hit region, but glyph ink is not used as the hit target. |
| Unsupported or unmigrated consumers | Existing bitmap renderer and existing layout policy. |

The Settings minimum was measured at **560 × 360** on the actual window. At that size the bitmap Fonts page retains visible navigation, picker, size, preview, status, and actions; the screenshot also shows that long instructional/status text is clipped intentionally inside its area. The shared compositor still clamps the requested minimum to the available work area on smaller displays.

## Font-change propagation and window state

The FONT6 selection service advances its selection generation only after a successful apply. The desktop system-font helpers fetch the current render handle/size at each call and retain no borrowed font pointer. The Settings app compares the active generation with its saved snapshot; when it changes, it refreshes active and pending state and invalidates the app’s drawing. A change arriving during pending edits discards those edits with a status message.

Control Panel label geometry is recalculated from the active metrics during draw and hit testing. The compositor preserves the Settings window object and its identity while honoring its minimum size during registration, drag-resize, and topology reconciliation. Font changes do not restart participating applications or replace the window; focus and selection behavior are covered by the native runtime/presentation regression suites.

## Settings coverage and bitmap recovery

The real Fonts page continues to offer the validated Roboto faces and Built-in Bitmap. Apply and Cancel remain separate from preview; Restore Defaults selects the bitmap identity, and the FONT6 transactional rollback path is unchanged. Status/error text uses the active fitted renderer where it fits and the bitmap fallback otherwise.

This Settings font flow reports selection and persistence failures inline; it has no modal font-selection dialog. Shared desktop dialogs remain on their existing bitmap path.

The guest applied Roboto Bold 16 px, reopened Settings after a fresh process boot, and showed the persisted selection. It later applied Built-in Bitmap and verified the 7 px bitmap selection in the same private ESP. A separate missing-font run applied Roboto Italic 16 px, moved only the private guest’s italic file out while QEMU was stopped, and booted from the saved preference. The desktop stayed up with bitmap active; the Fonts page listed only Roboto Bold and Regular and showed Built-in Bitmap 7 px. The missing face was not offered as selectable. The page does not currently explain that the persisted italic file is missing; the fallback is safe but silent.

## Memory, cache, and bounded work

FONT8 adds no persistent font storage, cache, or allocator path. Layout and drawing reuse existing manager/cache synchronization and bounded caller buffers.

| Existing resource | Measured/declared bound |
|---|---:|
| FONT4 shared raster scratch | 238,880 bytes in kernel BSS, including its shared 16,384-byte grayscale coverage area; single-owner under the font-manager lock. |
| FONT5 glyph cache | 66,896 bytes statically allocated; 73,728-byte configured maximum, 16 entries, deterministic LRU. |
| FONT6 selection service | 223 KiB static BSS: 192 KiB file staging, 16 KiB candidate glyph probe, 8 KiB directory names, 4 KiB preference buffers, and about 3 KiB of records/state. |
| FONT7 Settings preview scratch | Separate fixed 16 KiB preview buffer; it does not publish active selection or alter the FONT5 cache. |
| FONT8 layout | No heap; maximum 4,096 input bytes. Desktop fitting buffers are fixed at 256 bytes, with a 192-byte bitmap fallback buffer. |

The FONT5 reference Roboto workload recorded init 83 µs, cold draw 930 µs, warm draw 68,033 ns/call, 1,403 cache hits, 11 misses/rasterizations, zero evictions, and 3,923 current/peak coverage bytes. These are FONT5 reference measurements, not a new FONT8 timing claim. FONT8 tests exercise cache validity and warm-hit behavior; no FONT8-specific cycle timer or high-water rasterization counter was added. The bounded worst-case layout call performs at most three source passes of 4,096 bytes (measurement, metric/ink scan, and incremental prefix fit when ellipsis is needed), plus three period-glyph lookups. This is a deterministic work bound, not an elapsed-time measurement. No speculative memory reduction was attempted.

## Hosted regression results

| Suite | Result |
|---|---:|
| FONT8 single-line layout | 46/46 |
| FONT2 shared font contract | 196/196 |
| FONT3 TrueType loading/validation | 1,150/1,150 |
| FONT4 glyph rasterization | 4,454/4,454 |
| FONT5 glyph cache | 228/228 |
| FONT6 system font selection | 210/210 |
| Settings presentation core | 2,651/2,651 |
| Settings Center | 89/89 |
| Settings Users models | 32/32 |
| Settings Device/Storage models | 45/45 |
| Settings Default Apps models | 48 checks; 100 lifecycle repetitions |
| Settings S6 model | 42/42 |
| Settings S7 models | 58/58 |
| Settings native runtime | 828 checks; 100 lifecycle repetitions, 250 provider refreshes, 1,000 state/notification transitions |
| Settings presentation AMD64 freestanding harness | PASS; bounded link and unresolved-symbol checks |
| Native presentation validation | PASS |

FONT8 layout coverage includes variable-width measurement, baseline/vertical metrics, negative bearings, clipping, ellipsis, long input, 8/48 px sizes, malformed UTF-8, missing-glyph fallback, buffer limits/canaries, and cache generation behavior. FONT6/FONT7 suites cover failed loads, missing fonts, transactional rollback, Apply/Cancel, bitmap reset, and lifecycle behavior.

## AMD64 build and import validation

A forced full build completed with the real AMD64 freestanding flags (-std=c++14 -ffreestanding -nostdlib -nostdinc++ -fno-exceptions -fno-rtti -fno-builtin plus the repository’s AMD64 flags). Both new production translation units were compiled, all kernel objects linked, and the build ended with Built: build/amd64/bin/kernel.elf.

- ELF: 2,807,872 bytes.
- PE: 2,928,979 bytes.
- objdump -p kernel/build/amd64/bin/kernel.pe reports a zero-sized Import Directory.
- The converted ELF symbol table still contains legacy undefined names documented by FONT7; these are not PE imports and are kept separate from this font change.
- Existing unrelated compiler warnings remain non-fatal. No unrelated ABI declaration was changed.

The full log is [kernel-build-forced-final.log](../../out/font8-validation/kernel-build-forced-final.log); the native objects include desktop_system_font.cpp and font_text_layout.cpp under the freestanding flags.

## Isolated QEMU desktop acceptance

QEMU 11.0.0 ran with a private OVMF copy, private writable ESP and serial log, QMP on 127.0.0.1:61191, VNC on 127.0.0.1:5909, 1 GiB RAM, and networking disabled. Fresh process censuses recorded command lines and guest ownership. Other projects’ QEMU guests were identified by path and left untouched. At final census no process referenced the FONT8 private workspace.

- **Minimum size:** [Settings at 560 × 360](../evidence/font8/settings-minimum-560x360.png).
- **Applied font:** [Roboto Bold 16 px in Settings](../evidence/font8/settings-roboto-bold-16-applied.png); [8 px preview](../evidence/font8/settings-roboto-bold-8-preview.png); [Roboto Bold 48 px applied](../evidence/font8/settings-roboto-bold-48-applied.png).
- **Other production surfaces:** [Start Menu User label](../evidence/font8/start-menu-user-label-ttf.png); [Control Panel labels](../evidence/font8/control-panel-labels-ttf.png).
- **Persistence and rollback:** [TTF restored after fresh process boot](../evidence/font8/settings-persistence-after-reboot.png); [bitmap restored](../evidence/font8/settings-bitmap-restored.png).
- **Missing-font recovery:** [italic before removal](../evidence/font8/missing-font-italic-applied-before-removal.png); [desktop fallback](../evidence/font8/missing-font-desktop-recovery.png); [Fonts page showing bitmap and only installed faces](../evidence/font8/missing-font-settings-recovery.png).

Serial excerpts, process censuses, and guest notes are in [docs/evidence/font8](../evidence/font8/). Complete serial logs remain under out/font8-qemu-20261017-fontsupport and were preserved unmodified. The persistence log records 11 Settings launch/shutdown pairs, including ten deliberate open/close repetitions. Preference persistence was verified by quitting the private QEMU process and launching a fresh one with the same ESP; QMP system_reset exited under WHPX with “Unexpected VP exit code 4”. During an additional save attempt, QEMU’s directory-backed FAT VVFAT backend emitted “Error handling renames (-2)” when a transaction replaced a host-directory slot that already existed. Guest saves were therefore demonstrated with unused slots; hosted transactional tests cover slot replacement and rollback. This limitation and the repeated fresh-boot cursor-area rectangle are recorded in [guest-acceptance-notes.txt](../evidence/font8/guest-acceptance-notes.txt).

## Remaining unsupported paths and future work

- Most desktop shell/taskbar text, window decorations, Explorer, menus/dialogs, and third-party GXApp text remain bitmap or app-specific. FONT8 does not change the Console, boot, recovery, or panic font.
- Layout is one-line and bounded, not a Unicode shaping engine. It does not add kerning, ligatures, bidirectional layout, grapheme handling, or wrapping.
- The system font catalog remains limited to validated direct-child TTF files in /system/fonts/ (three external faces; 8–48 px). FONT8 adds no font installation workflow.
- QEMU’s folder-backed FAT behavior is not a power-loss or existing-slot-replacement qualification; FAT remains non-journaled as documented by FONT6.
- Future typography work should first extend the measured-rectangle contract to Explorer/dialog/menu consumers, then separately evaluate multiline layout and shaping requirements. This report stops at the FONT8 boundary; FONT9 has not started.

## Deliverables and repository state

Production files changed:

- kernel/core/font_text_layout.cpp
- kernel/core/include/kernel/font_text_layout.h
- kernel/core/include/kernel/desktop_font.h
- kernel/core/desktop_system_font.cpp
- kernel/core/desktop.cpp
- kernel/core/kernel_apps.cpp
- kernel/core/include/kernel/kernel_app.h
- kernel/core/kernel_compositor.cpp

Regression and evidence files:

- tests/font_text_layout_test.cpp
- scripts/run-font-text-layout-test.ps1
- This report and screenshots/serial/census artifacts under docs/evidence/font8/.

Branch FONT_SUPPORT was retained. Existing untracked artifacts under out/ were preserved. The tracked FONT8 files are included in the required local commit; no push was made.

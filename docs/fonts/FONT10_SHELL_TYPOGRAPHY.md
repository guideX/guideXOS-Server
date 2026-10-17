# guideXOS Server — FONT10: Shell Typography & Window Decoration Integration

**Outcome: A — Accepted.** Native window titles, taskbar application labels, and Start Menu labels now use the FONT6 active system font through bounded FONT8 text layout. The fixed shell geometry, interaction targets, bitmap fallback, and FONT9 preference storage remain in place.

## 1. Shell text ownership audit

| Surface | Owner and implementation | Previous sizing / clipping | FONT10 result |
|---|---|---|---|
| Native window titlebars | Kernel-owned chrome in `kernel/core/kernel_compositor.cpp`, `KernelCompositor::drawTitlebar`; title and control hit tests remain in the compositor | Bitmap `measureText` and a fixed `y + 7` baseline; title did not have a shared measured clip rectangle | Uses the shared window-title role, centered in the caption area between the left inset and the fixed control reserve. Active/inactive color and all control geometry are unchanged. |
| Built-in Terminal titlebar | Kernel-owned chrome in `kernel/core/desktop.cpp`, the Terminal window draw path | Bitmap title measurement and fixed baseline | Uses the same window-title role and reserve as native titlebars. |
| Native taskbar labels | Kernel-owned taskbar in `kernel/core/kernel_compositor.cpp`, `TaskbarManager::drawButtons` | Button bounds came from the existing bitmap-era layout; label drawing used a centered bitmap path | Labels use the taskbar role inside the existing button inset. Button bounds and active underline remain unchanged. |
| Desktop taskbar / Terminal label | Kernel-owned taskbar in `kernel/core/desktop.cpp`, `draw_taskbar_buttons` | Built-in Terminal label was bitmap-drawn; the clock and date are also bitmap-drawn | Terminal label now uses the taskbar role, including the vertical-dock layout. Clock/date and static legacy taskbar entries remain bitmap text. |
| Start Menu application rows | Kernel-owned in `kernel/core/desktop.cpp`, `draw_start_menu` | Fixed row geometry; text used the bitmap renderer and could extend past the available label span | Left-column app names use left alignment in a rectangle after the icon and before the row's right inset. Existing row, selection and scroll geometry is preserved. |
| Start Menu navigation and footer | `draw_start_menu` right-column shortcuts, “All Programs” toggle, and Sleep / Restart / Shut Down buttons | Fixed widths and hit boxes with bitmap-era labels | Labels use the shared role, clipped/ellipsized to their existing rectangles. No separate category-heading surface exists in this two-column menu. |
| Start Menu profile label | `draw_start_menu`, “User” header label | Already adopted by FONT8 using the general system text helper | Preserved on the FONT8 path; it is not rerouted through the new role helper. |
| Taskbar clock and system status | Kernel-owned widgets in `kernel/core/desktop.cpp` | Bitmap text; widths and widget bounds come from existing taskbar layout | Deliberately unchanged. |
| Desktop icon labels | Kernel-owned desktop icon renderer in `kernel/core/desktop.cpp` | Bitmap label and icon layout | Deliberately unchanged. |
| App windows and widgets | Application-owned body drawing; invoked by the compositor after chrome in `KernelCompositor::drawWindow` | Each application owns its drawing contract and text policy | Deliberately unchanged. FONT10 migrates shell-owned captions, not app body text or the general widget framework. |
| Early boot, panic, recovery, diagnostics and Console body | Boot/recovery and Console paths | Existing bitmap/diagnostic renderer | Remain independent of external fonts. |

The titlebar and taskbar text are kernel-owned. The compositor still owns window IDs, focus, client drawing dispatch and control hit-testing. Start Menu drawing and input state remain in the desktop shell's existing draw and input paths. FONT10 changes the label renderer only; it does not change a drawing contract or add a renderer to app bodies.

## 2. Shared typography policy

`kernel/core/desktop_system_font.cpp` supplies `draw_shell_text_in_rect` and role-aware measurement. It reuses FONT8's `FontTextLayoutSingleLine` and FONT4's grayscale renderer rather than adding another layout engine. The selected preference remains authoritative in FONT6/FONT9. Shell roles cap only the effective paint size:

| Role | Effective size |
|---|---:|
| Window title | min(configured pixel size, 13 px) |
| Taskbar label | min(configured pixel size, 12 px) |
| Start Menu label | min(configured pixel size, 12 px) |
| General system text helper | min(configured pixel size, 48 px) |

These caps keep glyphs within existing titlebar, taskbar and menu rows without changing their heights. The cap does not rewrite or persist the user's selected size. If scalable layout or drawing cannot safely complete, the shell helper fits the text to the same rectangle with the built-in bitmap font.

The scalable path gets the selected font handle, pixel size and selection generation for each draw. It lays out one bounded UTF-8 line with variable glyph advances and the font's ascent, descent, line height and baseline. Vertical placement centers the measured line box in the existing control box; baseline placement then uses `baselineFromTopPx`. Centered captions use measured ink width, while menu rows use left alignment. Layout accounts for negative left bearings when setting the draw origin.

Input length is bounded by the shared FONT contract (`kFontMaxTextBytes`). The output fit buffer is 256 bytes, and framebuffer dimensions are limited to 4096×4096 for this helper. Checked rectangle, pitch and destination-size arithmetic reject invalid bounds before drawing. The bitmap fit path scans at most 4096 source bytes, fits to a 256-byte output, and uses a measured `...` when the label does not fit. The original stored title or app name is never modified.

The compositor retains its original active/inactive title colors, titlebar height, button layout and hit regions. The title rectangle reserves the close, maximize and minimize controls. Taskbar button widths, IDs and active indicators are still calculated by their existing layout code. Start Menu row and footer hit boxes, selection highlights, scrolling, keyboard navigation and launch actions are unchanged. The shortened string is a paint result only.

## 3. Font changes, redraw and lifetime

FONT6 increments its selection generation when the active font or size changes and requests the existing desktop redraw. The render-state bridge in `kernel/core/system_font_selection.cpp` returns that generation even when bitmap is active, while reporting that there is no scalable handle. FONT10 observes generation changes in its shell statistics, but intentionally caches no text widths or layouts, so there is no stale measurement table to invalidate.

Each paint obtains a fresh handle and size. The shell does not retain font-source pointers, glyph views, or cache-buffer pointers. A stale/unavailable handle or a layout/draw failure falls through to bounded bitmap text. The selection getter uses the existing selection guard, and the font draw API validates handles before use. No notification loop was added: selection uses the existing redraw request, and the next regular desktop draw paints current state.

During the guest size-change operation, the selected Roboto Italic size changed from 16 px to 17 px. GDB observed active record 1 generation 4 → 5; window count remained 1, window ID remained 1005, and its position and size remained `(95, 70, 760, 500)`. The next shell frame drew with the new generation. No window was recreated to apply the change.

## 4. Compatibility and fallback

Bitmap mode remains a selectable FONT6 choice. It uses the same shell rectangle, alignment and bounded-fit entry point, so reverting to bitmap preserves control geometry and truncates long labels rather than drawing through neighboring controls. The existing Start Menu profile text remains on its FONT8 helper.

Taskbar active/inactive backgrounds, active underlines, hover colors, window focus state, close/maximize/minimize controls, taskbar click behavior, Start Menu focus and navigation, and window identity are outside the typography helper and retain their existing state. The actual guest run exercised focus changes, movement, resizing, multiple windows, Start Menu selection and both bitmap/TrueType modes.

When the configured font file is unavailable, FONT6 starts with bitmap active and retains the existing recovery behavior. The shell does not need an external font file to draw or start.

## 5. Memory, cache and performance evidence

FONT10 adds no heap allocation, per-window glyph scratch, second glyph cache or copied font bytes. A paint uses bounded local fit/layout state; the existing FONT4 raster scratch and FONT5 shared glyph cache remain shared. The FONT6 selection service's existing static storage remains approximately 223 KiB; FONT10 does not refactor it.

Measurements from the existing FONT5 hosted run:

| Measure | Observation |
|---|---:|
| FONT5 shared cache configured byte capacity | 66,896 bytes |
| FONT5 checks | 228, 0 failures |
| FONT5 warm draw sample | 45,686 ns/call |
| FONT5 hits / misses / rasterizations / evictions | 1,403 / 11 / 11 / 0 |
| FONT5 resident bytes / peak bytes in that test | 3,923 / 3,923 |

The existing FONT4 raster scratch is approximately 238,880 bytes and is not duplicated per window by this integration. In the representative live guest workload after opening the menu and launching apps, the cache snapshot was: 8,585 hits, 10,376 misses, 10,376 rasterizations, 10,360 evictions, 16 entries, 797 bytes resident, and 1,545 bytes peak resident. This indicates substantial churn across the workload, but it stayed within the existing shared cache. FONT10's shell counters independently observed frames, text draw requests, scalable draws, bitmap fallback draws/failures and generation changes.

The available shell counters do not measure per-frame elapsed time. No frame-cost number is claimed. The captured GDB observations are workload counters and state snapshots, not a frame-time benchmark.

## 6. Hosted regressions

The new `tests/font10_shell_typography_test.cpp` test is run with `scripts/run-font10-shell-typography-test.ps1`. It uses a controlled FONT8 layout / FONT4 draw fixture and independent expected advances and baseline geometry. The latest run passed **71 checks, 0 failures**.

Coverage includes role caps and larger general sizes (24 px and 48 px), variable-width centering, measured baseline, active and inactive title colors, narrow title ellipsis, narrow taskbar ellipsis, multiple independently centered taskbar labels, left-aligned Start Menu text, unsupported glyph fallback, malformed UTF-8 fallback, stale-handle bitmap restoration, unavailable-font fallback, oversized bounded input, short destination height, out-of-frame rejection and canary preservation, generation changes, and renderer counters. It verifies that the fitted output is shortened while the supplied source string remains unchanged.

FONT2–FONT9 and relevant Settings/native regressions were rerun. Results:

| Suite | Result |
|---|---:|
| FONT2 shared font contract | 196/196 |
| FONT3 TrueType load/validation | 1,150/1,150 |
| FONT4 rasterization | 4,454/4,454 |
| FONT5 glyph cache/rendering | 228/228 |
| FONT6 selection and fallback | 376 checks, 0 failures |
| FONT8 text layout | 46/46 |
| Settings presentation core | 2,651/2,651 |
| Settings Center | 89/89 |
| Settings S6 | 42/42 |
| Settings S7 Accessibility, Developer, navigation/search | 58/58 |
| Settings S8 Users model/navigation/layout | 32/32 |
| Settings device/storage | 45/45 |
| Settings Default Apps | 39 model + 9 interaction; 100/100 lifecycle cycles |
| Native Settings runtime | 828/828; 100 lifecycle cycles, 250 provider refresh cycles, 1,000 notification/state transitions |
| Native presentation validation | PASS |
| Freestanding Settings presentation validation | PASS |

The separate known native ABI layout test remains outside this result: it expects `sizeof(gx_host_calls) == 248`, while the current MinGW build reports 256. No ABI was changed for FONT10, and this check is recorded separately in `out/font10-validation/native_abi_layout_test.log`.

The hosted FONT10 test focuses on renderer/layout behavior; it does not simulate compositor window lifetimes or exhaust the entire system's resource limits. Window identity, focus, resizing, multiple windows and menu interaction were therefore checked in the real guest. FONT6 selection regressions cover repeated selection, failed loads and bitmap recovery.

## 7. AMD64 build and link qualification

After the production changes, `mingw32-make ARCH=amd64 -B` forced a complete AMD64 kernel rebuild and link. It finished with `Built: build/amd64/bin/kernel.elf`. The changed freestanding units compiled with the actual kernel flags and the kernel PE import directory has no entries (import directory entry 1 size 0); no hosted runtime dependency was introduced.

Build products measured 2,813,608 bytes for `kernel.elf` and 2,934,903 bytes for `kernel.pe`. The full build log is preserved at `out/font10-validation/amd64-build-accepted.log`. Native presentation validation also passed. No production source changed after this forced build; the later edits were confined to the hosted test and documentation/evidence.

## 8. Isolated QEMU desktop acceptance

The guest workflow used private OVMF firmware, private raw writable 512 MiB FAT32 media, 1 GiB guest RAM, disabled networking, fresh QEMU processes, and private serial/QMP/GDB endpoints. A process census was captured before each process. No QEMU owned by another checkout was attached to, modified or terminated. The first guest applied and saved TrueType and bitmap selections. The second process booted against the same writable image to verify persisted TrueType state. The missing-font test used an offline clone so the accepted image remained intact.

| Boot | Process / storage | Guest result |
|---|---|---|
| 1 — UI | PID 9368; `font10-acceptance-fat.img` | Used Settings → Fonts to select Roboto Italic, apply TrueType, switch to bitmap and verify it, then reapply/save TrueType. Opened Settings and Terminal, opened the Start Menu, switched focus, and moved/resized a window. Changed font size from 16 to 17 px and observed repaint with the same window ID. |
| 2 — persistence | PID 11316; fresh QEMU process on the same accepted image | Normal UEFI boot restored Roboto Italic at 17 px. Start Menu labels used the scalable renderer: 25 requests, 25 scalable draws, 0 bitmap fallbacks and 0 fallback failures in the captured menu snapshot. |
| 3 — missing font | PID 16640; fresh QEMU process on a clone | Removed `/system/fonts/Roboto-Italic.ttf` only from the clone while preserving the seq 6 preference that refers to it. Normal boot selected the built-in bitmap font. Start Menu and Settings Fonts remained usable; current selection showed `guideXOS Bitmap | Regular 7 px`, with 167 bitmap draws and 0 scalable draws in the captured workload. |

The saved preference inspected on the accepted image was sequence 6, TrueType, 17 px, path `/system/fonts/Roboto-Italic.ttf`. Its image SHA-256 before the recovery boot was `ABAD59D7CD1B19EB59A5ABD237818B800C93B53E007DA0AE05A366D02F2FB3AA`; the recovery guest used a distinct clone. The serial logs show normal boot and selection recovery.

The real window titled “guideXOS Navigator” was tested at its 204 px minimum width. The title remained visible at the 13 px role size, so this particular live title did not visibly truncate. Hosted tests force a 32-character wide title into a 56 px caption and a long Navigator label into a 24 px task button, verifying ellipsis and bounded placement. The guest screenshots demonstrate the actual integrated text surfaces; the synthetic hosted cases exercise overflow that existing app titles did not reach on screen.

Screenshots and raw QEMU evidence are indexed in [`../evidence/font10/README.md`](../evidence/font10/README.md). Raw serial logs, process censuses, launch manifests, private disk images, GDB notes and test logs remain under `out/font10-validation/`; all `out/` artifacts were preserved and excluded from the commit.

## 9. Remaining shell text consumers and limits

FONT10 intentionally migrates the priority surfaces and keeps existing ownership boundaries:

- Taskbar clock/date and system tray/status remain bitmap text.
- Desktop icon labels remain bitmap text.
- Static legacy taskbar entries remain bitmap text; native compositor buttons and the built-in Terminal label are migrated.
- Start Menu profile text remains on the existing FONT8 helper.
- Console body, boot/panic/recovery/diagnostic text and application-owned window content remain outside this change.
- The live 204 px Navigator case fit; visible guest ellipsis for an overlong title was not obtained. Renderer clipping is covered in hosted tests.
- Frame-time cost was not measured; live cache and shell draw counters were recorded instead.

## 10. FONT11 recommendations

Keep FONT11 as a separate adoption phase. First consider migrating the taskbar clock/date and remaining suitable shell-owned labels through the same role helper, with each surface retaining its existing bounds and hit targets. Audit desktop icon caption behavior separately because its available width and icon interaction geometry differ from menus and task buttons. Do not fold app-owned body text or boot/recovery/Console output into a broad replacement. Add an actual on-screen overlong-title case when a stable guest window title can exceed the minimum caption width without changing the product's app titles solely for the test. Add elapsed-frame measurements before making a performance-driven cache or raster change.

FONT10 ends here. No FONT11 implementation or phase-tracking update was made.

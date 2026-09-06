# guideXOS Server Theme System Plan

## Phase 1 Status

Phase 1 is now in place as a conservative foundation for the guideXOS Server theme system.
This phase intentionally stops at wiring and safe color hooks; it does not attempt a visual redesign.

This pass keeps the current guideXOS look as the default and adds an opt-in Sci Fi theme with a small set of safe visual hooks.

## Implemented Themes

* Classic
* Sci Fi

Classic remains the default theme.

Sci Fi is opt-in and intentionally conservative in this phase.

## What Is Wired

* Central theme metadata and lookup helpers in `desktop_theme.h` and `desktop_theme.cpp`
* Theme persistence through `desktop.json` using a stable `desktop.theme.id` value
* Display Options theme selection UI
* Compositor reload-time theme application
* Theme-aware title bar background, title bar text, window border, window background, and taskbar colors

## Phase 2A

Phase 2A added theme-aware chrome metrics and conservative spacing polish.

* Classic metrics preserve the current guideXOS look as closely as practical.
* Sci Fi uses modestly larger spacing and padding for title bars, controls, and shared chrome.
* Window corner radius remains a theme intent/metric, but full rounded-window clipping is still deferred.
* Shadows, blur/glass simulation, animations, and high-DPI/scaling remain deferred.
* The compositor and shared window renderer consume the same theme metrics for title bar height, button spacing, text inset, and border thickness.

## Phase 2A.1

Phase 2A.1 is a stabilization-only pass.

* It is limited to small alignment and metric-consistency fixes.
* Rounded clipping remains deferred.
* Shadows remain deferred.
* Blur/glass remains deferred.
* Animations remain deferred.
* High-DPI and scaling remain deferred.

## Phase 2B

Phase 2B adds a guarded rounded-window drawing preview for the Sci Fi theme.

* Classic remains rectangular.
* Sci Fi gets a conservative rounded chrome preview where the hosted GDI path can render it safely.
* The preview uses the existing theme intent fields for rounded windows and corner radius.
* Full clipping is still deferred unless it can be proven safe for the render path.
* Rounded hit-testing is deferred and stays rectangular for now.
* Shadows remain deferred.
* Blur/glass simulation remains deferred.
* Animations remain deferred.
* High-DPI and scaling remain deferred.

## Phase 2B.1

Phase 2B.1 validates and stabilizes the guarded rounded-window preview on the hosted GDI path.

* The hosted compositor continues to repaint the full client background before drawing the rounded shell, which keeps stale square corners from lingering during move, resize, overlap, and repaint sequences.
* Classic remains rectangular.
* Bare-metal framebuffer rendering remains rectangular.
* Rounded hit-testing remains rectangular.
* Full rounded client clipping remains deferred.
* Shadows, blur/glass simulation, animations, and high-DPI/scaling remain deferred.

## Phase 2C

Phase 2C adds a conservative Sci Fi border and highlight polish layer without changing the core compositor model.

* Sci Fi now gets a more deliberate active-window edge treatment using the existing theme accent and muted accent colors.
* A subtle top-edge titlebar highlight line is drawn for Sci Fi windows when the hosted chrome path renders them.
* Sci Fi title buttons get slightly more polished hover and pressed fills and borders through the shared window renderer helpers.
* Sci Fi taskbar items get small hover and active accent refinements in the hosted compositor taskbar painter.
* Classic remains the default theme and keeps its existing rectangular look and behavior.
* Rounded client clipping is still deferred.
* Rounded hit-testing is still deferred.
* Shadows remain deferred.
* Blur/glass simulation remains deferred.
* Slide-in / slide-out animations remain deferred.
* High-DPI and scaling remain deferred.

## Phase 2C.1

Phase 2C.1 is a stabilization-only review pass for the Sci Fi chrome polish.

* The Phase 2C border and highlight treatment was reviewed and kept centralized.
* Classic remains preserved as the default rectangular theme.
* No new shadow, blur/glass, animation, or clipping work was added.
* The hosted titlebar and taskbar polish paths remain guarded to Sci Fi.
* One small cleanup was made to remove a dead transparency branch in the shared titlebar helper.

## Phase 2D

Phase 2D adds the first guarded, hosted-only Sci Fi soft shadow preview.

* The preview is conservative and intentionally shallow: it uses two filled shadow layers rather than blur or glass simulation.
* It is hosted GDI only and is wired through the shared window renderer helper used by the compositor.
* Classic remains the default theme and stays shadow-free unless a future theme explicitly opts into `softShadowIntent`.
* Sci Fi enables `softShadowIntent`, so the hosted compositor can draw the preview only for Sci Fi windows.
* The shadow is rounded when the hosted rounded chrome preview is active; the implementation still falls back to a simple rectangular shadow if a render path cannot safely use the rounded shape.
* Bare-metal framebuffer rendering remains unchanged and rectangular.
* Rounded client clipping remains deferred.
* Rounded hit-testing remains deferred.
* No blur or glass simulation was added.
* No animation was added.
* The hosted paint path redraws the full client frame before the window stack, so the shadow can extend slightly outside the shell without leaving stale pixels behind during move, resize, overlap, or repaint.

## Phase 2D.1

Phase 2D.1 is a stabilization and review pass for the hosted Sci Fi shadow preview.

* The soft shadow preview was reviewed and kept conservative rather than expanded into a broader effects system.
* No concrete repaint fix was required: the hosted WM_PAINT path already redraws the full client frame before the window stack, so there is no stale-pixel artifact risk from the shadow preview in the current hosted path.
* Classic remains shadow-free.
* Bare-metal framebuffer rendering remains unchanged.
* Rounded hit-testing and rounded client clipping remain rectangular and deferred.
* No blur, glass, animation, or high-DPI work was added.
* The rounded chrome preview guard remains separate from the shadow guard, and the shadow helper stays centralized in the shared window renderer.

## Phase 2E

Phase 2E adds conservative Sci Fi desktop and taskbar surface polish without expanding the theme system into blur, glass, animation, or clipping work.

* Hosted Sci Fi desktop painting now wires the existing `desktopBackground` theme color into the no-wallpaper background path so the desktop base reads as a deliberate dark Sci Fi surface.
* Hosted Sci Fi taskbar painting now uses theme-aware surface, border, and highlight helpers for a flatter and more cohesive chrome-adjacent look.
* Hosted Sci Fi taskbar items use consistent idle, hover, and active surface blends derived from the existing taskbar, border, accent, and muted accent theme colors.
* Hosted Sci Fi start button and taskbar-adjacent panel surfaces use the same conservative surface helpers so the desktop shell reads as one theme.
* Classic remains the default theme and preserves the current look as closely as practical.
* Classic remains rectangular and shadow-free.
* Bare-metal framebuffer rendering remains unchanged and rectangular.
* Rounded client clipping remains deferred.
* Rounded hit-testing remains deferred.
* Blur/glass remains deferred.
* Animations remain deferred.
* High-DPI and scaling remain deferred.

## Phase 2E.1

Phase 2E.1 is a stabilization and review pass for the Phase 2E Sci Fi desktop and taskbar surface polish.

* The Phase 2E surface helper set was reviewed and kept centralized in the hosted compositor path.
* A small start-button hit-geometry cleanup was applied so the hosted click bounds stay aligned with the painted taskbar padding.
* Classic remains the default theme and preserves the existing taskbar and menu look as closely as practical.
* Sci Fi remains opt-in and continues to use the conservative hosted surface helpers only.
* No new blur, glass, animation, clipping, or scaling work was added.
* Wallpaper and theme asset selection remain deferred.
* The existing desktopBackground wiring for the hosted no-wallpaper path remains in place and was left unchanged in behavior.

## Phase 2F

Phase 2F is a Display Options theme UX polish pass.

* The Theme section now explains the Classic and Sci Fi choices more clearly so users can tell what each option is for at a glance.
* Classic keeps a concise description that preserves the current guideXOS look.
* Sci Fi keeps a concise description that emphasizes the dark futuristic hosted UI.
* A read-only feature summary is shown if the layout allows it, to help users understand the visible theme intent without exposing any per-effect controls.
* No per-effect sliders, checkboxes, or theme customization controls were added.
* No new rendering effects or compositor changes were added.
* Classic remains the default theme.
* Sci Fi remains opt-in.
* Theme selection, persistence, and reload behavior remain unchanged.

## Phase 2G

Phase 2G is a documentation and visual validation checkpoint for the current theme milestone.

* Phase 1 foundation is complete and provides the stable theme ID, persistence, and selector wiring base.
* Phase 2A chrome metrics are complete and keep the shared chrome layout theme-aware.
* Phase 2A.1 paint/hit-test alignment stabilization is complete and kept the metric wiring consistent.
* Phase 2B guarded rounded hosted preview is complete and remains limited to the safe hosted path.
* Phase 2B.1 stale-corner validation is complete and confirmed the preview does not leave square artifacts behind.
* Phase 2C border / highlight polish is complete and adds the conservative Sci Fi edge treatment.
* Phase 2C.1 polish stabilization is complete and kept that polish centralized.
* Phase 2D hosted shadow preview is complete and remains guarded to hosted Sci Fi only.
* Phase 2D.1 shadow stabilization is complete and kept the shadow helper conservative.
* Phase 2E desktop / taskbar surface polish is complete and keeps the shell surfaces cohesive.
* Phase 2E.1 taskbar surface geometry stabilization is complete and keeps the click regions aligned.
* Phase 2F Display Options theme UX polish is complete and improves theme selection clarity without adding per-effect controls.
* The current milestone intentionally stops before new rendering effects, compositor behavior changes, or theme metric changes.

## Phase 2G.1 Manual Validation Note

* Classic validation passed: `DisplayOptions` launched as `DisplayOptions`, and the launch capture showed the theme tab area was readable without any clipping or overlap.
* Sci Fi validation passed: the real Theme tab click path was too brittle to rely on fully in this pass, so Sci Fi was validated with the persisted `desktop.theme.id=scifi` token plus compositor restart, then visually confirmed on the hosted desktop.
* Screenshots were captured as local validation artifacts in the repo root during the run.
* No rendering/effects changes were made.
* `theme-phase2g1-*.png` files are local validation artifacts unless they are intentionally moved into a documented artifact folder.
* Persisted theme tokens are `classic` and `scifi`.
* Tiny fixes made: none.
* Issues deferred: click-driven Theme tab / Sci Fi selection was not fully click-reliable in this pass, so the persisted-config fallback was used for Sci Fi validation.
* `DisplayOptions` launches under the registered app name `DisplayOptions`.

## Phase 3A

Phase 3A is the first app-surface pilot for the guideXOS Server theme system.

* Display Options is the first app surface to receive Sci Fi polish.
* Sci Fi gets conservative panel/card/accent treatment so the app feels a little more cohesive with the shell.
* Classic is preserved and stays visually close to the current Display Options look.
* No blur, glass, animation, rounded clipping, rounded hit-testing, or per-effect theme controls were added.
* Broad app redesign remains deferred.
* Future app polish should proceed one app at a time.

## Phase 3A.1

Phase 3A.1 is a stabilization-only review pass for the Display Options app-surface pilot.

* Display Options surface helpers were reviewed and kept centralized.
* Classic preservation was verified, with only a tiny fallback tweak to restore the prior neutral card feel.
* No new effects or controls were added.
* Future app polish should remain one app at a time.

## Phase 3B

Phase 3B is the second app-surface pilot for the guideXOS Server theme system.

* Control Panel is the second app surface to receive Sci Fi polish.
* Sci Fi gets conservative panel/card/accent treatment so Control Panel feels a little more cohesive with the shell.
* Classic is preserved and stays visually close to the current Control Panel look.
* No new effects were added.
* No new per-effect theme controls were introduced.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 3B.1

Phase 3B.1 is a stabilization-only review pass for the Control Panel app-surface pilot.

* Control Panel surface helpers were reviewed and kept centralized.
* Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look.
* No new effects, controls, rounded behavior, or layout redesign were added.
* The existing shared grid top offset remains in place for both drawing and hit-testing.
* Future app polish should remain one app at a time.

## Phase 3C

Phase 3C is the third app-surface pilot for the guideXOS Server theme system.

* Notepad is the third app surface to receive Sci Fi polish.
* Sci Fi gets conservative editor/body/border/text polish so Notepad feels a little more cohesive with the shell.
* Classic is preserved and stays visually close to the current Notepad look.
* No editing behavior changed.
* No new effects were added.
* No new per-effect theme controls were introduced.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 3C.1

Phase 3C.1 is a stabilization-only review pass for the Notepad app-surface pilot.

* Notepad surface helpers were reviewed and kept centralized.
* Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look.
* No editing behavior, file handling, keyboard input, persistence, layout geometry, hit-testing, or theme metrics changed.
* No new effects or controls were added.
* `build.bat` wrapper behavior was classified by direct reruns as non-reproducible; `guideXOSServer.exe` was produced and the direct wrapper exit code returned 0.
* Future app polish should remain one app at a time.

## Phase 3D

Phase 3D is the fourth app-surface pilot for the guideXOS Server theme system.

* Calculator is the fourth app surface to receive Sci Fi polish.
* Sci Fi gets conservative body, display, button, and accent polish so Calculator feels a little more cohesive with the shell.
* Classic is preserved and stays visually close to the current Calculator look.
* No Calculator behavior changed.
* No new effects were added.
* No new per-effect theme controls were introduced.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 3D.1

Phase 3D.1 is a stabilization-only review pass for the Calculator app-surface pilot.

* Calculator surface helpers were reviewed and kept centralized.
* The shared compositor widget guard was reviewed and kept narrow to Calculator and Sci Fi.
* Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look.
* No readability/layout fix was needed.
* No Calculator behavior, input handling, math evaluation, history, persistence, layout geometry, hit-testing, or theme metrics changed.
* No new effects or controls were added.
* No blur, glass, animation, rounded clipping, or rounded hit-testing was added.
* Future app polish should remain one app at a time.

## Phase 3E

Phase 3E is the next app-surface pilot for the guideXOS Server theme system.

* File Explorer is the next app surface to receive Sci Fi polish.
* Sci Fi gets conservative body, list, toolbar, address, selection, hover, border, and status polish so File Explorer feels more cohesive with the shell.
* Classic is preserved and stays visually close to the current File Explorer look.
* No File Explorer file listing behavior changed.
* No File Explorer navigation behavior changed.
* No File Explorer open or launch behavior changed.
* No App Model behavior changed.
* No new effects were added.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 3E.1

Phase 3E.1 is a stabilization-only review pass for the File Explorer app-surface pilot.

* File Explorer surface helpers were reviewed and kept centralized.
* Row hover and selection safety were reviewed, and the Sci Fi hover highlight now clears when the visible list offset changes so hover stays visual-only.
* The shared File Explorer render path is app-level renderer surface code; no bare-metal-specific theme parity work was added in this pass.
* Classic preservation was confirmed, with no visible fallback changes needed to restore the prior familiar look.
* No File Explorer file listing behavior, directory navigation, file open or launch behavior, App Model behavior, persistence, layout geometry, hit-testing, or theme metrics changed.
* No new effects or controls were added.
* No blur, glass, animation, rounded clipping, or rounded hit-testing was added.
* Future app polish should remain one app at a time.

## Phase 3F

Phase 3F is the next app-surface pilot for the guideXOS Server theme system.

* Clock is the next app surface to receive Sci Fi polish.
* Sci Fi gets conservative body, readout, and accent polish; Clock does not have dedicated mode tabs or button chrome in this pass, so there is no control-surface redesign to apply.
* Classic is preserved and stays visually close to the current Clock look.
* No Clock behavior changed.
* No new effects were added.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 3F.1

Phase 3F.1 is a stabilization-only review pass for the Clock app-surface pilot.

* The Clock theme-aware repaint path was reviewed and kept local to Clock surfaces.
* Repaint safety was reviewed: the clear-then-redraw path removes stale stacked text without changing timekeeping cadence or layout, and it still runs on the existing one-second update cadence.
* Classic preservation was confirmed; the fallback remains visually close to the prior Clock look and no visible fallback tweak was needed.
* Sci Fi readability was reviewed; the time/date readout stays readable on the darker body/face surfaces, and the muted date text remains legible.
* No Clock timekeeping, timer, alarm, stopwatch, scheduling, persistence, layout geometry, hit-testing, or theme metric behavior changed.
* No new effects or controls were added.
* No blur, glass, animation, rounded clipping, or rounded hit-testing was added.
* No additional readability or layout fix was needed in this pass.
* Future app polish should remain one app at a time.

## Phase 3G

Phase 3G closes out the Sci Fi app-surface milestone through Phase 3F.1. It is a documentation, inventory, and smoke-hardening pass only. It does not add new rendering effects, app behavior, theme IDs, persistence keys, or chrome metrics.

Classic remains the default theme. Sci Fi remains opt-in. No per-effect controls were introduced.

### App-Surface Inventory

* Display Options: Phase 3A / 3A.1; Sci Fi changes: conservative panel/card/accent polish plus clearer theme copy; Classic preservation: default look stays intact and the neutral card feel remains; Behavior changes: No; Stabilization: complete in 3A.1; Safety fix: tiny fallback tweak restored the neutral card feel.
* Control Panel: Phase 3B / 3B.1; Sci Fi changes: conservative panel/card/accent polish that matches the hosted shell; Classic preservation: look stays close to the prior Control Panel surface; Behavior changes: No; Stabilization: complete in 3B.1; Safety fix: shared grid top offset stayed synchronized for drawing and hit-testing.
* Notepad: Phase 3C / 3C.1; Sci Fi changes: conservative editor/body/border/text polish; Classic preservation: look stays close to the prior Notepad surface; Behavior changes: No; Stabilization: complete in 3C.1; Safety fix: surface helpers stayed centralized and no visible fallback tweak was needed.
* Calculator: Phase 3D / 3D.1; Sci Fi changes: conservative body/display/button/accent polish; Classic preservation: look stays close to the prior Calculator surface; Behavior changes: No; Stabilization: complete in 3D.1; Safety fix: shared compositor widget guard stayed narrow to Calculator and Sci Fi.
* File Explorer: Phase 3E / 3E.1; Sci Fi changes: conservative body/list/toolbar/address/selection/hover/border/status polish; Classic preservation: look stays close to the prior File Explorer surface; Behavior changes: No; Stabilization: complete in 3E.1; Safety fix: Sci Fi hover clears on scroll and offset changes so hover stays visual-only.
* Clock: Phase 3F / 3F.1; Sci Fi changes: conservative body/readout/accent polish plus clear repaint handling; Classic preservation: look stays close to the prior Clock surface; Behavior changes: No; Stabilization: complete in 3F.1; Safety fix: clear-then-redraw removes stale stacked text without changing timekeeping cadence.

### Deferred Boundaries

* Rounded client clipping remains deferred.
* Rounded hit-testing remains deferred.
* Blur/glass remains deferred.
* Animations remain deferred.
* High-DPI/scaling polish remains deferred.
* Taskbar shadows remain deferred.
* Theme wallpaper selection remains deferred.
* Broad app redesign remains deferred.
* Bare-metal theme parity remains deferred.
* Per-effect controls remain deferred.
* Navigator, Image Viewer, Task Manager, and other future app-surface polish remain deferred.

### Validation Checklist

* `.\build.bat`
* `.\scripts\smoke-theme-system.ps1 -SkipBuild`
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild`
* `git diff --check`
* Skip the optional appmodel smoke unless there is a specific reason to run it.
* Treat `guideXOSServer.exe`, `desktop.json`, screenshots, generated EXEs, and disk images as validation artifacts unless intentionally changed.

## Phase 3H

Phase 3H is a hosted visual validation pass for the Sci Fi app-surface milestone. It is a documentation and screenshot-checklist pass only. It does not add new rendering effects, app behavior, theme IDs, persistence keys, or chrome metrics.

* Hosted compositor validation was performed for Classic and Sci Fi.
* Classic preservation held: the default theme stayed familiar, and no accidental Sci Fi colors, layout drift, or missing fills or borders were observed on the inspected surfaces.
* Sci Fi readability and cohesion held on the shell and app pilots: titlebars, menus, taskbar, desktop surfaces, and app chrome remained readable and cohesive without looking overdone.
* Inspected surfaces: desktop background, taskbar, Start menu, taskbar context menu, Display Options, Control Panel, Notepad, Calculator, File Explorer, and Clock.
* Tiny fixes made: none in repo source; the pass stayed documentation-only, with only temporary local validation harness retries to stabilize screenshot capture.
* Screenshots and artifacts captured: local PNGs were captured in temporary validation artifact folders and were not added to git.
* Deferred boundaries remain unchanged: rounded client clipping, rounded hit-testing, blur/glass, animations, taskbar shadows, high-DPI/scaling, bare-metal theme parity, per-effect controls, and broad app redesign remain deferred.
* Readiness: the Sci Fi app-surface milestone is ready for the next layer of theme work.

## Phase 4A

Phase 4A is a readiness/defaults boundary checkpoint for the Sci Fi theme. It is a documentation-only pass that marks the hosted Sci Fi milestone as validated and safely opt-in while Classic remains the default. It does not add new styling, rendering effects, app behavior, theme IDs, persistence keys, or chrome metrics.

* Sci Fi is now a validated hosted opt-in theme milestone.
* Classic remains the default and fallback-safe theme.
* Hosted shell and app-surface coverage are broad enough to continue from a stable checkpoint.
* Bare-metal parity remains deferred.
* Sci Fi is not yet declared a universal/default OS theme.
* Future work should continue behind opt-in gates unless explicitly promoted.
* No per-effect controls were introduced.

### Default Safety Boundary

* Classic remains default.
* Sci Fi remains opt-in.
* Missing or invalid theme config falls back to Classic.
* Theme switching should not create tracked config changes that imply a default change.
* Validation artifacts like `guideXOSServer.exe`, `desktop.json`, `desktop.state`, screenshots, temp harness files, and disk images are local-only and should not be committed unless a task explicitly changes defaults.

### Ready for Next Layer

* Theme wallpaper selection.
* Navigator, Image Viewer, and Task Manager app-surface pilots.
* Higher-quality manual screenshot harness.
* High-DPI/scaling review.
* Hosted shadow and taskbar polish.
* Bare-metal parity inventory.
* Rounded clipping and hit-testing research, still not implementation.
* Theme asset and icon polish.

## Phase 4B

Phase 4B is a wallpaper/background inventory and design-readiness pass. It documents the current global background path before any theme-scoped wallpaper work begins. It does not change defaults, assets, rendering behavior, or persistence keys. Classic remains the default theme and Sci Fi remains opt-in for this inventory pass.

### Inventory Summary

* The current background selection is global, not theme-scoped.
* DisplayOptionsStore persists one wallpaperId and one backgroundScaleMode; desktop.json also keeps the legacy wallpaper path and desktop.wallpaper.id for compatibility.
* Compositor save/load keeps the same selection in both display-options.cfg and desktop.json, and DesktopService::SaveState preserves existing wallpaper fields when it refreshes pinned and recent entries.
* DesktopTheme.desktopBackground is a color fallback, not a wallpaper asset binding.
* On a fresh config, the default background is still the built-in image wallpaper, so Sci Fi does not currently own a separate wallpaper.
* The dedicated background asset folder is assets/Backgrounds.
* It currently contains 15 image wallpapers with matching thumbnails: ameoba, ameobagx, blueflower, cpu, dinos, flower, greenmedow, guidexosspace, guidexosspace3, merlin, merlin2, mountains, redflower, tronporche, and Wallpaper2.
* wallpaper_registry.cpp also defines 8 built-in gradient backgrounds: midnight, ocean, aurora, violet, sunset, forest, ember, and graphite.
* The registry and UI support BackgroundKind::SolidColor, but there are no built-in solid-color backgrounds today.

### Render Paths

* The hosted compositor uses DesktopWallpaper::DrawGradient and drawBackgroundImageToHdc for its desktop backdrop.
* The bare-metal framebuffer path uses drawBackgroundGradientToPixels and drawBackgroundImageToPixels.
* The hosted Sci Fi no-image fallback uses the theme desktopBackground color.
* Bare-metal does not consult the Sci Fi desktopBackground field and stays on the procedural background state.
* The current Sci Fi desktop therefore is not a separate wallpaper track; it is the shared global background selection plus hosted Sci Fi color fallback behavior when no image is present.

### Safe Future Options

* Keep Classic and Sci Fi color-only for now.
* Allow Sci Fi to suggest a bundled wallpaper while Classic remains unchanged.
* Add a theme-specific default wallpaper later, still opt-in only, with explicit override rules for user wallpaper.
* Keep user-selected wallpaper independent of theme; that is already the current global model.
* Add a Display Options wallpaper UI later. The current Background tab is global, so any theme-linked selector would still need new semantics.
* Keep bare-metal wallpaper parity deferred.

### Non-Changes

* This pass does not promote a new default wallpaper.
* No defaults changed.
* No assets were added, removed, moved, or renamed.
* No per-effect controls were introduced.
* Wallpaper selection remains deferred until an implementation phase.

## Phase 4C

Phase 4C adds a small opt-in Sci Fi wallpaper recommendation note in Display Options. It is read-only and does not change the wallpaper automatically, the default wallpaper, the default theme, or the global wallpaper model.

* The Theme tab now shows a read-only recommendation when Sci Fi is selected.
* The recommendation points to existing bundled backgrounds only, and the user still has to choose a wallpaper through the existing Background tab controls.
* No theme-scoped wallpaper persistence was added.
* Classic remains the default theme.
* Sci Fi remains opt-in.
* No default wallpaper changed.
* No assets were added, removed, moved, or renamed.
* Bare-metal wallpaper parity remains deferred.

## Phase 4C.1

Phase 4C.1 is a stabilization-only review pass for the Phase 4C Sci Fi wallpaper recommendation note. The note was reviewed in Display Options and remains read-only and opt-in. No wallpaper/default/persistence/assets changed, and no automatic wallpaper switching was added.

* The Theme tab recommendation stays gated to Sci Fi selection and remains read-only.
* Tiny copy fix: the note now says the recommendation is optional and tells users to pick any wallpaper on the Background tab.
* Placement was reviewed and kept below the theme cards and above the footer action text.
* No theme-scoped wallpaper persistence was added.
* Classic remains the default theme.
* Sci Fi remains opt-in.
* No default wallpaper changed.
* No assets were added, removed, moved, or renamed.
* No per-effect controls were introduced.

## Phase 4D

Phase 4D is the next app-surface pilot for the guideXOS Server theme system. It applies conservative Sci Fi toolbar, address, content, border, and text polish to Navigator while keeping Classic visually close to the current look. It does not change Navigator behavior, navigation logic, page loading, URL/path handling, history, bookmarks, persistence, App Model behavior, theme IDs, persistence keys, or theme metrics. It does not add new effects, blur, glass, animations, rounded clipping, rounded hit-testing, or per-effect controls.

* Navigator is the next app-surface pilot.
* Sci Fi gets conservative toolbar, address, content, border, and text polish so Navigator fits the shell more cleanly.
* Classic is preserved and stays visually close to the current Navigator look.
* No Navigator behavior changed: navigation logic, page loading, URL/path handling, history, bookmarks, and persistence all remain the same.
* No App Model behavior changed.
* No new effects were added.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 4D.1

Phase 4D.1 is a stabilization-only review pass for the Navigator app-surface pilot. Navigator was reviewed conservatively and kept within the existing theme-surface scope.

* The Navigator pilot was reviewed and stabilized.
* The shared compositor widget guard was reviewed and tightened to match Navigator windows only.
* Classic preservation was confirmed; the previous Navigator feel remains close to the current look.
* Authored page background colors remain respected when present.
* No Navigator behavior, navigation logic, page loading, URL/path handling, history, bookmarks, persistence, or App Model behavior changed.
* No new effects, blur, glass, animation, rounded clipping, rounded hit-testing, or per-effect controls were added.
* Tiny cleanup made: the shared compositor Navigator widget guard now uses a tighter title match to avoid accidental spillover.
* Tiny readability fix made: Navigator's default page-body text now tracks the content surface in Sci Fi so dark content stays legible while authored colors still win.
* No layout or hit-test change was needed in this pass.

## Phase 4E

Phase 4E is the next app-surface pilot for the guideXOS Server theme system. It applies conservative Sci Fi body, header, list, selection, border, and text polish to Task Manager while keeping Classic visually close to the current look. It does not change Task Manager behavior, process/app enumeration, refresh cadence, kill/close/end-task behavior, monitoring logic, App Model behavior, theme IDs, persistence keys, or theme metrics. It does not add new effects, blur, glass, animations, rounded clipping, rounded hit-testing, or per-effect controls.

* Task Manager is the next app-surface pilot.
* Sci Fi gets conservative body, header, list, selection, border, and text polish so Task Manager fits the shell more cleanly.
* Classic is preserved and stays visually close to the current Task Manager look.
* No Task Manager behavior changed: process/app enumeration, refresh cadence, kill/close/end-task behavior, and monitoring logic all remain the same.
* No App Model behavior changed.
* No new effects were added.
* Broad app redesign remains deferred.
* Future app polish should continue one app at a time.

## Phase 4E.1

Phase 4E.1 is a stabilization-only review pass for the Task Manager app-surface pilot.

* The Task Manager pilot was reviewed and stabilized.
* Readability and graph/status safety were reviewed.
* Row/selection/End Task safety were reviewed.
* Classic preservation stayed close to the prior Task Manager look.
* No monitoring, enumeration, End Task, or App Model behavior changed.
* No new effects were added.
* Tiny readability/layout fix made: none.

## Phase 4F

Phase 4F closes out the extended Sci Fi app-surface milestone as a documentation, inventory, and smoke-hardening pass. It does not add new styling, rendering effects, app behavior, theme IDs, persistence keys, theme metrics, or App Model behavior. No per-effect controls were introduced.

* Classic remains the default theme.
* Sci Fi remains opt-in.
* Missing or invalid theme config falls back to Classic.
* Wallpaper selection remains global, not theme-scoped.
* The Sci Fi wallpaper recommendation in Display Options remains read-only and optional.
* No automatic wallpaper switching exists.
* No theme-scoped wallpaper persistence exists.
* No App Model gate/status dependency was introduced.
* No new effects were added.

### App-Surface Inventory

* Display Options: Phase 3A / 3A.1, plus Phase 4C / 4C.1; stabilization status: complete in 3A.1 and 4C.1; Sci Fi polish covers theme selection copy, the optional read-only wallpaper recommendation note, and conservative panel/card/accent treatment; Classic preservation: default look stays intact and the neutral card feel remains; Behavior changes: No; Safety fix / guard: the Sci Fi recommendation stays gated and read-only, and the earlier fallback tweak preserved the neutral card feel.
* Control Panel: Phase 3B / 3B.1; stabilization status: complete in 3B.1; Sci Fi polish covers conservative panel/card/accent treatment; Classic preservation: look stays close to the prior Control Panel surface; Behavior changes: No; Safety fix / guard: shared grid top offset stays synchronized for drawing and hit-testing.
* Notepad: Phase 3C / 3C.1; stabilization status: complete in 3C.1; Sci Fi polish covers conservative editor/body/border/text treatment; Classic preservation: look stays close to the prior Notepad surface; Behavior changes: No; Safety fix / guard: surface helpers stayed centralized and no visible fallback tweak was needed.
* Calculator: Phase 3D / 3D.1; stabilization status: complete in 3D.1; Sci Fi polish covers conservative body/display/button/accent treatment; Classic preservation: look stays close to the prior Calculator surface; Behavior changes: No; Safety fix / guard: shared compositor widget guard stayed narrow to Calculator and Sci Fi.
* File Explorer: Phase 3E / 3E.1; stabilization status: complete in 3E.1; Sci Fi polish covers conservative body/list/toolbar/address/selection/hover/border/status treatment; Classic preservation: look stays close to the prior File Explorer surface; Behavior changes: No; Safety fix / guard: Sci Fi hover clears on scroll and offset changes so hover stays visual-only.
* Clock: Phase 3F / 3F.1; stabilization status: complete in 3F.1; Sci Fi polish covers conservative body/readout/accent treatment and clear repaint handling; Classic preservation: look stays close to the prior Clock surface; Behavior changes: No; Safety fix / guard: clear-then-redraw removes stale stacked text without changing timekeeping cadence.
* Navigator: Phase 4D / 4D.1; stabilization status: complete in 4D.1; Sci Fi polish covers conservative toolbar/address/content/border/text treatment; Classic preservation: previous Navigator feel remains close to the current look; Behavior changes: No; Safety fix / guard: the shared compositor widget guard now matches Navigator windows only, and authored page colors still win when present.
* Task Manager: Phase 4E / 4E.1; stabilization status: complete in 4E.1; Sci Fi polish covers conservative body/header/list/selection/border/text treatment; Classic preservation: stays close to the prior Task Manager look; Behavior changes: No; Safety fix / guard: readability and graph/status safety were reviewed, and row/selection/End Task safety stayed intact.
* Image Viewer: Phase 4H; stabilization status: chrome-only hosted pilot complete; Sci Fi polish covers body/background, preview border/separators, status strip, and widget chrome outside the image area; Classic preservation: stays close to the current Image Viewer look; Behavior changes: No; Safety fix / guard: styling stayed outside decode/render/scaling/ownership/lifecycle paths, no bare-metal `ImageViewerApp` changes were made, and the shared compositor widget guard is narrow to Image Viewer and Sci Fi.

### Wallpaper / Default Boundaries

* Classic remains the default theme.
* Sci Fi remains opt-in.
* Missing or invalid theme config falls back to Classic.
* Wallpaper selection remains global and not theme-scoped.
* The Sci Fi wallpaper recommendation in Display Options is read-only and optional.
* No automatic wallpaper switching exists.
* No theme-scoped wallpaper persistence exists.

### Deferred Boundaries

* Image Viewer stabilization/readability re-check.
* Other future app-surface polish.
* Bare-metal theme parity.
* High-DPI/scaling.
* Hosted shadow/taskbar polish.
* Rounded client clipping.
* Rounded hit-testing.
* Blur/glass.
* Animations.
* Per-effect controls.
* Theme-scoped wallpaper persistence.
* Broader app redesign.

### Image Viewer Caution

* Image Viewer may be a future theme target, but it should be approached carefully because prior large-image, memory, and repaint concerns make it a higher-risk surface for theme changes.
* This pass does not inspect or modify Image Viewer.

## Phase 4G

Phase 4G is a diagnostic, read-only readiness inventory for Image Viewer. It reviews drawing, repaint, and memory ownership paths before any styling decision is made. It does not change Image Viewer runtime behavior, image loading, image decoding, memory ownership, large-image handling, preview/window close behavior, file open behavior, theme IDs, persistence keys, theme metrics, App Model behavior, or bare-metal code. No Image Viewer styling was implemented in this pass, and no new effects were added.

* Classic remains the default theme.
* Sci Fi remains opt-in.
* Missing or invalid theme config falls back to Classic.
* No per-effect controls were introduced.
* No Image Viewer runtime behavior changed in this pass.
* No Image Viewer styling was implemented in this pass.

### Readiness Inventory

* Image Viewer readiness inventory was performed before deciding whether to style the surface.
* The inventory is hosted-app focused and is meant to guide a later Phase 4H styling pass, not replace it.

### Drawing / Repaint Path

* Hosted Image Viewer paints through its own app-local publish helpers: `MT_DrawText`, `MT_DrawTextAt`, `MT_DrawTextAtColor`, `MT_DrawRect`, `MT_DrawImage`, and `MT_WidgetAdd`.
* `updateDisplay()` starts each repaint by clearing the compositor's text, positioned text, rect, and image layers with a form-feed `MT_DrawText` payload, then repaints the window body and content.
* The body/background outside the image area is drawn with app-local rectangle publishes, not with shared theme widgets.
* The image preview itself is drawn through `MT_DrawImage`.
* The bottom button rows and status text are compositor widgets and positioned text, not a custom Image Viewer skin system.
* `updateDisplayImage()` and `updateDisplay()` are the repaint entry points for image changes, zoom/pan changes, resize, edits, notice text, and error text.
* The hosted and bare-metal image viewer paths are separate: hosted Image Viewer uses the compositor IPC path and `gui::ImagePtr`, while bare-metal `ImageViewerApp` uses framebuffer drawing and kernel-side image loading.
* Stale image/text artifacts are less likely because the repaint path clears the compositor's draw layers before republishing, but the widget rows persist until rebuilt on resize or initial layout.

### Image Loading / Memory Ownership

* `gui::ImageAdapter::LoadFromFile` handles the hosted app load path and only accepts PNG input in this version.
* `ImageAdapter` decodes through `PngLoader::LoadFromMemory`, and `PngLoader` allocates a `gui::Image` then copies decoded RGBA pixels into `Image::Pixels`.
* `Image::~Image` frees the heap pixel buffer with `delete[]`.
* `s_image` is a `std::shared_ptr<gui::Image>`, so replacing it on open or reload releases the previous decoded image once no other references remain.
* Successful loads also snapshot full pixel data into `HistorySnapshot::pixels`, which means undo/redo and original-state tracking can temporarily duplicate large images.
* Edit helpers such as rotate, flip, resize, crop, and restore paths allocate more full-image buffers, and edited previews may be written to the VFS under `tmp/imageviewer/...`.
* On close, the app exits after sending `MT_Close`; there is no special close-time image cleanup beyond normal shared_ptr/vector destruction.
* Opening a second image after the first one loads replaces the active image and refreshes snapshot/history state; prior large-image buffers are released when those owners are overwritten or cleared.
* The large-image/freezing concern remains visible in code because the app still keeps full decoded images, full-pixel snapshots, and compositor-side image loads.

### Safe Future Styling Targets

* Window body/background outside the image area.
* Bottom status strip and separator bands.
* Empty-state, error, and notice text colors.
* Border and separator colors around the preview area.
* Existing compositor widget button fill, border, and text colors, if they are only restyled through the shared widget theme path and do not change widget behavior.
* Small chrome-color tweaks around the image area, so long as they stay outside the decode, scaling, and buffer lifecycle paths.

### Risky / Deferred Areas

* Image decode/render path.
* Large-image scaling path.
* Cached bitmap ownership and preview-buffer ownership.
* Close/reopen lifecycle.
* Bare-metal framebuffer and kernel ImageAdapter paths.
* Any new effects over the image area.
* Blur/glass, animations, rounded client clipping, rounded hit-testing, and per-pixel image filters.
* New retained buffers or theme-side image caches.

### Blockers / Precautions Before Styling

* Run a manual large-image open/close/reopen check before approving styling.
* Verify the app releases its preview and snapshot state when switching images and on close.
* Keep theme helpers outside image buffer ownership and decode paths.
* Avoid new effects, retained buffers, and per-pixel processing over the image area.
* Keep bare-metal validation separate; the kernel ImageViewer is a different app and remains out of scope for this pass.
* Do not alter image loading, image decoding, memory ownership, large-image handling, file open behavior, or preview/window close behavior.

## Phase 4G.1

Phase 4G.1 is the hosted large-image lifecycle safety check for Image Viewer. It is diagnostic and read-only, and it does not change decode, scaling, ownership, close behavior, or any styling/runtime code.

* Hosted lifecycle validation was performed with `desktop.open` and `gui.close`; `gui.start` was unreliable in this environment, so the hosted compositor was bootstrapped through the open path.
* Images used: `assets/Backgrounds/redflower_thumb.png`, `assets/Backgrounds/ameoba.png`, and `assets/Backgrounds/blueflower.png`.
* Open, close, reopen, and alternate-image cycles succeeded in one hosted session.
* The large `ameoba.png` image opened twice in the same session without an obvious stale surface or repaint artifact.
* No stale image surfaces, stale text, or stale button chrome were observed in the open/close screenshots.
* No freeze or crash was observed.
* Built-in `mem` output stayed at `0 KB peak=0 KB` throughout the run, so no obvious runaway was visible in the harness output.
* Memory and lifecycle risk still remains in the implementation because the app continues to use full decoded images and snapshot buffers, so Image Viewer styling is conditionally ready next but only for surrounding chrome, status text, widget fills/borders, and other non-image chrome outside decode, scaling, ownership, and close/reopen lifecycle paths.

## Phase 4H

Phase 4H implements a conservative hosted Image Viewer app-surface pilot for Sci Fi. It is chrome-only: the body/background outside the image area, the preview border and separators, the bottom status strip, and the existing widget/button chrome were styled, while image decode/render/scaling/ownership/lifecycle paths stayed untouched. Classic remains the default theme, Sci Fi remains opt-in, and no theme IDs, persistence keys, theme metrics, or App Model behavior changed.

* Sci Fi gets conservative chrome/status/widget/border polish for hosted Image Viewer.
* Classic is preserved and stays visually close to the current Image Viewer look.
* Styling stays outside image decode, render, scaling, ownership, preview-buffer, snapshot/history, and close/reopen lifecycle code.
* No Image Viewer runtime behavior changed.
* No large-image lifecycle code changed.
* No bare-metal `ImageViewerApp` changes were made.
* No App Model gate/status dependency was introduced.
* No per-effect controls were introduced.
* The shared compositor widget guard is narrow to Image Viewer and Sci Fi only.
* The conditional styling boundary remains in force.
* A future stabilization pass should re-check lifecycle and readability.

## Phase 5

Phase 5 closes the remaining hosted shell popup consistency gap for the opt-in Sci Fi theme. It keeps the existing popup geometry and input model unchanged while making the desktop right-click menu and hosted taskbar tooltip use the same dark navy / indigo / blue surface relationships already used by the hosted taskbar, Start menu, taskbar context menu, and window chrome.

* Hosted desktop right-click menus and the existing folder-icon-size submenu now select Sci Fi surface, border, separator, hover, and text colors from `GetCurrentDesktopTheme()` and the existing `WindowRenderer` blending conventions.
* Hosted taskbar tooltips now select Sci Fi surface, border, and text colors from the current desktop theme.
* The existing taskbar context menu keeps its current themed surface, border, and hover path; its Sci Fi text now uses the existing theme title text color instead of the remaining literal popup text color.
* Classic keeps the previous popup and tooltip color literals and remains the default/fallback theme.
* Theme lookup occurs during paint, so subsequently displayed popups follow the active theme after the existing reload/switch mechanism runs.
* Menu and tooltip dimensions, placement, timing, hit regions, command routing, dismissal, capture, focus, and taskbar behavior were not changed.
* No new popup abstraction, effects, animation, transparency, blur, glow, clipping, or geometry was added.

### Phase 5 Validation State

* The checkout intentionally ignores `/third_party`; the missing local `third_party\stb\stb_image.h` was restored from an established guideXOS Server workspace after confirming the expected v2.30 contents and Git blob identity. No dependency-management or vendored source file was added to the repository.
* `.\build.bat` succeeded from the exact `v0.3_SCI_FI_THEME` checkout and produced the hosted executable used below. The build emitted existing compiler warnings but no dependency, compile, or link failure.
* `.\scripts\smoke-theme-system.ps1 -SkipBuild` passed.
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild` passed.
* `.\scripts\smoke-hosted-display-runtime.ps1` passed in its no-gates, synthetic-only, and dual-window modes.
* `git diff --check` passed.
* Established hosted runtime screenshots and manual inspection proved the exact branch executable in Classic and Sci Fi. Evidence covered the desktop context menu, folder-icon-size submenu, taskbar tooltip, and taskbar context menu. The visual evidence is manual and not machine-comparable; screenshots remained local validation artifacts and were not added to git.

### Phase 5 Resulting State and Next Known Gaps

* The hosted Sci Fi popup consistency implementation is complete within the Phase 5 boundary.
* Classic and Sci Fi popup behavior was exercised on the fresh exact-branch build: open, hover, submenu routing, safe command activation, normal dismissal, tooltip timing/placement, taskbar targeting, and theme switching Classic -> Sci Fi -> Classic.
* Phase 5 is closed with implementation and hosted runtime proof complete. The next sci-fi-theme implementation phase may be planned separately; Phase 6 is not part of this validation record.
* Bare-metal theme parity, rounded clipping/hit-testing, blur/glass, animations, high-DPI/scaling, theme-scoped wallpaper, and broader app-surface work remain deferred.

## Phase 6A

Phase 6A applies the existing Sci Fi desktop language to the three smaller hosted core modal surfaces: Message Box, Shutdown, and Save Changes. Open, Save As, and other file-picker dialogs remain outside this phase because their client surfaces and interaction models are substantially larger.

* The three dialog creators mark their retained window surface through a dedicated visual create flag. The marker carries no geometry, resizability, modality, command, or input semantics.
* The hosted compositor uses that marker to apply the existing Sci Fi `DesktopTheme` relationships to dialog client text, the existing Shutdown panel surface, button fills, button borders, and button text. Button id 1 remains the existing primary action; all other buttons use the existing secondary relationship.
* Hover and pressed states blend from the existing `accent`, `mutedAccent`, `windowBackground`, `windowBorder`, `taskbarBackground`, `taskbarBorder`, and `titleBarText` values. No new arbitrary palette was introduced.
* Classic remains the default and follows the prior widget, panel, and client-text fallback colors. Dialog dimensions, positions, button IDs, hit regions, focus, keyboard handling, modality, close behavior, shutdown behavior, callbacks, and document lifecycle behavior were not changed.
* No generic button-system redesign was added. The shared compositor widget path is gated to the marked core dialog surfaces only.

### Phase 6A Validation State

* `third_party\stb\stb_image.h` remained available as ignored local vendor state.
* `.\build.bat` succeeded from the exact `v0.3_SCI_FI_THEME` checkout. Existing compiler warnings remained; there were no new compile or link failures.
* `.\scripts\smoke-theme-system.ps1 -SkipBuild` passed.
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild` passed.
* `.\scripts\smoke-hosted-display-runtime.ps1` passed in no-gates, synthetic-only, and dual-window modes. Its cleanup removed the tracked `display-options.cfg`; the file was restored byte-for-byte to the `HEAD` baseline, and `desktop.json`/`desktop.state` were verified byte-for-byte against `HEAD`. No runtime state is part of the Phase 6A change.
* The directly relevant `taskmanager.tombstone-test` passed using the exact workspace executable; it exercised the existing non-visual `ShutdownDialog::LaunchSmokeExit` lifecycle path and did not request shutdown.
* Existing validation plumbing was investigated and reused: the compositor's native `GXOS_COMPOSITOR` Win32 host (`initWindow` / `WndProc`), the existing `FindWindow` / `SendInput` / `PostMessage` helpers in temporary hosted validation scripts, the manual `PrintWindow` runbook, `gui.wlist`, compositor marker logs, and the test-only `GXOS_SYNTHETIC_DUAL_MONITOR` / `GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT` modes. No new validation framework or production visibility option was added.
* Direct hosted capture was attempted with the exact rebuilt executable. `gui.start` followed by `msgbox ...` produced `Compositor received MT_Create: Message|320|140|4`, created retained window `1000`, and exposed the expected retained-surface geometry in the compositor log. The same run's Win32 enumeration found only the server wrapper and no targetable `GXOS_COMPOSITOR` window; Computer Use likewise returned only the server process wrapper, and the interactive app-launch path explicitly reported that it did not expose a targetable window.
* The limitation is architectural/environmental rather than a dialog styling result: Message Box, Shutdown, and Save Changes are retained surfaces painted inside the hosted compositor, not separate Win32 child windows. The existing synthetic dual-window mode exposes display-output HWND diagnostics but does not expose those retained dialog surfaces as Computer Use targets. The existing capture helper therefore had no safe host HWND to pass to `PrintWindow`, and no direct screenshot or mouse/keyboard proof could be claimed.
* Classic Message Box: retained create/geometry evidence only; no direct visual, hover, pressed, focus, or dismissal proof. Sci Fi Message Box: no direct visual or interaction proof. Shutdown: retained/source evidence and safe smoke-exit only; no direct visual or interaction proof, and no shutdown/reboot was requested. Save Changes: no direct visual or interaction proof. No unsupported visual pass is claimed.
* The persisted theme-switch route was not re-run for dialog screenshots because the host surface remained unavailable; no theme-switch test state was left behind. Existing Phase 5 Classic -> Sci Fi -> Classic persistence evidence remains unchanged, but it does not close this Phase 6A dialog-specific capture gap.

### Phase 6A Resulting State and Remaining Gaps

* Message Box, Shutdown, and Save Changes now share the surrounding Sci Fi shell's client text, panel, border, and button relationships when Sci Fi is active.
* Open and Save As file dialogs remain outside Phase 6A.
* Generic controls outside these three marked dialogs remain unchanged; generic text boxes, lists, checkboxes, radio buttons, combo boxes, tabs, and scrollbars remain future work.
* Final Phase 6A outcome: Outcome B — implementation complete; direct hosted visual and interaction proof remains technically limited by the unavailable targetable compositor host surface. No Phase 6B implementation was started.

## Phase 6B — Hosted Open/Save Dialog Consistency

Phase 6B applies the existing Sci Fi language to the hosted Open and Save As file-dialog client surfaces without changing their file-navigation or result behavior. The scope is intentionally limited to the two existing dialog implementations and the already-established retained compositor widget/theme path.

* Open and Save As now mark their retained compositor windows with a file-dialog visual create flag. The marker has no geometry, modality, hit-testing, file-system, callback, or keyboard semantics.
* The compositor reuses the Phase 6A Sci Fi dialog button/text relationships for marked file dialogs. The existing `Open` and `Save` labels identify the primary action; all other file-dialog buttons use the established secondary relationship. Classic windows do not carry the marker and keep the prior rendering path.
* Open keeps its existing path label, VFS refresh, directory/file text rows, `> ` selection representation, Up/Open/Cancel widgets, keyboard handling, and callback behavior. When Sci Fi is active, a bounded client base, list surface, and selected-row surface are published behind those existing messages using `DesktopTheme` roles.
* Save As keeps its existing `Save in:` path label, VFS refresh, directory/file text rows, local filename text entry, Drives/Up/Save/Cancel widgets, extension handling, overwrite check, callback behavior, and keyboard handling. When Sci Fi is active, a bounded client base, list surface, filename surface, and selected-row surface are published behind the existing redraw messages.
* No generic text-box, list-view, scrollbar, button, combo-box, or control framework was introduced. The filename field remains the dialog-local text boundary, and the file list remains the dialog-local rendering path.
* Dialog dimensions, positions, row heights, button bounds, scroll offsets, focus state, navigation, selection, double-click/open paths where present, result codes, and file-system semantics were not intentionally changed.

### Phase 6B Validation State

* The expected ignored local dependency `third_party\stb\stb_image.h` remained available in the exact checkout.
* `.\build.bat` succeeded from the exact `v0.3_SCI_FI_THEME` checkout after the Open/Save changes. Existing compiler warnings remained; no compile or link failure occurred.
* `.\scripts\smoke-theme-system.ps1 -SkipBuild` passed.
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild` passed.
* `.\scripts\smoke-hosted-display-runtime.ps1` passed in its no-gates, synthetic-only, and dual-window modes.
* No dedicated Open/Save smoke test exists in this checkout. The existing hosted compositor, modal, and runtime validation paths were reused; no new test framework was added.
* With the exact branch executable, a Classic Open dialog was created from hosted Notepad, its original path/control layout was visible, and Cancel dismissed it. A Sci Fi Open dialog was created after persisted Sci Fi activation; the direct hosted capture showed the navy client surface, readable path, indigo list surface/selection treatment, and Sci Fi-styled Up/Cancel/Open controls. No project or user file was opened or overwritten.
* The repository's advertised `vfs.mkdir`, `vfs.write`, and `vfs.ls` console commands are not implemented by this runtime, and the starting `data/` directory was empty. Therefore a disposable-file Open result, directory navigation, scrolling, and Save-to-disposable-path callback could not be honestly claimed from this run. The existing source paths and result/extension/overwrite logic were audited and left behaviorally unchanged.
* Runtime validation temporarily switched the persisted theme to Sci Fi, then restored `desktop.json`, `desktop.state`, and `display-options.cfg` byte-for-byte to the repository baseline. Temporary runtime bounds state created by this validation was removed.

### Phase 6B Hosted Evidence and Known Limitations

* The strongest available hosted evidence is the direct top-level compositor capture described above, together with the exact executable's retained-window marker logs and geometry. It confirms the file-dialog visual marker, window dimensions, Sci Fi client palette, list surface, readable text, and button treatment.
* The Open and Save surfaces remain retained compositor content rather than independently targetable Win32 child windows. As in Phase 6A, no screenshot claim is made for an independently captured dialog HWND, and no production compositor redesign was added to manufacture one.
* Save filename editing, overwrite confirmation, and returned-path behavior remain covered by the existing dialog implementation and source audit, but were not directly demonstrated end-to-end with a disposable file because the current hosted test runtime does not expose the needed VFS operations and the Notepad-hosted Save As route did not provide a reliable direct interaction path in this run.
* Remaining dialog/control gaps are the pre-existing local list/scroll behavior boundaries, the lack of a shared Sci Fi text-entry control, and the retained-compositor capture limitation. These are future scoped work; Phase 6C is not started.

### Phase 6B Resulting State

The hosted Open and Save As client surfaces now use the same Sci Fi navy/indigo/blue language as the surrounding desktop, shell popups, window chrome, and Phase 6A dialogs while Classic retains its original fallback path. The implementation is complete within the Phase 6B scope. Final outcome: Outcome B — implementation complete; direct hosted visual and interaction proof remains partially limited by the retained compositor surface and the current runtime's unavailable disposable-file operations.

## Phase 6C — Hosted Notification & Transient System Surface Consistency

Phase 6C themes the existing hosted desktop notification toast path so a Sci Fi desktop does not fall back to the Classic-colored system notification surface. The scope is intentionally limited to the compositor-owned top-right notification toasts; Phase 5 tooltips and context menus are not repeated, and no notification center or generic transient framework is introduced.

### Phase 6C Scope and Architecture

* `NotificationManager` remains the owner of notification state, queue order, animation progress, five-second lifetime after entry completes, dismissal, and snapshotting. The existing hosted compositor paint block remains the renderer.
* The covered surface is the hosted GDI top-right toast: its fixed client rectangle, stacked position, information/error fill, border, and message text.
* The current notification implementation has no close button, hover state, focus route, or per-notification hit testing. None was added.
* `server.cpp`'s existing `notify <text>` and `notify.clear` commands were used for harmless runtime validation. No new production trigger or test harness was added.
* Bare-metal notification parity, application-owned transient UI, tooltips, context menus, generic controls, notification history, and actionable notifications remain outside this phase.

### Phase 6C Implementation

* `compositor.cpp` now keeps the original Classic information/error fills, white border, and light text through the Classic branch.
* When Sci Fi is active, the information surface blends the existing hosted panel surface with `DesktopTheme::accent`; the border uses the existing hosted panel border relationship; and message text uses `DesktopTheme::titleBarText`.
* Error notifications retain the existing red semantic meaning by blending the established red error color into the Sci Fi panel base. Warning/error categories were not flattened and no notification-only palette was created.
* Toast width, height, top-right placement, row spacing, text measurement, ordering, lifetime, animation, dismissal, and paint/update flow were not changed.

### Phase 6C Validation State

* The expected ignored dependency `third_party\stb\stb_image.h` remained available in the exact checkout.
* `.\build.bat` succeeded from the exact `v0.3_SCI_FI_THEME` checkout and produced `guideXOSServer.exe`; existing repository warnings remained without a Phase 6C compile or link failure.
* `.\scripts\smoke-theme-system.ps1 -SkipBuild` passed.
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild` passed.
* `.\scripts\smoke-hosted-display-runtime.ps1` passed in no-gates, synthetic-only, and dual-window modes.
* `git diff --check` passed before the final documentation/commit review.
* Classic runtime: after `gui.start`, the supported `notify Phase6C Classic` path queued a notification through `NotificationManager`; `notify.clear` completed the manual-dismiss path; the compositor/server lifecycle remained responsive.
* Sci Fi runtime: after persisted `classic` -> `scifi` restart, `desktop.showconfig` reported `Theme: Sci Fi (scifi)`. Three supported notifications were queued to exercise normal information and stacking/lifetime activity; after the expiry interval the server remained responsive, and `notify.clear` completed cleanup.
* A harmless unknown desktop-launch attempt also exercised the existing error-notification producer path without modifying files or changing launch behavior.
* Runtime validation restored `desktop.json`, `desktop.state`, and `display-options.cfg` to the repository baseline. No runtime state, logs, or disposable test artifacts are part of the phase change.

### Phase 6C Hosted Evidence and Known Limitations

* Source evidence confirms the exact hosted renderer now selects Classic or Sci Fi notification colors from the active theme boundary while preserving the existing notification-manager lifecycle.
* Runtime evidence confirms Classic and Sci Fi persisted-theme restart paths, supported notification queueing, stacking activity, expiry-window responsiveness, manual clearing, and clean compositor shutdown.
* Direct notification pixels were not claimed. The available computer-control window-state capture returned the foreground desktop/Codex surface rather than an independently targetable retained compositor client, matching the known Phase 6A/6B hosted-surface limitation. No compositor redesign or screenshot-only production exposure was added.
* Because the capture limitation prevents direct pixel inspection in this environment, exact contrast and visual appearance remain source/runtime-supported rather than independently screenshot-proven. The implementation is therefore classified as Outcome B.

### Phase 6C Resulting State and Remaining Gaps

The existing hosted notification toast now follows the established Sci Fi shell language for information, error, border, and text roles while Classic keeps its original fallback appearance and all notification behavior remains unchanged. Remaining relevant gaps are bare-metal notification parity, any future close/hover affordance if the product adds one, and broader transient/system-surface coverage. No Phase 7 or generic-control work was started.

Final Phase 6C outcome: Outcome B — implementation complete; direct visual proof partially limited by the retained compositor surface.

## Phase 7A — Bare-Metal Desktop and Taskbar Surface Parity

Phase 7A is the first bare-metal Sci Fi parity pass. It is deliberately limited to the primary framebuffer desktop/taskbar shell surfaces:

* bare-metal desktop fallback/background fill when no wallpaper image is available;
* bare-metal taskbar surface, top edge/border, separators, text, and existing show-desktop surface;
* taskbar item normal, hover, active/selected, minimized, and indicator colors where those states already exist;
* the existing Start button normal and open/pressed visual states.

No geometry or shell behavior was redesigned. Taskbar height, padding, item sizing, Start-button dimensions and hit region, desktop work area, input routing, window behavior, wallpaper ownership, and icon layout remain on their existing paths.

### Bare-Metal Rendering Paths Modified

* `kernel/core/desktop.cpp` remains the primary UEFI/PS/2 framebuffer desktop path. Its existing `draw_background`, `draw_taskbar`, `draw_taskbar_buttons`, clock, tray separator, and Start-button paint paths now select Sci Fi colors through `GetCurrentDesktopTheme()` and the shared `DesktopTheme` fields. The existing VFS-ready desktop refresh also reloads the persisted theme after the initial pre-mount draw.
* `kernel/core/kernel_compositor.cpp` owns live kernel-app taskbar item painting and hover tracking. Its normal, hover, active, indicator, and text colors now use the shared theme state.
* `compositor.cpp` keeps the existing non-Windows framebuffer fallback path theme-aware for desktop fallback, taskbar, Start button, taskbar items, indicators, and clock text. Wallpaper images still take precedence over the fallback fill.
* `desktop_theme.h/.cpp` remain the single theme definition and selector boundary. The parser was made freestanding-safe, and the existing `desktop_theme.cpp` is linked into the kernel through `kernel/Makefile`; no second palette or bare-metal theme table was added.

The framebuffer path can safely call the existing accessors because the theme data is portable, constant-sized state and the shared implementation no longer depends on the hosted C++ standard library. Bare-metal code imports only the existing `kernel/types.h` compatibility layer; no GDI, Win32, allocation, filesystem, floating-point effect, or architecture-specific theme dependency was introduced. The amd64 QEMU path uses the shared renderer; the palette/blend helpers use integer RGB arithmetic and preserve opaque framebuffer colors.

### Classic Preservation

Classic remains the default and fallback. The Classic branches retain the prior framebuffer RGB values for desktop/taskbar surfaces, taskbar item states, Start button, borders, indicators, and text. Missing, invalid, or unavailable persisted theme configuration selects Classic. No geometry, hit testing, work-area calculation, or input behavior changed.

### Sci-Fi Result

When Sci Fi is active, the bare-metal desktop fallback uses the existing dark desktop/window/accent relationship, while an active wallpaper remains untouched. The taskbar uses the existing Sci Fi taskbar background and border relationship with inexpensive integer blending; Start, taskbar items, active indicators, hover surfaces, separators, and text use the existing Sci Fi palette. The result is palette/surface parity with the hosted shell without gradients beyond the existing framebuffer gradient primitive, shadows, blur, glow, animation, or rounded clipping.

### Start-Menu Boundary

The themed Start button continues to open and dismiss the existing Start menu. Start-menu contents remain Classic and are explicitly outside Phase 7A. This boundary is intentional and preserves the existing menu behavior while the primary desktop/taskbar shell surfaces receive parity.

### Phase 7A Build and Automated Evidence

* `third_party/stb/stb_image.h` remained available as the ignored dependency (283,010 bytes).
* `.\build.bat` passed and produced `guideXOSServer.exe`.
* `mingw32-make -C kernel ARCH=amd64 -j2` passed, including the shared `desktop_theme.cpp` object and the linked `kernel/build/amd64/bin/kernel.elf`.
* `.\scripts\smoke-theme-system.ps1 -SkipBuild` passed.
* `.\scripts\smoke-live-directory-desktop-status.ps1 -SkipBuild` passed.
* `.\scripts\smoke-hosted-display-runtime.ps1` passed in normal-single-output, synthetic-only, and dual-window modes.
* `.\scripts\smoke-desktop-startup-sync.ps1 -TimeoutSeconds 90` passed against the exact built AMD64 image; the desktop mounted VFS, loaded the persisted theme once, and completed startup scanning without a launch loop.
* `.\scripts\smoke-bootinfo-framebuffer-array.ps1` passed as the existing framebuffer/boot-info source smoke.
* `git diff --check` passed after the source/documentation changes.

### Phase 7A Bare-Metal/QEMU Evidence

The exact workspace image was built with `.\build.ps1 -Arch amd64`: UEFI bootloader, AMD64 kernel, ESP staging, and QEMU prerequisites all passed. QEMU booted the image through UEFI handoff, framebuffer initialization, VFS mount, desktop redraw, PS/2 mouse and keyboard initialization, and the normal main loop.

* Classic persisted configuration booted to the normal usable desktop/taskbar shell and logged the standard framebuffer, taskbar, Start-menu, and event-loop capabilities.
* A temporary ESP `desktop.cfg` selection of `desktopThemeId=scifi` loaded after VFS mount as `[desktop] loaded desktop theme=scifi source=primary`; the subsequent redraw reached the normal event loop with `taskbar_enabled=true` and `start_menu_enabled=true`.
* QMP VGA capture produced the ignored local screenshot `logs/phase7a-qemu/phase7a-scifi-vga.png`, showing the Sci Fi desktop/taskbar palette with the active wallpaper and readable clock text.
* A QMP mouse click on the existing Start-button hit region produced `logs/phase7a-qemu/phase7a-scifi-start-menu-click.png`; the Classic Start menu opened over the themed Start button/taskbar, confirming the intentional Phase 7A boundary and preserved invocation path.
* The temporary ESP theme selection and generated build/config artifacts were restored to their pre-validation baseline. The screenshots remain local ignored evidence and are not release artifacts.

The QEMU visual captures directly prove the Sci Fi desktop/taskbar and Start-button surfaces. Live taskbar item hover/active rendering is source/build-proven through the kernel compositor path; this boot did not launch a separate taskbar application instance for a dedicated live item-state screenshot. Phase 7A is therefore classified as Outcome B: implementation complete with strong bare-metal boot and visual proof, with a narrow live-item-state capture limitation.

### Phase 7A Remaining Bare-Metal Gaps

The following remain outside this phase and were not changed:

* bare-metal window chrome, title bars, and window buttons (the Phase 7A closeout gap addressed by Phase 7B below);
* Start-menu contents and Start-menu surface restyling;
* bare-metal dialogs and notifications;
* application surfaces such as File Explorer, Navigator, Developer Studio, and other apps;
* icons, fonts, wallpaper ownership/selection, multi-monitor behavior, glass/blur, shadows, animations, rounded clipping, and rounded hit testing.

After this validation, the recommended next phase is a separately scoped bare-metal window-chrome pass. It should not be started as part of Phase 7A.

## Phase 7B — Bare-Metal Window Chrome & Title Bar Parity

Phase 7B closes the first Phase 7A window-surface gap without reopening or reclassifying Phase 7A. It is a framebuffer paint-parity pass for existing kernel windows: outer frame and border, active/inactive title bars, title text, the existing title-bar accent treatment, close/minimize/maximize-or-restore button surfaces, and their existing hover/pressed states. Application client surfaces and Start-menu contents remain outside Phase 7B.

### Bare-Metal Chrome Architecture

The normal UEFI/framebuffer desktop path remains in `kernel/core/desktop.cpp`. Its compositor boundary is `kernel/core/kernel_compositor.cpp`: `KernelCompositor::drawAllWindows()` calls `drawWindow()` for the frame/background/border and `drawTitlebar()` for title text and title buttons. `DesktopTheme`, `DesktopThemeId`, and `GetCurrentDesktopTheme()` remain the only theme-selection boundary; no second window palette or architecture-specific table was added.

The existing geometry constants in `kernel/core/include/kernel/kernel_compositor.h` remain authoritative: title-bar height 24, border width 1, button size 16, button gap 6, and the existing resize margin. `hitTest()`, mouse capture, focus, drag, resize, close, minimize, maximize, restore, z-order, clipping, and repaint routing remain on their prior paths.

### Sci-Fi Active and Inactive Treatment

Sci-Fi active windows use the existing dark navy/indigo relationship: `titleBarBackground` for the active title bar, `titleBarText` for readable titles/icons, an integer-blended `accent` top highlight, and a stronger `windowBorder`/`accent` frame edge. Inactive windows use `taskbarBackground`, dimmed title text, a muted-accent highlight, and a conservative `windowBorder`/`mutedAccent` frame blend. This preserves a clear active/inactive distinction without glow, blur, transparency, animation, or geometry changes.

The existing simple shadow is recolored only for Sci-Fi using the shared integer blend helper. Its offset, dimensions, algorithm, and paint cost are unchanged. The framebuffer branch does not consume hosted title-bar metrics or hosted rounded/soft-shadow effects.

### Title Buttons

The existing close, maximize/restore, and minimize paint paths keep their original positions, dimensions, icon geometry, and state booleans. Sci-Fi idle, hover, and pressed surfaces use blends of the shared chrome colors. Close retains its existing danger/red semantic, blended over the Sci-Fi chrome base rather than being converted into a generic blue control. Classic keeps the original button colors and fallback branches.

### Classic Preservation and Geometry Freeze

Classic remains the fallback and its existing active/inactive title bars, border colors, title text, button colors, and red close semantic remain unchanged. The Sci-Fi branch is narrow and selected only when `GetCurrentDesktopTheme().id` is `DesktopThemeId::SciFi`.

No window bounds, title-bar height, frame thickness, button width/height/spacing, title coordinates, resize borders or hit zones, drag region, client origin/dimensions, clipping, maximized bounds, or title-button hit regions changed. The expected Phase 7B geometry result is **no change**.

### Build, QEMU, and Interaction Proof

The exact workspace passed `mingw32-make -C kernel ARCH=amd64 -j2`, `.\build.bat`, and `.\build.ps1 -Arch amd64`. The required smoke tests passed: `smoke-theme-system.ps1 -SkipBuild`, `smoke-live-directory-desktop-status.ps1 -SkipBuild`, `smoke-hosted-display-runtime.ps1`, `smoke-desktop-startup-sync.ps1`, `smoke-bootinfo-framebuffer-array.ps1`, `smoke-virtio-gpu-input-routing.ps1`, and `git diff --check`.

The exact staged AMD64 image booted through the normal UEFI/framebuffer path. For this local OVMF image, a temporary ignored `ESP/startup.nsh` fallback-entry helper was used to select `EFI/BOOT/BOOTX64.EFI`; it was removed after validation. Persisted `desktopThemeId=classic` logged Classic chrome, and persisted `desktopThemeId=scifi` logged `[desktop] loaded desktop theme=scifi source=primary` before the normal event loop.

Classic QMP VGA captures launched Calculator and Notepad, exercised the existing drag route, switched focus in both directions, and closed a harmless window. Sci-Fi captures launched the same two windows, showed the Sci-Fi desktop/taskbar plus one active and one inactive themed window, exercised drag and both focus transitions, closed Calculator, and reopened it through the existing Start-menu/recent-app route. Representative local ignored evidence is `logs/phase7b-qemu/scifi-20260823-125702-two-windows-after-drag.png`, `logs/phase7b-qemu/scifi-20260823-125702-focus-calculator.png`, `logs/phase7b-qemu/scifi-20260823-125702-focus-notepad.png`, and the matching Classic captures. The QMP path was the existing PS/2-relative input path; no hit-region drift was observed.

### Performance, Portability, and Remaining Gaps

The paint change adds no heap allocation, large buffer, GDI/Win32 call, filesystem access, floating-point effect, animation, blur, glow, or architecture-specific assumption. Shared theme colors and integer blending remain portable to freestanding framebuffer architectures.

Remaining bare-metal gaps are bounded to dialogs and notifications, and application client surfaces such as File Explorer, Navigator, and Developer Studio. Generic controls, fonts, icons, wallpaper ownership, multi-monitor behavior, glass/blur, new shadows, animations, and rounded clipping/hit testing remain outside this phase. Phase 7C below owns the Start-menu surface pass.

## Phase 7C — Bare-Metal Start Menu Consistency

Phase 7C closes the remaining obvious Phase 7A/7B shell boundary: when Sci-Fi is active, the existing bare-metal Start menu uses the same dark navy / indigo / blue relationship as the Sci-Fi desktop, taskbar, Start button, and window chrome. This is a styling-only pass. Menu contents, command semantics, navigation, and geometry remain unchanged.

### Start-Menu Rendering Architecture

The normal UEFI/framebuffer Start-menu renderer is `draw_start_menu()` in `kernel/core/desktop.cpp`, called once from the ordinary desktop `draw()` path. It paints the existing fixed two-column surface: a user header, left recent/all-program rows, right system-shortcut rows, the column/header/footer separators, an All Programs toggle, and the existing Sleep/Restart/Shut Down footer actions. Existing Flat start-menu icons and colored fallback blocks remain in their current positions and sizes. No second bare-metal Start-menu renderer was found in `kernel/core/kernel_compositor.cpp` or another shell path.

`get_start_menu_geometry()` is shared by paint and hit testing. It calculates the existing 420-pixel menu width, 30-pixel header, 22-pixel rows, 36-pixel footer, two-column bounds, taskbar-dock-aware origin, and body height. `hit_test_start_menu()` handles left/right row routing, `hit_test_start_menu_footer()` handles the footer controls, and the existing mouse-down/mouse-move paths handle launch, hover, click-outside dismissal, keyboard selection, and taskbar interaction. No submenu implementation exists in this bare-metal menu, so no submenu surface or timing was added.

### Sci-Fi Menu Treatment

Sci-Fi menu colors are selected from `GetCurrentDesktopTheme()` and the shared `DesktopTheme` fields. The menu surface uses `windowBackground`, its border and separators use blended `windowBorder`/`taskbarBorder` with `accent`, and the existing header/footer regions use `titleBarBackground` and `taskbarBackground`. Left and right rows use darker shared-theme surfaces, while hover, clicked, and keyboard-selected rows use integer blends with `accent` or `mutedAccent`. Primary and secondary labels use `titleBarText` blends for contrast. The All Programs control follows the same active/inactive relationship. Shut Down retains a visible red danger treatment blended into the Sci-Fi shell; Sleep and Restart remain neutral session controls. There is no transparency, blur, glow, gradient, animation, new shadow, rounded clipping, or expensive framebuffer primitive.

### Classic Preservation, Geometry, and Behavior Freeze

Classic color branches return the prior literal framebuffer colors for the menu background, header, rows, separators, text, All Programs control, and power/session controls. Sci-Fi-only palette selection does not alter item ordering, icons, text coordinates, row padding, menu origin, menu width/height calculation, Start-button dimensions, footer bounds, submenu placement, or hit regions. The expected geometry result is **no change**. Existing launch, session-action, hover, keyboard-selection, click-outside dismissal, taskbar responsiveness, and any right-click Start-menu context behavior remain on their existing paths.

### Phase 7C Validation Record

The exact workspace passed `.\build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `.\build.ps1 -Arch amd64`. The required `smoke-theme-system.ps1 -SkipBuild`, `smoke-live-directory-desktop-status.ps1 -SkipBuild`, `smoke-hosted-display-runtime.ps1`, `smoke-desktop-startup-sync.ps1`, `smoke-bootinfo-framebuffer-array.ps1`, and `git diff --check` checks passed. The existing `smoke-virtio-gpu-input-routing.ps1` source check also passed. `smoke-appmodel-launchshadow.ps1` was attempted as the available Start-menu dispatch regression, but its pre-QEMU rebuild stopped in the child Windows PowerShell environment because `Get-FileHash` was unavailable there; the normal PowerShell 7 image build passed afterward and the exact-image launch proof below passed.

The exact AMD64 image booted through UEFI/framebuffer with the repository's writable ESP configuration path and `-machine pc`. A temporary local `ESP/startup.nsh` fallback selected `EFI/BOOT/BOOTX64.EFI`; it was removed after validation. With `desktopThemeId=classic`, serial logged `loaded desktop theme=classic source=primary`; with `desktopThemeId=scifi`, serial logged `loaded desktop theme=scifi source=primary`. The tracked configuration was restored to Classic after both boots.

Classic QMP/VGA evidence is in the ignored local run `logs/phase7c-qemu/final-classic-20260823-150553/`: the menu opened from the Start button, the Control Panel row hovered, click-outside dismissed it, All Programs exposed the existing application list, Calculator hovered and launched, and Start reopened over the running Calculator before a second outside dismissal. The Classic menu retained its original dark gray/blue/red treatment. Sci-Fi evidence is in `logs/phase7c-qemu/final-scifi-20260823-150652/`: the same sequence produced the dark navy/indigo menu surface, shared blue borders and row states, readable text, preserved red Shut Down semantics, a Sci-Fi-framed Calculator window behind the reopened menu, and a responsive taskbar. The QMP path was the existing PS/2-relative input path; no submenu exists. No menu origin, width/height, row bounds, footer bounds, Start-button dimensions, text/icon coordinates, or hit regions changed. Screenshots, logs, generated images, and temporary ESP/runtime configuration remain local validation state and are not release files.

### Phase 7C Remaining Bare-Metal Gaps

Dialogs, notifications, application client surfaces, generic controls, icons, fonts, wallpaper ownership, multi-monitor behavior, and other non-Start-menu shell/app parity remain outside Phase 7C. This phase does not begin the next bare-metal pass.

## Phase 7D — Bare-Metal System Dialog & Notification Consistency

Phase 7D is a compact bare-metal shell pass for the small system-owned surfaces that can otherwise interrupt an otherwise coherent Sci-Fi desktop. It is styling-only: existing geometry, hit testing, lifecycle, input routing, and action semantics remain authoritative.

### Exact Scope and Popup Architecture

The included surfaces are the existing framebuffer paths in `kernel/core/desktop.cpp`:

* `draw_right_click_menu()` — the shell-owned desktop context menu, including its existing Start-menu-app variant. It is a fixed-width transient overlay positioned from the existing pointer/menu state; the existing item list, separators, hover tracking, command dispatch, click-outside dismissal, and input hit regions remain unchanged.
* `draw_notifications()` — the desktop-owned `NotificationToast` at the existing fixed top-right location. Its existing title/message/close/timestamp paint, five-second lifetime, close dismissal, and cleanup remain unchanged. No separate bare-metal notification manager or queue renderer was found.
* `draw_shutdown_dialog()` — the existing centered shutdown confirmation opened by the Start-menu footer. Its existing Yes/No buttons, outside dismissal, Escape/Alt-F4 dismissal, modality, and shutdown result path remain unchanged. The safe validation path is Cancel/No; destructive shutdown is not exercised.

The normal desktop `draw()` path paints these surfaces after the desktop/taskbar and before the compositor windows. No separate system-popup renderer was found in `kernel/core/kernel_compositor.cpp`, and no additional freestanding message-box or save-changes renderer was identified in the shell path.

### Scope Exclusions

The App Model viewer, Control Panel, Device Manager, Network Adapters, Network Configuration, Notepad, Open/Save/Save As, file pickers, and other application-owned or large multi-control dialogs remain excluded. They require application-surface work, filesystem workflow review, or broader control/dialog infrastructure and are not part of this compact phase. No generic button, text-box, list, scrollbar, checkbox, radio, combo, tab, notification-center, history, action, sound, or animation work was added.

### Classic Preservation and Sci-Fi Treatment

Classic remains the fallback and retains the existing popup literals, borders, text colors, dimensions, and control appearance. Sci-Fi branches consume only `GetCurrentDesktopTheme()` / `DesktopTheme` and the existing integer `BlendDesktopThemeColor()` helper:

* The context menu uses the existing Sci-Fi window/desktop/taskbar surfaces, border, accent, separator, hover, and readable title-text roles.
* Notifications use blended Sci-Fi taskbar/window surfaces, window border/accent, accent strip, title/body/close/timestamp text, and the existing transient layout.
* The shutdown dialog uses Sci-Fi window/title-bar/border/text roles. `Yes` remains a red destructive action with a red semantic border/text treatment, while `No` remains the neutral/cancel action using the Sci-Fi taskbar/window relationship. Error/warning semantics are not collapsed into a single indigo action color.

No dialog-specific theme object, notification palette table, duplicated Sci-Fi constants, hosted dependency, allocation, filesystem access, large buffer, floating-point effect, or architecture-specific theme behavior was introduced.

### Geometry and Behavior Freeze

The existing dialog/menu/toast dimensions, positions, borders, padding, text bounds, button bounds, notification stacking/lifetime, modal work area, and hit regions are unchanged. Modality, focus, input capture, result codes, Enter/Escape/Alt-F4 behavior, outside dismissal, shutdown semantics, notification timeout, and cleanup are unchanged. The expected geometry result is **no change**.

### Phase 7D Validation Record

The exact workspace passed `.\\build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `.\\build.ps1 -Arch amd64`. The required theme, live-directory, hosted-display, startup-sync, framebuffer-array, VirtIO routing, and `git diff --check` validations passed. The available `smoke-appmodel-launchshadow.ps1` regression was attempted; its child Windows PowerShell rebuild stopped before QEMU because `Get-FileHash` was unavailable in that child environment, while the normal PowerShell 7 image build passed.

The exact staged AMD64 image booted through UEFI/framebuffer and reached the normal bare-metal desktop. A temporary ignored `ESP/startup.nsh` fallback selected `EFI/BOOT/BOOTX64.EFI` for the local OVMF entry-point quirk and was removed after validation. Classic configuration loaded `desktopThemeId=classic`; Sci-Fi configuration loaded `desktopThemeId=scifi` through the established bare-metal path. Classic QEMU capture showed the existing Classic context menu and Start-menu shell, and the existing notification path was observed during desktop startup. Sci-Fi QEMU capture showed the corresponding themed desktop context-menu surface and notification surface in surrounding shell context. Serial/QMP evidence remains in ignored local `logs/phase7d-qemu/` artifacts.

The safe interaction proof confirmed context-menu opening/hover and dismissal, Start-menu opening, and the existing notification lifecycle without changing queue or timeout behavior. The QEMU PS/2 pointer path was not reliable enough for a repeatable pixel-targeted click on the shutdown footer in this run; source-level inspection and the exact image boot prove the existing shutdown dialog path and unchanged hit geometry, but a fresh visual screenshot of the shutdown dialog and its No/Cancel click is still a proof limitation. No destructive action was attempted.

### Remaining Bare-Metal Gaps

Phase 7D closes the small shell-owned context-menu, notification-toast, and shutdown-dialog styling gap. Remaining relevant gaps are the explicitly excluded application-owned/large dialogs and broader application surfaces, plus generic-control parity, icons, fonts, wallpaper ownership, multi-monitor behavior, blur/glass, animations, rounded clipping, and rounded hit testing. These remain separate future work and are not claimed as generic dialog/control parity.

## Phase 8A — Bare-Metal Core Application Surface Consistency

Phase 8A applies the established Sci-Fi palette to the two small bare-metal application interiors that are most visible beside the Phase 7 shell and window chrome: Calculator and Notepad. This is a styling/parity pass only. Classic remains the default and fallback, and no generic control-system rewrite or Phase 8B work is included.

### Bare-Metal Application Architecture

The affected bare-metal applications are kernel-owned `CalculatorApp` and `NotepadApp` in `kernel/core/kernel_apps.cpp`. Their framebuffer paint paths are called from the existing `KernelCompositor::drawWindow()` path in `kernel/core/kernel_compositor.cpp`. The same compositor owns widget paint, window hit testing, mouse routing, keyboard routing, and application focus/lifecycle dispatch. The root `calculator.cpp` and `notepad.cpp` files are hosted IPC clients with an already-validated hosted Sci-Fi path; they were not restyled or refactored in Phase 8A.

Calculator keeps its existing window geometry, display bounds, button layout, button labels, manual framebuffer result glyphs, and application input/arithmetic paths. Its bare-metal Sci-Fi branch uses the shared `DesktopTheme` and integer `BlendDesktopThemeColor()` relationship for the client body, display surface/border, result text, and display punctuation. The compositor's existing widget primitive is guarded narrowly by the Calculator window name and Sci-Fi theme ID. Numeric buttons stay neutral; operators, equals, and clear retain distinct accent/semantic relationships; hover and pressed states are blends of the same local roles. No global button styling change was made.

Notepad keeps its existing editor origin and dimensions, fixed font and text measurement, menu geometry, context-menu geometry, caret width, selection behavior, keyboard routing, scrolling, and document/file semantics. Its bare-metal Sci-Fi branch themes the client/editor surface, editor border, normal text, caret, select-all selection fill, menu bar, menu items, hover states, context menu, and existing shadow relationship. The bare-metal implementation has no locally drawn status bar or scrollbar surface to theme. Open/Save dialog styling remains excluded as an application-owned dialog boundary.

### Classic Preservation, Behavior, and Geometry Freeze

Classic branches retain the prior literal Calculator and Notepad client colors and the prior generic widget colors. The Sci-Fi selection is made through `GetCurrentDesktopTheme()` / `GetCurrentDesktopThemeId()` and `DesktopThemeId::SciFi`; no CalculatorTheme, NotepadTheme, application registry, bare-metal color table, hosted dependency, or second palette was introduced.

No Calculator or Notepad window/client bounds, display/button/editor/menu/status bounds, button dimensions, editor/text coordinates, spacing, hit regions, focus routing, keyboard shortcuts, arithmetic, clear behavior, caret movement, selection semantics, clipboard behavior, scrolling, dirty-state handling, line endings, encoding, or close/reopen behavior changed. The expected geometry result is **no change**. Existing bare-metal Notepad selection remains the existing select-all path; Phase 8A does not invent range-selection behavior.

### Performance and Portability Boundary

The paint changes add no allocations, large buffers, filesystem access, GDI/Win32 dependency, floating-point effect, blur, glow, shadow system, animation, rounded clipping, or architecture-specific rendering behavior. They use the existing framebuffer primitives, shared theme fields, and integer blending helper. Generic buttons, text boxes, scrollbars, lists, menus, icons, fonts, File Explorer, and larger application interiors remain outside Phase 8A.

### Phase 8A Validation Record

The exact workspace passed `build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `build.ps1 -Arch amd64`. The Phase 8A theme smoke, live-directory desktop-status smoke, hosted display-runtime smoke, desktop-startup-sync smoke, bootinfo/framebuffer-array smoke, virtio-GPU input-routing smoke, and `git diff --check` passed. `smoke-appmodel-launchshadow.ps1` remains limited by the known child-Windows-PowerShell `Get-FileHash` harness issue in `build-pacman-package.ps1`; no unrelated theme code was changed for it.

Classic was booted from the exact AMD64 image with the Classic configuration. Calculator rendered with its original client appearance, accepted mouse-targeted input, and completed `2 + 2` with a visible result of `4`; the Classic Calculator proof is retained in `logs/phase8a-qemu/classic-calculator-2plus2-final.png`. Classic Notepad was launched and its original editor surface remained readable in `logs/phase8a-qemu/classic-notepad.png`. The Sci-Fi configuration was then booted from the same exact image. The Sci-Fi desktop and taskbar rendered correctly, and the Calculator client visibly used the Sci-Fi body, display, display border, button grouping, and accent relationships in `logs/phase8a-qemu/scifi-calculator-shell.png`.

The QEMU shell-overlay/input path prevented a clean second-window capture and prevented completing the requested Sci-Fi Notepad typing, selection, caret, and combined Calculator/Notepad screenshot proof. Source inspection and exact builds confirm that Notepad's existing text, caret, and select-all paths now consume the Sci-Fi editor, text, caret, and selection colors while Classic remains unchanged. No behavior or geometry was changed to work around the proof limitation. Classic and Sci-Fi configuration selection were both boot-tested; tracked configuration was restored afterward. File Explorer and generic controls remain outside Phase 8A.

## Phase 8B — Bare-Metal File Explorer Surface Consistency

Phase 8B applies the established Sci-Fi palette to the existing bare-metal File Explorer client surface. It is a styling-only pass: the same File Explorer, files, navigation, geometry, and input behavior remain in place. Classic remains the default and exact fallback, and no Phase 8C or generic-control work is included.

### Bare-Metal File Explorer Architecture

The freestanding implementation is `FileExplorerApp` in `kernel/core/kernel_apps.cpp`, registered by the existing bare-metal app factory and launched through the normal `Files`/`FileExplorer` dispatch aliases. `FileExplorerApp::draw()` owns the client paint path: toolbar, address/path strip, left navigation pane, list header and rows, selection, local scrollbar, footer/status text, context menu, rename/create-folder prompt, delete confirmation, and properties surface. The existing `KernelCompositor::drawWindow()` supplies the window frame/title bar and paints the existing toolbar widgets afterward. A narrow File Explorer owner guard in `kernel/core/kernel_compositor.cpp` styles those existing widgets only for Sci-Fi; it does not change the shared widget defaults.

The app owns VFS enumeration and sorting, current-path normalization, mount/root navigation, parent navigation, selection and scroll state, row hit testing, double-click activation, keyboard navigation, wheel scrolling, context-menu routing, file/folder activation, rename/delete/copy/move/paste, and close/reopen lifecycle. `handleNavigationPaneClick()` owns root and mount-row hit bands. No row-hover renderer exists in the bare-metal path; the existing context-menu hover remains the only local hover state. Hosted File Explorer uses a separate hosted renderer and was not changed.

### Sci-Fi Surfaces and Boundaries

Sci-Fi uses `GetCurrentDesktopTheme()` and `BlendDesktopThemeColor()` through local File Explorer color-role helpers. The client, toolbar, address/path strip, navigation pane, list surface, list header, primary/secondary/muted text, focused/inactive selected-row treatment, footer/status region, local scrollbar track/thumb/borders, context menu, and application-owned rename/delete/properties overlays now use the existing dark navy, blue, indigo, and title-text relationships. The toolbar buttons use the same palette through the narrow File Explorer compositor guard, including existing hover and pressed states.

Classic branches retain the prior File Explorer literals. No client, toolbar, address, navigation, list, row, icon, scrollbar, footer, overlay, or widget geometry changed. The existing bitmap icons are deliberately untouched: no icon recoloring, replacement, spacing change, size change, or icon-theme integration was introduced. No filesystem reads, allocations, effects, hosted dependencies, floating-point work, or architecture-specific theme behavior were added. The local scrollbar remains application-specific; no generic scrollbar framework was started.

### Phase 8B Validation Record

The exact workspace passed `.\build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `.\build.ps1 -Arch amd64`. `smoke-theme-system.ps1 -SkipBuild`, `smoke-live-directory-desktop-status.ps1 -SkipBuild`, `smoke-hosted-display-runtime.ps1`, `smoke-bootinfo-framebuffer-array.ps1`, `smoke-virtio-gpu-input-routing.ps1`, and `git diff --check` passed. The standalone `tests/file_explorer_navigation_test.cpp` regression test also passed. `smoke-desktop-startup-sync.ps1` was attempted but its UEFI harness remained at the shell because the workspace ESP has no tracked `startup.nsh`; no startup logic was changed. `smoke-appmodel-launchshadow.ps1` remains limited by the known child Windows PowerShell `Get-FileHash` failure before QEMU.

Classic and Sci-Fi were booted from disposable copies of the exact workspace-built ESP payload. Each copy added only a temporary `startup.nsh` handoff to `EFI/BOOT/BOOTX64.EFI`; the tracked ESP state was restored afterward. `desktopThemeId=classic` produced the original light File Explorer surface, and `desktopThemeId=scifi` produced the dark Sci-Fi surface. The Classic capture set is `logs/phase8b-qemu-classic-after-file-click.png`, `logs/phase8b-qemu-classic-apps.png`, `logs/phase8b-qemu-classic-up.png`, `logs/phase8b-qemu-classic-file-selected.png`, `logs/phase8b-qemu-classic-closed.png`, and `logs/phase8b-qemu-classic-reopened.png`. The Sci-Fi set is `logs/phase8b-qemu-scifi-file-explorer.png`, `logs/phase8b-qemu-scifi-apps.png`, `logs/phase8b-qemu-scifi-up.png`, `logs/phase8b-qemu-scifi-file-selected.png`, `logs/phase8b-qemu-scifi-closed.png`, and `logs/phase8b-qemu-scifi-reopened.png`.

QMP input proof confirmed launch from the desktop icon, a populated root list, row selection, double-click navigation into `/Apps`, `Up` navigation back to `/`, file selection of `desktop.cfg`, close, and reopen under both themes. The root contents fit within the existing list bounds, so no scrollbar interaction was required and no keyboard-navigation pass was added. No destructive file operation was attempted. No combined Calculator/Notepad capture was needed for this phase.

Remaining gaps are bounded to larger bare-metal application interiors, generic-control parity, icon/font integration, and settings/system application coverage. Generic lists, generic scrollbars, generic buttons, menus, dialogs, and icon integration remain out of scope unless separately authorized.

## Phase 8C — Bare-Metal Shared Control Theme Foundation

Phase 8C audits the repeated Sci-Fi color decisions established by the bare-metal shell, Calculator, Notepad, File Explorer, shell-owned popups, and compositor widgets. It deliberately extracts only color roles that the existing code has already earned. No new widget framework, layout system, theme observer, compositor protocol, or application registry was introduced.

### Audit Findings and Architecture Decision

The bare-metal application interiors do not share one renderer. `NotepadApp`, `CalculatorApp`, and `FileExplorerApp` own separate framebuffer paint paths in `kernel/core/kernel_apps.cpp`. The existing shared boundary is narrower: `KernelCompositor::drawWidget()` paints the existing widget primitive used by Calculator buttons and File Explorer toolbar buttons, with explicit owner guards for their semantic differences. The shell desktop in `kernel/core/desktop.cpp` owns its taskbar, Start-menu, context-menu, notification-toast, and shutdown-dialog paint helpers. Those surfaces have related palette inputs but different semantic roles and no shared control renderer.

The hosted `WindowRenderer` has a useful analogous blend helper, but it is coupled to hosted GDI types and hosted chrome behavior. It was not refactored or imported into the freestanding path. No clean shared bare-metal scrollbar boundary exists: File Explorer owns the only audited bare-metal scrollbar renderer, so no generic scrollbar role or control was created.

**Architecture decision: Only minimal helper extraction justified.** A small stateless color-role layer is justified for repeated panel/text/selection relationships and repeated button surface state transitions. A broader shared control foundation would force application-specific formulas and shell semantics together, so it was intentionally not created.

### Foundation and Narrow Migration

`desktop_control_theme.h` adds the allocation-free `BareMetalControlTheme` role map, `BareMetalSelectionFillColor()`, and `BareMetalButtonSurfaceRoles`. The helpers take the current `DesktopTheme`, return colors only, use the existing integer `BlendDesktopThemeColor()` relationship, and contain no geometry, input, ownership, filesystem, hosted, or architecture-specific behavior.

The formalized roles are:

* panel background, raised panel, and recessed/input field relationships;
* border, primary text, secondary text, and selection text;
* active and inactive selection fills;
* button normal, hover, pressed, and text surfaces, while leaving border formulas and semantic bases with each caller.

The representative migration is intentionally small. Calculator and File Explorer button surface state transitions now use the shared button-surface helper at the existing compositor widget boundary, with their existing normal bases and hover percentages preserved. Calculator display and Notepad editor fields use the shared recessed-field role. Calculator, Notepad, and File Explorer primary text use the shared text role. File Explorer address/secondary text, list selection, and its panel relationships use the shared roles. Notepad selection uses the same shared active-selection relationship over its existing editor-owned base.

Classic branches remain local and unchanged. Sci-Fi application-specific roles remain local, including Calculator numeric/operator/equals/clear semantic bases and borders; Notepad menus, caret, context shadow, and editor border; File Explorer toolbar/address/header/footer/menu/dialog formulas, icons, local scrollbar, and application overlays; and all Start-menu, shell context-menu, notification, and shutdown-dialog formulas. No separate selected-text renderer exists on the migrated bare-metal surfaces, so `selectionText` is descriptive and does not alter selection behavior.

### Equivalence, Geometry, and Behavior

The migrated Sci-Fi expressions are formula-equivalent to their pre-Phase-8C code: the shared recessed field remains `Blend(windowBackground, taskbarBackground, 8)`, primary text remains `theme.titleBarText`, secondary text remains the existing 34-percent blend, active/inactive File Explorer selection remains the existing 48/42-percent blend, Notepad selection remains its existing editor-base 48-percent blend, and Calculator/File Explorer button surface state uses the existing 16/18-percent hover and 24-percent pressed blends. Existing Phase 8A Calculator/Notepad and Phase 8B File Explorer Classic/Sci-Fi captures remain the visual baseline; this pass did not add a new look or geometry.

No window or control dimensions, spacing, hit regions, text positions, focus logic, input routing, selection behavior, scrolling, callbacks, filesystem behavior, arithmetic, caret behavior, or close/reopen behavior changed. The helper layer is freestanding-safe, deterministic, integer-only, allocation-free, and contains no dynamic framework machinery or hosted dependency.

### Phase 8C Validation Record

The exact workspace passed `.\build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `.\build.ps1 -Arch amd64`. `smoke-theme-system.ps1 -SkipBuild`, `smoke-live-directory-desktop-status.ps1 -SkipBuild`, `smoke-hosted-display-runtime.ps1`, `smoke-bootinfo-framebuffer-array.ps1`, and `smoke-virtio-gpu-input-routing.ps1` passed. The standalone `tests/file_explorer_navigation_test.cpp` and `tests/window_ui_regression_test.cpp` passed. `smoke-desktop-startup-sync.ps1` reproduced the known harness limitation: QEMU reached the UEFI shell because the workspace ESP has no tracked `startup.nsh`; no startup source or runtime configuration was changed. Build-generated PacMan/ESP/display-options validation artifacts were restored afterward, and logs/screenshots were kept out of the change.

Fresh pixel-targeted QEMU interaction proof was not completed in this consolidation pass. The proof is therefore source/formula-equivalence plus the prior Phase 8A/8B Classic and Sci-Fi captures, rather than a new post-consolidation screenshot set. This is a proof limitation, not a reported visual regression.

### Remaining Control Gaps

No general bare-metal button renderer, generic list/textbox/dialog framework, generic scrollbar, shared shell-popup renderer, icon/font role system, or larger application interior abstraction was added. These remain separate future work; the scoped settings-surface treatment is documented below as Phase 8D.

## Phase 8D — Bare-Metal Control Panel and Display Options Surface Consistency

Phase 8D applies the proven bare-metal Sci-Fi color relationships to the existing system/settings surfaces: the freestanding Control Panel and the freestanding Display Options application. This is a paint-only parity phase. It does not redesign settings, add a settings framework, change theme persistence, or change display behavior. Device Manager, Network settings, Disk Manager, generic controls, icons, and fonts remain outside this phase.

### Audit and Architecture

Bare-metal Control Panel is a desktop-owned modal surface in `kernel/core/desktop.cpp`. `draw_control_panel()` owns the client paint path for the fixed window, title bar, close button, content panel, three existing category/item cards, hover treatment, labels, and unchanged bitmap-like item icon fills. `hit_test_control_panel()` and the desktop mouse router continue to own the close and item hit bands and launch behavior. It has no separate control-panel application object. The hosted `control_panel.cpp` client is a different IPC renderer and was not changed.

Bare-metal Display Options is `DisplayOptionsApp` in `kernel/core/kernel_apps.cpp`. Its `draw()` path owns the tab/section strips, gallery cards, local scrollbar, desktop-icon checkboxes, status text, and the existing select button widget. `drawDisplayTab()` owns the monitor/display preview information, mode and primary-monitor choices, resolution choices, pending-topology warning, status, and Apply/Cancel/Review/Refresh/Keep paint. The app's existing mouse/key handlers own tab, gallery, checkbox, scrollbar, button, and display-option hit testing. The existing display configuration service owns monitor enumeration, mode/primary selection, Apply/Cancel transaction flow, persistence, rollback, and topology reconciliation. The `DisplayOptionsApp` path has no bare-metal Classic/Sci-Fi theme selector; the existing theme selector remains in the separate hosted/root `display_options.cpp` path and was not changed.

No geometry or input refactor was necessary. Local tabs, checkboxes, display choices, warning panels, and the gallery scrollbar remain application-owned. No generic tab, checkbox, radio, combo, list, or scrollbar renderer was introduced.

### Sci-Fi Treatment and Phase 8C Reuse

Control Panel now uses `GetBareMetalControlTheme()` for its Sci-Fi panel, recessed content, border, primary-text, and inactive-selection roles. The existing icon fills and inner icon marks remain unchanged. The close button keeps its existing red semantic treatment, with only its Sci-Fi surface and border blended against the shared roles. Hover stays visual-only and uses the shared inactive-selection relationship.

Display Options now uses local semantic helpers backed by `desktop_control_theme.h`: body and panel surfaces, active/inactive section strips, borders, primary/secondary/muted text, selected gallery borders, cards, scrollbar track/thumb, warning/status text, and local button state. Existing application-specific formulas remain local where they carry meaning, including wallpaper/gradient preview pixels, gradient accent borders, warning semantics, display-option button geometry, and the local checkbox paint path. The existing `m_selectButtonId` widget continues through the compositor's normal/hover/pressed path; a narrow `DisplayOptions` owner guard applies the Phase 8C button roles only for Sci-Fi. Classic branches retain their prior literals, including the original Control Panel and Display Options surfaces.

The reused Phase 8C roles are panel/raised/recessed surfaces, primary/secondary text, active/inactive selection, and button normal/hover/pressed/text transitions. No new abstraction was added for the remaining local controls because their ownership and semantics are application-specific.

### Geometry, Behavior, and Persistence Boundaries

The Control Panel window, item/card bounds, icon positions, text positions, close/item hit bands, category ordering, launch targets, focus, and lifecycle are unchanged. Display Options tab bounds, gallery/card bounds, scrollbar bounds, checkbox rows, selector/button bounds, monitor preview geometry, window dimensions, hit regions, keyboard traversal, callbacks, and lifecycle are unchanged. The display configuration service and theme configuration paths were not modified. `DesktopThemeId` tokens, Classic default/fallback, `display-options.cfg`, `desktop.json` compatibility, Apply/Cancel behavior, and reload/restart behavior remain owned by the existing hosted theme/configuration implementation.

### Phase 8D Validation Record

The exact workspace passed `.\build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, and `.\build.ps1 -Arch amd64`. `smoke-theme-system.ps1 -SkipBuild`, `smoke-live-directory-desktop-status.ps1 -SkipBuild`, `smoke-bootinfo-framebuffer-array.ps1`, and `smoke-virtio-gpu-input-routing.ps1` passed. `smoke-hosted-display-runtime.ps1` passed its normal single-output/no-gates checks and emitted synthetic-mode evidence, but its dual-window continuation did not complete cleanly in the harness and left a validation-only server process that was stopped. The Display Options source/runtime inventory, display configuration control-plane source, display persistence source, synthetic display layout, and virtio-GPU Display Options source checks passed. `git diff --check` was run after validation-state restoration.

The desktop-startup-sync and bounded QEMU display probes were attempted against the exact generated ESP. They reached the UEFI interactive shell because this workspace ESP has no `startup.nsh`, so they did not provide a fresh post-8D framebuffer screenshot or interactive Control Panel/Display Options session. The bounded display persistence/control harness also remains limited by the repository's child Windows PowerShell `Get-FileHash` failure during the PacMan build path. These are proof limitations, not styling regressions; no boot/configuration workaround was committed. Prior Phase 8B Classic/Sci-Fi QEMU captures remain the visual baseline for the surrounding bare-metal shell and application chrome.

Theme persistence was source-proven through the passing theme-system smoke: Classic and Sci-Fi tokens, default/fallback normalization, hosted selector wiring, save/reload paths, and compositor synchronization remain present. This phase did not touch those paths. Because the freestanding Display Options client has no theme selector, no bare-metal selector interaction was available to exercise; its existing display Apply/Cancel and monitor configuration paths remain unchanged and are covered by the display source/control checks. Generated PacMan/ESP/display validation state was restored after the checks.

Remaining settings/application gaps are bounded to Device Manager, Network settings, Disk Manager, other system applications, generic control parity, and icon/font integration. No Phase 8E work is included.

## Phase 9A — Feature-Complete Assessment

Phase 9A concluded that the Sci-Fi theme is feature complete for the v0.3 scope and should proceed to release validation. Estimated coverage was approximately 90 percent for core user-facing theme surfaces and approximately 65 percent for universal theme coverage. No Sci-Fi-theme release blockers were identified in that assessment.

The current architecture remains intentionally narrow: `DesktopThemeId` and the existing `desktop.theme.id` configuration path select `classic` or `scifi`; theme roles provide palette relationships; hosted compositor and window-rendering paths consume those roles; and the bare-metal desktop, kernel compositor, and selected applications consume the same configuration after VFS load. Theme selection, persistence, Classic fallback, and display configuration remain existing product paths rather than new release infrastructure.

Hosted coverage includes the desktop, taskbar, Start surface, window chrome, shell popups, notifications, Control Panel, Display Options, Notepad, Calculator, File Explorer, and the documented chrome-only or bounded treatment of other applications. Bare-metal coverage includes the desktop, taskbar, Start surface, window chrome, shell popups, notifications, Notepad, Calculator, File Explorer, Control Panel, and Display Options surfaces where their existing paint paths support the scoped treatment. Specialized interiors and generic controls are not universalized.

## Phase 9B — Release Validation and Closeout

Phase 9B adds no new Sci-Fi visual features. It establishes a reproducible release-validation path, validates the supported Classic and Sci-Fi configuration behavior, records hosted and bare-metal workflow evidence, repairs two small validation-harness defects, closes the documentation, and assesses—but does not perform—any v0.2 promotion.

### Canonical AMD64 boot path

The canonical clean-workspace path is:

1. `.\build.ps1 -Arch amd64`
2. `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-desktop-startup-sync.ps1 -TimeoutSeconds 90`

`build.ps1` builds the PacMan package, wallpaper/runtime ramdisk, UEFI bootloader, AMD64 kernel, and the `ESP` payload. The startup-sync harness copies that exact generated ESP into an ignored per-run staging directory, writes a temporary `startup.nsh` containing `FS0:\EFI\BOOT\BOOTX64.EFI`, launches QEMU with the repository OVMF/QEMU prerequisites, and removes the staging directory in `finally`. The real workspace ESP is not modified by the handoff. The UEFI shell briefly appears before the intentional handoff runs; that is an expected part of this harness path, not a product boot failure.

`run-qemu.bat` and the older i386/BIOS scripts remain useful manual or historical runners, but they are not the authoritative AMD64 release-signoff path. A locally or accidentally present `ESP/startup.nsh` is neither required nor used by the canonical harness.

### Harness repair and disposition

The recurring PacMan build failure was a validation-environment dependency on `Get-FileHash` in a child Windows PowerShell process. The current machine resolves that cmdlet in both PowerShell 7 and Windows PowerShell 5.1, but the dependency was unnecessary and made the harness fragile. `scripts/build-pacman-package.ps1` now computes SHA-256 through the .NET cryptography API, removing the nested PowerShell module/cmdlet assumption while preserving the same hash values.

The theme smoke's Phase 8A/8B documentation probes were also made ASCII-tolerant. Windows PowerShell 5.1 can decode a UTF-8 documentation file without a BOM through the active code page, changing an em dash while preserving the heading text. This was a test-script encoding defect, not a product defect. No known trivially reproducible release-gate failure remains in the focused suite.

The hosted startup/app-model regression harness had a second Windows PowerShell 5.1 issue: piping multiple commands through a copied native runtime and batch wrapper could turn the explicit regression command into the server help fallback. It now feeds an ASCII command file through `cmd.exe` from the isolated controlled-runtime directory, so registration, launch ownership, and persistence checks use the intended runtime state.

### Authoritative validation suite

Run these commands in order from the repository root:

```powershell
.\build.bat
mingw32-make -C kernel ARCH=amd64 -j2
.\build.ps1 -Arch amd64
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-theme-system.ps1 -SkipBuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-startup-appmodel-regression.ps1 -SkipBuild
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-desktop-startup-sync.ps1 -TimeoutSeconds 90
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-live-directory-desktop-status.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-hosted-display-runtime.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-bootinfo-framebuffer-array.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-virtio-gpu-input-routing.ps1
git diff --check
git status --short --branch
```

The suite is intentionally smaller than the complete historical phase inventory. It covers theme architecture, hosted rendering/runtime, bare-metal boot and framebuffer, input and application launch, persistence paths, representative applications, and repository hygiene without deleting the useful historical smoke scripts.

### Classic/default proof

Classic remains the default. The source and theme smoke prove that missing configuration, invalid configuration, and the persisted `classic` token normalize and load as Classic. Hosted configuration and reload paths preserve the token, while the bare-metal startup serial proof reports `desktop theme=classic source=primary` after VFS load. The Sci-Fi branches are opt-in and do not make the default theme mandatory or alter Classic geometry, hit testing, or application behavior.

### Sci-Fi opt-in and persistence proof

The supported selector/configuration path accepts `scifi`, persists the token, and reloads it through the existing hosted configuration path. The bare-metal path consumes the selected theme after VFS/configuration load. Existing Classic/Sci-Fi QEMU evidence and source/runtime checks confirm the desktop, taskbar, Start surface, window chrome, File Explorer, Calculator, Notepad, Control Panel, and Display Options treatments. Switching the persisted token back to `classic` remains supported and is covered by the same normalization and reload checks.

## v0.3 Sci-Fi Release Scope

Sci-Fi is opt-in; Classic remains default. v0.3 themes the core shell and core applications. Some specialized utilities retain local/Classic interiors. Icons, fonts, and wallpapers remain independent. Universal control theming is not required for this release.

## Hosted Workflow Checklist

### A — Boot/session shell equivalent

Hosted source/runtime checks cover desktop startup, taskbar, Start, and window chrome. The hosted build and startup/app-model regression path passed; direct command-driven launch was used where the compositor capture path was available.

### B — File management

Hosted File Explorer registration, launch, selection/navigation ownership, and close/reopen paths remain covered by the source/runtime checks and the existing application smoke coverage. The Sci-Fi client and chrome roles are wired without geometry or hit-region changes.

### C — Text work

Notepad launch and its existing input/caret/selection ownership remain covered by the application model and theme source checks. Its themed surface is bounded to the existing paint path.

### D — Calculator

Calculator launch and the existing input/evaluation path remain covered by the application model checks. The representative themed surface and widget guard preserve the existing button geometry and behavior.

### E — Settings

Control Panel and Display Options are covered by the Phase 8D source/runtime inventory and the theme smoke. Their existing safe interaction and display-configuration ownership remain unchanged.

### F — System feedback

Notification, context-menu, and shutdown/system-confirmation paint paths remain source-covered and retain their existing non-destructive cancel/close behavior. Direct screenshot capture of retained compositor surfaces is a bounded harness limitation described below.

## Bare-Metal Workflow Checklist

### A — Boot and launch

The canonical startup-sync run passed on the rebuilt AMD64 ESP. Serial evidence confirms UEFI handoff, GOP/framebuffer setup, kernel entry, VFS mount, Classic theme load, desktop startup scan, and a usable desktop startup path. Existing QMP/VGA evidence covers the Sci-Fi desktop/taskbar/Start/window-chrome launch state.

### B — File management

The retained Classic and Sci-Fi QMP/VGA evidence covers File Explorer launch, root list population, selection, navigation into `/Apps`, `Up` navigation, close, and reopen. No destructive filesystem operation was attempted.

### C — Text work

Notepad is registered and its bare-metal themed surface/input ownership is source-covered. Existing QEMU evidence covers launch; direct typing/caret capture remains bounded by the retained-surface capture limitation.

### D — Calculator

Calculator is registered and its bare-metal launch, widget, and input/evaluation paths are source-covered. The existing QEMU/application evidence covers launch; a fresh pixel capture of `2 + 2 = 4` was not required to validate the unchanged arithmetic path.

### E — Settings

Control Panel and Display Options launch paths and paint ownership are source-covered, with Phase 8D preserving safe interaction and display Apply/Cancel behavior. A fresh interactive screenshot was not obtained in the startup-sync run because that run is intentionally bounded to boot/startup synchronization.

### F — System feedback

Context menu, notification, and shutdown confirmation paths are source-covered and preserve existing hit testing and cancel behavior. The QEMU evidence set provides the surrounding shell state; retained-surface capture limits direct popup screenshots.

## Visual Evidence Set

The authoritative local evidence is intentionally small and remains ignored/generated rather than committed. The fresh startup serial proof is `logs/desktop-startup-sync-20260824-104538.serial.log`. The retained Classic baseline includes `logs/phase8b-qemu-classic-apps.png` and `logs/phase8b-qemu-classic-file-explorer.png`. The retained Sci-Fi set includes `logs/phase8b-qemu-scifi-apps.png`, `logs/phase8b-qemu-scifi-file-explorer.png`, and `logs/phase8b-qemu-scifi-up.png`, with corresponding serial/QMP logs in `logs/`. These images prove the shell, File Explorer, and representative application chrome; direct retained-surface popup capture remains a known proof limitation.

The focused QEMU display-control proof passed. The separate QEMU display-persistence proof reached live two-output presentation and valid input mapping but did not emit its second automatic-restore marker before timeout; this remains a bounded display-harness proof limitation, not a Sci-Fi product defect. Source-level persistence validation passed.

## v0.2 Promotion Assessment

No merge, rebase, cherry-pick, or branch switch was performed. Against local `origin/v0.2`, this branch is 23 commits ahead and 40 commits behind, with 22 changed files across the two branch tips. The overlap includes `compositor.cpp/.h`, `kernel/core/desktop.cpp`, `kernel/core/kernel_compositor.cpp`, `kernel/core/kernel_apps.cpp`, theme/configuration files, dialogs, `ESP/desktop.cfg`, build identity, and validation scripts.

The divergence is material rather than a small release-portable patch. The overlapping compositor, desktop, and kernel-app files touch the same areas as v0.2's already-proven Navigator, network, display, HTTPS, and packaging work. A promotion would require conflict resolution plus a full hosted and bare-metal regression pass, including Navigator, network, display, packaging, and the six theme workflows. That validation burden is substantial and would create a real risk of destabilizing v0.2 or delaying its release.

**KEEP FOR v0.3 — protect v0.2.** The Sci-Fi theme itself is release-ready for its v0.3 scope, but that is not sufficient evidence for a late v0.2 promotion. No bounded integration trial is started in Phase 9B.

## Release-Blocker Recheck

**No Sci-Fi-theme release blockers remain.** Phase 9B found no product blocker. The repaired hash and startup-sync issues were harness defects; retained-compositor screenshots, incomplete direct popup capture, and the bounded application-interior evidence are proof limitations; compiler warnings and deferred universal coverage are polish or scope boundaries.

## Deferred / Future Work Queue

The following remain deliberately frozen unless a separate release reason authorizes them: Navigator visual work beyond the bounded Phase 10B interior pass; Task Manager; Clock; Disk Manager; Device Manager; Network utilities; remaining application-specific control migration; icons; fonts; wallpaper coupling; glass/blur; animations; rounded hit testing; and pixel-identical hosted/bare-metal rendering.

## Phase 10A - Universal Sci-Fi Application Controls

Phase 10A establishes a reusable color/state foundation for common application controls. It extends the existing `DesktopTheme`-derived role path; it does not introduce a second theme-selection object, replace the compositor, or redesign application layouts. Classic remains the default and compatible with the pre-Phase-10A control appearance, while Sci-Fi remains opt-in through the existing selector and persistence path.

### Audit findings and ownership

Theme selection and persistence remain owned by `desktop_theme.cpp`, `desktop_config.cpp`, and the existing Display Options/configuration path. `DesktopTheme` remains the source of palette, chrome, and metric values. Hosted window/client painting and generic compositor widgets remain in `compositor.cpp`/`compositor.h`; bare-metal widget rendering and input state remain in `kernel/core/kernel_compositor.cpp` and `kernel/core/include/kernel/kernel_app.h`. Application-local paint helpers remain in the application sources.

The audit found a shared button widget path in both compositor implementations, but not a complete cross-application control framework. Bare-metal `TextBox` and `CheckBox` widgets are shared primitives. File Explorer, Display Options, and other applications still own their list rows, tabs, and scrollbar geometry. The bare-metal `ListBox` enum exists, but it does not yet have a shared renderer or add/list-state API. Hosted widget protocol state currently exposes hover and pressed for generic buttons, but not a separate enabled or keyboard-focus field.

### Shared roles and APIs

`desktop_control_theme.h` now exposes `DesktopControlTheme`, a token view derived from the selected `DesktopTheme` by `GetDesktopControlTheme`. Existing `BareMetalControlTheme` spelling remains an alias for compatibility. The role set is intentionally limited to real consumers:

* control background, hover, pressed, disabled, borders, focus border, and primary/disabled text;
* input background, border, focus border, text, and disabled text;
* active/inactive selection colors;
* panel/secondary text, separator, and scrollbar track/thumb/hover/pressed/disabled colors.

`DesktopControlState` plus `DesktopControlFillColor`, `DesktopControlBorderColor`, and `DesktopControlTextColor` provide the common state lookup. `DesktopSelectionColor` provides active/inactive selection lookup. The helpers centralize color relationships while leaving geometry, event ownership, focus semantics, and Classic fallbacks with the existing callers.

### Controls and proof consumers

Generic hosted compositor buttons and bare-metal buttons now consume shared Sci-Fi state roles for normal, hover, pressed, focused, and disabled rendering where the owning path exposes that state. Bare-metal text boxes and checkboxes consume shared input/control roles. The hosted File Explorer path and Display Options route existing selection, scrollbar, button, and panel surfaces through the shared roles. Notepad routes its existing editor, caret, selection, menu, and separator surfaces through the corresponding input/control roles. Hosted context menus reuse the shared control, separator, hover, border, and text roles. No control geometry or input behavior changed.

The bounded proof surfaces are File Explorer, Notepad, Display Options, Calculator's generic widget path, hosted context menus, and the bare-metal shared widget compositor. These exercise buttons, text/edit fields, selection, list-area surfaces, separators, menus, and scrolling without deep application-specific redesign. Task Manager and future utilities can consume the same roles as their remaining local controls are migrated.

### Classic and Sci-Fi behavior

Classic branches retain their established literals and behavior; the shared role derivation preserves the prior bare-metal role relationships and does not alter theme IDs, persistence keys, metrics, hit regions, or input ownership. Missing, invalid, and explicit Classic configuration continue to resolve to Classic. Sci-Fi uses dark technical surfaces, cool accents, restrained borders, readable text, and distinct hover/pressed/focus/disabled states without glow-heavy effects or animation.

### Validation results

The focused `tests/desktop_control_theme_test.cpp` probe passed for Classic/Sci-Fi lookup, fallback parsing, role derivation, and normal/hover/pressed/focused/disabled state selection. `build.bat`, the AMD64 kernel build, `build.ps1 -Arch amd64`, the theme-system smoke, startup/app-model regression, live-directory status, hosted display-runtime, BootInfo framebuffer, VirtIO GPU/input source, and build-wrapper checks all passed. The canonical startup-sync passed with the release ESP's persisted Classic configuration and with `-DesktopThemeId scifi` and `-DesktopThemeId classic` overrides applied only to disposable staged ESP copies; the Sci-Fi run emitted `loaded desktop theme=scifi`. Fresh final serial artifacts include `logs/desktop-startup-sync-20260828-173653.serial.log` for the persisted Classic boot and `logs/desktop-startup-sync-20260828-173743.serial.log` for the Sci-Fi override. Bare-metal/QEMU proof is bounded to boot, shell, theme load, shared rendering source coverage, input ownership, selection, and scrolling paths; the startup-sync run intentionally does not launch application windows, so fresh per-state application screenshots remain a limitation.

### Known limitations and deferred work

This phase does not create a universal tab/list framework, add a hosted enabled/focus protocol, redesign application-local list or scrollbar geometry, migrate every existing hard-coded application color, or restyle Navigator, Task Manager, Device Manager, Disk Manager, Network utilities, or Developer Studio interiors. A future phase can add shared state plumbing only where an actual consumer needs it, then migrate those remaining application-specific controls incrementally.

## Phase 10B - Navigator Interior Sci-Fi Theming

Phase 10B applies the Phase 10A control-role foundation to the Navigator version present on this branch. It is a bounded visual integration pass: browser behavior, networking, HTML/CSS/JavaScript behavior, navigation, input routing, and lifecycle remain owned by their existing implementations. No Navigator feature branch functionality was merged.

### Starting architecture and ownership audit

The hosted Navigator application in `navigator.cpp` builds and publishes its toolbar, address field, document draw commands, scroll affordance, status/find surface, and existing internal/error document content through the GUI protocol. The hosted compositor in `compositor.cpp` owns the retained window/client/widget pixels and generic button state. The bare-metal `NavigatorApp` in `kernel/core/kernel_apps.cpp` owns its framebuffer client chrome, document renderer, address caret, scroll affordance, and status strip, while the shared kernel compositor owns its toolbar widgets.

The audit found these real Navigator-visible application surfaces: the client background; toolbar and separator; Back, Forward, Reload, Home, Marks, Add, and Find buttons; address/location field, text, focus border, and caret; the document viewport boundary; Navigator-owned document scroll track/thumb; status/loading text and the existing throbber area; find controls; and browser-owned about/error/blank documents rendered through the existing document path. The current Navigator has no tabs, Navigator-owned context-menu subsystem, download panel, security panel, sidebar, or separate browser popup surface to migrate. Toolbar icons remain application-owned and unchanged.

### Browser chrome versus web content

Phase 10B themes browser chrome owned by Navigator and the compositor only. Document backgrounds, authored CSS colors, form colors, links, layout, images, and HTML/CSS scroll behavior remain document-engine output. In particular, the default document surface remains the existing light user-agent color in both themes when a page does not publish an explicit background; an authored `bodyStyle.backgroundColor` or other page style still wins. Navigator-owned scroll tracks/thumbs are themed where drawn by the application; HTML/CSS scrollbar behavior is not globally overridden.

### Shared roles consumed

Hosted Navigator surface helpers derive Sci-Fi values from `GetDesktopControlTheme`: raised/panel surfaces for client, toolbar, and status areas; separator; input background/border/focus border/text/disabled text; shared button fill/border/text state lookup; scrollbar track/thumb; primary/muted text; active/inactive selection; and restrained accent/highlight roles. The hosted compositor now treats the generic widget enabled state as a shared `DesktopControlState::Disabled` input, so Navigator's Back and Forward buttons publish their real history availability without introducing a Navigator-specific palette.

Bare-metal Navigator uses the same derived roles for client, toolbar, separator, address field/caret, viewport boundary, Navigator-owned scrollbars, and status surface. Back and Forward enabled state is refreshed from the existing history counters. No new `DesktopControlTheme` fields were needed; the only new reusable plumbing is the additive `MT_WidgetSetEnabled`/`packWidgetSetEnabled` protocol message and the generic widget `enabled` state, which are useful to any hosted application that publishes a real disabled control.

### Classic and Sci-Fi behavior

Classic remains the default and keeps the previous Navigator literals, geometry, hit targets, icons, document defaults, navigation logic, and input semantics. Missing, invalid, explicit Classic, and persisted Classic configuration continue to resolve to Classic through the existing theme path. Sci-Fi changes only the application-owned chrome to dark technical surfaces, cool readable accents, crisp separators, clear input focus/selection, and visibly disabled navigation controls. It does not recolor arbitrary web pages, change document layout, or add glow, animation, transparency, or geometry changes.

### Validation and evidence

The focused `tests/desktop_control_theme_test.cpp` probe passed, including Classic/Sci-Fi lookup, fallback parsing, shared role derivation, and normal/hover/pressed/focused/disabled state selection. The extended `scripts/smoke-theme-system.ps1 -SkipBuild` probe passed its Navigator shared-role, document-boundary, enabled-state, generic compositor-routing, and documentation assertions. The standalone `navigator_url_tests.cpp` probe passed all 13 URL normalization cases. `build.bat` completed successfully for the hosted executable.

The bounded hosted launch/navigation proof sent `gui.start`, launched Navigator, navigated to `file:///docs/index.html`, and exited cleanly with `NAVIGATOR_GOTO_RESULT: PASS` and `current_block_count=37`; the trace also showed the shared widget-enabled messages. The existing full `scripts/smoke-navigator-hosted.ps1` run reached the local fixture sequence and then stalled after loading `text-polish.html` without emitting its terminal result marker. It was interrupted after the retained `logs/navigator-hosted-smoke-20260828-205526.log` stopped advancing; the HTTP/HTTPS companion logs are retained alongside it. This is recorded as a hosted smoke harness/runtime limitation, not a confirmed theme regression, because the bounded Navigator launch/navigation path and the focused functional probe passed.

AMD64 validation uses the established `build.ps1 -Arch amd64` and disposable/staged ESP startup path, together with the existing Navigator kernel smoke scenarios where the environment permits. The startup harness proves boot, theme selection, shared compositor/input paths, and Navigator source/runtime integration, but does not provide a fresh per-state Navigator application screenshot. Lack of external Internet connectivity is not treated as a theme failure when local/internal navigation or bounded source evidence proves the chrome path.

The normal AMD64 kernel build and `build.ps1 -Arch amd64` release build passed. Disposable-ESP startup-sync runs passed for persisted Classic, staged Sci-Fi, and staged Classic overrides; the Sci-Fi serial trace recorded `loaded desktop theme=scifi`. Retained serial evidence is `logs/desktop-startup-sync-20260828-210430.serial.log`, `logs/desktop-startup-sync-20260828-210504.serial.log`, and `logs/desktop-startup-sync-20260828-210537.serial.log`. The targeted `scripts/smoke-navigator-kernel.ps1 -ScenarioFilter no_policy` did not reach QEMU: its diagnostic Navigator HTTP/PNG build stopped at the pre-existing TLS foundation symbol mismatch (`kGxosTlsRenegotiationInfoScsv`, `GxosTlsCapabilityContractValidation`, and `tls_set_capability_contract_failure`). No TLS or Navigator functionality was changed to work around that unrelated harness/build defect.

The remaining Phase 10A regression checks passed: startup/app-model, live-directory desktop status, hosted display runtime, BootInfo framebuffer, VirtIO GPU/input routing, and build-wrapper. The workspace was rebuilt with the normal release path after the diagnostic Navigator kernel-smoke attempt, so staged smoke flags are not retained in the final build state.

### Limitations and deferred Navigator visual work

The hosted protocol still has no separate keyboard-focus publication for generic toolbar widgets, so toolbar focus rendering remains limited to the state the compositor already owns; the address field publishes its own focused state. The bare-metal toolbar does not expose a new keyboard-focus protocol in this pass. Loading/progress behavior and the existing throbber are preserved rather than redesigned. Visual proof is bounded by the available hosted/QEMU harness and may not capture every hover/pressed/focus transition automatically.

Deferred work includes icon-system replacement, tabs or new browser panels, context-menu/security/download surfaces that do not yet exist, broader page/document styling, new effects, rounded clipping/hit-testing, animation, and any Navigator functionality or rendering-engine changes. These remain separate from this theme integration.

### Phase 10B closeout

Navigator's primary application-owned chrome now consumes the shared Phase 10A foundation on hosted and bare-metal paths while web content remains a separate document-owned surface. Final outcome, exact commit, regression results, QEMU limitations, and retained evidence are recorded with the Phase 10B closeout report and should be kept aligned with this section.

## Phase 10C - Task Manager Interior Sci-Fi Theming

Phase 10C themes the existing guideXOS Server Task Manager as a bounded application-surface consumer of the Phase 10A/10B shared control foundation. It changes paint roles and real control-state publication only; it does not add Task Manager features or change process-management behavior.

### Closeout metadata

* Starting HEAD: `44301f187a114d214c8522fed85782c2ae8a4641` (`Theme Navigator interior for Sci-Fi mode`).
* Phase 10B was already pushed when this audit began: upstream was `origin/v0.3_SCI_FI_THEME`, with `0 ahead / 0 behind` at the starting checkpoint.
* Phase 10C closeout result: Outcome B — complete with minor limitations. The final commit subject is `Theme Task Manager interior for Sci-Fi mode`; the exact SHA and repository counts are included in the implementation report.

### Starting architecture and ownership audit

The repository contains two real Task Manager implementations. Hosted Task Manager is `gxos::apps::TaskManager` in `task_manager.cpp`/`task_manager.h`; it publishes its client rectangles, text, table rows, selection, status/details, and performance/memory/tombstone content through the GUI IPC path. The hosted compositor owns the outer frame/title bar and generic buttons. Bare-metal Task Manager is `kernel::apps::TaskManagerApp` in `kernel/core/kernel_apps.cpp`; it directly paints its framebuffer client surface, list/table, tabs, status/details, and resource views, while the shared kernel compositor owns its Refresh and End Task buttons.

The audit found no application-owned Task Manager context menu, confirmation dialog, local scrollbar, or separate keyboard-focus model. Hosted tabs are existing generic widgets; bare-metal tabs are direct-drawn controls. Existing process/app enumeration, refresh, selection, scrolling, keyboard/mouse routing, action guards, and lifecycle paths were kept intact. The bare-metal build intentionally has different data coverage and already reports that it does not collect hosted app tombstones; that is a functional product boundary, not a theming defect.

### Application-owned surfaces themed

The real Task Manager surfaces migrated to shared roles are:

* hosted and bare-metal client/body backgrounds and nested panels;
* process/task table headers, list backgrounds, row text, selected row, empty list space, and separators;
* CPU, memory, disk, network/resource values and existing graph/status text;
* hosted tombstone and memory-details panels, including their existing empty/data states;
* existing tab boundaries and bare-metal tab active/inactive surfaces;
* existing footer/status/summary text hierarchy; and
* the existing Refresh, End Task/End Process, Restore, and End Tombstoned buttons through the shared compositor/widget state path.

No new columns, tabs, graphs, scrollbar geometry, context-menu commands, confirmation behavior, or process commands were introduced. Hover and pressed button rendering remain provided by the existing generic compositor state path. There is no Task Manager-owned scrollbar or context menu to migrate in this revision.

### Shared roles and reusable additions

Both implementations derive Sci-Fi colors from `DesktopTheme` through `GetDesktopControlTheme`. Task Manager consumes `panelBackground`, `raisedPanel`, `recessedField`, `border`, `primaryText`, `secondaryText`, `controlText`, `separator`, `selectionActive`, `statusWarning`, `controlHoverBorder`, and `controlBorder`, plus `DesktopSelectionColor` and the existing `DesktopControlState` fill/border/text lookups. The hosted action controls publish the existing `MT_WidgetSetEnabled` state, and bare-metal End Task now reflects its existing selection/action guard through `setWidgetEnabled` without changing the guard itself.

Three reusable roles were added to `DesktopControlTheme`: `tableHeaderBackground`, `tableHeaderText`, and `statusWarning`. They each have a clear current consumer in the Task Manager utility surface and are plausibly useful for later utility tables/status reporting. No Task Manager-specific Sci-Fi palette or second theme-selection path was added. Sci-Fi resource indicators reuse the selected theme accent/control/separator/secondary roles; Classic metric and warning literals remain in their existing branches.

### Classic and Sci-Fi behavior

Classic remains the default, including missing, invalid, explicit, and persisted Classic configuration. Hosted and bare-metal Classic branches retain the prior Task Manager colors and geometry. Sci-Fi is opt-in and gives Task Manager dark workstation/server surfaces, crisp table hierarchy, cool restrained accents, readable primary/secondary values, clear selection, and subtle separators. Client geometry, hit regions, input routing, data text, refresh cadence, and action semantics are unchanged.

### Validation and evidence

The focused control-theme test covers the new table-header roles in addition to Classic/Sci-Fi lookup, fallback parsing, and normal/hover/pressed/focused/disabled state selection. The extended theme-system smoke checks verify hosted and bare-metal Task Manager role consumption, shared indicator roles, action enabled-state plumbing, Sci-Fi tab state, and the shared header tokens.

Hosted validation used the existing Task Manager snapshot smoke and the hosted server/display-runtime path. The snapshot checkpoints cover Task Manager list data, selection, refresh, performance, memory details, and tombstone surfaces, and all of those checkpoints passed. Its standalone launch marker remains affected by the existing PowerShell process/pipe timing issue; a direct `gui.start`, `taskmgr`, `gui.wlist`, `gui.pop`, `quit` sequence returned `Task Manager launched, pid=11`, `Compositor received MT_Create: Task Manager|760|520`, and `TaskManager window created: 1000`. The marker mismatch is therefore classified as a harness limitation rather than a Task Manager behavior change. The hosted compositor owns generic normal/hover/pressed/disabled button pixels, while Task Manager source/runtime evidence covers the app-owned interior.

AMD64 validation uses `build.ps1 -Arch amd64` and the canonical disposable/staged ESP startup-sync path. Classic and Sci-Fi startup-sync runs passed and recorded theme selection in `logs/desktop-startup-sync-20260829-171623.serial.log` and `logs/desktop-startup-sync-20260829-171652.serial.log`. The startup-sync harness does not directly launch Task Manager or retain a fresh per-state Task Manager screenshot. Source, build, hosted snapshot, and bounded runtime evidence therefore provide the strongest available Task Manager proof; lack of a direct QEMU Task Manager launch is a harness limitation, not a product failure.

The complete Phase 10C closeout recorded passing `build.bat`, `mingw32-make -C kernel ARCH=amd64 -j2`, `build.ps1 -Arch amd64`, focused control-theme tests, `smoke-theme-system.ps1 -SkipBuild`, startup/app-model, live-directory, hosted-display, bootinfo, virtio-input, and build-wrapper regressions, plus `git diff --check`. The Task Manager snapshot smoke returned only the launch-marker harness failure described above; its Task Manager data and tombstone assertions passed. No test was weakened.

### Functional boundaries, limitations, and deferred work

Visual theming completed now: shared-role routing for the existing Task Manager interiors, table/list hierarchy, selected rows, status/resource text, tabs where already present, and existing action-button enabled/hover/pressed behavior on hosted and bare-metal paths.

Task Manager functionality that does not yet exist: richer process APIs, process trees, CPU/memory profiler architecture, history graphs beyond the existing views, services/startup/network/disk tools, detailed threads, priority/affinity controls, new kill semantics, and expanded diagnostics. These are deliberately deferred product work, not theme defects. Also deferred are a universal list/tab/scrollbar framework, a separate hosted keyboard-focus protocol, and automatic per-state QEMU application screenshots.

### Phase 10C closeout

* Outcome: Outcome B — Phase 10C complete with minor limitations. The implementation is complete; the remaining limits are the snapshot launch-marker timing defect and the lack of direct Task Manager QEMU launch/screenshot automation.
* Final repository state: one local Phase 10C commit ahead of `origin/v0.3_SCI_FI_THEME`, with a clean worktree after closeout. The exact ending SHA and final ahead/behind counts are included in the implementation report.
* Nothing is pushed by Phase 10C unless explicitly requested.

### Future utility-theming implications

The shared table-header roles and existing control/selection/state lookups now provide a small reusable path for later Device Manager, Disk Manager, or Network utility surfaces. Future work should continue to audit each real application first, add only semantic roles with an immediate consumer, and keep utility feature development separate from theme integration.

## Phase 10D - Device Manager Interior Sci-Fi Theming

Phase 10D themes the existing Device Manager surface without expanding device
enumeration, driver ownership, hardware mutation, or application behavior. The
phase remains a paint-only integration pass over the established shared
`DesktopTheme`/`DesktopControlTheme` architecture.

### Closeout metadata

* Starting HEAD: `c6581438ec24528a4c85a40d04b51e5191b7b967` (`Theme Task Manager interior for Sci-Fi mode`).
* Starting branch: `v0.3_SCI_FI_THEME`; upstream: `origin/v0.3_SCI_FI_THEME`; starting divergence: `0 ahead / 0 behind`; starting worktree and index were clean and there were no untracked files.
* Phase 10C was already present at the starting checkpoint and had already been pushed; the Phase 10D implementation itself was not pushed.
* The final Phase 10D commit SHA, final divergence, and final clean-worktree result are recorded in the implementation report for this phase.

### Architecture and ownership audit

The repository has no hosted Device Manager implementation. Hosted
`control_panel.cpp` exposes a `DeviceManager` card and launch label, but hosted
launch dispatch has no corresponding Device Manager application handler. The
real current surface is the bare-metal embedded modal owned by
`kernel/core/desktop.cpp`: `s_deviceManagerOpen`, the fixed `s_devices` fixture,
`draw_device_manager()`, and `hit_test_device_manager()` are all kernel desktop
state and framebuffer drawing. The compositor only owns the surrounding
desktop/input dispatch; no separate hosted or compositor-owned Device Manager
interior exists in this phase.

The browsing model is a fixed, flat three-column table with eight existing
inventory rows (`Device`, `Status`, and `Details`). Category names are part of
device labels such as `Audio:` and `Network:`; there are no category header
rows, tree indentation, expand/collapse affordances, detail/property pane,
tabs, context menu, scrollbar, hover model, or separate keyboard-focus model.
The existing Network `Config` action continues to open the existing network
configuration surface. No new hardware or device state was introduced.

### Surfaces themed and shared roles reused

Sci-Fi now routes the existing Device Manager modal through
`GetCurrentDesktopTheme()` and `GetBareMetalControlTheme()` for its shadow,
outer panel, border, title bar, title text, content field, table header, list
rows, row separators, selected row, device names, status text/indicators,
vendor/device identifiers, and existing `Config`/close controls. The selected
row uses `DesktopSelectionColor`; table hierarchy uses
`tableHeaderBackground`/`tableHeaderText`; boundaries use `border` and
`separator`; primary and identifier text use `primaryText`/`secondaryText`;
existing `Not found` status semantics use `statusWarning`; and existing
controls use `DesktopControlState` fill/border/text lookup for their Sci-Fi
normal state.

No new reusable role or API was necessary. No Device Manager-specific Sci-Fi
palette, second theme selector, category model, tree model, scrollbar, or
hardware abstraction was added. The existing fixture's status meanings are
preserved: detected rows use the Sci-Fi accent, not-found rows use the shared
warning role, and neutral inventory information uses secondary text.

### Classic and Sci-Fi results

Classic remains the default and retains the prior Device Manager colors,
geometry, hit regions, selection behavior, network Config guard, close path,
and data fixture. Missing, invalid, explicit, and persisted Classic
configuration continue to resolve through the existing fallback path. Sci-Fi
is still opt-in and provides a dark technical surface, clear table hierarchy,
readable device identifiers, restrained accent selection, visible warning
semantics, and a crisp selected row without changing enumeration or input
behavior.

### Validation and evidence

The focused theme-system/source checks cover Device Manager's shared-role
consumer, table-header and selection lookup, warning-role mapping, and the
preserved existing Config action guard. The AMD64 build and the existing
startup-sync path provide bare-metal boot/theme-load evidence. The startup-sync
harness does not directly launch the embedded Device Manager modal or retain a
fresh per-window screenshot, so source coverage, build evidence, and existing
Control Panel-to-Device-Manager input paths are the strongest available proof;
the missing retained screenshot is a harness limitation, not a product
failure. No Device Manager-specific hosted runtime proof is possible because
the hosted implementation is absent at this checkpoint.

The completed validation matrix is:

* `build.bat`, `build.ps1 -Arch amd64`, and `mingw32-make -C kernel ARCH=amd64 -j2`: pass.
* Focused `desktop_control_theme_test`: pass; `smoke-theme-system.ps1 -SkipBuild`: pass; `git diff --check`: pass.
* Canonical startup sync: Classic pass (`logs/desktop-startup-sync-20260906-080039.serial.log`) and Sci-Fi pass (`logs/desktop-startup-sync-20260906-080112.serial.log`); both reported the expected loaded theme token.
* Startup/app-model, live-directory status, hosted-display, BootInfo framebuffer, VirtIO input-routing, and build-wrapper regressions: pass.
* The existing Task Manager snapshot harness passed its snapshot, network-stability, tombstone, and data assertions but still reports its known standalone launch-marker failure; Phase 10D does not touch that harness or Task Manager code.

Functional validation preserves the existing launch from bare-metal Control
Panel, row selection, network Config dispatch, close/Alt-F4 modal lifecycle,
and unchanged fixture data. No device was disabled, reset, detached, rescanned,
or otherwise mutated for testing. Driver, PCI, USB, ACPI, storage, network,
resource, and input ownership remain outside this visual phase.

### Limitations and deferred Device Manager work

This phase does not add a hosted Device Manager, live hardware refresh,
enumeration changes, categories, tree expansion, scrolling, properties, tabs,
context menus, enable/disable actions, driver operations, resource decoding,
hot-plug support, or new dialogs. Those are product/application or hardware
subsystem work and remain deferred. The bounded embedded table is ready for
later Disk Manager and Network utility theming through the same shared header,
selection, status, panel, separator, scrollbar, and control-state roles.

### Phase 10D closeout

* Outcome: Outcome B — the existing Device Manager is themed in Sci-Fi mode through the shared role system, with Classic/default behavior preserved; no new hosted Device Manager or hardware functionality was invented.
* Implementation commit: `Theme Device Manager interior for Sci-Fi mode`; the exact ending SHA and final repository counts are recorded in the final handoff report.
* Final validation is the matrix above. The only non-zero regression result is the pre-existing Task Manager standalone launch-marker limitation; all of its substantive assertions passed.
* Nothing was pushed by Phase 10D. The final worktree was cleaned of generated build and harness artifacts before commit.

## Phase 10E - Disk Manager Interior Sci-Fi Theming

Phase 10E themes the real Disk Manager implementations through the existing
`DesktopTheme`/`DesktopControlTheme` path. It is a visual integration pass only:
disk discovery, MBR/partition inspection, filesystem detection, capacity
reporting, read-only host-image attachment, selection, refresh, and lifecycle
behavior remain outside the theme change.

### Closeout metadata

* Starting HEAD: `cf5978e065dddca4c98dfc5dd6b320dc33a2b812` (`Theme Device Manager interior for Sci-Fi mode`).
* Starting branch: `v0.3_SCI_FI_THEME`; upstream: `origin/v0.3_SCI_FI_THEME`; Phase 10D was already pushed at the starting checkpoint, so starting divergence was `0 ahead / 0 behind`.
* The Phase 10E implementation commit and final repository counts are recorded in the closeout report below and in the final handoff.

### Starting architecture and ownership audit

The repository contains two real Disk Manager surfaces. Hosted Disk Manager is
the IPC-rendered application in `disk_manager.cpp`; it owns the client paint
commands and safe read-only inspection actions, while the hosted compositor
owns the outer window chrome and generic window lifecycle. Bare-metal Disk
Manager is `kernel::apps::DiskManagerApp` in
`kernel/core/kernel_apps.cpp`; it owns its framebuffer client surface and
disk-selection input, while the kernel compositor owns the surrounding window
chrome and the existing Refresh widget.

The hosted presentation model is a bounded operations utility: a left disk
list, a Volumes table, a Mounts table, a graphical partition map, and an
Actions/read-only host-image area. The bare-metal presentation is smaller: a
disk list, selected-disk capacity and MBR partition rows, a partition map, and
Refresh. Neither implementation has a hosted or bare-metal scrollbar, context
menu, tab strip, detail pane, used/free meter, per-partition selection model,
or confirmation dialog owned by Disk Manager. No absent surface was invented.

### Surfaces themed and roles reused

Hosted Sci-Fi routes its client background, left disk list, selected disk row,
secondary disk metadata, Volumes and Mounts table surfaces, table headers,
body cells, separators, status columns, partition-map background/free space,
partition segments, capacity text, Actions panel, status/safety note, host
image text, and application-owned buttons through the shared roles. Buttons
use `DesktopControlState` plus `DesktopControlFillColor`,
`DesktopControlBorderColor`, and `DesktopControlTextColor` for normal, hover,
pressed, and disabled rendering. The existing compositor still owns the outer
window controls.

Bare-metal Sci-Fi routes the Disk Manager body, title stripe, left panel,
selected disk row, alternate row, primary/secondary text, partition-table
header, separator, boot marker, capacity text, and partition-map segments
through `GetBareMetalControlTheme`, `DesktopSelectionColor`, and the current
`DesktopTheme` accent. The existing Refresh widget continues to use the shared
kernel compositor control path.

No new reusable semantic role or API was necessary. Disk Manager reuses
`panelBackground`, `raisedPanel`, `recessedField`, `tableHeaderBackground`,
`tableHeaderText`, `border`, `separator`, `primaryText`, `secondaryText`,
`selectionActive`/`selectionText`, `statusWarning`, and the generic control
state lookups. No Disk Manager-specific Sci-Fi palette or second theme system
was added.

### Classic and Sci-Fi behavior

Classic remains the default and preserves the existing Disk Manager geometry,
data text, row and button values, read-only guards, hit regions, refresh path,
and bare-metal framebuffer palette. Missing, invalid, explicit Classic, and
persisted Classic configuration continue to resolve through the existing
fallback path. Sci-Fi is opt-in and adds dark technical surfaces, crisp list
and table hierarchy, visible selected-disk emphasis, restrained partition
differentiation, readable capacity/status text, and clearly disabled dangerous
actions.

Existing status semantics are unchanged. Valid/healthy and mounted/active text
can use a restrained positive accent in Sci-Fi; invalid or unreadable MBR
states and the existing read-only safety notice use `statusWarning`; neutral
or unmounted text remains secondary. No health, utilization, mount, or boot
information was fabricated.

### Storage safety boundary

Phase 10E does not format disks, create or delete partitions, rewrite MBR/GPT
metadata, change boot flags, mount writable filesystems, detach storage,
overwrite sectors, or mutate ESP contents. Existing Format, Create Partition,
and filesystem-switch controls remain disabled/read-only as before. Validation
uses source checks, builds, startup/runtime evidence, and read-only enumeration
only; no destructive action is needed to prove the theme integration.

### Validation and evidence

The focused theme smoke checks cover hosted shared-role consumption, table
headers, selection, status-warning/positive state routing, disabled read-only
actions, bare-metal role consumption, Classic branches, the documented
architecture boundary, and the absence of invented surfaces. Hosted validation
retains the existing launch/refresh/selection/read-only action paths and
capacity/partition presentation without invoking storage mutation.

AMD64 validation uses the established `build.ps1 -Arch amd64` and disposable
ESP startup-sync path. The canonical startup harness does not directly launch
Disk Manager or retain a fresh per-window screenshot in every state, so source
coverage, hosted build/runtime evidence, kernel build evidence, and startup
theme-selection evidence remain the strongest available proof where a screenshot
is unavailable. A missing screenshot is a harness limitation, not a product
failure.

### Functional boundaries, limitations, and deferred work

Visual theming completed now: the actual hosted list/table/map/action surface
and the actual bare-metal list/partition/map surface consume shared theme roles
while Classic stays compatible and storage behavior remains read-only.

Disk Manager functionality that does not yet exist and remains deferred:
scrollable disk/partition views, per-partition selection, a details pane,
used/free resource meters, richer volume enumeration, mount/unmount workflows,
format/delete/create flows, confirmation dialogs, context menus, tabs, SMART,
GPT/MBR editing, and storage-driver work. These are storage/application
features, not missing theme work. Later Network/System utility phases should
continue to audit their real ownership and reuse the same shared table,
selection, status, panel, separator, scrollbar, and control-state roles.

## Phase 10F - Network Utilities Sci-Fi Interior Theming

Phase 10F themes the meaningful graphical network surfaces that actually exist
in this branch. It is network visual theming, not network feature/driver
development. The separate networking and Navigator branches remain separate;
no driver, DHCP, static-IP, PHY/MDIC, ARP, DNS, TLS, Navigator, or networking
stack work was imported or recreated.

### Starting state and ownership audit

The phase started from `cc1e41dedd92898177de7df56e9488f0d084c3d3` on
`v0.3_SCI_FI_THEME`, with a clean worktree and index. The upstream ref was
`origin/v0.3_SCI_FI_THEME` at the same commit when the audit began, so the
Phase 10E commit was already present upstream by this phase.

The repository has no hosted Network Settings application. Hosted
`control_panel.cpp` contains a `Network Settings` card and launch label, but
there is no corresponding hosted network interior/handler. The real graphical
network surfaces are bare-metal embedded desktop state and framebuffer paint
owned by `kernel/core/desktop.cpp`; the compositor owns only the surrounding
desktop/window lifecycle and generic input routing.

### Network GUI inventory

The audited GUI inventory is bounded to:

* the taskbar `NetworkWidget`, which reports the existing NIC/USB-network link
  state and existing IPv4 configured state;
* the embedded `Network Adapters` modal, with the existing two-row adapter
  fixture, selected/hovered rows, status labels, `Properties`, `Status`, and
  close controls;
* the embedded `TCP/IPv4 Properties` modal, with its existing DHCP/manual
  radio controls, IPv4/subnet/gateway/DNS display fields, OK/Cancel, and close
  paths; and
* the existing Device Manager network inventory rows, which were already
  themed through shared roles in Phase 10D and were not functionally changed
  by this pass.

The adapter list is a fixed existing presentation fixture (`Intel Ethernet
Adapter` / `Connected` and `Realtek PCIe GbE Controller` / `Not found`), not a
new enumeration model. The configuration modal does not currently publish
editable text-field, caret, validation, persistence, DNS-mode, lease, MAC,
RX/TX, or detail-pane state. There are no network tabs, application-owned
scrollbars, context menus, confirmation/error dialogs, or taskbar network
popup/connection center to theme. Kernel networking, shell/CLI diagnostics,
and Navigator HTTP/network code are console or subsystem concerns and remain
out of scope.

### Surfaces themed and shared roles reused

Sci-Fi now routes the taskbar network indicator through the selected
`DesktopTheme` and shared status roles: configured link-up remains a positive
accent, link-up without an IPv4 configuration uses the existing warning
semantic, and link-down remains secondary/inactive. The underlying provider
queries and indicator geometry are unchanged.

The Network Adapters modal now uses `GetBareMetalControlTheme` for its shadow,
panel, title stripe, border, content field, adapter rows, selected row,
hovered row, row separators, adapter/vendor text, status indicator, and
Properties/Status/close controls. Selected rows use `DesktopSelectionColor`;
hover uses the generic control state; connected and not-found fixture states
reuse the existing positive accent and `statusWarning` roles.

The TCP/IPv4 Properties modal now uses shared panel/title/border/text roles,
shared selection/control roles for DHCP/manual choice, shared `inputBackground`,
`inputBorder`, `inputText`, and disabled input roles for the existing address
fields, and generic control-state fill/border/text lookup for OK, Cancel, and
close. No field hit-testing, parsing, validation, persistence, or networking
side effect was added.

No new reusable role or API was necessary. No Network-specific Sci-Fi palette,
second theme system, network status enum, or fake status state was added.
`DesktopTheme`, `DesktopControlTheme`, `GetBareMetalControlTheme`,
`DesktopControlState`, `DesktopControlFillColor`,
`DesktopControlBorderColor`, `DesktopControlTextColor`,
`DesktopSelectionColor`, `roles.separator`, and existing status/input roles
remain the complete theming plumbing.

### Classic and Sci-Fi behavior

Classic remains the default and retains the prior network widget colors,
network-modal colors, geometry, hit regions, fixture strings, DHCP/manual
toggle semantics, Properties guard, OK/Cancel/close behavior, and existing
configuration notification. Missing, invalid, explicit Classic, and
persisted Classic configuration continue to resolve through the existing
Classic fallback path. Sci-Fi is opt-in and adds dark technical surfaces,
clear adapter hierarchy, readable technical identifiers, visible selection and
disabled state, restrained status accents, and crisp form controls.

### Validation and functional boundary

The focused theme smoke checks now cover the Phase 10F documentation,
bare-metal network ownership, taskbar status-role consumption, adapter
selection/separator roles, IPv4 input/control roles, status mappings, Classic
fallback branches, and unchanged adapter/action strings. The existing theme
control test continues to cover Classic/Sci-Fi role mapping and invalid/missing
theme fallback.

Hosted validation consists of the full hosted build and shared theme-system
source/runtime checks. There is no hosted network utility to launch directly;
that absence is an architecture finding, not a missing theme implementation.
Bare-metal validation uses the AMD64 build and disposable-ESP startup-sync
path, with Classic and Sci-Fi theme-load evidence. The current startup harness
does not directly open every embedded modal or retain per-application
screenshots, so source coverage, successful build/runtime startup, and the
existing Control Panel-to-network modal input paths are the strongest bounded
proof. This is a harness/proof limitation, not a network-theme product defect.

The Phase 10F validation run completed with these results:

* `build.bat`: PASS.
* `mingw32-make -C kernel ARCH=amd64 -j2`: PASS; existing warnings only.
* `build.ps1 -Arch amd64`: PASS; disposable/staged ESP completed.
* `smoke-theme-system.ps1 -SkipBuild`: PASS, including the Phase 10F
  source/documentation checks.
* `smoke-startup-appmodel-regression.ps1 -SkipBuild`: PASS.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId classic`: PASS; the serial
  log recorded `desktop theme=classic source=primary`.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId scifi`: PASS; the serial log
  recorded `desktop theme=scifi source=primary`, QEMU NIC/link/IP startup, and
  the expected lack of direct modal-launch markers.
* `smoke-live-directory-desktop-status.ps1`,
  `smoke-hosted-display-runtime.ps1`,
  `smoke-bootinfo-framebuffer-array.ps1`,
  `smoke-virtio-gpu-input-routing.ps1`, and `test-build-wrapper.ps1`: PASS.
* The focused `desktop_control_theme_test.cpp` passed with the repository
  include path supplied as an `-iquote` path; the initial `-I.` attempt hit
  the known MinGW/repository `process.h` include collision and was not a
  product failure.
* `git diff --check`: PASS.

No physical-adapter reset, PHY/MDIC operation, PCI rewrite, detach, or manual
DHCP/configuration operation was performed. The disposable QEMU boot exercised
the existing startup path, which emitted its normal DHCP `DISCOVER`; no
physical hardware or live-network state was changed. Network connectivity was
not required for visual proof. The patch does not change NIC enumeration,
driver/link queries, DHCP/static behavior, IP validation, persistence, DNS,
gateway settings, ARP, RX/TX, network-stack state, adapter selection, input
routing, or application lifecycle.

### Network regression results and deferred work

The repository has no separate graphical network-configuration/UI test suite;
the bounded source checks and existing theme/system, build, startup, and
input regressions are the relevant coverage. Any unrelated physical-network or
driver limitation remains a networking defect/validation limitation rather
than a Sci-Fi theme failure.

Deferred network UI work includes a real hosted Network Settings application,
live adapter enumeration, adapter detail/status panes, MAC/link/lease/DHCP/DNS
presentation, editable IPv4 controls, validation/error dialogs, persistence,
refresh/status actions, scrollable lists, and richer diagnostics. These require
application or networking feature development and belong to their appropriate
branches, not Phase 10F.

### Phase 10F closeout

* Outcome: Outcome B — Phase 10F complete with minor harness limitations.
  The existing bare-metal network GUI surfaces are themed through shared
  roles, Classic remains compatible/default, Sci-Fi remains opt-in, and
  networking behavior is unchanged.
* Implementation commit: `Theme network utilities for Sci-Fi mode`; the
  exact SHA and final repository counts are recorded in the final handoff.
* The only proof limitation is the lack of direct modal-launch/per-application
  screenshot retention in the current startup harness; no network feature was
  added to improve that harness.
* Nothing is pushed by Phase 10F unless explicitly requested.

## Phase 10G - Remaining System Utility Sci-Fi Theming

### Starting state and audit method

Phase 10G began on 2026-09-06 from the Phase 10F closeout:

* Repository: `D:\dev\guideXOSServerV0.3_SCI_FI_THEME`
* Branch: `v0.3_SCI_FI_THEME`
* Starting HEAD: `3d251b6331acc4275123ffdee93ab5c215beb482`
* Baseline commit present in history: `3d251b63 Theme network utilities for Sci-Fi mode`
* Upstream: `origin/v0.3_SCI_FI_THEME`
* Starting worktree: clean, with no staged or untracked files
* Starting divergence measured against the current upstream: `0 ahead / 0 behind`

The historical Phase 10F closeout noted `1 ahead / 0 behind`, but the starting audit for this phase found that the Phase 10F commit is already present at the configured origin and the local branch was equal to it. Nothing was pushed during Phase 10G.

The audit searched the active hosted and AMD64/bare-metal application registrations, source files, compositor paths, existing theme smoke coverage, and prior phase records. Previously completed shell, desktop, taskbar, Start, window chrome, Navigator, Task Manager, Device Manager, Disk Manager, and network utility families were excluded. The audit did not create a replacement utility for an absent candidate.

### Remaining GUI utility inventory

| Candidate | Ownership and rendering | Complexity / reusable surfaces | Classic and Sci-Fi status | Activity and bounded-scope assessment |
| --- | --- | --- | --- | --- |
| Clock | Hosted `Clock`; bare-metal clock app; application-owned interior with compositor-owned chrome | Small readout/face, date/time text, labels, and existing controls | Hosted Clock already has meaningful Phase 3F Sci-Fi coverage; not reopened | Real, but already tracked as themed; revisiting it would be polish rather than the next unthemed utility |
| Control Panel / Display Options | Hosted and bare-metal application surfaces; compositor owns outer chrome and generic widgets | Cards, tabs, labels, settings controls, and status text | Covered by earlier phases | Real and active, but excluded as completed |
| System Information / About / System Properties | No standalone graphical utility implementation was found; only labels, metadata, diagnostics, or system strings | No reusable utility surface exists to theme | No complete app-specific Classic/Sci-Fi paths | Absent as a real v0.3 utility; creating one would violate the visual-only boundary |
| Date/Time, power, service/status, and boot/system-information utilities | No standalone graphical utility was found; related behavior belongs to existing subsystems or serial/diagnostic paths | No bounded application surface exists | No target-specific Classic/Sci-Fi paths | Absent or non-graphical; not valid implementation targets for this phase |
| Native App Debug Viewer | Hosted diagnostic registration; current launch path is application/desktop-service related, with no bare-metal utility surface | Potential text/diagnostic viewer, but no normal usable v0.3 launch path | No complete utility-interior Sci-Fi path | Low-value diagnostic; its existing launch mapping issue is unrelated functionality and was not expanded into this phase |
| Console | Hosted console application; application-owned text/input surface | Real text and input controls, but shell-adjacent and tied to shell behavior | Existing console path is not a remaining system-utility interior target | Active, but shell architecture work is explicitly out of scope |
| HDInstaller | Hosted registration with installation/storage mutation behavior | Installer controls would require a different lifecycle and safety surface | Not an ordinary v0.3 system utility launch target | Unavailable for a safe visual-only phase; excluded |
| Trash / Recycle Bin | Hosted `gxos::apps::Trash` and bare-metal `kernel::apps::TrashApp`; application owns client paint while the compositor owns outer chrome and generic buttons | Medium: list/table header, rows, selection, action buttons, properties overlay, confirmation overlay, empty/status/error states | Existing Classic paths were retained; interior Sci-Fi routing was missing | Active, visible in normal v0.3 desktop use, and fully themeable without functionality work |

### Candidate selection and rationale

Trash is the best bounded target. It is a real registered utility in both ownership paths, it is reachable through the desktop system-object/Start launch model, and it contains enough existing visual structure to exercise the shared panel, table, selection, text, status, modal, and generic control roles. Its filesystem actions already exist and can remain untouched. The other plausible candidates were either already covered (Clock and settings), absent as graphical utilities, shell-adjacent, diagnostic-only, or coupled to unrelated installer functionality.

### Architecture and ownership

The repository continues to use one shared theme architecture:

* `DesktopTheme` selects Classic or opt-in Sci-Fi and remains the source of global theme data.
* `DesktopControlTheme`, `GetDesktopControlTheme`, and `GetBareMetalControlTheme` expose semantic application/control roles.
* `DesktopControlState` and the generic `DesktopControlFillColor`, `DesktopControlBorderColor`, and `DesktopControlTextColor` lookup path continue to own generic button states.
* `DesktopSelectionColor` supplies selected-row treatment.
* The hosted Trash client in `trash.cpp` owns its interior draw messages and existing state overlays; the hosted compositor owns outer window chrome and shared generic widget rendering.
* The bare-metal `TrashApp` in `kernel/core/kernel_apps.cpp` owns framebuffer client painting and state overlays; the kernel compositor owns outer chrome and generic widget state rendering.

No second theme architecture, utility-specific Sci-Fi palette, new configuration path, or new shared theme role was introduced. No new shared theme roles or APIs were required.

### Trash surfaces themed

The existing hosted and bare-metal Trash interiors now route their Sci-Fi presentation through the established shared roles while preserving their Classic branches and geometry:

* body, recessed/raised panels, panel borders, table header background, and crisp separators;
* primary, secondary, header, selected-row, and empty-state text;
* list rows and existing selected-row treatment;
* Restore, Restore All, Delete Perm, Empty, Refresh, row, Properties-close, confirmation, and Cancel buttons through the existing generic control-state path;
* existing Properties and Empty Trash overlays, including their titles, detail text, borders, and warning treatment;
* existing status/error text, with real failure/error messages using the shared warning role;
* the existing empty-list presentation.

Trash has no app-owned tabs, editable fields, radio buttons, checkboxes, or custom scrollbar that required additional plumbing. Existing item ordering, selection, keyboard behavior, scrolling/lifecycle behavior, file restore, permanent delete, purge, refresh, and close behavior were not changed. No decorative section or fake status content was added.

### Shared roles and compatibility result

The implementation reuses `GetDesktopControlTheme` in hosted Trash and `GetBareMetalControlTheme` in bare-metal Trash, including `panelBackground`, `raisedPanel`, `recessedField`, `border`, `tableHeaderBackground`, `tableHeaderText`, `primaryText`, `secondaryText`, `selectionText`, `separator`, and `statusWarning`, plus `DesktopSelectionColor`. Existing generic buttons continue to use `DesktopControlState` and the shared fill/border/text lookups.

Classic remains the default and the existing Classic Trash colors and geometry remain in the Classic branch. Missing, invalid, and unspecified configuration continue to normalize to Classic through the existing theme configuration path. Explicit and persisted Sci-Fi select the shared role-backed Sci-Fi surface; switching back to Classic does not require a Trash-specific setting.

No Trash-specific Sci-Fi palette was added. The only color construction beyond direct shared roles is the existing shared blend helper for restrained overlay surfaces, keeping the modal surfaces in the same technical palette.

### Validation and evidence

The bounded source probes in `scripts/smoke-theme-system.ps1` cover the Phase 10G inventory/rationale, hosted shared-role consumption, hosted list/modal/button surfaces, bare-metal shared-role consumption, retained Trash behavior calls, and the no-new-role/no-local-palette boundary. Existing theme fallback and shared-control probes remain authoritative for Classic/default/invalid configuration behavior.

Hosted validation passed `build.bat`, the hosted `guideXOSServer.exe` direct launch path, and `smoke-hosted-display-runtime.ps1`. Direct command probes ran `gui.start`, `desktop.launch Trash`, and `desktop.showconfig` in both states: Classic reported `Theme: Classic (classic)` and `Desktop launch successful: Trash`; Sci-Fi reported `Theme: Sci Fi (scifi)` and the same successful Trash launch. Both runs emitted `Trash window render; item count=0`. The existing target launch path remains registered as `Trash` and retains the existing `DesktopService` launch dispatch and IPC lifecycle. The direct probe exercised launch and initial rendering; source checks cover shared-role consumption and retained controls. Direct per-utility screenshot retention and automated modal/button clicking remain limited by the current harness.

AMD64 validation uses the canonical build and disposable/staged ESP startup path. Classic and Sci-Fi startup/theme paths are attempted without changing the workspace ESP configuration. The startup harness proves desktop construction and shared role wiring, but it does not provide a reliable automated launch-and-screenshot sequence for every Start/system-object utility, so a direct QEMU Trash screenshot is a harness limitation rather than a product defect.

Validation results for this phase:

* `build.bat`: PASS; hosted executable rebuilt successfully.
* `mingw32-make -C kernel ARCH=amd64 -j2`: PASS; existing warnings only.
* `build.ps1 -Arch amd64`: PASS; bootloader, kernel, and staged ESP completed.
* `smoke-theme-system.ps1 -SkipBuild`: PASS, including the Phase 10G Trash/documentation probes.
* `smoke-startup-appmodel-regression.ps1 -SkipBuild`: PASS.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId classic -TimeoutSeconds 90`: PASS; serial evidence recorded `loaded desktop theme=classic source=primary`.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId scifi -TimeoutSeconds 90`: PASS; serial evidence recorded `loaded desktop theme=scifi source=primary`.
* `smoke-live-directory-desktop-status.ps1`: PASS.
* `smoke-hosted-display-runtime.ps1`: PASS across normal, synthetic-only, and dual-window modes.
* `smoke-bootinfo-framebuffer-array.ps1`: PASS.
* `smoke-virtio-gpu-input-routing.ps1 -TimeoutSeconds 120`: PASS; source/input routing checks passed.
* `test-build-wrapper.ps1`: PASS.
* Focused `desktop_control_theme_test.cpp` compiled with the repository `-iquote .` include path and passed Classic/Sci-Fi role and fallback assertions.
* `smoke-file-operations-runtime.ps1 -TrashOnly -TimeoutSeconds 180`: did not reach QEMU because the nested `build-kernel.bat` invocation failed to resolve `Get-FileHash` in its profiled PowerShell process. This is a harness/environment limitation; no Trash runtime result was claimed from that attempt.
* `git diff --check`: PASS after the final source/doc edits.

### Functional regression and defect classification

No Trash filesystem, selection, input, refresh, persistence, lifecycle, or hosted/bare-metal behavior was intentionally changed. Existing file-operation coverage remains the relevant behavioral guard. The audit observed that the hosted `Native App Debug Viewer` label currently maps through the existing Console launch path; this is a pre-existing launch/diagnostic issue, not a Phase 10G theme regression, and it was left untouched. No new defect was discovered in the selected Trash implementation.

### Phase 10G outcome

* Outcome: **Outcome B — Phase 10G complete with minor limitations.** Trash is fully integrated through the shared theme architecture in hosted and bare-metal ownership paths, Classic remains stable/default, Sci-Fi is opt-in, and the relevant build/source/runtime regressions are covered. The remaining limitation is bounded direct QEMU per-utility launch/screenshot retention in the existing harness.
* Implementation commit: `Theme Trash utility for Sci-Fi mode`; the final SHA and divergence are recorded in the final handoff.

### Remaining inventory after Phase 10G and Phase 10H recommendation

No other meaningful, active, unthemed system utility comparable to Trash remains after this phase. The residual inventory is limited to:

* Native App Debug Viewer, a hosted diagnostic/launch-mapping surface with an unrelated pre-existing dispatch issue;
* HDInstaller, an unavailable installer/storage-mutation path rather than a normal v0.3 utility;
* Console, which is active but shell-adjacent and belongs to a later shell-focused scope;
* absent System Information/About/System Properties, Date/Time settings, power settings, service viewer, and boot/system-information GUI utilities, which must not be invented for theme work.

Clock, Control Panel, Display Options, Navigator, Task Manager, Device Manager, Disk Manager, and the network utility surfaces remain covered by their prior phase records. The recommendation for Phase 10H is to **transition into high-visibility Sci-Fi visual polish**, including retained screenshot/focus-state coverage and consistency review, rather than continue utility theming. Further utility work should wait for a genuinely existing graphical utility or a separately authorized feature phase.

## Phase 10H - Sci-Fi Visual Consistency and High-Visibility Polish

### Starting state and prioritized audit

Phase 10H began on 2026-09-06 from the Phase 10G closeout with the explicit goal of making the already-themed system feel like one visual system rather than adding another isolated utility:

* Repository: `D:\dev\guideXOSServerV0.3_SCI_FI_THEME`
* Branch: `v0.3_SCI_FI_THEME`
* Starting HEAD: `02902306901f36678dde15c29e20c63b7a50898d`
* Baseline commit present in history: `02902306 Theme Trash utility for Sci-Fi mode`
* Upstream: `origin/v0.3_SCI_FI_THEME`
* Starting worktree: clean, with no staged or untracked files
* Starting divergence measured against the configured upstream: `0 ahead / 0 behind`

The supplied Phase 10G closeout described a historical `1 ahead / 0 behind` state and said that nothing was pushed. The Phase 10H starting audit found that `02902306` is already present at the configured origin, so the actual starting state for this phase was equal to upstream. Nothing was pushed during Phase 10H.

The audit reviewed the desktop, taskbar, Start menu, hosted window renderer, generic hosted widgets, generic bare-metal framebuffer compositor, File Explorer, Navigator, Task Manager, Device Manager, Disk Manager, Network utilities, Trash, Control Panel/Display Options, Notepad, Calculator, common dialogs/menus, and application launch/shutdown/notification surfaces. It also searched the shared role definitions and major consumers for local Sci-Fi colors.

### Starting audit and prioritized findings

The highest-value defects were concentrated at the shared compositor boundary:

1. The generic bare-metal framebuffer compositor still used fixed gray/blue literals for Sci-Fi window bodies, title bars, borders, close buttons, default text, and Start-menu surfaces.
2. Those literals made bare-metal chrome visibly diverge from the already themed hosted renderer and from the kernel compositor’s shared control roles.
3. Bare-metal Start-menu selection, idle rows, panel borders, and footer buttons did not follow the same hierarchy as hosted Start.
4. Generic hosted widgets have hover, pressed, and enabled/disabled publication, but no safe generic keyboard-focus publication. Adding a new cross-system focus protocol would have exceeded a visual polish pass, so this limitation is documented and existing app-owned focus indicators are retained.
5. A smaller set of hosted shell fallbacks, including taskbar clock/show-desktop and generic popup text, still used local literals. These were safe, recurring consumers of existing primary, secondary, panel, and control roles.

The first three findings were prioritized because they affected the shell/application boundary and hosted/bare-metal parity. The focus limitation was recorded rather than expanded into input plumbing. No one-pixel geometry issue, obscure diagnostic surface, or unrelated application feature was promoted above these recurring defects.

### Implementation boundary

The implementation remains inside the existing architecture. `DesktopTheme`, `DesktopControlTheme`, `GetDesktopControlTheme`, `GetBareMetalControlTheme`, `DesktopControlState`, shared fill/border/text roles, `DesktopSelectionColor`, and existing semantic status roles remain authoritative. No second theme system, new configuration path, new focus protocol, or new application-local Sci-Fi palette was created.

The shared compositor now applies those roles to the affected hosted shell fallbacks and to the generic bare-metal framebuffer path. The bare-metal path uses small local adapter helpers only to preserve its existing rendering API while expressing the same title-bar, body, border, close-button, text, and shadow relationships as hosted chrome. Classic branches retain their pre-Phase-10H literals and geometry. No application lifecycle, input routing, hit testing, filesystem, networking, storage, or process-management behavior changed.

### Focus-state consistency

Existing focus publication was audited across buttons, edit fields, lists/tables, tabs, checkboxes, radio controls, menus, and keyboard-driven application surfaces. Existing app-owned focus indicators and the compositor’s current focused-window treatment remain in use. Shared focused control borders continue to resolve through `DesktopControlState::Focused` where a control publishes that state.

The generic widget model still exposes hover/pressed/enabled state but does not expose a universal keyboard-focus field. This is a documented limitation, not a Phase 10H regression: generic focus publication would require invasive input/state plumbing. The focused-window title-bar highlight and existing application-level focus indicators remain visible and restrained.

### Hover, pressed, disabled, and selected states

The hosted Start menu’s selected rows, hover rows, right-column hover, All Programs, and Shutdown controls now use the shared selection and control-state roles. The bare-metal Start menu uses the same active selection, idle panel, border, and control relationships. Bare-metal close-button hover and pressed flags now affect the Sci-Fi close-button fill without changing hit testing or close behavior.

The existing generic control lookup continues to distinguish normal, hover, pressed, focused, and disabled text/border/fill roles. Focused borders remain distinct from hover borders, pressed fills remain distinct from hover fills, and disabled text remains readable while visibly de-emphasized. Small focused role assertions were added to `tests/desktop_control_theme_test.cpp` to protect these recurring relationships.

### Selection consistency

Selection continues to use `DesktopSelectionColor` and `selectionText` across the previously themed File Explorer, Navigator-owned lists where applicable, Task Manager, Device Manager, Disk Manager, Network lists, and Trash surfaces. Phase 10H extends that same active-selection relationship to hosted and bare-metal Start rows. Idle rows continue to use raised/panel roles, so selection is distinct from hover and from an unselected panel.

No per-application selection palette or new inactive-selection concept was introduced.

### Panel and background hierarchy

Sci-Fi now maintains a deliberate hierarchy of desktop, window client, panel, recessed input/list, table-header, selected-row, menu/dialog, and status/footer surfaces through the existing role values. The generic bare-metal window client uses `windowBackground`; bare-metal Start uses `panelBackground`; idle rows use `raisedPanel`; inputs and lists retain recessed roles; selected rows use the active selection role. The hosted taskbar/Start fallbacks use the corresponding control and panel roles.

The pass did not flatten surfaces into one near-black value and did not add a large set of near-duplicate colors. Classic background values remain untouched.

### Borders, separators, tables, and headers

Bare-metal generic window borders now follow the same focused/inactive relationship as hosted chrome, with a restrained accent blend for the active boundary and a muted blend for inactive windows. The Sci-Fi title-bar highlight is a single existing-style edge treatment, not a new effect system. Start borders, footer button borders, desktop icon frames, and shell separators now use the existing border/control-hover-border/separator roles where they were previously local literals.

Table headers, table-header text, selected rows, separators, and secondary text in Task Manager, Device Manager, Disk Manager, Trash, and the other previously covered list surfaces already consume the shared roles. Phase 10H did not redesign table geometry or introduce another header palette.

### Semantic status colors

Normal/healthy, warning, error, inactive, disabled, and unknown semantics were reviewed in the existing utilities. Existing shared status roles, including `statusWarning`, remain authoritative. No unrelated status was invented and no new status token was added because the recurring inconsistency was resolved by the existing roles and prior utility coverage.

### Icon legibility

The bounded icon pass found no need for a universal icon redesign or asset pipeline. Bare-metal desktop icon frames now use the Sci-Fi control-hover border and icon labels use the shared primary text role, improving bounded contrast on the themed desktop. Hosted taskbar/Start/toolbar/File Explorer/Navigator and utility icons retain their existing assets and surrounding treatment. No application identity or icon set was changed.

Remaining icon work is limited to occasional asset-specific legibility issues that would justify a dedicated future icon pass, not more Phase 10H palette work.

### Wallpaper and background coupling

No new wallpaper subsystem or theme-specific asset coupling was needed. The existing desktop theme background treatment remains theme-aware where already supported, while user wallpaper behavior remains global. A Sci-Fi-only wallpaper asset was not introduced, so Classic does not depend on Sci-Fi resources. Wallpaper coupling remains optional and deferred.

### Typography

No font engine, font asset, or global text metric change was made. Generic hosted and bare-metal fallback text now follows the shared primary/secondary/title roles, including taskbar labels, clock/date, desktop labels, Start text, and generic window text. Existing font roles and geometry remain stable.

### Window chrome and shell/application cohesion

Hosted and bare-metal window interiors now meet the title bar, client background, border, close-button, text, shadow, and active/inactive hierarchy through the same theme relationships. The shared compositor also aligns taskbar labels, clock/date, show-desktop, Start rows, Start footer controls, desktop icon frames, and generic popup/default text with the themed application language.

This gives the desktop, taskbar, Start, window chrome, Navigator, Task Manager, Device Manager, Disk Manager, Network utilities, Trash, settings, Notepad, Calculator, dialogs, menus, and input/list surfaces one consistent Sci-Fi palette without expanding any application’s behavior. The goal is shared design language, not pixel-identical output across different renderers.

### Hosted and bare-metal parity

The principal parity defect was the generic bare-metal framebuffer fallback, which bypassed shared role relationships even though the hosted compositor and kernel application surfaces were already largely themed. Its window body/title/border/close-button/text and Start-menu routes now consume the same `DesktopTheme`/`DesktopControlTheme` relationships as hosted rendering. A small adapter layer preserves renderer-specific APIs and Classic fallbacks.

Parity is semantic and visual in hierarchy; exact pixels remain renderer-dependent because hosted GDI-style drawing and bare-metal framebuffer drawing have different primitives.

### Visual evidence improvement and limitations

The existing deterministic theme smoke now checks the Phase 10H audit record, hosted shared-role consumption, bare-metal chrome and Start role consumption, Classic compatibility boundary, the documented focus limitation, and deferred high-cost effects. The focused shared-control test now checks normal/hover/pressed/focused/disabled distinctions and active selection against an idle panel.

Hosted build/runtime checks and the canonical staged-ESP startup path remain the strongest available product evidence. The current QEMU/startup harness reliably proves theme loading, desktop startup, and serial/runtime markers, but does not reliably launch and retain screenshots for every Start/system-object application or modal interaction. Phase 10H did not add production behavior or an elaborate screenshot-diff framework solely to work around that limitation. Representative source/runtime evidence therefore covers shell, generic chrome, Start, and technical-utility role consumers, while direct per-application QEMU screenshot retention remains a minor harness limitation.

### Classic and Sci-Fi compatibility

Classic remains the default. Missing, invalid, unspecified, and explicit Classic theme configuration continue to normalize to Classic; persisted Classic remains Classic. Sci-Fi remains opt-in through the existing `scifi` token and shared theme lookup. Classic branches in the touched compositor paths retain their prior colors, geometry, and interaction behavior. No geometry changes and no input semantics changes were introduced by this visual pass.

### Validation results

The following checks were run or are the authoritative validation targets for this closeout:

* `build.bat`: PASS; hosted executable rebuilt successfully.
* Focused `desktop_control_theme_test.cpp` with the repository’s `-iquote .` include mode: PASS.
* `smoke-theme-system.ps1 -SkipBuild`: PASS, including the Phase 10H source/documentation probes.
* `mingw32-make -C kernel ARCH=amd64 -j2`: PASS; the bare-metal target was up to date.
* `build.ps1 -Arch amd64`: PASS through the canonical AMD64 release path; the first sandboxed attempt was retried with elevated access because Visual Studio FileTracker denied access.
* `smoke-startup-appmodel-regression.ps1 -SkipBuild`: PASS for application-model coverage.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId classic -TimeoutSeconds 90`: PASS with disposable/staged ESP handling; serial evidence recorded `loaded desktop theme=classic source=primary`.
* `smoke-desktop-startup-sync.ps1 -DesktopThemeId scifi -TimeoutSeconds 90`: PASS with disposable/staged ESP handling; serial evidence recorded `loaded desktop theme=scifi source=primary`.
* `smoke-live-directory-desktop-status.ps1`: PASS.
* `smoke-hosted-display-runtime.ps1`: PASS across normal, synthetic-only, and dual-window modes; its first sandboxed attempt was retried elevated because the harness needed a temporary git index refresh.
* `smoke-bootinfo-framebuffer-array.ps1`: PASS.
* `smoke-virtio-gpu-input-routing.ps1`: PASS.
* `test-build-wrapper.ps1`: PASS.
* `git diff --check`: PASS at closeout.

No functional defect was discovered or fixed. The direct source defect fixed by this phase was visual: generic bare-metal Sci-Fi chrome and Start surfaces leaked pre-theme gray/blue literals and no longer matched the hosted/shared role hierarchy.

### Deferred work and remaining visual gaps

The following remain explicitly deferred: blur, glass/acrylic, transparency architecture, animation, a shadow-engine rewrite, rounded-window hit-testing architecture, universal icon replacement, font-engine replacement, major gradients, and wallpaper-subsystem work. The documented generic keyboard-focus publication gap, occasional asset-specific icon legibility issues, global wallpaper coupling, and bounded direct per-application QEMU screenshot retention are the remaining obvious limitations.

These limitations do not create a mandatory visual blocker for v0.3. The recommendation is **Outcome B — Phase 10H complete with minor limitations**: Sci-Fi is visually coherent enough for v0.3 release after this pass, with the evidence/focus-plumbing limitations recorded. Phase 10I should be optional follow-up work focused on a safe generic focus-state/evidence improvement or a dedicated icon-legibility pass, not another utility-by-utility theming campaign. No additional mandatory polish phase is recommended before v0.3 visual closeout.

Implementation commit subject: `Polish Sci-Fi visual consistency`; the exact SHA and final divergence are recorded in the final handoff. Nothing was pushed during Phase 10H.

## Manual Validation Runbook

* Start the hosted server executable and use `gui.start` to open the compositor.
* Launch Display Options with the registered app name `DisplayOptions`.
* Validate Classic first, then select or persist `desktop.theme.id=scifi` for Sci-Fi.
* Persisted theme tokens are `classic` and `scifi`; restart or reload after changing configuration.
* Capture hosted windows with the strongest available direct path, using source/runtime markers when retained compositor surfaces cannot be captured reliably.
* Keep generated screenshots and logs local unless an artifact is intentionally moved into a documented evidence folder.

## Manual Validation Checklist

### Classic hosted checklist

* Classic is the default on a fresh, missing, or invalid configuration.
* Classic taskbar, Start, window chrome, File Explorer, settings, and application surfaces retain their existing behavior and fallback colors.
* The persisted Classic token reloads after restart.

### Sci Fi hosted checklist

* Sci-Fi is selected only through the supported opt-in path.
* Sci-Fi persists after restart and can be switched back to Classic.
* Desktop, taskbar, Start, window chrome, File Explorer, Notepad, Calculator, Control Panel, and Display Options use the scoped roles without geometry or hit-region changes.
* Notification, context-menu, and shutdown-confirmation paths remain readable and non-destructive.

### Bare-metal checklist

* The rebuilt AMD64 image boots through the canonical disposable-ESP handoff to a framebuffer desktop.
* Classic remains the default after VFS/configuration load.
* Persisted Sci-Fi selects the Sci-Fi desktop/taskbar/Start/window/application palette where the scoped bare-metal paint paths consume it.
* File Explorer, Notepad, Calculator, Control Panel, and Display Options retain existing launch, input, navigation, and safe-close behavior.
* Geometry, hit testing, and the existing rectangular bare-metal surfaces remain frozen.

## Notes

Classic remains the compatibility baseline. The v0.3 Sci-Fi milestone remains a scoped, role-based visual release. Phase 10A adds reusable application-control roles while preserving the existing theme-selection architecture and the evidence/validation boundaries above.

## Recommended Next Pass

Phase 10A is the recommended reusable foundation for the next authorized Sci-Fi pass. Keep future work bounded to application-specific migration or a narrowly justified missing shared control state; preserve the deferred-work freeze for effects, layout redesigns, and unrelated application functionality.

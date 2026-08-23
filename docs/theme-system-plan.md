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

## Manual Validation Runbook

* Start the hosted server executable.
* Use server command `gui.start` to open the hosted compositor.
* Launch Display Options with the registered app name `DisplayOptions`.
* Validate Classic first.
* Capture hosted window screenshots with `PrintWindow` if needed.
* If the Theme-tab click path is brittle, validate Sci Fi with the persisted `desktop.theme.id=scifi` token instead.
* Persisted theme tokens are `classic` and `scifi`.
* Restart or reload the compositor after changing persisted config if needed.
* Screenshot PNGs named `theme-phase2g1-*.png` are local validation artifacts unless intentionally moved into a documented artifact folder.

## Manual Validation Checklist

### Classic hosted checklist

* Classic is the default theme on a fresh config.
* Classic windows remain rectangular.
* Classic taskbar, menu, and search styling remains familiar.
* Classic has no Sci Fi shadow or highlight treatment.
* Theme selection persists after restart.

### Sci Fi hosted checklist

* Sci Fi can be selected from Display Options.
* Sci Fi selection persists after restart.
* Sci Fi windows show roomier titlebar and chrome spacing.
* Rounded hosted chrome preview appears.
* Active and inactive borders are distinct.
* Title button hover and pressed states are visible and aligned.
* Soft shadow appears behind hosted windows.
* Moving, resizing, and overlapping windows does not leave stale shadow or corner pixels.
* Taskbar hover and active states align with click regions.
* Start button paint and click region line up.
* Search box, start menu, and context menu remain readable.
* No-wallpaper desktop uses the dark Sci Fi surface.

### Bare-metal checklist

* Bare-metal still boots.
* Bare-metal remains rectangular for now.
* Bare-metal does not accidentally depend on hosted GDI theme helpers.
* Display Options and the theme config path do not break bare-metal.

## Deferred / Future Work Queue

* Full rounded client clipping
* Rounded hit-testing
* Blur/glass simulation
* Animations and slide-in/out transitions
* High-DPI/scaling/resolution polish
* Bare-metal theme parity
* Taskbar shadows
* Theme wallpaper/asset selection
* Per-theme user-editable settings
* Deeper app surface polish, one app at a time
* Visual screenshot-based smoke tests
* Broad app redesign / multi-app restyling

## Notes

* Classic should continue to look basically unchanged.
* Sci Fi is a first visible proof that the theme system exists, not a full futuristic redesign.
* Rounded-window clipping remains deferred; this foundation is intentionally limited to IDs, persistence, selector wiring, safe chrome colors, the Phase 2A chrome metric pass, the guarded Phase 2B rounded-shell preview, the Phase 2C border/highlight polish pass, the Phase 2D hosted shadow preview, the Phase 2E desktop/taskbar surface polish pass, the Phase 2G validation checkpoint, the Phase 3A Display Options app-surface pilot, the Phase 3B Control Panel app-surface pilot, the Phase 3C Notepad app-surface pilot, the Phase 3C.1 stabilization review pass, the Phase 3D Calculator app-surface pilot, the Phase 3D.1 stabilization review pass, the Phase 3E File Explorer app-surface pilot, the Phase 3E.1 File Explorer stabilization review pass, the Phase 3F Clock app-surface pilot, the Phase 3F.1 Clock stabilization review pass, the Phase 3G app-surface closeout, the Phase 3H hosted visual validation pass, the Phase 4A readiness/defaults boundary checkpoint, the Phase 4B wallpaper/background inventory and design-readiness pass, the Phase 4C wallpaper recommendation note, the Phase 4C.1 stabilization review pass, the Phase 4D Navigator app-surface pilot, the Phase 4D.1 Navigator stabilization review pass, the Phase 4E Task Manager app-surface pilot, the Phase 4E.1 Task Manager stabilization review pass, the Phase 4F extended app-surface closeout, the Phase 4G Image Viewer readiness inventory, the Phase 4G.1 hosted large-image lifecycle safety check, the Phase 4H hosted Image Viewer chrome-only app-surface pilot, and the Phase 5 hosted shell popup consistency pass.

## Recommended Next Pass

1. Restore the missing hosted build prerequisite and complete the Phase 5 Classic/Sci Fi popup visual and interaction checklist.
2. If useful, add a focused hosted screenshot harness for popup surfaces without changing popup behavior.
3. Continue only with separately scoped deferred work such as bare-metal parity, high-DPI/scaling, rounded clipping/hit-testing research, or future app-surface polish.

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

No general bare-metal button renderer, generic list/textbox/dialog framework, generic scrollbar, shared shell-popup renderer, icon/font role system, larger application interior abstraction, or settings/system-application theme pass was added. These remain separate future work. Phase 8D is not started.

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

* Bare-metal still boots to a usable framebuffer desktop and taskbar.
* Classic remains the default and retains its prior desktop/taskbar/Start-button colors.
* Sci Fi persisted configuration selects the Sci Fi desktop/taskbar/Start-button palette after VFS mount.
* The Sci Fi desktop fallback remains readable when wallpaper is unavailable; active wallpaper ownership is unchanged.
* Taskbar normal, hover, active/selected, and indicator paths use the shared palette without geometry drift.
* Classic bare-metal configuration retains the existing Classic Start menu colors and behavior.
* Sci Fi bare-metal configuration applies the shared Sci Fi palette to the existing Start-menu surface, rows, states, text, separators, and session footer while preserving its hit regions and routing.
* Sci Fi bare-metal windows use shared themed active/inactive frames, title bars, title text, and existing title-button states.
* Classic bare-metal window chrome remains on its original fallback colors.
* Bare-metal window geometry and title-button hit regions remain frozen.
* Bare-metal Start-menu geometry, row bounds, footer bounds, and input boundaries remain frozen.
* Bare-metal remains rectangular for now.
* Bare-metal does not depend on hosted GDI theme helpers.
* Display Options and the theme config path do not break bare-metal.

## Deferred / Future Work Queue

* Full rounded client clipping
* Rounded hit-testing
* Blur/glass simulation
* Animations and slide-in/out transitions
* High-DPI/scaling/resolution polish
* Remaining bare-metal parity: dialogs, notifications, and application surfaces
* Taskbar shadows
* Theme wallpaper/asset selection
* Per-theme user-editable settings
* Deeper app surface polish, one app at a time
* Visual screenshot-based smoke tests
* Broad app redesign / multi-app restyling

## Notes

* Classic should continue to look basically unchanged.
* Sci Fi is a first visible proof that the theme system exists, not a full futuristic redesign.
* Rounded-window clipping remains deferred; this foundation is intentionally limited to IDs, persistence, selector wiring, safe chrome colors, the Phase 2A chrome metric pass, the guarded Phase 2B rounded-shell preview, the Phase 2C border/highlight polish pass, the Phase 2D hosted shadow preview, the Phase 2E desktop/taskbar surface polish pass, the Phase 2G validation checkpoint, the Phase 3A Display Options app-surface pilot, the Phase 3B Control Panel app-surface pilot, the Phase 3C Notepad app-surface pilot, the Phase 3C.1 stabilization review pass, the Phase 3D Calculator app-surface pilot, the Phase 3D.1 stabilization review pass, the Phase 3E File Explorer app-surface pilot, the Phase 3E.1 File Explorer stabilization review pass, the Phase 3F Clock app-surface pilot, the Phase 3F.1 Clock stabilization review pass, the Phase 3G app-surface closeout, the Phase 3H hosted visual validation pass, the Phase 4A readiness/defaults boundary checkpoint, the Phase 4B wallpaper/background inventory and design-readiness pass, the Phase 4C wallpaper recommendation note, the Phase 4C.1 stabilization review pass, the Phase 4D Navigator app-surface pilot, the Phase 4D.1 Navigator stabilization review pass, the Phase 4E Task Manager app-surface pilot, the Phase 4E.1 Task Manager stabilization review pass, the Phase 4F extended app-surface closeout, the Phase 4G Image Viewer readiness inventory, the Phase 4G.1 hosted large-image lifecycle safety check, the Phase 4H hosted Image Viewer chrome-only app-surface pilot, the Phase 5 hosted shell popup consistency pass, and the Phase 7A bare-metal desktop/taskbar surface parity pass.

## Recommended Next Pass

1. After this phase is validated, separately scope bare-metal window chrome/title bars and window buttons; do not include Start-menu contents in that pass unless explicitly authorized.
2. Keep bare-metal dialogs, notifications, and application surfaces as later focused phases.
3. Continue to preserve Classic as the default and keep geometry/input behavior frozen for each visual parity pass.

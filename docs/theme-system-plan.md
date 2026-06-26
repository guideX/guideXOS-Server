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
* Rounded-window clipping remains deferred; this foundation is intentionally limited to IDs, persistence, selector wiring, safe chrome colors, the Phase 2A chrome metric pass, the guarded Phase 2B rounded-shell preview, the Phase 2C border/highlight polish pass, the Phase 2D hosted shadow preview, the Phase 2E desktop/taskbar surface polish pass, the Phase 2G validation checkpoint, and the Phase 3A Display Options app-surface pilot.

## Recommended Next Pass

1. Phase 2G.1 manual visual validation fixes, if screenshots or manual testing finds issues.
2. Phase 2H theme screenshot / manual smoke harness, if feasible.
3. Next app-surface pilot, one app at a time, after Control Panel.
4. Future high-DPI and resolution work as a separate track.

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

## What Is Deferred

* Rounded clipping for theme-specific window shapes
* Glass or blur simulation
* Slide-in / slide-out animations
* High-DPI and scaling polish
* More themes
* Deeper app-specific polish

## Notes

* Classic should continue to look basically unchanged.
* Sci Fi is a first visible proof that the theme system exists, not a full futuristic redesign.
* Rounded-window clipping remains deferred; this foundation is intentionally limited to IDs, persistence, selector wiring, safe chrome colors, the Phase 2A chrome metric pass, the guarded Phase 2B rounded-shell preview, the Phase 2C border/highlight polish pass, and the Phase 2D hosted shadow preview.

## Future Work

1. Wire rounded clipping safely for supported render paths.
2. Expand the conservative shadow preview behind clear feature gates, then add glass/blur experiments if the render path proves safe.
3. Refine padding, spacing, and scaling using per-theme metrics.
4. Add more themes once the infrastructure is proven stable.
5. Apply deeper per-app polish after the compositor chrome is settled.

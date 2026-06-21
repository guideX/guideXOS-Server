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

## What Is Deferred

* Rounded clipping for theme-specific window shapes
* Soft shadows
* Glass or blur simulation
* Slide-in / slide-out animations
* High-DPI and scaling polish
* Per-theme metrics
* More themes
* Deeper app-specific polish

## Notes

* Classic should continue to look basically unchanged.
* Sci Fi is a first visible proof that the theme system exists, not a full futuristic redesign.
* Rounded-window clipping remains deferred; this foundation is intentionally limited to IDs, persistence, selector wiring, and safe chrome colors.

## Future Work

1. Wire rounded clipping safely for supported render paths.
2. Add soft shadow and glass/blur experiments behind clear feature gates.
3. Refine padding, spacing, and scaling using per-theme metrics.
4. Add more themes once the infrastructure is proven stable.
5. Apply deeper per-app polish after the compositor chrome is settled.

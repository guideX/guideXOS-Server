# Hosted synthetic dual-monitor layout (experimental v0.2)

The hosted/dev path is gated by `GXOS_SYNTHETIC_DUAL_MONITOR=1`. It is off by
default, so the existing hosted single-display path is unchanged. The gate
creates two enabled logical descriptors:

```text
display-1: 1920x1080 at 0,0 (primary)
display-2: 1920x1080 at 1920,0
virtual desktop: 3840x1080
```

The optional `GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1` flag is separate and is not
needed for the virtual-coordinate test. Without it, the compositor still paints
one monitor-sized hosted window/framebuffer (the primary viewport). Mouse and
window coordinates are translated into virtual desktop space internally; a
point or window on monitor 2 is therefore valid but is not visible until the
viewport is switched with `desktop.display.viewport 2` or a real second output
exists. The diagnostic reports `virtualMouse` and
`mouseVisibleInViewport` to make that limitation explicit.

Wallpaper and taskbar remain conservative: wallpaper is painted in the current
hosted viewport and the taskbar belongs to the primary monitor only. Per-monitor
wallpaper/taskbar rendering is intentionally deferred until monitor-specific
framebuffers are available.

Useful commands:

```text
set GXOS_SYNTHETIC_DUAL_MONITOR=1
guideXOSServer.exe
desktop.display.summary
desktop.display.viewport 2
```

Display Options reflects both descriptors and defaults a newly recognized
synthetic layout to Extend. Mirror remains a model-level choice and is safely
persisted as two overlapping logical descriptors; it does not claim to provide
real mirrored hardware output.

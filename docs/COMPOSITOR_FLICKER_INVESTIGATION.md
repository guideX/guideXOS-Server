# Compositor Flicker Investigation

Hosted guideXOS Server on Windows now composes each hosted desktop frame into a retained offscreen DIB section first and then blits the completed frame once to the visible `WM_PAINT` DC.

Current hosted paint order:

1. `requestRepaint()` calls `InvalidateRect(..., FALSE)` on the compositor window.
2. `WM_PAINT` begins with `BeginPaint()`.
3. The compositor ensures a retained offscreen memory DC and top-down 32-bit DIB for the current client size.
4. The wallpaper/background is drawn first into that offscreen frame:
   - `DesktopWallpaper::DrawGradient(...)`
   - optional `drawBackgroundImageToHdc(...)`
   - otherwise `DesktopWallpaper::DrawBranding(...)`
5. Desktop icons are drawn next via `drawDesktopIcons(...)`.
6. Application windows are drawn in Z order.
7. Taskbar chrome is drawn after windows.
8. Search box, taskbar buttons, system tray, clock, show-desktop sliver, tooltips, notifications, menus, and overlays are drawn afterward.
9. The completed offscreen frame is captured for VNC when needed.
10. The finished frame is `BitBlt()`'d once to the visible paint DC.
11. `EndPaint()` ends the frame.

Why this can flicker:

- The hosted path now uses a single retained offscreen compositor backbuffer for presentation.
- The visible window only sees the completed frame after the final blit.
- The background is especially expensive because the gradient helper paints line by line.
- The old flicker mode occurred when the visible surface was sampled mid-paint, exposing a partially built frame:
  wallpaper first, then icons, then window chrome, then window contents, then taskbar.

Important contrast:

- Bare-metal rendering does build into the `VideoBackend` pixel buffer and calls `present()` once.
- Hosted Windows rendering does not use that same atomic pixel-buffer present path today.

Original suspected root cause:

- Hosted compositor painting was effectively immediate-mode and non-atomic before this change, so the desktop could be observed in intermediate states while the frame was still being assembled.

Implemented fix:

- The hosted path now draws into a retained offscreen DIB section via a memory DC, captures VNC from that completed buffer, and only then blits the finished frame to the visible `WM_PAINT` DC.

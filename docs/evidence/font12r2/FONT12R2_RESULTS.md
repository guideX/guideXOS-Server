# FONT12R2 — final guest interaction and visual restoration

Disposition: Outcome B — conditionally qualified. Fresh-process font readback, Settings focus changes, native pointer control selection, and one supported live resize passed. Long relative-pointer moves remained unreliable, so the Settings minimum-size boundary could not be verified. No font-specific production defect was demonstrated. No commit was made.

## Repository state and accepted results

- Checkout: D:/dev/guideXOSServer_FONT_SUPPORT; branch FONT_SUPPORT; starting and ending HEAD 89a58151c19da3d6532d838ae3a29a2c0ab4711a.
- The exact pre-existing status at task start is in starting-worktree-status.txt. Its three tracked changes were preserved; no kernel, driver, compositor, test, or test-runner source was edited for FONT12R2.
- Previously accepted FONT12R results remain in ../font12r/FONT12R_RESULTS.md: ABI and native filesystem corrections pass; 6,635 focused font checks and requested Settings/native suites pass; forced AMD64 build/link pass; PE import directories are zero. The prior real Apply saved Roboto Bold 18 px at preference sequence 32.
- FONT12R2 added documentation and guest evidence only. No broad tests or build were rerun because production and test sources did not change.

## Fresh-process visual restoration

One fresh QEMU process (PID 30864) booted a private clone of the qualified raw FAT32 PIIX IDE image. The clone was inspected before launch: active slot sequence 32, TrueType, Roboto Bold, 18 px, /system/fonts/Roboto-Bold.ttf, identity 3d90e9fa85436469; the previous slot is sequence 31, Bold 16 px. Startup serial reports [FONT6] system selection init=Ok active-font fallback-available=yes. When the Fonts page opens, its serial trace records the available Roboto assets.

The real Settings Fonts page shows Current: Roboto | Bold 18 px, the Bold row selected, Roboto-Bold.ttf, and the rendered preview. The Settings window, desktop icons, and taskbar are visible in the same QEMU capture, so this is visual readback rather than a preference-file inference. See guest/fresh-process-fonts-restored.png. The active TTF was selected; bitmap fallback was available but not used.

## Pointer path and hit testing

QEMU exposed one current QEMU PS/2 Mouse with absolute:false; no USB tablet was configured. The guest initializes PS/2 IRQ12 in kernel/core/main.cpp, consumes signed relative packets and inverts Y in kernel/core/ps2mouse.cpp, then passes deltas through DisplayInputMapper::mapRelativePointer(Ps2Relative, ...) in kernel/core/input_manager.cpp. Cursor coordinates are clamped to the virtual display by the existing input path. QMP relative events produced valid guest cursor placement and button actions; a VNC absolute-pointer assumption was not used.

Real pointer input brought Settings forward, opened Personalization and Fonts, selected the Italic row, and incremented the pending Bold preview size from 18 to 19. The pending-state captures show the row selection and size change while Current remains Bold 18; no Apply was issued. A second native File Explorer window came forward, then the same Settings instance regained focus. Serial shows Settings focus acquired, focus lost, and focus reacquired without a Settings shutdown between those transitions. See guest/focus-file-explorer-front.png, guest/focus-settings-returned.png, guest/pointer-font-row-selection.png, and guest/pointer-size-control-pending.png.

The compositor assigns IDs at registration; focus and bringToFront do not replace a window ID. Its resize drag state saves hitWin->id on pointer down and looks up that same ID on movement. Numeric window IDs are not emitted by current serial telemetry, so no numeric ID is claimed here; the same Settings instance is identified by its single launch/lifecycle between the focus transitions and by the continuous before/after resize capture. Font selection generation is also not printed. The active Bold 18 selection and preview are unchanged after resize, with no Apply or selection commit during the resize run.

Some UI actions repainted after a delay, so snapshots were taken after settling. A later long move from the observed pointer location toward the resize grip did not reach the requested position. A slower-paced repeat remained at approximately (560,405) instead of (918,623). The evidence is guest/pointer-motion-underrun-at-minimum-check.png, with submitted events in guest/qmp-input-trace.jsonl. Since that bounded check still undershot, no further coordinate guessing was attempted.

## Live resize

Production Settings constraints are 560×360 minimum, 760×500 preferred, and 1180×820 maximum, with WF_RESIZABLE and WF_CLOSABLE in kernel/core/kernel_apps.cpp. The real lower-right resize grip was hit with PS/2 left-button down/move/up on the Settings window. From 760×500, the observed drag delta was +65,+55, producing 825×555. The same Settings titlebar stayed front and focused; its resized Fonts page fits the selection list, size buttons, preview, and action buttons without clipping. Current selection and preview remain Roboto Bold 18 px. See guest/settings-before-live-resize.png, guest/settings-after-live-resize.png, and guest/settings-resized-fonts-visible.png.

The current app has no separate Settings resize callback. The implemented contract is compositor-owned: handleMouseDown captures the live window ID and starting dimensions, handleMouseMove updates width/height with the minimum clamp and marks the same window dirty, and the normal compositor draw pass redraws it. The post-drag screenshot confirms that redraw. The minimum-size clamp itself was not verified because the later pointer move failed to reach the grip; therefore the 560×360 boundary and Fonts layout at that boundary remain open.

## QEMU ownership and configuration

The prelaunch census found three unrelated QEMU processes, with media rooted in other projects. They were not attached to, modified, or terminated. Ports 59350 (QMP) and 5952 (VNC) were free at launch. The one FONT12R2 process used:

- pc,accel=whpx, 1 GiB RAM, two vCPUs, VGA, UTC RTC, no network.
- Private read-only OVMF code copy and private raw FAT32 IDE image clone.
- Serial log, QMP 127.0.0.1:59350, VNC loopback 127.0.0.1:5952.
- Default PC PS/2 relative pointer; no USB tablet.

Exact launch arguments are in guest/qemu-launch.json; ownership is recorded in guest/ownership-census-prelaunch.json; the full guest log and preference snapshot are guest/fresh-process.serial.log and guest/prelaunch-preferences.json.

## Final qualification

Outcome B remains because relative pointer movement to far targets did not stay reliable under either the normal or slower QMP event pacing, leaving the requested 560×360 resize check incomplete. Fresh-process visual restoration, Settings pointer focus and control targeting, and one live resize passed. No font feature expansion or production code change was made. Keep controlled integration conditional and close the minimum-size pointer pass on the integrated branch before desktop signoff.

## Guest shutdown and ending state

After capturing the evidence, QMP system_powerdown was accepted but the guest stayed running for eight seconds. I verified PID 30864 still pointed at the FONT12R2 private image and QMP port, then sent QMP quit to that endpoint only; PID 30864 exited. The post-run census is guest/ownership-census-postrun.json. It observed PID 34980 still running, PID 24196 from another checkout present, and prelaunch PIDs 29328 and 26056 no longer present. No command was sent to any of those external guests. This records observations only and does not attribute why their process set changed.

Private disk and firmware SHA-256 values are in guest/private-asset-hashes.json. The exact ending Git status is in ending-worktree-status.txt; branch and HEAD stayed unchanged, with the pre-existing tracked edits plus the FONT12R2 report/evidence directory and its preserved out/ run artifacts.

The unobscured post-close Fonts capture is guest/settings-resized-fonts-visible.png: the terminal was closed through its titlebar control, Settings remained front, and the taskbar and File Explorer shell surface remain visible. The initial fresh boot capture is also retained separately as guest/fresh-process-fonts-restored.png.

### Source traces

- [PS/2 initialization and IRQ12](../../../kernel/core/main.cpp#L1344-L1351), [relative packet decode, Y inversion and delta bounds](../../../kernel/core/ps2mouse.cpp#L290-L304), and [PS/2-to-display dispatch](../../../kernel/core/input_manager.cpp#L231-L254).
- [Settings preferred/minimum/maximum size and window flags](../../../kernel/core/kernel_apps.cpp#L16547-L16557) and [constraint values](../../../kernel/core/kernel_apps.cpp#L16769-L16779).
- [Settings pointer-button routing](../../../kernel/core/kernel_apps.cpp#L17292-L17303), [font-row/size hit regions](../../../kernel/core/kernel_apps.cpp#L17317-L17343), and [compositor resize-corner hit test](../../../kernel/core/kernel_compositor.cpp#L555-L560).
- [Resize drag ID capture and dimension update/redraw invalidation](../../../kernel/core/kernel_compositor.cpp#L574-L599) and [focus behavior](../../../kernel/core/kernel_compositor.cpp#L335-L360). This implementation has no separate per-app resize callback.


Coordinate mapping detail: PS/2 mouse packets produce relative dx/dy; the mapper computes the next virtual point as the prior cursor plus those deltas, clamps to the union of active monitor rectangles, and stores that point for the next event. The PS/2 driver also clamps to the framebuffer bounds; in this 1280×800 guest that is x=0..1279 and y=0..799. QMP target coordinates therefore describe the intended result of integrating sent relative deltas, not an absolute VNC coordinate. The guest has no per-IRQ coordinate trace in this build, so the observed long-move discrepancy cannot be assigned to a specific packet or layer.

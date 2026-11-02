# FONT12R3 — Minimum-Size Qualification & Final Closeout

**Disposition: Outcome B — conditionally qualified.** No font-specific correctness defect was demonstrated. The supported minimum remains unverified in the live Settings instance because the PS/2 pointer under-delivered before reaching the resize grip, and this checkout has no existing same-instance resize diagnostic that can replace that input path.

## Repository and scope

- Checkout: `D:/dev/guideXOSServer_FONT_SUPPORT`.
- Branch: `FONT_SUPPORT`.
- Starting and ending HEAD: `89a58151c19da3d6532d838ae3a29a2c0ab4711a`.
- The starting worktree contained three tracked edits and existing untracked evidence/output. The full initial snapshot is [starting-worktree-status.txt](starting-worktree-status.txt). All pre-existing files and `out/` artifacts were preserved.
- FONT12R3 changed documentation and added guest evidence only. No production, test, or test-runner source was changed. No build or broad regression suite was rerun.
- No commit or push was made because this is Outcome B. The ending status is [ending-worktree-status.txt](ending-worktree-status.txt).

## Production minimum and resize path

`SettingsWindowConstraints` returns a 560×360 minimum, 760×500 preferred size, and 1180×820 maximum in [kernel_apps.cpp](../../../kernel/core/kernel_apps.cpp#L16769-L16778). Settings copies those values to its registered `KernelWindow` at [kernel_apps.cpp](../../../kernel/core/kernel_apps.cpp#L16544-L16556).

These are **outer window dimensions**. The compositor draws the border and title bar within `KernelWindow::w/h`; it gives the app the same width and height minus the 24-pixel title bar for client drawing. At 560×360 the Settings app draw callback would therefore receive width 560 and client height 336. The one-pixel border is drawn inside the outer rectangle. See [kernel_compositor.cpp](../../../kernel/core/kernel_compositor.cpp#L865-L896).

The pointer resize path captures the registered window ID at pointer down, calculates new dimensions from the original drag geometry, applies `max(window minimum, generic 200×100 minimum)`, updates the same window object, and marks it dirty. The drag handler does not impose the Settings maximum values; display-topology reconciliation separately clamps dimensions to the available work area and caps its effective minimum to that work area. Registration applies the minimum only. The compositor redraws the visible window using the resized dimensions through the normal owner draw callback. See [resize hit testing and drag handling](../../../kernel/core/kernel_compositor.cpp#L555-L600), [topology reconciliation and registration](../../../kernel/core/kernel_compositor.cpp#L195-L255), and [drawWindow](../../../kernel/core/kernel_compositor.cpp#L865-L920). A newly registered window already at the minimum would not prove a live resize.

No native window-size request API is implemented in this checkout. `WindowResize` is declared in the IPC enum but has no consumer. The public compositor mouse handlers are used by the normal pointer route. The QEMU-only input proof creates a separate `QEMU Input Proof` window and cannot target the live Settings instance. The source review is preserved in [source-review.txt](source-review.txt).

## Previous minimum-size evidence

- **FONT8:** [settings-minimum-560x360.png](../font8/settings-minimum-560x360.png) shows a real 560×360 Settings Fonts window with Built-in Bitmap selected. Navigation, font rows, size, preview area, status, Apply, Cancel, and Restore Defaults are visible. The long status line is intentionally clipped to its field. The [FONT8 report](../../fonts/FONT8_NATIVE_DESKTOP_ADOPTION.md) and [guest notes](../font8/guest-acceptance-notes.txt) say the window was resized to 560×360. They do not preserve a before/after identity trace or pointer hit-test at that minimum, so same-instance identity and minimum-size input behavior are not independently established by those records. FONT8 does establish that the bitmap Fonts layout was rendered at the reported minimum without an invalid overall layout.
- **FONT12R2:** the same Settings instance was visibly resized from 760×500 to 825×555, with pointer focus changes and selection/size hit tests; the Fonts controls remained visible and active Roboto Bold 18 remained selected. Its attempted minimum-grip approach undershot and stopped. See [FONT12R2_RESULTS.md](../font12r2/FONT12R2_RESULTS.md). This qualifies pointer resizing above the minimum, not the 560×360 clamp or TTF rendering there.
- The open R3 gap is narrower: runtime minimum clamping and Fonts-page behavior at that bound with the accepted active TTF selection, including same-instance identity and below-minimum behavior.

## Fresh isolated guest and Settings state

A fresh ownership census found one QEMU process from another checkout (PID 34980). It was not contacted or terminated. Ports 59353 (QMP) and 5955 (VNC) were free. One isolated QEMU 11.0.0 guest then booted the byte-identical private copy of the FONT12R2 raw FAT32 image on PIIX IDE primary master, with a private read-only OVMF copy, 1 GiB RAM, two vCPUs, a 1280×800 VGA framebuffer, loopback QMP/VNC, and networking disabled. Exact args and hashes are in [qemu-launch.json](guest/qemu-launch.json) and the [ownership census](guest/ownership-census-prelaunch.json).

QMP reported the current `QEMU PS/2 Mouse` as relative (`absolute:false`). Startup serial showed preference sequence 32 and successful font-selection initialization. Settings launched once (`identity=gxos.builtin.settings`); there was no Settings shutdown before the guest was stopped. The real Fonts page showed Current: Roboto | Bold 18 px, the Bold row selected, all three Roboto faces available, the selected Bold file, preview text, size controls, and Apply, Cancel, and Restore Defaults. See [the Fonts-page capture](guest/settings-fonts-before-minimum.png) and [serial log](guest/fresh-process.serial.log).

The initial outer frame measured from the screendump was x=95, y=70, 760×500, matching the production preferred dimensions. The Settings instance and page remained the same after the pointer positioning attempt. The compositor ID itself is not printed by this guest build; identity is supported by one Settings launch, no Settings shutdown, and the continuous same-window captures, not by a numeric ID trace. Geometry observations are recorded in [observed-window-geometry.json](guest/observed-window-geometry.json).

## Pointer attempt and alternate path review

Before positioning, the visible pointer was approximately `(465,264)`. The lower-right resize hit zone for the measured 760×500 frame was derived from the visible frame and the production 8-pixel hit margin; the planned grip coordinate was `(850,565)`. The calibrated helper sent 11 PS/2 relative movement events in steps no larger than 35 screen pixels with 250 ms between events. QMP acknowledged the commands, but the resulting capture placed the pointer only around `(501,301)`, roughly one step from its starting position and far short of the grip. The before/after screenshots and full QMP trace are retained as [settings-pointer-underrun.png](guest/settings-pointer-underrun.png) and [qmp-input-trace.jsonl](guest/qmp-input-trace.jsonl).

No pointer button-down was sent at the grip, so no resize drag was started and no compositor resize event was delivered. The run used zero of the two allowed deliberate resize drags. Following the instruction to stop after an undershoot, no repeated PS/2 move or second guest attempt was made.

The source audit found no existing Settings resize request or diagnostic path that can request 560×360 on the live Settings object. The separate QEMU input proof is unsuitable because it registers its own window. No input driver or window-manager code was modified, and no minimum-size clamp was bypassed.

## Minimum-geometry checks and font state

The live Settings window stayed at 760×500. This run therefore does **not** establish the live 560×360 geometry, minimum clamp, redraw at that size, Fonts-page clipping at that size, or a below-minimum clamp request. Apply, Cancel, and Restore Defaults are visible on the 760×500 page, but their hit targets were not retested at the minimum. No minimum-size capture is claimed.

No Apply or font-selection mutation was issued. The on-screen active selection remained Roboto Bold 18 px. The raw disk image hash changed during the guest session, but read-only inspection found both preference records unchanged: sequence 31 Bold 16 and sequence 32 Bold 18 retained their original SHA-256 values. See [font-preference-comparison.json](guest/font-preference-comparison.json) and the [before/after asset hashes](guest/private-asset-hashes-before-run.json), [after run](guest/private-asset-hashes-after-run.json). This build does not expose the numeric in-memory font generation in guest telemetry, so no generation number is claimed. Serial showed one Settings launch and no Settings shutdown before QMP quit; no font-specific fatal error or stale glyph rendering was observed at 760×500.

## Validation and regression preservation

No production or test source changed, so no full build or regression run was repeated. The previously accepted FONT12R evidence remains applicable: 6,635 focused FONT2–FONT11 checks, the requested Settings/native suites, the forced AMD64 freestanding build, and zero PE import directories are recorded in [FONT12R_RESULTS.md](../font12r/FONT12R_RESULTS.md). FONT12R3 adds only documentation and test evidence. The isolated guest was stopped via QMP quit on its verified private endpoint. The post-run census observed the original foreign guest plus a newly started QEMU process from another checkout; neither was contacted or terminated. See [ownership-census-postrun.json](guest/ownership-census-postrun.json).

## Final recommendation

**Outcome B — conditional.** There is no demonstrated font-specific defect, and prior FONT12R/R2 evidence remains valid. The accepted FONT_SUPPORT changes remain conditionally qualified; FONT12R3 does not establish the missing same-instance minimum-size clamp or minimum Fonts UI behavior. Controlled integration should retain the existing conditional status and keep the minimum-size gate open; this is not v0.3 desktop signoff. Route the persistent PS/2/QEMU input-delivery limitation to the owning input project. Stop repeated QEMU attempts at this phase boundary. No FONT13 work, merge, push, or commit was started.


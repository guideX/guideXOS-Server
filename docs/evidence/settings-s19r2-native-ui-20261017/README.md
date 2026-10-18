# S19R2 native Settings evidence

This directory records the final S19R2 production guest review. Images were captured from the task-owned AMD64 QEMU guest at 1280×800. The production kernel is the unchanged S19R artifact identified in `runtime-identity.json`.

## Ordered lifecycle evidence

`settings-initial-system.png` and `settings-initial-personalization.png` show the initial native Settings launch and current Ocean appearance. `settings-initial-closed.png` shows the desktop after Settings was closed with Escape. The legacy flow then opened the right-column **Control Panel** launcher, opened Display Options, inspected Gradients, Backgrounds, and Desktop Icons, and returned to a clean desktop with Escape. `legacy-control-panel.png`, `legacy-display-options-gradients-ocean-applied.png`, `legacy-display-options-backgrounds.png`, `legacy-display-options-desktop-icons.png`, and `legacy-surfaces-closed-clean-desktop.png` preserve those states.

The legacy applied marker matched the authoritative Ocean gradient: Display Options showed the yellow applied star beside Ocean on Gradients, and no incorrect applied star beside Blue Flower on Backgrounds. The Blue Flower outline is a selected tile; it is not the applied marker. S19R had already repaired this mismatch, and S19R2 made no source change.

The visible post-legacy Settings reopen is in `post-legacy-settings-system.png` and `post-legacy-settings-personalization.png`; `post-settings-close-clean-desktop.png` shows the desktop after closing the reopened Settings window. That reopen was captured in a second task-owned boot of the same private disk, kernel, and firmware after closing the legacy Control Panel and Display Options. The ordered run separately captured the initial Settings close and the legacy flow. This separation is recorded because the pointer stopped responding after the legacy close in the ordered run; the reopen itself is still directly shown after the legacy surfaces were closed in the second run.

The System page showed Product `guideXOS`, kernel version `0.1.0`, and Architecture `AMD64 (x86-64)`. Personalization showed `Active background: Gradient / Ocean`, consistent with `/desktop.cfg` and the legacy Gradients marker.

## Resize qualification

Settings is live-resizable in the production source: it registers `WF_RESIZABLE`, declares minimum 660×440, preferred 760×560, and maximum 1180×820, and the compositor has a lower-right resize hit region with an 8-pixel margin and clamps changed dimensions to the window bounds. The Settings draw path receives current width and height. See `kernel/core/kernel_apps.cpp` lines 16625–16640, 16764–16770, and 16895–16904; `settings_native_layout.h` lines 13–18; `kernel/core/kernel_compositor.cpp` lines 540–545, 555–587, and 661–668; and `kernel/core/include/kernel/kernel_compositor.h` line 29.

`settings-resize-default.png` shows the normal window and lower-right grip. `settings-resize-unqualified-attempt.png` shows a resize attempt that did not produce a different window geometry. Additional QMP and visible GTK/tablet attempts also failed to produce a verified new size. These attempts do not establish that the compositor rejects resizing or identify a production defect. Minimum-size rendering, control reachability, and hit targets therefore remain unqualified. No resize behavior was waived and no source fix was made.

## Configuration, logs, and validation

`desktop.cfg.before` and `desktop.cfg.after` are both 234 bytes with SHA-256 `CF310FEA3047F36350F99DB5E2FF458A20E85E6AAE9449F320BFE57F14967249`. The guest reported the persisted wallpaper as `gradient_ocean`. S19R2 performed no Settings mutation and caused no configuration change.

`ordered-lifecycle-qmp.log` and `ordered-lifecycle.serial.log` record the initial Settings and legacy flow. `post-legacy-reopen-qmp.log` and `post-legacy-reopen.serial.log` record the visible reopen and clean close. `runtime-identity.json` records the kernel, firmware, private writable media, configuration hashes, and task-owned QEMU endpoints. `sha256-manifest.json` contains hashes for the evidence files.

No source files changed, so S19R/S19 tests and the production build were not rerun. Historical results remain historical: S19R native runtime 1,417/1,417; S19 layout 1,037/1,037 hosted checks. The hosted layout result does not qualify the production guest at minimum size.

**S19 remains Outcome B (Partial)** because the supported production resize behavior has not been visually qualified at minimum size. S20 remains out of scope; the existing Shift+Tab limitation stays documented. S9, Settings 1.0, and Server v0.3 readiness were not advanced.

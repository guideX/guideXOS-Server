# Settings S11 Native Runtime Slice — S11R Closeout

Status: **S11 — Outcome A: Native Settings Runtime Slice Accepted.**

S11 adds a dedicated native Settings application to the production kernel app
manager and compositor. The native window reuses the S10 presentation core,
shows a bounded read-only System/About snapshot from kernel-owned identity,
version, and architecture values, and responds to guest keyboard and mouse
input. S11R completed the remaining production guest validation of the legacy
Control Panel and verified that it continues to operate alongside native
Settings.

## Architecture decision

The implementation keeps the five runtime responsibilities separate:

1. **Shared presentation state:** `settings_presentation_core.cpp` remains the
   S10 source of navigation, focus, descriptors, capability state, and input
   transitions. The AMD64 kernel links this same freestanding source.
2. **Production rendering adapter:** `SettingsNativeApp` in
   `kernel/core/kernel_apps.cpp` implements the S10 adapter contract. It maps
   kernel window input into core events and paints the resulting state through
   the existing framebuffer and `KernelCompositor`. It does not introduce a
   second UI state machine or GUI framework.
3. **Runtime-owned provider snapshot:** `settings_native_provider.cpp` copies
   the product name, kernel version, and architecture into fixed-capacity
   caller-owned storage. The versioned structure carries a size, version,
   availability flag, and revision. It retains no pointers and returns an
   explicit unavailable status when a source cannot be copied safely.
4. **Registration and lifecycle:** built-in metadata gives Settings the
   canonical identity `gxos.builtin.settings`, a native `KernelApp` name, and
   bare-metal availability. `AppManager` assigns the registered identity to a
   private `KernelApp` field before initialization. Duplicate launch follows
   the existing manager policy and focuses the running instance; close
   unregisters the window and destroys the app, so reopening starts fresh.
   Different factories cannot claim the same canonical runtime identity.
5. **Authorization authority:** the provider authorizes by reading the
   AppManager-assigned identity from the caller object. An ordinary launch
   parameter cannot set that field. The adapter exposes no write operation;
   every operation submission is rejected, and no Settings mutation syscall
   or provider write API was added.

The hosted Settings Center continues to use the same S10 core. The visible
Start-menu Settings action remains on its legacy Control Panel route until the
native launch and lifecycle have passed acceptance.

## Provider slice

The snapshot currently renders these values:

| Field | Production source |
|---|---|
| Product | `KERNEL_NAME` |
| Kernel version | `KERNEL_VERSION_STRING` |
| Architecture | `kernel::arch::get_arch_name()` |

The snapshot is version 1, revision 1, and uses fixed caller-provided storage.
Unsupported versions, invalid or undersized structures, stale revisions,
unauthorized callers, and unavailable values have explicit status results.
Only the Settings runtime identity can read the provider. No memory totals,
account information, network details, or mutation surface are included.

## Validation evidence

### Hosted and focused tests

- S11's original native registration/provider harness recorded **549/549**;
  its 100 lifecycle repetitions and the **443/443** affected hosted Settings
  and service checks are historical S11 results, not new S11R runs. The prior
  S10 acceptance record remains **464/464 hosted regression checks**.
- S11R native launch/provider/window harness: **554/554 passed**, including
  resolver separation for the legacy Control Panel shell action and native
  Settings, rejection of hosted-only Control Panel KernelApp launches,
  duplicate-launch identity protection, provider denial for non-Settings
  callers, and independent close behavior. It also completed 100 deterministic
  Settings launch/duplicate-launch/close/reopen repetitions with balanced
  window registration and application destruction.
- S10 shared presentation core rerun during S11R: **2,645/2,645 passed**.
- The 443/443 hosted regression group was not rerun during S11R; no production
  source changed. Focused harness compilation emitted existing unused default
  handler parameter warnings in `kernel_app.h` and an existing unused helper
  warning in `app_launch_target_resolver.cpp`; no production kernel rebuild was
  needed. Final `git diff --check` completed cleanly.

### AMD64 kernel build and link

- Normal AMD64 production kernel build completed with zero compiler errors.
- Initial full build: 156 compile commands, 453 warning diagnostics.
- Final incremental builds after the input-mask, identity-collision, and focus
  hook fixes: 13 additional compile commands and 267 warning diagnostics.
- Across the recorded build passes: 169 compile commands and 720 warning
  diagnostic lines, with zero compiler errors.
- No warning was attributable to the new Settings source or its new code in
  `kernel_apps.cpp`; the diagnostics came from existing unused base-handler
  parameters and existing unrelated app code. No warning suppression was
  added.
- `settings_native_provider.o` and the shared presentation-core object have no
  undefined symbols. Comparing final `kernel.elf` with the S10 kernel found no
  new unresolved symbols (42 final versus 46 in that S10 baseline; four old
  unresolved entries were removed).
- The native provider and presentation path use `KernelApp` and
  `KernelCompositor`; they add no hosted `ProcessTable`, hosted IPC, or hosted
  `Compositor` dependency, and require no additional C++ runtime facility.

Final kernel image SHA-256:

`B5349D89DF7127D8CFE03F9697D28642B9EFC867FA4A1280A87B8F4BF7B46AF7`

### QEMU guest evidence

S11's earlier guest evidence established native Settings operation but did not
activate Control Panel. S11R closed that gap with two isolated graphical QEMU
boots of the already-built production AMD64 kernel. Each run used copied ESP
and OVMF inputs in `out/validation/settings-s11r-20261013/` and separate QMP
ports. The existing production kernel image hash remains
`B5349D89DF7127D8CFE03F9697D28642B9EFC867FA4A1280A87B8F4BF7B46AF7`.
No production source changed, so S11R did not rebuild the kernel.

The actual Control Panel launch path is:

1. The guest Start-menu hit handler passes the selected right-column label to
   `show_start_menu_notification` in `kernel/core/desktop.cpp`.
2. The `Control Panel` entry calls `activate_start_menu_control_panel(5)`,
   which closes Start and sets the embedded legacy control-area state open.
3. The typed resolver maps `Control Panel` to a bare-metal `ShellAction` with
   the `Control Panel` dispatch label. This is an embedded desktop surface,
   not a bare-metal `KernelApp`; the hosted-only canonical identity
   `gxos.builtin.controlpanel` has no KernelApp factory.
4. Selecting the legacy panel's Display tile first closes that embedded panel,
   then calls `launch_app("DisplayOptions")`, dispatching the existing
   `DisplayOptions` KernelApp factory.

The separate native Settings route remains
`desktop.launch gxos.builtin.settings`. The guest resolver dispatches that
canonical identity to the registered `Settings` KernelApp, whose private
runtime identity is assigned by AppManager. The Start-menu `Settings` entry
continues to open the legacy control area; S11R did not change that behavior.

In the first S11R production guest, guest keyboard and calibrated QMP mouse
input opened the Start menu and activated Control Panel. The legacy window
rendered its Control Panel title and System Settings, Display, Device Mgr, and
Network tiles. Selecting Display closed Control Panel and opened the real
Display Options page; guest Escape closed that page and returned to the
desktop. In a fresh production guest, Control Panel was launched again from
the same Start-menu row, closed through its own title-bar close control, and
reopened through that same guest action while native Settings was running.
It remained open underneath Settings and was still present after Settings
closed. The final Control Panel close returned to a clean desktop without a
crash, hang, or stale window.

In that same guest, Settings was independently launched using the canonical
shell command. The System page displayed Product `guideXOS`, kernel version
`0.1.0`, architecture `AMD64 (x86-64)`, snapshot revision 1, and runtime
identity `gxos.builtin.settings`. Pressing the guest `a` key selected About
through the shared presentation core. Closing Settings logged shutdown and
focus loss while Control Panel remained visible. Reopening Settings selected
the fresh default System page, reacquired focus, and rendered the same
provider-backed fields. Closing it again left Control Panel operable. The
guest serial log records both typed Settings dispatches, both focus lifecycle
transitions, both ready snapshots, the About `shared-core=true` input, and
both shutdowns. Settings retained its AppManager-assigned identity and
independently owned KernelCompositor window; the legacy Control Panel remained
in its separate embedded desktop state. Closing either surface did not
terminate or stale the other.

QMP used the calibrated relative PS/2 pointer path (the guest reports a
non-absolute QEMU PS/2 Mouse) and keyboard input, with host pointer-grab
behavior accounted for. Early pointer placement missed a menu row and one
session became unresponsive to pointer movement; those were input/session
calibration issues. A fresh isolated boot and corrected pointer movement
completed the required actions. No production defect was found or repaired.
Only task-owned QEMU processes were exited through QMP; the previously
identified unrelated QEMU session was not signaled.

S11R focused test and QEMU evidence:

- First Control Panel and Display page screenshots:
  `out/validation/settings-s11r-20261013/guest-visible/control-panel-open-foreground.png`,
  `out/validation/settings-s11r-20261013/guest-visible/display-options-open.png`,
  `out/validation/settings-s11r-20261013/guest-visible/display-options-escape-close.png`
- Reopened Control Panel and closed state:
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/control-panel-second-instance.png`,
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/control-panel-second-closed.png`
- Native provider fields and shared-core About navigation:
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/settings-system-current.png`,
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/settings-about-current-delayed.png`
- Fresh Settings reopen, Control Panel retained after Settings close, and clean final desktop:
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/settings-reopened-clean.png`,
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/post-terminal-escape.png`,
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/desktop-final-clean.png`
- S11R serial and QMP transcripts:
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/guest-com1.serial.log`,
  `out/validation/settings-s11r-20261013/guest-reopen-fresh/qmp-transcript.log`
- Final focused harness executable:
  `out/validation/settings-s11r-20261013/final-native-runtime/settings_native_runtime_test.exe`

Key evidence files:

- Final kernel guest serial: `out/validation/settings-s11-production-20261011/guest-final-kernel/guest-com1.serial.log`
- Final kernel Settings launch screenshot: `out/validation/settings-s11-production-20261011/guest-final-kernel/settings-system.png`
- Final kernel keyboard About screenshot: `out/validation/settings-s11-production-20261011/guest-final-kernel/settings-about-keyboard.png`
- Provider fields and mouse-selected About screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/settings-about-mouse-selected.png`
- Earlier close/reopen screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/settings-reopen-system.png`
- Start-menu Control Panel entry screenshot: `out/validation/settings-s11-production-20261011/guest-acceptance-final/pointer-recover-y.png`

## Limitations and next phase

Only System/About is backed by a real provider. Other categories are visibly
deferred or unavailable; there are no mock providers. The snapshot revision is
constant at 1 for this immutable boot identity/version slice and must be
advanced if a later provider adds changing values. The legacy Start-menu
Settings routing has not been redirected.

**S11 final acceptance state:** S11 is accepted as **Outcome A — Native
Settings Runtime Slice Accepted**. S9 and Settings 1.0 are not accepted by this
closeout. The v0.3 readiness state was not re-evaluated and is unchanged.

**Recommended S12 scope — Native Settings Default Launch Routing and Initial
Page Expansion:** route Start → Settings to `gxos.builtin.settings`, retain a
separate working Control Panel launch entry, and verify both in QEMU. Expand
native Settings only through the S10 shared core and authoritative production
providers; show unsupported capabilities as unavailable. Keep authorization
runtime-owned and preserve read-only restrictions until mutation capabilities
are explicitly implemented and validated. Do not implement S12 as part of
this closeout.

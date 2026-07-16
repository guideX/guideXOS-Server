# guideXOS Server v0.2 Dual-Monitor Implementation Track

Validated through `2026-07-15`. The post-audit recovery preserved the audited QEMU-only advanced virtio-gpu implementation and the intentional removal of the duplicate hosted synthetic document. The new milestone is the typed guest display-configuration control plane; separate-launch persistence restoration remains deferred.

## Purpose

- Prove the hosted compositor and display model can represent a synthetic second monitor without changing the default hosted path.
- Validate virtual desktop bounds, viewport selection, render-target accounting, taskbar routing, and input mapping in a controlled hosted validation lane.
- Prove the QEMU-only virtio-gpu dual-output compositor snapshot path while preserving the static diagnostic patterns and keeping real hardware GPU work out of scope.

## Safety Boundary

- QEMU virtio-gpu work is allowed in this branch, but only through the x86_64 diagnostic probe gate.
- The probe rendering path stays QEMU-only and does not activate on normal production bare-metal boots.
- Real bare-metal Intel/onboard GPU work is not allowed yet.
- REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
- Real hardware GPU mode-setting, MMIO probing, scanout enabling, and display register programming remain out of scope for this pass.

## Current Probe Status

- Stage A is visually confirmed on scanout 0.
- The latest confirmed scanout-0 capture is `logs\qemu-display-probe-20260712-214504\virtio-gpu-stageA\captures\scanout0-stageA-gpu0-head0.png`.
- The Stage A physical backing audit validated one mem entry, one contiguous physical run, `coveredBytes=4096000`, and `physicalCoverageValid=yes`.
- Stage B renders a distinct second pattern on scanout 1, with the latest captures at `logs\qemu-display-probe-20260712-214504\virtio-gpu-stageB\captures\scanout0-stageB.png` and `logs\qemu-display-probe-20260712-214504\virtio-gpu-stageB\captures\scanout1-stageB.png`.
- The first single-shot compositor frame now renders into both virtio-gpu resources with target-local clipping, a primary-only taskbar, and distinct visual evidence for each viewport.
- The controlled QEMU-only live path now repeats that compositor presentation through the normal desktop update cadence, with dirty-generation skipping, a hard 10 FPS cap, and a bounded 60-attempt proof mode.
- The backend-independent display input mapper and QEMU-only bounded input proof are now implemented. The current launcher inventory identifies the guest path as PS/2-relative; it does not silently assume an absolute tablet.
- Dual-output rendering is proven, while `GET_DISPLAY_INFO` still reports `enabledScanoutsAfter=1` and `post-render scanout[1] enabled=no`; that connector state is tracked separately from resource assignment and presentation readiness.
- The output inventory now reports two operational outputs, two monitors, two backed render targets, and presentation confirmation for scanout 1 when capture validation is available.
- Latest input-routing run root: `logs\qemu-display-probe-20260713-121002\`.
- The typed guest command/response bridge, authoritative configuration service, bounded safe-point transactions, hosted adapter, and bare-metal adapter are now implemented.
- The QEMU-only proof coordinator now queries active state, proves Extend -> Mirror -> Extend, switches primary/taskbar routing to Display 2 and back, and injects one validation failure to prove rollback and presentation resume.

## Display Options Real-Output Configuration Track

The existing Display Options persistence model is now the requested display
configuration. Detected backend inventory, requested settings, and active
applied settings are separate records; a failed request cannot overwrite the
active layout. The QEMU-only virtio-gpu bridge supplies stable monitor/output
identity and assigned geometry, while resource IDs, backing addresses, and
connector state remain runtime diagnostics rather than persisted authority.

Display Options consumes operational virtio-gpu outputs when the gated backend
inventory is published. Output 2 is shown as `VirtIO-GPU Output 2` and
`Operational` even when `connectorEnabled` is false; connector state is shown
on its own diagnostic line. If the backend inventory is unavailable, the
existing hosted synthetic and single-framebuffer paths remain active.

Apply is a bounded transaction: it snapshots the active configuration, pauses
new presentation, waits for the current synchronous presentation to become
idle, validates the request, rebuilds monitor/view/target geometry, clamps
windows and cursor-related capture state, presents a validation frame, and
only then commits persistence. Failure restores the prior desktop state and
reports the exact blocker, with rollback/fallback status in the compact
summary. UI edits are not live-previewed: Apply commits them, OK follows the
existing window flow, and Cancel restores the last applied selection.

Extend uses persisted virtual origins and preserves the current horizontal
2560x800 arrangement for two 1280x800 outputs. Mirror requires compatible
assigned dimensions, uses one logical 1280x800 viewport at origin `0,0`, and
keeps independent output resources while presenting the same logical content
to both outputs. Primary selection updates the taskbar monitor and default
work-area routing without silently reordering the arrangement. Saved output
identities are reconciled by stable backend/output identity and then by
inventory order; stale entries do not create phantom monitors.

The shared configuration transaction and source/runtime contract smoke are:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-display-options.ps1
powershell -ExecutionPolicy Bypass -File scripts\smoke-qemu-display-options-runtime.ps1
```

The QEMU runtime wrapper reuses the bounded live virtio-gpu proof to verify the
two operational targets, both captures, validation counters, and zero target
failures while the configuration transaction remains bounded and gated.

The hosted Display Options surface and the bare-metal QEMU app now submit the
same fixed-size `DisplayConfigurationCommand` contract to
`DisplayConfigurationService`. The service owns serialization, active-state
snapshots, pause/resume, target-layout application, input bounds, primary and
taskbar routing, validation, rollback, last-known-good state, and persistence
commit. The current app model runs callbacks on the desktop owner path, so the
service processes its single bounded mutation slot at that compositor-safe
point; the explicit safe-point hook remains available for a future process-IPC
adapter.

The runtime wrapper now proves the real QEMU endpoint rather than calling
private transaction helpers. Separate-launch restoration is intentionally not
claimed in this pass.

## Typed Display Configuration Control Plane

`display_configuration_command.h` is the kernel-safe, backend-neutral contract.
It contains a version, structure size, request ID, command type, flags, a
fixed output arrangement, and no pointers, UI references, resource IDs,
physical addresses, backing addresses, or MMIO fields. It supports queries,
ApplyConfiguration, RestoreLastKnownGood, and ForceValidationFrame. Responses
carry accepted/completed/success state, stable result codes, validation,
pause/resume, target rebuild, rollback, persistence, detected and active
snapshots, and bounded diagnostics.

The service has one serialized mutation slot. Queries read stable snapshots;
additional mutations receive `BackendBusy`. Apply uses the existing
transaction sequence: snapshot, validate, pause presentation, wait boundedly,
rebuild monitor/target geometry, update virtual desktop and input bounds,
route primary/taskbar state, render and flush a validation frame, resume, then
persist. Any failure restores the previous layout and reports rollback success
or failure. The one-shot injected validation failure is accepted only by the
explicit QEMU control-proof build gate and cannot be enabled by normal boots.

Hosted and bare-metal Display Options are thin adapters. Apply leaves the
window open, OK closes only after success, and Cancel discards local edits and
reloads active state without submitting a backend rollback. Window generation,
request ID, and command type prevent stale responses from updating a newly
opened window.

The bounded proof coordinator emits request/response diagnostics and host-QMP
capture markers for `initial`, `mirror`, `extend`, `primary-2`, `primary-1`,
and `rollback`. QMP remains host-harness-only for QEMU lifecycle, screenshots,
and evidence collection; it is not guest IPC.

### Runtime control proof

The initial active query must report the operational `virtio-gpu` backend, two
outputs, Extend mode, Display 1 primary, and the runtime virtual desktop. The
successful proof then records:

- Extend -> Mirror: both independent resources stay bound, both target
  viewports become `0,0`, the logical desktop uses the current 1280x800
  geometry, validation succeeds, and presentation resumes.
- Mirror -> Extend: Monitor 2 returns to the right at the runtime-equivalent
  origin, the virtual desktop returns to the extended width, and input bounds
  are restored.
- Primary Display 2 -> Display 1: taskbar ownership follows the primary
  monitor, existing valid windows remain usable, and the active response is
  checked after each apply.
- One-shot validation failure: the request is accepted, presentation pauses,
  rollback restores the previous Extend/primary-1 state, persistence is not
  committed for the failed request, and presentation resumes.

The separate-launch persistence restoration proof is the next milestone.

## Implementation Roadmap

1. Hosted synthetic validation complete.
1. Hosted two-window synthetic output complete.
1. QEMU GOP/backend diagnostics complete.
1. virtio-gpu discovery/capability walk complete.
1. Runtime MMIO mapping complete in the QEMU-only x86_64 probe build.
1. Controlled modern VirtIO transport initialization complete.
1. GET_DISPLAY_INFO complete.
1. First 2D resource, backing attachment, scanout 0 assignment, transfer, and flush complete.
1. Scanout 1 resource, backing, transfer, flush, and distinct visual proof are present.
1. Dual-output rendering is proven, while scanout 1 connector state remains a separate diagnostic signal.
1. QEMU-only virtio-gpu output inventory, `DisplayMonitor`, `DisplayViewport`, and `DisplayRenderTarget` bridge complete for the static diagnostic patterns.
1. First single-shot compositor frame complete across both virtio-gpu backing buffers with virtual desktop clipping, per-target viewport origins, and a primary-only taskbar.
1. Controlled live presentation complete in the explicit QEMU-only `compositor-live-bounded` mode; the default probe remains diagnostic/single-shot.
1. Backend-independent display input mapping and a bounded QMP input-routing smoke are complete; the current QEMU path proves relative-global pointer movement, ordinary click/focus, and cross-boundary window dragging.
1. The live presenter retains stable resources, scanouts, backing lifetime, synchronous descriptor reclamation, per-target failure isolation, and static-pattern fallback.
1. Display Options real-output inventory bridge, separate detected/requested/active configuration records, Apply/Cancel semantics, and compact backend diagnostics are complete.
1. Bounded transactional Extend/Mirror layout, primary selection, window clamping, rollback, and persistence reconciliation are complete in the shared compositor configuration path.
1. Versioned typed guest display command/response, one-slot service serialization, hosted and bare-metal adapters, safe-point routing, and stale-response protection are complete.
1. Real QEMU active query, Extend/Mirror cycling, primary/taskbar switching, per-stage captures, and one-shot rollback proof are complete through the public service endpoint.
1. Full-target transfer/flush remains a temporary whole-target implementation; dirty rectangles are deferred.
1. Real hardware remains Mule Territory.

## Gates

- `GXOS_SYNTHETIC_DUAL_MONITOR=1` enables the synthetic dual-monitor desktop model.
- `GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1` enables hosted dual-window output only when the first gate is also on.
- Normal hosted mode remains the default when both gates are unset.

## QEMU Virtio-GPU Content and Presentation Modes

The QEMU probe keeps three explicit content/presentation modes:

- `diagnostic-patterns`: the conservative default, with the known-good static patterns and fallback.
- `compositor-single-frame`: the preserved one-shot guideXOS compositor proof across both operational targets.
- `compositor-live-bounded`: an explicit `GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE` proof that also requires `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE`; the automated smoke selects `GXOS_QEMU_VIRTIO_GPU_COMPOSITOR_LIVE_BOUNDED` and stops at 60 presentation attempts or 800 PIT ticks.

The live presenter is scheduler-owned but compositor-state-independent. It consumes the existing desktop invalidation bridge: `desktop::needs_redraw()` advances a monotonic redraw generation, and the presenter compares that generation with the last presented generation. Window changes, focus/z-order changes, taskbar or clock changes, desktop redraws, and explicit redraw requests therefore remain normal compositor causes. The live diagnostic marker requests redraw through that same bridge and is clipped to each target’s viewport work area; it does not become product UI and does not touch the primary-only taskbar.

Presentation is capped at 10 FPS with a hard PIT-tick interval. Each dirty generation renders target 0 and target 1 synchronously from the same generation, transfers and flushes each complete target rectangle, and reclaims its descriptor chain before the next target. Clean generations and calls inside the interval are skipped. The stable resource IDs, scanout assignments, attached backing stores, dimensions, pitch, and `PixelSurface` descriptors remain valid for the live interval. Slow queue polling does not run while desktop state is owned by the presenter.

This phase intentionally uses whole-target transfer/flush rather than dirty rectangles. A failure on one target is recorded with its frame, target, command, response, and timeout state while the other target continues when safe. After a small bounded failure streak, the affected target may repaint the known-good static pattern; the other resource is not reset or collapsed into the primary virtual region. Fallback activation and stop reason are part of the compact live summary.

The bounded QEMU harness captures `initial-head0.png`, `initial-head1.png`, `final-head0.png`, and `final-head1.png`, records SHA-256 checksums and changed status, and verifies guideXOS content, the 2560x800 viewport split, and primary-only taskbar behavior. The `compositorInputBounded` extension additionally captures `input-click-head*.png`, `input-boundary-head*.png`, and `input-after-drag-head*.png`. It must never be used as an unrestricted production boot mode. Cursor queues, cursor commands, display hotplug, per-monitor taskbars, and arbitrary runtime mode setting remain deferred from the low-level probe.

## Validated Modes

| Mode | `presentationMode` | `renderTargetCount` | `backedTargetCount` | `dualWindowOutput` | What was proven |
| --- | --- | ---: | ---: | --- | --- |
| `normal-single-output` | `normal-single-output` | 1 | 1 | `false` | Default hosted behavior stays unchanged. |
| `synthetic-camera` | `synthetic-camera` | 2 | 1 | `false` | Monitor 2 is modeled at virtual origin `1920,0` and no secondary hosted window is created. |
| `synthetic-two-window-output` | `synthetic-two-window-output` | 2 | 2 | `true` | Target 1 uses origin `0,0` with the taskbar visible, target 2 uses origin `1920,0` with the taskbar suppressed, and input mapping is correct. |

For the dual-window mode, the validated input mapping was:

- primary local `x=120` maps to virtual `x=120`
- secondary local `x=120` maps to virtual `x=2040`

## How To Launch

Preferred launcher:

```bat
.\run-hosted-synthetic-dual-window.bat
```

Equivalent manual launch:

```powershell
$env:GXOS_SYNTHETIC_DUAL_MONITOR = '1'
$env:GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT = '1'
.\run-server.bat
```

If both gates are unset, `.\run-server.bat` stays in normal single-output mode.

## Validation

| Command | What it proves |
| --- | --- |
| `build.bat` | The hosted runtime and display code compile successfully. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-display-synthetic-layout.ps1` | The synthetic gate plumbing, display-model helpers, render-target wiring, and summary diagnostics are present and consistent. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-hosted-display-runtime.ps1` | All three validated runtime modes behave as expected, including summary logs, paint routing, input mapping, and cleanup. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-qemu-display-probe.ps1` | The QEMU probe confirms Stage A visually, validates the physical backing contract, captures distinct Stage B patterns on scanout 0 and scanout 1, and records the split between connector state, operational output readiness, presentation confirmation, and the one-shot compositor proof. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-compositor-frame.ps1` | The QEMU-only compositor-frame smoke renders one static guideXOS desktop snapshot into both virtio-gpu outputs, validates target-local clipping and the primary-only taskbar, and preserves the static-pattern fallback path. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-live-presentation.ps1` | Source and bounded QEMU smoke for the explicit live gate: dirty-aware repeated presentation, 10 FPS cap, 60-attempt bound, initial/final head captures, checksum change, both targets, and primary-only taskbar. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-input-routing.ps1` | Source safety smoke for the backend-independent mapper and bounded QEMU input proof. Add `-Runtime` for the PS/2-relative click/focus, cross-boundary drag, both-output capture, and live-presentation proof. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-mmio-mapping.ps1` | The generic MMIO mapping API contract, safety flags, and virtio-gpu probe wiring stay aligned with the runtime mapping prerequisite. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-diagnostic-source.ps1` | The virtio-gpu probe source keeps the full capability walk, the `cfg_type=0x05` diagnostic-only treatment, QEMU-only gates, bounded polling, response validation, the diagnostic/single-shot modes, the bounded live bridge, and the no-real-hardware constraint. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-output-backend.ps1` | The QEMU-only virtio-gpu output backend keeps connector state separate from operational readiness, bridges into `DisplayMonitor` / `DisplayViewport` / `DisplayRenderTarget`, and preserves the diagnostic, single-shot, live, and fallback boundaries. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-display-options.ps1` | Source contract for QEMU-only gates, separate detected/requested/active models, transactional pause/rebuild/validation/rollback, Extend/Mirror geometry, primary/taskbar routing, and safe persistence. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-qemu-display-options-runtime.ps1` | Bounded QEMU runtime wrapper for the real output inventory, both live targets, output captures, validation counters, and per-target GPU-failure reporting. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-display-configuration-control-plane.ps1` | Source contract smoke for the versioned command/response, bounded service, hosted and bare-metal adapters, transaction ordering, rollback, persistence gating, QEMU-only injection, and prohibited-feature exclusions. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-display-configuration-control.ps1` | Bounded QEMU runtime proof for active query, Mirror/Extend cycling, primary/taskbar switching, one-shot rollback, per-stage dual-head captures, and zero GPU failures/fallback. |
| `build-kernel.bat` | The bare-metal service, adapter, and QEMU coordinator compile and link. |
| `git diff --check` | The working tree is free of whitespace and patch-format issues. |

The hosted runtime smoke restores `desktop.json` and `desktop.state` and removes `display-options.cfg` as part of its cleanup, so validation does not leave those runtime state files dirty.

## Current Architecture Terms

- `DisplayMonitor` here means the code-level `DisplayMonitorDescriptor` concept: one monitor record with virtual coordinates, size, enabled/primary flags, and virtual bounds.
- `DisplayViewport` is the active hosted view into the virtual desktop; it tracks the active origin, size, synthetic-hosted state, preferred geometry, assigned geometry, and local-to-virtual coordinate conversion.
- `DisplayRenderTarget` is one renderable target entry; it records the target index, monitor mapping, viewport origin, and whether the target is backed by a hosted framebuffer or an output resource.
- For the QEMU virtio-gpu path, those descriptors now carry separate connector state, resource binding, transfer readiness, present readiness, and presentation confirmation flags.
- `virtual desktop bounds` are the aggregate rectangle reported by `DisplayVirtualDesktop::left`, `top`, `right`, `bottom`, `width()`, and `height()`.
- `backed render target` means the target has a hosted framebuffer backing.
- `conceptual render target` means the target exists in the model and diagnostics even when it is not backed in single-window synthetic-camera mode.

## Intentionally Deferred

- Hardware multi-output
- Real-hardware multi-output
- Per-monitor taskbars
- Per-monitor wallpaper
- VNC/video multi-target capture
- Cursor warping and advanced focus semantics
- Real hotplug, refresh rate, rotation, and DPI behavior

## Known Limitations

- This is a hosted synthetic experiment, not real hardware multi-monitor support.
- QEMU virtio-gpu scanout 0 and scanout 1 each receive the compositor snapshot and bounded live updates; connector state remains a separate diagnostic signal from operational output readiness.
- The second hosted window is still synthetic, not a hardware-backed second display.
- Default hosted mode stays single-output unless the gates are enabled.

## Current Paging Inventory

The runtime MMIO work is starting from the existing huge-page / page-table scaffolding, not from a blank slate.

- `kernel/core/include/kernel/hugepages.h` defines the current x86-64 and ARM64 page-table flag vocabulary, including writable, user, cache-disable, write-through, write-combining, and no-execute bits.
- `kernel/core/hugepages.cpp` already contains the huge-page-oriented helpers: `map2M`, `map1G`, `unmap2M`, `unmap1G`, `make_pde_2m`, `make_pdpe_1g`, `split_2m_to_4k`, and `split_1g_to_2m`.
- Those helpers establish the shape of the eventual mapping path, and the current QEMU-only runtime MMIO window now layers on top of that scaffolding.
- The active x86_64 probe build uses a reserved high-half MMIO window with UC-style `PCD|PWT` leaf mappings, `NX`, supervisor-only permissions, and a bounded bump allocator.
- The page-table writer stores physical child-table addresses in hardware entries, and the probe enables `EFER.NXE` before it emits NX-marked MMIO leaves; if NXE is unavailable, the probe stops before touching the BAR.
- PAT/MTRR cache-attribute plumbing is still unresolved globally, so the safe current phase uses the UC-compatible `PCD|PWT` path instead of mapping PCI MMIO as ordinary cacheable RAM.
- `kernel::mmio::mapForDevice` now installs the QEMU-only transport mappings and stops before any transport write path if it cannot prove a safe MMIO window.
- The same mapping path now feeds the controlled virtio-gpu transport init milestone, but only behind the diagnostic QEMU gate.

## QEMU / Bare-Metal Output Investigation

This phase records the backend matrix plus the QEMU-only resource-backed output path; the boot path itself still does not gain real-hardware multi-output rendering.

### Observed Backend Matrix

| Backend | QEMU args | Booted? | GOP handles | Raw descriptors | Unique candidates | Result | Interpretation |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| `std` | `-vga std` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Two GOP handles alias the same framebuffer memory. Descriptor `1` is not a second monitor. |
| `virtio-gpu` | `-vga none -device virtio-gpu-pci,max_outputs=2` | yes | 2 | 0 | 0 | `BootInfo array disabled`, kernel `FramebufferCount=0` | The bootloader rejects the selected GOP framebuffer with `unsupported-pixel-format` after a `1280x800` snapshot with `base=0`, `pitch=0`, `bpp=0`, `format=Unknown`. |
| `virtio-vga` | `-vga virtio` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Same alias pattern as `-vga std`; no additional unique framebuffer candidate. |
| `qxl-vga` | `-vga qxl -spice addr=127.0.0.1,port=5930,disable-ticketing=on` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | QXL/SPICE is usable as a diagnostic probe, but it still collapses to one unique framebuffer candidate through GOP. |

Fresh backend evidence is captured in `logs\qemu-display-probe-20260712-214504\qemu-display-probe.evidence.txt`.

Current takeaways:

- `std`, `virtio-vga`, and `qxl-vga` all expose two GOP handles, but only one unique framebuffer candidate. The second handle is a duplicate alias, not a second monitor.
- `virtio-gpu-pci,max_outputs=2` and the optional modern-only `disable-legacy=on` variant now both expose the modern VirtIO PCI capability set, and the probe now maps the transport window read-only before starting the QEMU-only 2D render milestones.
- `virtio-vga` still resolves the same capability set, so the transport mapping is now the first diagnostic boundary rather than a one-off command-line quirk.
- The full capability walk now logs every vendor-specific VirtIO capability, including `cfg_type=0x05`, and it keeps scanning for common, notify, ISR, and device config capabilities even when the PCI config capability is malformed.
- The bootloader still reaches the kernel on the virtio-gpu path, so this is not a boot-before-handoff problem. The current boundary is the read-only transport probe, not GOP discovery.
- The current QEMU probe path now creates a 2D resource, attaches DMA-visible backing memory, assigns scanout 0, transfers a deterministic CPU-generated test pattern, and flushes it.
- Stage B adds a second independently backed 2D resource, assigns scanout 1, transfers the secondary pattern, and flushes it, but QEMU still does not report scanout 1 as enabled.
- Hardware multi-output remains unimplemented in guideXOS. The QEMU probe path is separate from real hardware and now consumes the compositor snapshot through the explicitly gated bounded live presenter.

The new probe launchers are:

- `scripts\run-qemu-display-probe.bat`
- `scripts\run-qemu-multimonitor-probe.bat`

The smoke harness is:

```powershell
powershell -ExecutionPolicy Bypass -File scripts\smoke-qemu-display-probe.ps1
```

It boots QEMU headlessly, captures the probe's serial output into `logs\qemu-display-probe-<timestamp>\`, and stops QEMU automatically once the framebuffer diagnostics have been observed.

The standard `std` mode still validates:

- `GOP handles discovered`
- `BootInfo FramebufferCount`
- `UniqueFramebufferCount`
- `DuplicateFramebufferCount`
- descriptor `0` as `primary selected`
- descriptor `1` as `duplicate alias same-as-primary` when present
- the kernel's mirror of the same counts
- the kernel's `Framebuffer ready` marker, which shows the primary framebuffer is still the render target

### What guideXOS cannot use yet

- Multiple framebuffer descriptors are not part of the current boot handoff contract.
- The kernel does not yet consume an array of framebuffer targets from the boot path.
- The compositor does not yet render to more than one real framebuffer in bare-metal mode.
- The QEMU-only virtio-gpu path now has bounded single-shot, live, and relative-global interactive compositor proofs; head-aware absolute routing is unavailable with the current PS/2 launcher path, and normal product multi-output behavior remains deferred.

### Next roadmap milestone

The next implementation milestone is resolution and output-mode selection,
safe resource rebuild/resizing, virtio-gpu display events/config-change
handling, hotplug, dirty-rectangle presentation, manual interactive QEMU
validation, and eventual real-hardware backend planning under a separate Mule
checkpoint. Fixed assigned dimensions, no hotplug, no arbitrary mode setting,
and no head-aware absolute QMP route remain current limitations. Real-hardware
GPU enablement remains Mule Territory.

## Virtio-GPU Driver Investigation

Diagnostic-only probe results from `logs\qemu-display-probe-20260709-211742\virtio-gpu\serial.log` and `logs\qemu-display-probe-20260709-211742\virtio-gpu-modern-only\serial.log`:

| Item | Result |
| --- | --- |
| PCI discovery | Success. The probe found a virtio-gpu PCI function at `00:02.00` with `vendor=0x1AF4`, `device=0x1050`, `subsystem=0x1AF4:1100`, `class=0x03`, `subclass=0x80`. |
| Capability walk | Success. The full PCI capability list walk logs 6 caps, 5 vendor-specific caps, and every vendor-specific VirtIO capability. `cfg_type=0x01` common, `0x02` notify, `0x03` ISR, and `0x04` device are resolved. `cfg_type=0x05` is diagnostic evidence only and is not treated as success by itself. On `virtio-gpu` and `virtio-gpu-modern-only`, the PCI config capability is malformed because BAR0 is unassigned. |
| Modern transport | Detected and mapped in the QEMU-only probe build. The common config BAR resolves to `0x000000C000000000`, the transport MMIO window is mapped into the reserved kernel range, and the probe now continues into bounded status progression. |
| Feature negotiation | Complete for `VIRTIO_F_VERSION_1` and the minimal modern transport feature set. |
| Control queue | Ready. Queue 0 is configured and enabled; the cursor queue stays disabled. |
| GET_DISPLAY_INFO | Success. One diagnostic control command is issued before the render milestone and one more after flush. |
| Scanouts reported | `deviceConfigNumScanouts=2`, `slots=16`, `enabled=1`, `disabled=15`, `qemuMaxOutputsIntent=2`, `qemuTwoUsableScanouts=no` in the current QEMU run. |
| Stage B initial state | Success. The latest Stage B probe logs `scanout1InitialEnabled=no` before the second resource is created, while still confirming `deviceConfigNumScanouts=2` and `qemuMaxOutputsIntent=2`. |
| Stage B activation | Success on the guest side. A second resource and backing store are created, scanout 1 is assigned, transferred, and flushed, and the static diagnostic pattern is visibly distinct. `GET_DISPLAY_INFO` still reports `enabledScanoutsAfter=1` and `scanout1 enabled=no`, but that connector state is tracked separately from operational readiness. |
| Output inventory | Success. The operational output inventory now reports two outputs, two monitors, and two backed render targets; `connectorEnabled=1` and `presentationConfirmed=2` are tracked separately from `GET_DISPLAY_INFO.enabled`. |
| Virtual desktop | Success. The virtual desktop is computed from the assigned scanout rectangles, so the current `1280x800` outputs land at `0,0` and `1280,0` with a combined `2560x800` desktop. |
| Visual proof | The Stage B capture artifacts at `logs\qemu-display-probe-20260712-214504\virtio-gpu-stageB\captures\scanout0-stageB.png` and `logs\qemu-display-probe-20260712-214504\virtio-gpu-stageB\captures\scanout1-stageB.png` were manually inspected and are distinct. |
| Resource / backing | Success. Stage A creates one `B8G8R8X8_UNORM` 2D resource with bounded DMA-visible backing, and Stage B creates a second independent resource with its own backing store. |
| Scanout 0 assignment | Success. The diagnostic resource is assigned to scanout 0 before Stage B adds the second scanout. |
| Transfer / flush | Success. Stage A transfers and flushes the primary pattern; Stage B separately transfers and flushes the secondary pattern. |
| Modern-only QEMU mode | Supported locally. The smoke successfully launches `-device virtio-gpu-pci,max_outputs=2,disable-legacy=on`. |
| Current interpretation | QEMU exposes a modern virtio-gpu PCI candidate, guideXOS proves the safe QEMU-only transport initialization milestone plus both scanout diagnostic render milestones, and the output inventory records two operational outputs while `GET_DISPLAY_INFO` keeps connector state separate. The one-shot compositor proof and the controlled bounded live presenter both render through the two operational targets with static fallback preserved. |

Next roadmap milestone:

- Resolution and output-mode selection.
- Safe resource rebuild/resizing.
- Virtio-gpu display events/config-change handling.
- Hotplug.
- Dirty-rectangle presentation.
- Manual interactive QEMU validation.
- Eventual real-hardware backend planning under a separate Mule checkpoint.
- Real hardware stays Mule Territory.

## Virtio-GPU Transport Milestone

The virtio-gpu discovery path now maps the modern transport MMIO regions into a reserved kernel window, performs bounded modern status progression, negotiates the minimal feature set, configures controlq 0, completes the diagnostic `GET_DISPLAY_INFO` requests, and carries the QEMU-only scanout-0 and scanout-1 diagnostic render milestones.

- PCI discovery still finds the QEMU virtio-gpu function at `00:02.00`.
- The modern VirtIO capability walk still resolves `common`, `notify`, `isr`, and `device` capabilities.
- The `pci` capability remains malformed in the current QEMU setup because BAR0 is unassigned, and it is still not treated as a transport region.
- The active x86_64 probe build gates runtime mapping behind `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE`.
- `kernel::mmio::mapForDevice` installs the common, notify, ISR, and device config regions into the reserved kernel MMIO window using supervisor-only, non-executable, UC-style `PCD|PWT` leaf mappings.
- The current cache policy avoids ordinary write-back RAM semantics and does not touch global MTRRs.
- The probe now performs bounded reset/status progression, requires `VIRTIO_F_VERSION_1`, writes the minimal driver feature set, verifies `FEATURES_OK`, and configures only control queue 0.
- `GET_DISPLAY_INFO` now runs before rendering and again after flush, and both responses are parsed.
- The current QEMU run reported `deviceConfigNumScanouts=2`, `slots=16`, `enabled=1`, `disabled=15`, `qemuMaxOutputsIntent=2`, and `qemuTwoUsableScanouts=no` even after the second resource and flush completed; that remains a connector-state diagnostic, not a statement about operational output readiness.
- The probe now creates one `B8G8R8X8_UNORM` 2D resource for scanout 0, then a second independent 2D resource for scanout 1, attaches bounded DMA-visible backing memory to both, transfers deterministic test patterns, flushes them, and publishes an operational output inventory with two monitors and two backed render targets.
- The render path stays QEMU-only, uses the reserved diagnostic resources, and does not set up cursor, virgl, blob, 3D, or context integration; the controlled single-shot and bounded-live compositor proofs share this 2D resource path.
- Unmap currently clears only MMIO-owned leaf entries and retains the intermediate page-table pages for now.

## Runtime MMIO Window

- Base: `0xFFFFC00000000000`
- Size: `16 MiB`
- Allocation strategy: a bounded bump allocator over the reserved high-half window, with per-page conflict checks and no automatic recycling yet.
- Architecture gate: x86_64 QEMU probe builds only.
- Build gate: `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE`
- Page-table flags: present, supervisor-only, writable leaf, `NX`, `PCD`, and `PWT`
- `NX` is only used after the probe confirms `EFER.NXE` can be enabled on the QEMU-only build.
- Cache strategy: UC-compatible `PCD|PWT`, not write-back
- Unmap limitation: only MMIO-owned PTEs are cleared; shared higher-level paging structures are retained

### Next Intended Phase

- Interactive manual mouse validation.
- Display Options integration with the real virtio-gpu backend.
- Persistent Extend/Mirror configuration.
- Primary-monitor switching.
- Resolution/output configuration.
- Hotplug/config-change events.
- Dirty-rectangle optimization.
- Real hardware remains Mule Territory.

## Framebuffer Array Handoff

The bootloader and kernel now preserve a bounded diagnostic framebuffer array in `BootInfo`, but the legacy single-framebuffer fields remain the authoritative rendering contract.

- `FramebufferBase`, `FramebufferSize`, `FramebufferWidth`, `FramebufferHeight`, `FramebufferPitch`, and `FramebufferFormat` still describe the primary framebuffer that the renderer uses today.
- `FramebufferCount` is the raw GOP descriptor count, `FramebufferUniqueCount` is the deduplicated physical framebuffer count, `FramebufferDuplicateCount` counts exact aliases, and `FramebufferSuspiciousCount` records same-base/same-size collisions with mismatched geometry or format.
- `FramebufferDescriptors[]` preserve the raw GOP descriptors so the boot path can classify aliases without changing rendering behavior.
- UEFI GOP can populate more than one descriptor if firmware or QEMU exposes multiple usable handles.
- BIOS / Multiboot remains a single-descriptor handoff with `FramebufferCount = 1`.
- Secondary framebuffer rendering stays deferred until the bare-metal compositor grows explicit multi-target present support.
- Deduplication prevents guideXOS from treating aliased GOP handles as separate monitors.
- The optional `virtio-gpu` comparison path remains QEMU-only and does not change the boot handoff contract. Boot handoff still reports `unsupported-pixel-format`; the separate kernel-side resource path now proves two operational output targets and controlled single-shot/bounded-live presentation without enabling a real-hardware multi-output compositor.

## Bare-Metal Display Target Inventory

The kernel now derives a read-only inventory of unique framebuffer-backed display candidates from the preserved BootInfo descriptors.

- Raw BootInfo descriptors stay intact for diagnostics, while duplicate and alias entries are excluded from the candidate list.
- The inventory bridges into the display model using disabled `DisplayMonitorDescriptor` candidates for anything that is not the preserved primary/selected framebuffer.
- Current QEMU `-vga std`, `-vga virtio`, and `-vga qxl` all expose `FramebufferCount=2`, but the deduplicated inventory reports `UniqueCount=1`, `ActiveRenderTargetCount=1`, and `DisabledCandidateCount=0`.
- Rendering remains primary-only in bare-metal mode.
- If later hardware proof finds additional distinct framebuffers, those extra candidates should remain disabled until a future risky phase explicitly promotes them into active render targets.

## Next Recommended Steps

- Keep the synthetic path isolated and continue using the existing smoke tests as the regression fence.
- Keep the diagnostic patterns and single-shot proof as explicit fallbacks while validating the bounded live path.
- Run manual interactive QEMU validation of the Display Options inventory, Extend/Mirror transaction, primary switch, and persistence across two launches.
- Add resolution and output-mode selection only with safe resource rebuild/resizing.
- Add virtio-gpu display events/config-change handling and hotplug after a separate safety review.
- Optimize whole-target transfers with dirty rectangles later.
- Keep head-aware absolute QMP routing explicitly unavailable until QEMU exposes a verified source-head path.
- Keep real hardware enablement marked as Mule Territory and preserve the fallback behavior contract.

## QEMU Input Routing Milestone (2026-07-13)

The input path was inventoried before changing the launcher. `scripts\run-qemu-display-probe.bat` uses `-machine q35,usb=off`, does not add a USB tablet or virtio-input device, and keeps the `virtio-gpu-pci,id=gpu0,max_outputs=2` GTK frontend. Therefore the current interactive guest path is `ps2-relative`: PS/2 packet deltas are accumulated against one global virtual cursor. The smoke records the installed QEMU version, graphical arguments, `gpu0`, head count, QMP port, and the result of QMP schema/device queries. QMP is only a deterministic test injector; the kernel does not depend on QMP.

`DisplayInputMapper` is backend-independent and consumes the runtime `DisplayMonitor` geometry. It keeps source head and monitor ID separate, maps head-local absolute coordinates through monitor virtual origins, scales normalized absolute ranges with signed 64-bit intermediates, and clamps the global cursor to the aggregate virtual desktop. For the current two `1280x800` monitors, head 0/local `100,200` maps to virtual `100,200`; head 1/local `100,200` maps to virtual `1380,200`. The dimensions and origins are read from descriptors, not hardcoded. Relative input remains global across the `x=1280` boundary. An absolute event without a source head uses the last active monitor or the primary monitor and emits an explicit fallback reason.

The QEMU-only proof adapter feeds the mapped events into the ordinary `KernelCompositor` hit-test, focus, title-bar drag, and capture state. It creates one bounded ordinary window titled `QEMU Input Proof`, draws a small software diagnostic cursor marker through the compositor, and does not use a virtio-gpu cursor queue or hardware cursor command. The bounded QMP sequence performs a head-0 click/focus attempt, then moves through several relative steps while dragging across the runtime boundary. The proof records the initial, boundary-crossing, and final window rectangles, pointer/active-monitor transitions, button-up cleanup, target captures, and live presentation counters.

The required result line is intentionally explicit about the capability boundary:

```text
Dual-monitor input proof: relativeGlobal=ok headAwareAbsolute=unavailable reason=guest-input-path-ps2-relative-no-source-head ...
```

If a future QEMU exposes source-head routing, the same mapper can consume head-local or normalized absolute events and report the corresponding `sourceHead=0/1`, `monitor=1/2`, local, and virtual coordinates. If head-1 routing cannot be established, the smoke retains the relative-global result and reports the precise missing capability; it does not claim a head-aware absolute proof.

Presentation diagnostics now separate `presentationPolls`, `eligibleAttempts`, `boundedProofIterations`, `renderedFrames`, `cleanSkips`, and `rateLimitSkips`. `rateLimitSkips` counts scheduler polls rejected by the hard rate interval and can exceed the bounded proof iteration count; the working 10 FPS cap and 60-attempt bound are unchanged.

Runtime capture evidence is stored under the timestamped QEMU run root, for example:

```text
logs\qemu-display-probe-<timestamp>\virtio-gpu-compositorInputBounded\captures\initial-head0.png
logs\qemu-display-probe-<timestamp>\virtio-gpu-compositorInputBounded\captures\input-boundary-head0.png
logs\qemu-display-probe-<timestamp>\virtio-gpu-compositorInputBounded\captures\input-after-drag-head1.png
```

The remaining limitations are fixed assigned output dimensions, no arbitrary
mode setting, no display hotplug/config-change path, the current PS/2-relative
QEMU path with no head-aware absolute source identity, no real-hardware
GPU/MMIO path, no virtio-gpu cursor queue, no hardware cursor commands, and no
unrestricted production enablement. The next roadmap milestone is resolution
and output-mode selection, safe resource rebuild/resizing, virtio-gpu display
events/config-change handling, hotplug, dirty-rectangle presentation, manual
interactive QEMU validation, and eventual real-hardware backend planning under
a separate Mule checkpoint. Real-hardware GPU enablement remains Mule
Territory.

## Current Next Milestone

The typed control-plane milestone is complete in-process. The next milestone is:

- persistence restoration across two separate QEMU launches;
- manual interactive Display Options proof;
- later resource resize/resolution design;
- configuration-change events and hotplug;
- dirty-rectangle optimization.

Separate-launch restoration is deliberately deferred from the current runtime
proof. Resolution changes, resource resizing, display hotplug, EDID, rotation,
cursor queues, 3D/virgl/Venus/blob/context commands, QMP guest IPC, and
real-hardware GPU/MMIO enablement remain out of scope. The Mule Territory
warning remains required near every active service/backend path.

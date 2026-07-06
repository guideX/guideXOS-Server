# guideXOS Server v0.2 Hosted Synthetic Multi-Monitor Experiment

Validated milestone closeout for `2026-07-05`. This note records what the hosted synthetic multi-monitor experiment proves today. It does not add new display behavior.

## Purpose

- Prove the hosted compositor and display model can represent a synthetic second monitor without changing the default hosted path.
- Validate virtual desktop bounds, viewport selection, render-target accounting, taskbar routing, and input mapping in a controlled hosted experiment.
- Keep real hardware multi-output support intentionally out of scope for this pass.

## Gates

- `GXOS_SYNTHETIC_DUAL_MONITOR=1` enables the synthetic dual-monitor desktop model.
- `GXOS_SYNTHETIC_DUAL_WINDOW_OUTPUT=1` enables hosted dual-window output only when the first gate is also on.
- Normal hosted mode remains the default when both gates are unset.

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
| `git diff --check` | The working tree is free of whitespace and patch-format issues. |

The hosted runtime smoke restores `desktop.json` and `desktop.state` and removes `display-options.cfg` as part of its cleanup, so validation does not leave those runtime state files dirty.

## Current Architecture Terms

- `DisplayMonitor` here means the code-level `DisplayMonitorDescriptor` concept: one monitor record with virtual coordinates, size, enabled/primary flags, and virtual bounds.
- `DisplayViewport` is the active hosted view into the virtual desktop; it tracks the active origin, size, synthetic-hosted state, and local-to-virtual coordinate conversion.
- `DisplayRenderTarget` is one renderable target entry; it records the target index, monitor mapping, viewport origin, and whether the target is backed by a hosted framebuffer.
- `virtual desktop bounds` are the aggregate rectangle reported by `DisplayVirtualDesktop::left`, `top`, `right`, `bottom`, `width()`, and `height()`.
- `backed render target` means the target has a hosted framebuffer backing.
- `conceptual render target` means the target exists in the model and diagnostics even when it is not backed in single-window synthetic-camera mode.

## Intentionally Deferred

- Hardware multi-output
- QEMU/virtio/QXL/GOP multi-output
- Per-monitor taskbars
- Per-monitor wallpaper
- VNC/video multi-target capture
- Cursor warping and advanced focus semantics
- Real hotplug, refresh rate, rotation, and DPI behavior

## Known Limitations

- This is a hosted synthetic experiment, not real hardware multi-monitor support.
- QEMU multi-output is not implemented or validated here.
- The second hosted window is still synthetic, not a hardware-backed second display.
- Default hosted mode stays single-output unless the gates are enabled.

## QEMU / Bare-Metal Output Investigation

This phase is diagnostic-only. It records what the current boot path can safely see before any real multi-output rendering work begins.

### What guideXOS currently detects

- UEFI x86_64 boots through the bootloader's `LocateProtocol` lookup for GOP, then copies one selected framebuffer into `BootInfo`.
- The bootloader now logs how many GOP handles UEFI exposes, but `BootInfo` still carries only one selected framebuffer descriptor.
- BIOS / Multiboot x86 and amd64 boots carry a single framebuffer via the Multiboot info block.
- The kernel's bare-metal compositor bridge still binds one framebuffer to one `DisplayRenderTarget`.
- The hosted display model can already represent multiple monitors, but that is currently a hosted synthetic construct, not a real hardware handoff.

### What QEMU can be asked to expose

| Backend | Diagnostic expectation | Guest-side implication |
| --- | --- | --- |
| `-vga std` | Legacy VGA / Bochs-style linear framebuffer | One framebuffer, no real multi-output. This is the current stable probe path. |
| `-device virtio-gpu-pci,max_outputs=2` | Multi-output-capable virtual GPU | QEMU can expose multiple outputs, but guideXOS still needs a multi-framebuffer handoff and a guest driver or GOP-aware bridge. |
| `-device virtio-vga` | VGA-compatible virtio GPU variant | Similar driver requirements to virtio-gpu; still not usable for real multi-output rendering without guest support. |
| `-device qxl-vga` with SPICE | Remote-display-oriented virtual GPU | Useful for SPICE/QXL experiments, but it still requires guest support that guideXOS does not have yet. |

The new probe launchers are:

- `scripts\run-qemu-display-probe.bat`
- `scripts\run-qemu-multimonitor-probe.bat`

### What guideXOS cannot use yet

- Multiple framebuffer descriptors are not part of the current boot handoff contract.
- The kernel does not yet consume an array of framebuffer targets from the boot path.
- The compositor does not yet render to more than one real framebuffer in bare-metal mode.
- There is no virtio-gpu or QXL guest driver in this pass.

### Next required implementation step

Extend the boot handoff so the kernel can receive a list of framebuffer descriptors, then teach the bare-metal compositor path to log and hold disabled diagnostic targets before enabling any real multi-output rendering.

## Next Recommended Steps

- Keep the synthetic path isolated and continue using the existing smoke tests as the regression fence.
- Treat real hardware multi-output and QEMU backend work as a separate capability-probe effort.
- Add backend-specific validation only after a real multi-output implementation exists, so the current contract stays stable.

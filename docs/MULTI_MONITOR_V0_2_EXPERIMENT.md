# guideXOS Server v0.2 Hosted Synthetic Multi-Monitor Experiment

Validated milestone closeout for `2026-07-07`. This note records what the hosted synthetic multi-monitor experiment proves today. It does not add new display behavior.

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
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-qemu-display-probe.ps1` | The QEMU probe boots headlessly, captures serial output to a deterministic log, and asserts the framebuffer-array and deduplication evidence for the standard `-vga std` path. |
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

### Observed Backend Matrix

| Backend | QEMU args | Booted? | GOP handles | Raw descriptors | Unique candidates | Result | Interpretation |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| `std` | `-vga std` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Two GOP handles alias the same framebuffer memory. Descriptor `1` is not a second monitor. |
| `virtio-gpu` | `-vga none -device virtio-gpu-pci,max_outputs=2` | yes | 2 | 0 | 0 | `BootInfo array disabled`, kernel `FramebufferCount=0` | The bootloader rejects the selected GOP framebuffer with `unsupported-pixel-format` after a `1280x800` snapshot with `base=0`, `pitch=0`, `bpp=0`, `format=Unknown`. |
| `virtio-vga` | `-vga virtio` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Same alias pattern as `-vga std`; no additional unique framebuffer candidate. |
| `qxl-vga` | `-vga qxl -spice addr=127.0.0.1,port=5930,disable-ticketing=on` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | QXL/SPICE is usable as a diagnostic probe, but it still collapses to one unique framebuffer candidate through GOP. |

Fresh backend evidence is captured in `logs\qemu-display-probe-20260709-055854\qemu-display-probe.evidence.txt`.

Current takeaways:

- `std`, `virtio-vga`, and `qxl-vga` all expose two GOP handles, but only one unique framebuffer candidate. The second handle is a duplicate alias, not a second monitor.
- `virtio-gpu-pci,max_outputs=2` still does not produce a usable multi-framebuffer handoff through the current UEFI/GOP path.
- The bootloader reaches the kernel on the virtio-gpu path, so this is not a serial-capture timing problem and not a boot-failed-before-handoff problem. It is a firmware/GOP selection issue on the current boot path.
- Hardware multi-output remains unimplemented in guideXOS. Rendering is still primary-only.

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
- There is no virtio-gpu or QXL guest driver in this pass.

### Next required implementation step

Keep the diagnostic framebuffer array flowing through the boot path, then teach the bare-metal compositor bridge to materialize disabled secondary targets for inspection before any real multi-output rendering is enabled.

## Framebuffer Array Handoff

The bootloader and kernel now preserve a bounded diagnostic framebuffer array in `BootInfo`, but the legacy single-framebuffer fields remain the authoritative rendering contract.

- `FramebufferBase`, `FramebufferSize`, `FramebufferWidth`, `FramebufferHeight`, `FramebufferPitch`, and `FramebufferFormat` still describe the primary framebuffer that the renderer uses today.
- `FramebufferCount` is the raw GOP descriptor count, `FramebufferUniqueCount` is the deduplicated physical framebuffer count, `FramebufferDuplicateCount` counts exact aliases, and `FramebufferSuspiciousCount` records same-base/same-size collisions with mismatched geometry or format.
- `FramebufferDescriptors[]` preserve the raw GOP descriptors so the boot path can classify aliases without changing rendering behavior.
- UEFI GOP can populate more than one descriptor if firmware or QEMU exposes multiple usable handles.
- BIOS / Multiboot remains a single-descriptor handoff with `FramebufferCount = 1`.
- Secondary framebuffer rendering stays deferred until the bare-metal compositor grows explicit multi-target present support.
- Deduplication prevents guideXOS from treating aliased GOP handles as separate monitors.
- The optional `virtio-gpu` comparison path is still diagnostic-only; it currently reports `unsupported-pixel-format`, `BootInfo array disabled`, and a kernel `FramebufferCount=0` path. It does not add a supported multi-output render path.

## Bare-Metal Display Target Inventory

The kernel now derives a read-only inventory of unique framebuffer-backed display candidates from the preserved BootInfo descriptors.

- Raw BootInfo descriptors stay intact for diagnostics, while duplicate and alias entries are excluded from the candidate list.
- The inventory bridges into the display model using disabled `DisplayMonitorDescriptor` candidates for anything that is not the preserved primary/selected framebuffer.
- Current QEMU `-vga std`, `-vga virtio`, and `-vga qxl` all expose `FramebufferCount=2`, but the deduplicated inventory reports `UniqueCount=1`, `ActiveRenderTargetCount=1`, and `DisabledCandidateCount=0`.
- Rendering remains primary-only in bare-metal mode.
- If later hardware proof finds additional distinct framebuffers, those extra candidates should remain disabled until a future risky phase explicitly promotes them into active render targets.

## Next Recommended Steps

- Keep the synthetic path isolated and continue using the existing smoke tests as the regression fence.
- Treat real hardware multi-output and QEMU backend work as a separate capability-probe effort.
- Add backend-specific validation only after a real multi-output implementation exists, so the current contract stays stable.

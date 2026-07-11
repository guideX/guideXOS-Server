# guideXOS Server v0.2 Dual-Monitor Implementation Track

Validated milestone closeout for `2026-07-10`. This note now tracks the implementation path toward fully functional v0.2 dual-monitor support. Historical "experiment" wording remains only where it describes earlier validation phases.

## Purpose

- Prove the hosted compositor and display model can represent a synthetic second monitor without changing the default hosted path.
- Validate virtual desktop bounds, viewport selection, render-target accounting, taskbar routing, and input mapping in a controlled hosted validation lane.
- Keep the current implementation track focused on QEMU virtio-gpu bring-up rather than bare-metal GPU programming.

## Safety Boundary

- QEMU virtio-gpu work is allowed in this branch, but only through the x86_64 diagnostic probe gate.
- Real bare-metal Intel/onboard GPU work is not allowed yet.
- REAL HARDWARE GPU/MMIO ENABLEMENT IS MULE TERRITORY AND REQUIRES A SEPARATE SAFETY CHECKPOINT.
- Real hardware GPU mode-setting, MMIO probing, scanout enabling, and display register programming remain out of scope for this pass.

## Implementation Roadmap

1. Hosted synthetic validation complete.
1. Hosted two-window synthetic output complete.
1. QEMU GOP/backend diagnostics complete.
1. virtio-gpu discovery/capability walk complete.
1. Runtime MMIO mapping complete in the QEMU-only x86_64 probe build.
1. VirtIO common config access next.
1. Controlled feature negotiation next.
1. Virtqueue setup next.
1. GET_DISPLAY_INFO next.
1. Resource, backing, scanout, transfer, and flush support.
1. Single virtio-gpu output.
1. Dual virtio-gpu scanouts.
1. DisplayRenderTarget integration.
1. Real hardware and Mule Territory later.

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
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-mmio-mapping.ps1` | The generic MMIO mapping API contract, safety flags, and virtio-gpu probe wiring stay aligned with the runtime mapping prerequisite. |
| `powershell -ExecutionPolicy Bypass -File scripts\smoke-virtio-gpu-diagnostic-source.ps1` | The virtio-gpu probe source keeps the full capability walk, the `cfg_type=0x05` diagnostic-only treatment, the modern-only QEMU gate, the safe-MMIO stop, and the no-rendering constraint. |
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

## Current Paging Inventory

The runtime MMIO work is starting from the existing huge-page / page-table scaffolding, not from a blank slate.

- `kernel/core/include/kernel/hugepages.h` defines the current x86-64 and ARM64 page-table flag vocabulary, including writable, user, cache-disable, write-through, write-combining, and no-execute bits.
- `kernel/core/hugepages.cpp` already contains the huge-page-oriented helpers: `map2M`, `map1G`, `unmap2M`, `unmap1G`, `make_pde_2m`, `make_pdpe_1g`, `split_2m_to_4k`, and `split_1g_to_2m`.
- Those helpers establish the shape of the eventual mapping path, and the current QEMU-only runtime MMIO window now layers on top of that scaffolding.
- The active x86_64 probe build uses a reserved high-half MMIO window with UC-style `PCD|PWT` leaf mappings, `NX`, supervisor-only permissions, and a bounded bump allocator.
- PAT/MTRR cache-attribute plumbing is still unresolved globally, so the safe current phase uses the UC-compatible `PCD|PWT` path instead of mapping PCI MMIO as ordinary cacheable RAM.
- `kernel::mmio::mapForDevice` now installs the QEMU-only transport mappings and stops before any transport write path if it cannot prove a safe MMIO window.

## QEMU / Bare-Metal Output Investigation

This phase is diagnostic-only. It records what the current boot path can safely see before any real multi-output rendering work begins.

### Observed Backend Matrix

| Backend | QEMU args | Booted? | GOP handles | Raw descriptors | Unique candidates | Result | Interpretation |
| --- | --- | --- | ---: | ---: | ---: | --- | --- |
| `std` | `-vga std` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Two GOP handles alias the same framebuffer memory. Descriptor `1` is not a second monitor. |
| `virtio-gpu` | `-vga none -device virtio-gpu-pci,max_outputs=2` | yes | 2 | 0 | 0 | `BootInfo array disabled`, kernel `FramebufferCount=0` | The bootloader rejects the selected GOP framebuffer with `unsupported-pixel-format` after a `1280x800` snapshot with `base=0`, `pitch=0`, `bpp=0`, `format=Unknown`. |
| `virtio-vga` | `-vga virtio` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | Same alias pattern as `-vga std`; no additional unique framebuffer candidate. |
| `qxl-vga` | `-vga qxl -spice addr=127.0.0.1,port=5930,disable-ticketing=on` | yes | 2 | 2 | 1 | `FramebufferCount=2`, `DuplicateFramebufferCount=1` | QXL/SPICE is usable as a diagnostic probe, but it still collapses to one unique framebuffer candidate through GOP. |

Fresh backend evidence is captured in `logs\qemu-display-probe-20260709-211742\qemu-display-probe.evidence.txt`.

Current takeaways:

- `std`, `virtio-vga`, and `qxl-vga` all expose two GOP handles, but only one unique framebuffer candidate. The second handle is a duplicate alias, not a second monitor.
- `virtio-gpu-pci,max_outputs=2` and the optional modern-only `disable-legacy=on` variant now both expose the modern VirtIO PCI capability set, and the probe now maps the transport window read-only before stopping at the transport-mapping milestone.
- `virtio-vga` still resolves the same capability set, so the transport mapping is now the first diagnostic boundary rather than a one-off command-line quirk.
- The full capability walk now logs every vendor-specific VirtIO capability, including `cfg_type=0x05`, and it keeps scanning for common, notify, ISR, and device config capabilities even when the PCI config capability is malformed.
- The bootloader still reaches the kernel on the virtio-gpu path, so this is not a boot-before-handoff problem. The current boundary is the read-only transport probe, not GOP discovery.
- Hardware multi-output remains unimplemented in guideXOS. Rendering is still primary-only and remains disabled in this probe pass.

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
- There is now a diagnostic-only virtio-gpu transport probe with a mapped MMIO window, but there is still no rendering-capable virtio-gpu or QXL guest driver in this pass.

### Next required implementation step

Keep the diagnostic framebuffer array flowing through the boot path, then teach the bare-metal compositor bridge to materialize disabled secondary targets for inspection before any real multi-output rendering is enabled.

## Virtio-GPU Driver Investigation

Diagnostic-only probe results from `logs\qemu-display-probe-20260709-211742\virtio-gpu\serial.log` and `logs\qemu-display-probe-20260709-211742\virtio-gpu-modern-only\serial.log`:

| Item | Result |
| --- | --- |
| PCI discovery | Success. The probe found a virtio-gpu PCI function at `00:02.00` with `vendor=0x1AF4`, `device=0x1050`, `subsystem=0x1AF4:1100`, `class=0x03`, `subclass=0x80`. |
| Capability walk | Success. The full PCI capability list walk logs 6 caps, 5 vendor-specific caps, and every vendor-specific VirtIO capability. `cfg_type=0x01` common, `0x02` notify, `0x03` ISR, and `0x04` device are resolved. `cfg_type=0x05` is diagnostic evidence only and is not treated as success by itself. On `virtio-gpu` and `virtio-gpu-modern-only`, the PCI config capability is malformed because BAR0 is unassigned. |
| Modern transport | Detected and mapped in the QEMU-only probe build. The common config BAR resolves to `0x000000C000000000`, the transport MMIO window is mapped into the reserved kernel range, and the probe stops after read-only sanity reads. |
| Feature negotiation | Not reached. |
| Control queue | Not reached. |
| GET_DISPLAY_INFO | Not queried. No control queue was laid out and no `GET_DISPLAY_INFO` request was issued. |
| Scanouts reported | `n/a` for this run, because the safe MMIO gate stopped the probe before any queue writes. |
| Modern-only QEMU mode | Supported locally. The smoke successfully launches `-device virtio-gpu-pci,max_outputs=2,disable-legacy=on`. |
| Current interpretation | QEMU exposes a modern virtio-gpu PCI candidate and the full modern capability set, and guideXOS now proves a safe read-only runtime MMIO transport mapping before any reset or feature negotiation. Rendering remains disabled. |

What remains before rendering can happen:

- Keep the modern probe read-only until the MMIO mapping story is understood.
- Do not enable resource creation, backing attachment, scanout selection, transfers, flushes, or rendering in this branch.
- Continue using the diagnostic-safe stop as the boundary until a later pass can prove a safe mapped path.

## Virtio-GPU MMIO Mapping Milestone

The virtio-gpu discovery path now maps the modern transport MMIO regions into a reserved kernel window and stops at a read-only sanity milestone.

- PCI discovery still finds the QEMU virtio-gpu function at `00:02.00`.
- The modern VirtIO capability walk still resolves `common`, `notify`, `isr`, and `device` capabilities.
- The `pci` capability remains malformed in the current QEMU setup because BAR0 is unassigned, and it is still not treated as a transport region.
- The active x86_64 probe build gates runtime mapping behind `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE`.
- `kernel::mmio::mapForDevice` now installs the common, notify, ISR, and device config regions into the reserved kernel MMIO window using supervisor-only, non-executable, UC-style `PCD|PWT` leaf mappings.
- The current cache policy avoids ordinary write-back RAM semantics and does not touch global MTRRs.
- Read-only sanity reads are limited to observational modern VirtIO fields only: `num_queues`, `device_status`, `config_generation`, `numScanouts`, and `numCapsets`.
- The probe stops before any transport reset, feature negotiation, queue setup, or MMIO writes.
- `GET_DISPLAY_INFO`, resource creation, backing attachment, scanout changes, transfers, and flushes remain disabled in this branch.
- Unmap currently clears only MMIO-owned leaf entries and retains the intermediate page-table pages for now.

## Runtime MMIO Window

- Base: `0xFFFFC00000000000`
- Size: `16 MiB`
- Allocation strategy: a bounded bump allocator over the reserved high-half window, with per-page conflict checks and no automatic recycling yet.
- Architecture gate: x86_64 QEMU probe builds only.
- Build gate: `GXOS_QEMU_VIRTIO_GPU_PROBE_ACTIVE`
- Page-table flags: present, supervisor-only, writable leaf, `NX`, `PCD`, and `PWT`
- Cache strategy: UC-compatible `PCD|PWT`, not write-back
- Unmap limitation: only MMIO-owned PTEs are cleared; shared higher-level paging structures are retained

### Next Intended Phase

- Controlled feature negotiation.
- Controlled virtqueue setup.
- Read-only transport validation stays in place until the next checkpoint proves the command path is still safe.

## Framebuffer Array Handoff

The bootloader and kernel now preserve a bounded diagnostic framebuffer array in `BootInfo`, but the legacy single-framebuffer fields remain the authoritative rendering contract.

- `FramebufferBase`, `FramebufferSize`, `FramebufferWidth`, `FramebufferHeight`, `FramebufferPitch`, and `FramebufferFormat` still describe the primary framebuffer that the renderer uses today.
- `FramebufferCount` is the raw GOP descriptor count, `FramebufferUniqueCount` is the deduplicated physical framebuffer count, `FramebufferDuplicateCount` counts exact aliases, and `FramebufferSuspiciousCount` records same-base/same-size collisions with mismatched geometry or format.
- `FramebufferDescriptors[]` preserve the raw GOP descriptors so the boot path can classify aliases without changing rendering behavior.
- UEFI GOP can populate more than one descriptor if firmware or QEMU exposes multiple usable handles.
- BIOS / Multiboot remains a single-descriptor handoff with `FramebufferCount = 1`.
- Secondary framebuffer rendering stays deferred until the bare-metal compositor grows explicit multi-target present support.
- Deduplication prevents guideXOS from treating aliased GOP handles as separate monitors.
- The optional `virtio-gpu` comparison path is still diagnostic-only; boot handoff still reports `unsupported-pixel-format`, and the kernel-side probe now proves the reserved MMIO transport window without enabling rendering. It does not add a supported multi-output render path.

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

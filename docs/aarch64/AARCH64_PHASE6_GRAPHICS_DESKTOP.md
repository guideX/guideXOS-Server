# guideXOS Server AArch64 Phase 6 — Common Graphics and First Desktop

**Repository:** `D:\dev\guideXOSServer_AARCH64`
**Branch:** `AARCH64_SUPPORT`
**Phase:** AARCH64-6
**Scope:** display output only; keyboard, mouse, USB, Navigator, networking,
and interactive application windows remain deferred to later phases.

## Result

Outcome A is established by the Phase-6 harness. A fresh AArch64 UEFI boot
acquires the firmware GOP surface, maps it in the ARM64 MMU, initializes the
common framebuffer and drawing primitives, loads the normal wallpaper resource
through VFS, initializes the common compositor, renders the production/common
guideXOS desktop path, verifies the resulting pixels, runs the Phase-5
NativeElf proof, and completes the existing preemptive scheduler workload.

The final marker is emitted only after all of those checks complete:

```text
AARCH64_PHASE6_PASS
```

## Existing graphics path audited

The AMD64/common path was traced before adding the ARM64 integration:

| Layer | Existing implementation | Phase-6 use |
|---|---|---|
| UEFI GOP | `guideXOSBootLoader/main.cpp`, `MapGopPixelFormat`, `CaptureGopFramebuffer` | The AArch64 loader uses the same GOP data model and performs the firmware calls before EBS. |
| Boot framebuffer data | `guideXOSBootLoader/guidexOSBootInfo.h` and common `BootInfo` conventions | `aarch64/phase6/phase6_contract.h` carries fixed-width GOP fields; `CommonBootInfo::framebuffer` is populated after EBS. |
| Framebuffer abstraction | `kernel/core/framebuffer.cpp` / `kernel/core/include/kernel/framebuffer.h` | ARM64 calls `init_from_common_bootinfo`; the renderer has no ARM64 branch. |
| Primitive drawing | `fill_rect`, `draw_line`, `blit`, `blit_alpha`, clipping, double buffering, `present` | Reused. The portable scalar path is selected on ARM64. |
| Font/text | `kernel/core/system_font.cpp`, `desktop_font.cpp`, and `kernel_compositor.cpp` | Reused embedded Roboto atlas and the common glyph path. |
| Compositor | `kernel/core/kernel_compositor.cpp` | `KernelCompositor::init`, `TaskbarManager`, and common draw calls are reused. |
| Desktop/theme | `kernel/core/desktop.cpp` | `desktop::init`, `desktop::draw`, wallpaper selection, icons, taskbar, Start button, and branding are reused. |
| Resource path | `kernel/core/ramdisk.cpp`, `vfs.cpp`, FAT32, and desktop GXIMG loader | The boot ramdisk is mounted at `/`; the same image is aliased at `/system` for the normal wallpaper paths. |

The AMD64-only edges found in the audit are framebuffer acquisition through
the old AMD64 BootInfo shape, x86 VESA/BGA/PCI fallback code, PIC/port-I/O
diagnostic inventory, and unrelated input/network/audio/shell services. Those
remain outside the ARM64 graphics path. The Phase-6 image provides explicit
no-device platform stubs for those optional desktop dependencies; it does not
replace the common renderer.

## GOP and QEMU configuration

The installed environment was tested rather than assumed:

```text
QEMU: C:\Program Files\qemu\qemu-system-aarch64.exe
Version: 11.0.0 (v11.0.0-12122-ga4bb4b10c9)
UEFI code: C:\Program Files\qemu\share\edk2-aarch64-code.fd
UEFI vars: C:\Program Files\qemu\share\edk2-arm-vars.fd
Machine: virt,gic-version=2,acpi=off
CPU: cortex-a53
Memory: 512M
Display device: ramfb
Serial: file capture from the PL011-backed guest console
```

The reproducible display portion is:

```text
-device ramfb -display none -monitor none -serial file:<log>
```

`ramfb` lets the installed AArch64 UEFI firmware expose a persistent linear
GOP framebuffer while serial capture remains enabled. The installed
`virtio-gpu-pci` device was also investigated: this firmware reported GOP
`PixelBltOnly` (`raw format=0x00000003`) with zero masks, so it was correctly
rejected because it did not provide a usable linear framebuffer. No kernel
virtio-gpu, DRM/KMS, acceleration, or GPU DMA driver was added.

The loader captures all GOP values before `ExitBootServices`. No kernel code
calls UEFI after EBS.

## Framebuffer handoff and MMU mapping

The selected firmware/QEMU combination reported the following on each fresh
boot:

| Field | Value |
|---|---:|
| Physical base | `0x5c7a0000` |
| Firmware framebuffer size | `0x300000` bytes |
| Width × height | `800 × 600` |
| Pixels per scan line / pitch | `3200` bytes |
| Bits per pixel | `32` |
| Raw GOP format | `PixelBlueGreenRedReserved8BitPerColor` (`0x1`) |
| Common normalized format | `B8G8R8A8` / format `2` |
| Required visible bytes | `3200 × 600 = 0x1d4c00` |

The loader validates nonzero geometry, `scanlines >= width`, checked
`scanlines × 4` and `pitch × height` arithmetic, pitch narrowing, the
firmware-reported framebuffer size, and checked `base + required-size`.
`CommonFramebufferInfo` repeats the geometry contract after EBS and rejects
overflow, undersized pitch/area, unknown format, and invalid address ranges.

The Phase-6 ARM64 MMU builder maps the complete firmware framebuffer range
after page rounding. It first creates the normal RAM identity mapping, then
overrides the framebuffer pages with the framebuffer attribute, and rejects
overlap with the kernel, PL011 UART, and GIC ranges. The framebuffer mapping
uses MAIR Attr2 `0x44`: Normal memory, inner/outer non-cacheable, with inner
shareability, access flag, PXN, and UXN. It is deliberately not ordinary
WBWA RAM. The existing kernel WXN and 4 KiB table policy remain enabled.

## Common rendering proof

Before the compositor runs, the common framebuffer abstraction writes and
reads three known pixels: red at the top-left, green at the bottom-right valid
pixel, and blue at the center. This proves vertical addressing, the selected
pitch, and channel normalization through the common interface. The boot then
emits:

```text
[guideXOS] GOP framebuffer: OK
[guideXOS] framebuffer mapping: OK
[guideXOS] pixel format: OK
```

The desktop uses the existing static ARGB back buffer at 800×600 and the
common `present()` conversion into the BGRX GOP surface. No ARM64-specific
drawing primitives were introduced and no NEON optimization is required.
Clipping and geometry guards cover overflowed rectangles, fully/partially
off-screen rectangles, pitch/area overflow, and invalid framebuffer ranges.

Text uses the existing embedded Roboto atlas and common desktop/compositor
glyph functions. Phase 6 enables the normal desktop branding flag after
`desktop::init()` so the rendered frame identifies guideXOS; it does not add a
Phase-specific bitmap font or screenshot renderer.

## Compositor, theme, and resources

The graphics task executes the normal desktop path:

1. Mount the existing boot ramdisk at `/` and alias it at `/system` through
   the common block/VFS layer.
2. Read `/system/wall/blueflwr.gxi` through VFS and validate the resource
   header bytes.
3. Call `desktop::init()`, which initializes theme defaults, wallpaper
   selection, desktop icon positions, taskbar layout, common IPC state,
   double buffering, `KernelCompositor`, and `TaskbarManager`.
4. Call `desktop::draw()`, which runs the normal background/wallpaper,
   desktop-icon, taskbar, notification, Start-button, compositor-window, and
   final `present()` path.

The normal wallpaper registry selects `legacy_blue_flower` and loads the
GXIMG resource from `/system/wall/blueflwr.gxi`. The image is staged from the
existing `out\wallpaper-pack` through the Phase-6 FAT32 ramdisk; no large
wallpaper or icon blob is embedded in the kernel. The frame includes the
guideXOS desktop background/wallpaper path, taskbar, Start button, standard
desktop icon path, text/branding, and the static/default no-input state.

The shell, kernel-app registration, and input-driven event pump are not
required for this static proof. The ARM64 Phase-6 build omits those optional
services at the desktop integration boundary while retaining the common
desktop/compositor implementation. Hardware cursor movement, keyboard, mouse,
click routing, and Start Menu interaction are explicitly deferred.

## Deterministic framebuffer verification

`kernel::framebuffer::verification_hash()` computes FNV-1a 64 over every
normalized ARGB pixel in the bounded full visible region `(0,0,800,600)`.
The hash reads through the common front-buffer accessor, so it validates both
desktop output and the BGRX channel conversion rather than hashing the back
buffer alone.

Expected value for the pinned QEMU/UEFI/resource set:

```text
region: full visible 800 × 600 frame
algorithm: FNV-1a 64 over normalized ARGB, four bytes per pixel
checksum: 0xcd1073981e16c449
```

The Phase-6 test script treats a changed value as a deliberate regression and
requires all three fresh boots to agree with the expected value. This is in
addition to the known-pixel sanity proof.

## Scheduler and durability

Rendering executes as scheduler task 6 while the existing Phase-3/4/5 worker,
VFS, and NativeElf tasks remain active. Timer preemption stays enabled; no
global interrupt masking surrounds a frame. The configured scheduler minimum is
10,000 preemptions; because completion is gated on all graphics, VFS, and
application postconditions, the final boots ran beyond that minimum and report
their measured statistics below.

The graphics task redraws the desktop 96 times after the first frame and
requires the final full-frame hash to equal the first hash. The common heap,
surface/compositor state, clipping, and wallpaper cache therefore survive a
bounded redraw workload without a detected allocation delta or render
corruption.

The Phase-6 completion handoff is held until the graphics task, the Phase-5
App Model task, and the mixed VFS task have all reported their postconditions.
This preserves the common scheduler's configured preemption target while
preventing a slow render from returning to bootstrap before the desktop proof
is complete.

The final three fresh boots reported:

```text
boot-1: redraws=96 ticks=17176 context-switches=27182 preemptions=17176 vfs-reads=226 vfs-enumerations=14 pages-used=112 heap-used=32
boot-2: redraws=96 ticks=17708 context-switches=27714 preemptions=17708 vfs-reads=208 vfs-enumerations=12 pages-used=112 heap-used=32
boot-3: redraws=96 ticks=15776 context-switches=25782 preemptions=15776 vfs-reads=18 vfs-enumerations=1 pages-used=112 heap-used=32
all boots: unexpected-irq=0 exceptions=0
```

The same boot runs the Phase-5 proof after graphics: `/Apps` discovery,
`EM_AARCH64` validation, `gx_main`, return value 42, cleanup, relaunch, wrong
architecture rejection, and 100-launch durability with
`allocator-delta-pages=0`.

## Negative controls

`tests/aarch64_phase6_host_tests.cpp` runs during every Phase-6 build and
covers:

- valid geometry;
- zero/undersized pitch;
- insufficient framebuffer area;
- unknown pixel format;
- framebuffer address-end overflow;
- partially clipped rectangles;
- fully outside rectangles; and
- huge coordinate/size overflow.

The loader and MMU path add the target-side checks for GOP arithmetic,
framebuffer/kernel/MMIO overlap, page-rounded mapping bounds, and unsupported
formats. Allocation failure remains bounded by the existing static back-buffer
capacity check.

## Tests and evidence

Repository-local commands:

```text
scripts\build-aarch64-phase6.ps1
scripts\run-aarch64-phase6.ps1
scripts\test-aarch64-phase6.ps1
```

`test-aarch64-phase6.ps1` builds and verifies the PE/ELF artifacts, runs the
host negative controls, stages the VFS/resources, performs three independent
fresh UEFI-variable boots with `ramfb`, validates ordered serial markers and
the exact framebuffer hash, checks fatal/error markers, and runs the Phase 1–5
historical suites serially. Generated logs, firmware variable copies, and
optional screenshot output live under ignored `out\aarch64-phase6`.

The three completed Phase-6 boots were:

```text
boot-1: PASS, hash=0xcd1073981e16c449
boot-2: PASS, hash=0xcd1073981e16c449
boot-3: PASS, hash=0xcd1073981e16c449
```

Historical regression results, run serially after the Phase-6 proof, were:

| Suite | Result | Evidence |
|---|---|---|
| Phase 1 | PASS | three fresh boots plus wrong-machine control |
| Phase 2 | PASS | three fresh boots plus host allocator/MMU controls |
| Phase 3 | PASS | three fresh boots plus host scheduler controls; 10,000 preemptions per boot |
| Phase 4 | PASS | three fresh boots plus host allocator/VFS controls |
| Phase 5 | PASS | three fresh boots plus App Model host controls and wrong-machine fixture |

The Phase-6 harness raises the unchanged Phase-3 and Phase-4 historical boot
bound to 240 seconds on this installed QEMU TCG build because those kernels
continue in `wfi` after printing their pass markers. It does not alter their
kernel sources or assertions.

The installed headless QEMU display surface did not produce a PPM through
the frame-triggered HMP request in the available `-display none`/GTK attempts.
This is a capture limitation, not a framebuffer validation gap: the firmware
GOP surface was mapped and the full normalized 800×600 frame hash passed. The
harness keeps the optional HMP capture path for environments whose graphical
backend supports `screendump`.

AMD64 validation is additive: the changed common framebuffer/header path was
compiled with MinGW AMD64 flags for `framebuffer.cpp`, `desktop.cpp`,
`system_font.cpp`, and `kernel_compositor.cpp`; the host geometry controls
also passed. `mingw32-make -C kernel ARCH=amd64 info` remains blocked before
compilation by the repository’s known incomplete/generated Mbed TLS profile
dependency. No AMD64 BootInfo or VESA path was removed or semantically
redirected.

## Remaining limitations and AARCH64-7 recommendation

Phase 6 intentionally does not add keyboard, mouse, USB HID, virtio-input,
touch, cursor movement from hardware input, click routing, interactive Start
Menu behavior, application windows, networking, Navigator, physical storage,
SMP, GICv3, EL0/userspace, or GPU acceleration.

Recommended exact AARCH64-7 scope: keep this `ramfb`/GOP desktop as the
regression baseline; add one selected QEMU input device and its smallest
validated interrupt/polling path; surface keyboard/mouse events through the
existing common input interfaces; render the existing static/default cursor;
then prove cursor movement, click routing for the Start button, and one safe
window drag with bounded input/event-queue and scheduler tests. Do not add
USB HID, virtio-input, or application-window requirements until that minimal
path is stable.

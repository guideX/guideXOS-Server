# AARCH64 RENDER1 — Native GUI host rendering foundation

**Outcome:** `RENDER_HOSTED_ACCEPTED`
**Qualification date:** 2026-11-02
**Repository:** `D:\dev\guideXOSServer_AARCH64`
**Branch:** `AARCH64_SUPPORT`
**Local implementation commit:** `3ba208cd2d651412ec357142cfab5044a1375f85`

This phase implements the existing AARCH64 SDK `draw_rect` and `draw_text`
callbacks. It qualifies the rendering services with hosted pixel tests and a
freestanding ARM64 kernel build. It does not qualify a Developer Studio package
or claim guest/board execution.

## Git and artifact preflight

The requested starting revision was `2a01312a09713dc0f6607a63a7d8c2d0e623049b`
on `AARCH64_SUPPORT`, tracking `origin/AARCH64_SUPPORT`. The phase preflight
recorded a clean tracked worktree and 483 pre-existing untracked files in 25
qualification directories below `out/`; those files and directories were
preserved. Commit `85f6179e90887460e23b1874d0eddcc1ed735782` was inspected; it
contains the SDK include-provenance guard and its report note, with no renderer
implementation.

Before this continuation completed, the branch advanced to
`3a16ef6672331fa151b21ccb77a3687d0ba9e0a4` (parent `2a01312a`, upstream
`origin/AARCH64_SUPPORT`). That upstream commit only adds two `.gitignore`
lines and was preserved. RENDER1 was committed on top of it. No branch switch,
reset, stash, cleanup, push, or external checkout modification was performed.
The separate Hypervisor, Developer Studio, and Server v0.5 repositories were
not modified.

All pre-existing `out/` artifacts were retained. New outputs are under
`out/aarch64-render1-*`; they are ignored by the current `/out/` rule and were
left in place, including failed exploratory attempts.

## Architecture and ownership

The inspected AARCH64 path is:

```text
Developer Studio / Native ELF
  -> gx_app_context.host->draw_rect / draw_text
  -> published AARCH64 callback
  -> runtime-owned HostBinding and generation-checked window handle
  -> Phase9GuiOwner SurfaceRegistry, one linear surface per application window
  -> Phase9GuiOwner::drawWindow for that exact KernelWindow
  -> framebuffer::put_pixel (active draw target and scanout-format conversion)
  -> desktop redraw and framebuffer::present
```

UEFI GOP framebuffer base, dimensions, byte pitch, 32-bit format, and size are
copied into `CommonBootInfo`. The ARM64 handoff validates this descriptor, and
`framebuffer::init_from_common_bootinfo` passes it through the common geometry
validator. The framebuffer module owns the active render-target abstraction;
the firmware-provided front buffer is not an application drawing surface. The
abstraction routes `put_pixel` to its active draw target and converts logical
`0xAARRGGBB` pixels for `R8G8B8A8` or `B8G8R8A8` scanout using the actual pitch.
When the desktop back buffer is enabled, `put_pixel` writes it and the existing
desktop `present()` copies/converts it to the front buffer. This implementation
does not write framebuffer memory directly.

There was no per-window Native ELF content surface in the AARCH64 Phase-9
owner. The compositor already owns window ordering, title bars, widgets, and
screen clipping, and it called the application owner with client-area geometry.
The implementation adds a window-aware `KernelApp::drawWindow` hook with a
default implementation that preserves existing `draw()` behavior. The Phase-9
owner overrides it to resolve the matching window's surface and copy only the
visible intersection into the compositor's active framebuffer target.

Each `Phase9GuiOwner` owns a `SurfaceRegistry` sized to the existing three
window quota. Each surface is an independent, tightly pitched array of logical
32-bit `0xAARRGGBB` pixels, initialized to opaque black, bounded to 16 MiB, and
surrounded by leading/trailing canary words. The callback resolves a fresh
surface view for each call and retains no surface pointer. A handle must match
the owning runtime, window slot, and generation. Window destruction releases
that surface and advances the generation; owner teardown releases any remaining
surfaces. The app's `userData` points to a runtime-owned `HostBinding` only for
the active call. It is marked inactive before window and runtime teardown.

Successful drawing marks the `KernelWindow` dirty and calls the existing
desktop redraw request. The desktop redraw draws windows in compositor order
and then uses its existing `framebuffer::present()` path. Thus application
surfaces remain private to their runtime/window while window movement, screen
clipping, and scanout conversion remain owned by existing infrastructure.

## Callback behavior

The genuine AARCH64 SDK signatures are unchanged:

```c
gx_result GX_CALL draw_text(gx_app_context*, gx_handle, int x, int y,
                            const char* text);
gx_result GX_CALL draw_rect(gx_app_context*, gx_handle, int x, int y,
                            int width, int height, uint32_t color);
```

`draw_rect` rejects missing/invalid contexts, unknown or stale handles, invalid
surfaces, and non-positive width or height with `GX_ERROR_INVALID_ARGUMENT`.
For positive dimensions, it computes the right and bottom edges in 64-bit
signed arithmetic, clips to the surface, and treats a fully clipped rectangle
as a successful no-op. Rectangles use half-open bounds. The SDK's `gx_color` is
used as `0xRRGGBB` (as in the SDK samples); the renderer masks to 24-bit RGB and
writes an opaque `0xFFRRGGBB` pixel. The SDK does not define alpha blending for
this callback.

`draw_text` uses the existing shared `gxos::gui::BitmapFont` 5x7 bitmap font.
The supplied `(x,y)` is the glyph's top-left pixel origin, not a baseline.
Visible set bits are opaque white; the cell advance is six pixels (five glyph
columns and one spacing column). ASCII bytes 32 through 126 have glyphs.
Unsupported bytes, including UTF-8 multibyte bytes, paint no glyph and advance
one six-pixel cell; general Unicode and UTF-8 decoding are not claimed. Empty
text succeeds without changing pixels. Null text and strings without a NUL
within the SDK's 256-byte maximum are rejected. Glyph coordinates use 64-bit
intermediates, and all writes are clipped to the surface.

The callback publisher requires a live `HostBinding`, matching context, host
table, runtime, owner, and resolver/invalidation functions before it assigns
either slot. It writes only `draw_text` and `draw_rect`. Production setup
publishes them after the context and owner are initialized; all pre-existing
Phase-9, Phase-11, and Phase-12 callback assignments remain in their existing
fields.

Compile-time assertions in the production renderer and hosted test verify the
genuine AARCH64 table: `draw_text` offset 32, `draw_rect` offset 40,
`guiVersion` offset 240, Build-start offset 288, final Run-release offset 392,
alignment 8, and total size 400 bytes. The tests also seed neighboring callback
fields and confirm publication leaves them intact. No v0.5 header or 464-byte
table is used.

## Hosted pixel evidence

The dedicated hosted runner compiles `kernel/core/aarch64_gui_render.cpp`
directly with the real AARCH64 SDK header and invokes the published functions
through `gx_host_calls`. It inspects output buffers from the production
renderer and production `SurfaceRegistry`; this is not a callback-count-only
test.

The final run passed **1,380 assertions, zero failures**, including:

- rectangle fills, single pixels, adjacency, overlap, exclusive edges,
  zero/negative dimensions, clipping at all four edges, fully clipped no-ops,
  large positive/negative coordinates, invalid stride/capacity, nontrivial
  pitch, canaries, and row padding;
- single and multiple ASCII glyphs, letters/digits, whitespace, empty and null
  strings, the maximum 255-byte terminated string, unterminated input,
  clipping on all edges, off-surface text, white glyph pixels, unchanged pixels
  outside glyph coverage, deterministic redraw, and canaries;
- callback publication rejection before context setup and success after
  context setup, pixel writes through the published slots, preservation of
  neighboring host fields, stale/foreign handle rejection, repaint
  invalidation, and independent application surfaces; and
- 100 create/draw/verify/destroy/inactivate/recreate/draw cycles through the
  published callbacks. Every cycle verifies the old handle is rejected and
  both old and replacement surfaces retain their guards.

The IDE-like sample was rendered through the published host callbacks to a
320x200 P6 PPM (RGB888). It contains the background, panel, title bar, button,
title, and three labels. Pixel probes, panel coverage, glyph footprint, padding
and canary checks pass. The full-buffer FNV-1a 64-bit hash is
`0x87a200866bd34051`; a second independently rendered buffer has the same hash,
and an untouched buffer has a different hash.

Artifact and result log:

- `D:\dev\guideXOSServer_AARCH64\out\aarch64-render1-host-final-retry1-20261102\developer-studio-render-demo.ppm`
- `D:\dev\guideXOSServer_AARCH64\out\aarch64-render1-host-final-retry1-20261102\render-host-results.txt`

## Build and regression qualification

| Gate | Command / output | Result |
| --- | --- | --- |
| Hosted renderer and IDE-like pixels | `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-render-host.ps1 -OutputDirectory .\out\aarch64-render1-host-final-retry1-20261102` | PASS; 1,380 assertions; 100/100 lifecycle cycles; 320x200 demo hash above |
| Phase-9 runtime and event lifecycle controls | `g++ -std=c++17 -Wall -Wextra -O2 -iquote . -iquote kernel/core/include -I sdk/include tests/aarch64_phase9_host_tests.cpp kernel/core/application_event_service.cpp kernel/core/application_runtime.cpp -o .\out\aarch64-render1-phase9-host-retry2-20261102\aarch64-phase9-host-tests.exe`, then run that executable | PASS; forged/stale/cross-app/wrong-kind handles, quotas, saturation, close guarantee, and duplicate cleanup |
| ARM64 freestanding foundation and Phase-10 controls | `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase10.ps1 -Phase12Proof -OutputDirectory .\out\aarch64-render1-arm64-publish-final-20261102` | PASS; all 12 build stages; kernel linked; Phase-10 resolver and native ABI controls pass; both package payload machine identities verified |
| GICv3 hosted regression | `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-gicv3-host.ps1 -OutputDirectory .\out\aarch64-render1-gicv3-20261102` | PASS; 62 assertions; no QEMU |
| AARCH64/v0.5 SDK alignment | `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase12-sdk-alignment.ps1 -OutputDirectory .\out\aarch64-render1-sdk-alignment-20261102` | PASS; AARCH64 400-byte and v0.5 464-byte layouts, negative incompatibility control, and v0.5 document activation 9/9 |
| Full Phase-12 package guard | `powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase12.ps1 -OutputDirectory .\out\aarch64-render1-phase12-guard-20261102` | Expected stop: `PHASE12_SDK_CONTRACT_BLOCKED`, before build or package write |

The final kernel artifact is
`out/aarch64-render1-arm64-publish-final-20261102/kernel.elf`, 51,718,208
bytes, SHA-256
`ebd732ea512789cd9d673caed729238976eec8072e07c3ccf48dd5cc27ef1289`.
The ARM64 UEFI loader in the same output is 11,264 bytes, SHA-256
`c6799231c67c594eb752183d611deaad67cd5ce9ef81c0c69e850e9cda7ba4ca`.
`llvm-objdump -f` reports `elf64-littleaarch64`, architecture `aarch64`, and
entry `0x40000000`. `llvm-nm -u` reports no undefined symbols. The linked image
contains the AARCH64 renderer callbacks and `SurfaceRegistry`; the build also
verifies the `EM_AARCH64` proof payload and the ARM64 UEFI loader. This is
freestanding build evidence only, not evidence of physical framebuffer
operation.

The previous ABI2 audit's App Model activation result (145/145) and lifecycle
result (100/100) came from the pinned Developer Studio hosted build in an
isolated output root. They were not rerun during RENDER1. The Phase-10 hosted
resolver/loading controls and Phase-9 common runtime controls above were run
here. The v0.5 document activation 9/9 and GICv3 62 assertions were rerun.
Guest tests are excluded; QEMU and physical hardware were not launched.

Early hosted-runner and manual compile attempts during test setup failed before
the final runs because of include-path resolution/shadowing, two incorrect
expected-pixel probes, and a test-source declaration-ordering mistake. The
include paths and expectations were corrected; the final named renderer and
ARM64 runs above passed with zero test failures. Those output directories
remain in `out/`; no existing qualification output was deleted or overwritten.

## Remaining limitations and next phase

This supplies only opaque RGB rectangles and fixed-font ASCII labels. It does
not add alpha blending, UTF-8/Unicode, antialiasing, image blits, a GPU surface,
or a new presentation API. Existing compositor widgets remain drawn above the
application surface. The SDK's other missing Debug/Run types, constants,
document activation callback, and 464-byte v0.5 contract conflict are still
separate ABI3 work. The full Developer Studio package remains blocked by the
preserved `PHASE12_SDK_CONTRACT_BLOCKED` guard.

Recommended next phase: ABI3 should adapt the pinned Developer Studio source
against the genuine 400-byte AARCH64 SDK, add only compatible APIs, and retain
the ABI layout and hosted rendering gates. Guest qualification should remain a
separately authorized phase.

## Commit and worktree closeout

The verified RENDER1 implementation and its qualification report landed in
`3ba208cd2d651412ec357142cfab5044a1375f85` on `AARCH64_SUPPORT`, based on the
preserved upstream `.gitignore` commit `3a16ef6672331fa151b21ccb77a3687d0ba9e0a4`.
At that implementation commit the tracked worktree was clean. A documentation-
only closeout commit records this SHA; the final branch HEAD is reported in the
turn closeout. Generated qualification outputs remain ignored under `out/` and
were preserved.

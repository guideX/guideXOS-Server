# C122 — NativeAOT managed Label / status text

## Purpose

C122 adds `GuideXosLabel`, a reusable bounded presentation-only control for
read-only single-line text. It is suitable for labels, paths, captions, and
short status text. The application owns the meaning and source of the value;
the label owns only stored presentation text, geometry, and visibility.

## Reusable API

The control is constructed as `(x, y, width, text, maximumTextLength)`, with
the final two arguments optional. Its public operations are:

- `SetText(string)` and `SetText(ReadOnlySpan<char>)`
- `Clear()`
- `SetVisible(bool)`
- `TrySetBounds(int x, int y, int width)`
- `Reset()`
- `Render(GuideXosSurface)`

`Text`, `Length`, `Visible`, `X`, `Y`, `Width`, `Height`, `RenderWidth`,
`MaximumTextLength`, and `RejectedInputCount` expose bounded state. There are
deliberately no `Focus`, `Blur`, pointer, key, character, activation, or
selection methods.

## Storage and bounds

Each instance owns one fixed `char[]`. The default and Managed Notes production
limit is 48 printable-ASCII characters; the supported per-instance limit is 56
characters. Empty text is valid. Text is validated completely before copying,
so a null, control character, non-ASCII character, or over-capacity update is
rejected without changing the previous value. `Clear()` is an accepted empty
replacement, and a later `SetText` safely starts at position zero.

Geometry is explicit and deterministic. X and Y are bounded to 0–4095. Width
is a pixel width from 8 through 504, must be a multiple of the existing
8-pixel character width, and represents 1–63 render columns. Height is the
fixed one-row value of 18. Coordinate-plus-size overflow is rejected before
mutation; invalid X, Y, or width preserves the prior geometry.

## Visibility and dynamic updates

Labels are visible by default. `SetVisible(false)` suppresses the text call but
retains the stored text and geometry. Showing the label again renders the same
stored value. Repeated updates replace the complete value, including shorter
after longer values, without stale trailing characters or shared/static
buffers. Visibility and text are per-instance state.

## Rendering and clipping

Rendering uses the existing `GuideXosSurface.TrySetText` primitive and a fixed
64-byte temporary stack buffer. Hidden and empty labels emit no text. A
non-empty value is clipped at the configured render width by bounded end
clipping; there is no scrolling, wrapping, or ellipsis. Clipping changes only
the emitted prefix and never mutates `Text` or `Length`. The label has no focus
styling, caret, selection, enabled/disabled chrome, or fill rectangle.

## Non-focusable design and ControlHost policy

`GuideXosLabel` is intentionally not a `GuideXosControlHost` registration
kind. No `TryRegisterLabel` API was added. `GuideXosControlHost` remains the
owner of interactive controls only: Open, Save, Save As, Show path, and
Document. Adding, updating, hiding, or showing a label therefore cannot change
the active control, keyboard routing, pointer ownership, Tab order, reverse
Shift+Tab order, wrapping, disabled skipping, modal isolation, or focus
restoration. Label input is proved through host/application integration rather
than by inventing no-op input methods.

## Managed Notes integration

In the C122 composite, Managed Notes owns `_currentPath` and one
`GuideXosLabel` instance. The application builds the bounded presentation
value `Path: <canonical path>` in a fixed stack buffer and calls
`SetText`; the label does not query Notes, the VFS, or the picker. Open and
successful Save As path changes update this same instance. The C121 `Show path`
checkbox now drives `_pathLabel.Visible` through `RenderPathLabel`, rather
than selecting between a second direct path-rendering implementation.

The required C122 proof covers the initial Notes path, checkbox hide/show with
text preservation, Open to `02-POINT.TXT`, Save As to `THIRD.TXT`, no stale
path tail, unchanged focus order while hidden, active-focus preservation after
path updates, picker isolation, focus restoration, and Reload from the saved
VFS path.

## Allocation and runtime impact

Per label: one fixed character array plus scalar text length, geometry,
visibility, and rejection-count fields. Render uses one fixed stack buffer and
does not allocate. Managed Notes has one production path-label instance. The
application's bounded stack composition buffer is 48 characters. There is no
growing text history, collection, reflection, event list, binding engine, or
per-render allocation path.

C122 requires no Host ABI growth: version remains 1 and table size remains
104. It adds no capability bit, no input-transport field, no NativeAOT runtime
source, no GC change, no executable-mapping change, and no VFS interface
change.

## Proof and validation strategy

`GuideXosLabelTests` covers construction, defaults, valid/empty/maximal text,
over-capacity rejection and preservation, clear/set-after-clear, repeated and
shorter/longer replacement, visibility, geometry rejection/preservation,
normal/empty/hidden/clipped/updated rendering, clipping preservation, and two
independent instances (44 deterministic assertions). `GuideXosLabelHostTests`
covers non-registration, 5-control traversal and wrapping, reverse traversal,
active-focus preservation across text/visibility/geometry changes, rejected
label focus/pointer attempts, and keyboard/pointer non-interference (22
assertions).

The dedicated runner is:

`scripts/dotnet/run-c122-managed-label.ps1`

Its evidence root is `out/dotnet/c011ec122-managed-label/`. Serial output is
authoritative. It performs three fresh accepted QEMU boots of the same C122
proof sequence and records source, kernel, composite, ramdisk, and serial
hashes, lifecycle counters, missing-image behavior, independent-image Busy
behavior, per-boot results, label rendering, dynamic updates, visibility,
clipping, focus order, non-registration, Notes path changes, modal behavior,
and lifecycle preservation. Allocation-heavy historical focused suites remain
isolated in their dedicated C116–C121 runners; C120 and C121 dedicated
regression runners are run separately and are not replaced by a synthetic
marker in the C122 image.

## Limitations and out of scope

C122 intentionally does not provide text editing, text selection, hyperlinks,
rich text, multiple fonts, wrapping, scrolling, selectable labels, automatic
layout, a localization framework, Unicode shaping, accessibility metadata,
animation, general data binding, status history, or a general widget tree.

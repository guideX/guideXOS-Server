# NativeAOT C126 — Managed structural GroupBox

## Purpose

C126 adds `GuideXosGroupBox`, a reusable bounded managed presentation boundary
for a logical region of a UI. It is deliberately a structural primitive, not a
general widget tree. The box stores geometry, an optional caption, visibility,
and one bounded rejection counter. Existing controls remain independently
constructed, rendered, and registered with `GuideXosControlHost`.

Managed Notes uses one instance with the caption `Path Display` to surround its
real C124 `Full path` and `File name` radio controls. The radios remain owned
and routed by `GuideXosControlHost`; the GroupBox only supplies the relative
coordinate calculation used to place them.

## API and state model

`GuideXosGroupBox` exposes:

* `X`, `Y`, `Width`, `Height`, `RenderWidth`, and `RenderRowCount`;
* `Caption`, `CaptionLength`, `MaximumCaptionLength`, and
  `RenderCaptionLength`;
* `Visible`, `Focusable` (always `false`), and `RejectedInputCount`;
* `TrySetCaption`, `ClearCaption`, `TrySetBounds`, `SetVisible`, `Render`,
  `ContainsPoint`, `TryResolvePoint`, and `Reset`.

There are no keyboard, pointer, event, child, registration, or focus APIs.
There is no `AddChild`, collection, recursive render, or parent lifetime.

## Bounds and bounded storage

The coordinate domain is `0..4095`. Bounds are pixel coordinates with a
half-open endpoint convention, and updates reject negative values, unsupported
sizes, unaligned sizes, and coordinate-plus-size overflow before mutation.

| Property | Contract |
| --- | --- |
| Minimum width | 64 pixels, 8 text columns |
| Maximum width | 504 pixels, 63 text columns |
| Minimum height | 54 pixels, 3 text rows |
| Maximum height | 288 pixels, 16 text rows |
| Text column | 8 pixels |
| Text row | 18 pixels |
| Maximum stored caption | 48 printable ASCII characters |

Width is a multiple of 8 and height is a multiple of 18. `X + Width` and
`Y + Height` must remain within the supported coordinate ceiling. Rejected
geometry preserves the prior valid geometry.

Caption storage is one fixed 48-character buffer. Empty caption is valid and
renders an ordinary uncaptained frame. Characters must be printable ASCII
(`0x20..0x7E`). Null, control characters, and captions longer than 48 are
rejected before storage mutation; the prior caption remains intact.

## Rendering and clipping

Rendering uses existing `GuideXosSurface.TrySetText` calls and one fixed
63-column stack buffer. The top and bottom rows are `+`/`-` borders and each
interior row is a bounded `| ... |` row. The top border starts with two dashes,
a space, the caption, and a trailing space when a caption is present.

The renderable caption capacity is `RenderWidth - 6`, clamped at zero. A long
caption is clipped only in the emitted top row; the stored caption and
`CaptionLength` do not change. A complete compositor frame redraw precedes the
managed draw calls, so moving, resizing, or shrinking the box cannot leave a
stale trailing border. Hidden boxes emit no structural text calls.

## Visibility and geometry changes

`SetVisible(false)` preserves geometry and caption. A hidden box emits no
structural rendering, while independently rendered children remain visible and
functional. Showing the box emits its current frame again. `TrySetBounds`
supports bounded move, width change, and height change without changing the
caption or visibility.

This visibility rule is intentionally not parent visibility propagation. C126
does not hide, disable, unregister, or destroy any control when the GroupBox is
hidden.

## Geometry helpers

`ContainsPoint(x, y)` uses the documented half-open rectangle:

```text
X <= x < X + Width
Y <= y < Y + Height
```

The last valid pixel is inside; the right and bottom boundary are outside.
`ContainsPoint` is a geometry utility only and does not claim input ownership.

`TryResolvePoint(relativeX, relativeY, out absoluteX, out absoluteY)` accepts
relative coordinates in the same half-open domain, checks negative values and
overflow, and returns `X + relativeX`, `Y + relativeY` without mutating state.
For example, `(20,80)` plus `(12,24)` resolves to `(32,104)`.

## Managed Notes integration

The C126 Notes layout is:

* GroupBox: `(12,264)`, `456 x 90`, caption `Path Display`;
* Full path radio: relative `(8,14)`, resolved `(20,278)`;
* File name radio: relative `(148,14)`, resolved `(160,278)`.

The application calls `TryResolvePoint` and then applies the resulting absolute
positions through the existing radio `TrySetBounds` API. Dynamic proof keys can
move, grow, shrink, and restore the box and recompute both radio positions.
The path label still follows Show path, and the radio group still controls full
path versus basename presentation. The canonical path string is unchanged.

The document-usage `GuideXosProgressBar` remains independent and continues to
mirror the authoritative `GuideXosTextArea.Length` against
`GuideXosTextArea.MaximumCharacters`. GroupBox movement, resizing, visibility,
and caption rendering do not affect progress state or document length.

## Focus, input, and modal behavior

`Focusable` is always false. The GroupBox is absent from
`GuideXosControlHost`, Tab, and Shift+Tab. Notes registration remains exactly:

```text
Open, Save, Save As, Show path, Full path, File name, Document
```

The C125 Shift-state forwarding fix remains in force, including Shift+Right
selection behavior in the text area. Picker modal isolation and saved-ID focus
restoration continue to be provided by the existing control host. The GroupBox
does not route events, interpret contained pointers, or become a second focus
authority.

## Allocation and runtime impact

Each GroupBox owns one fixed 48-character caption buffer and scalar geometry
and visibility state. Rendering uses one fixed 64-byte stack buffer and a
bounded row count. Managed Notes owns one instance. C126 adds no dynamic child
collection, event list, render history, reflection, layout tree, GC change, or
runtime initialization path.

Host ABI remains version 1 with table size 104. There are no capability,
input-transport, NativeAOT runtime, GC, or VFS changes.

## Proof and regression strategy

The focused suite records 60 deterministic GroupBox cases covering construction,
all geometry limits and preservation, caption capacity and clipping, visibility,
move/resize, stale-border protection, containment, relative coordinates,
reset, independent instances, focus policy, and no ControlHost registration.

The dedicated runner is
`scripts/dotnet/run-c126-managed-group-box.ps1`. Its evidence root is
`out/dotnet/c011ec126-managed-group-box/` and its acceptance target is three
fresh QEMU boots. Serial evidence proves the Notes frame, caption, visibility,
move/resize, radio relationship, focus order, modal behavior, progress
independence, Shift routing, lifecycle reuse, and recent regressions.

## Deliberate limitations and out of scope

C126 does not provide automatic child ownership, automatic child visibility or
enabled-state propagation, recursive containers, child clipping, scrolling,
anchors, docking, automatic layout, event routing, focus ownership, z-order,
drag/move UI, a collapsible GroupBox, or a theme framework. It does not replace
`GuideXosControlHost` and does not begin a general widget-tree phase.

# NativeAOT C138 — Reusable Managed Vertical ScrollBar

## Scope

C138 adds one reusable managed vertical `GuideXosScrollBar`. It is a visual
and interactive controller over the C137 viewport state; it does not own a
TextArea or ListBox and it does not introduce a second scroll offset.
Horizontal scrolling, smooth or kinetic scrolling, touch input, overlay
theming, nested scrolling, and multiple simultaneous drag owners remain
deferred.

## API and integer semantics

The control exposes `Minimum`, `Maximum`, `Value`, `PageSize`, `SmallChange`,
`LargeChange`, `Enabled`, `Visible`, focus state, `Changed`, and bounded
pointer/key/wheel handlers. The invariant is:

```text
Minimum <= Value <= Maximum
```

Assignments clamp deterministically. A `Maximum` below `Minimum` collapses the
range to `Minimum`; a `Minimum` above `Maximum` raises `Maximum` to the new
minimum. Reassigning the effective value does not fire `Changed`. A callback
is invoked once for an effective change. Callback-driven mutation is guarded
against recursive notification, so one callback cannot recurse without bound.

`PageSize` is the visible logical extent and is used for thumb geometry. When
the range is empty (`Maximum <= Minimum`), the thumb fills the track and all
movement is inert.

## Geometry and mapping

For a track of `T` pixels, logical range `R = Maximum - Minimum`, and positive
page `P` (zero is treated as one for geometry), C138 uses:

```text
thumb = floor(T * P / (R + P))
thumb = clamp(thumb, MinimumThumbPixels, T)
```

`MinimumThumbPixels` is 8. The usable travel is
`A = max(0, T - thumb)`. The forward mapping is integer arithmetic:

```text
thumbOffset = floor((Value - Minimum) * A / R)
```

The inverse drag mapping uses rounded integer division and clamps at both
ends. The exact endpoints are always `Minimum` at the top and `Maximum` at
the bottom. The drag stores the pointer-to-thumb-top offset. It also preserves
the starting value when the pointer is moved back to the exact starting thumb
offset, preventing a one-pixel round-trip jump.

The default 12-pixel arrow regions are used when the control is tall enough.
Primary clicks in those regions step by `SmallChange`; track clicks page by
`LargeChange`. Secondary clicks are ignored.

## Keyboard and wheel behavior

Focused bars accept Up/Down, Home, and End. Up/Down use `SmallChange`; Home
and End select the range endpoints. PageUp/PageDown are not transported as
new native key concepts in this phase; track paging supplies the bounded
large-change operation.

Wheel uses the C137 normalized signed delta and moves by three
`SmallChange` units per bounded wheel unit. A wheel event over a bound bar
updates the same viewport through the binding callback. A wheel event during a
thumb drag is ignored, so two interaction modes cannot modify the value at
once.

## Drag ownership and lifecycle

`GuideXosControlHost` has one bounded pointer-drag owner, separate from its
existing one-owner transient popup lease. A primary press on a registered
scrollbar thumb establishes the owner. Pointer motion is routed to that owner
even when the coordinates leave the scrollbar rectangle; primary release ends
the owner. There is no capture stack and no second focus subsystem.

The two ownership categories are mutually exclusive:

- an active popup lease blocks scrollbar drag acquisition;
- an active scrollbar drag blocks popup lease acquisition.

Hide, disable, modal entry, unregister, panel/membership invalidation, reset,
and application close cancel the drag and clear the owner. A stale release is
ignored and cannot affect a later control.

## Binding model

The binding is an explicit callback pair, not a general data-binding system:

```text
ScrollBar.Changed -> authoritative viewport setter
viewport changed by wheel/key/edit -> ScrollBar.Value synchronization
```

Same-value suppression and the scrollbar notification guard prevent loops.
The viewport remains authoritative.

### TextArea

The TextArea binding is:

```text
Minimum = 0
Maximum = TextArea.MaximumFirstVisibleLine
Value = TextArea.FirstVisibleLine
PageSize = TextArea.VisibleLineCount
SmallChange = 1
LargeChange = max(1, VisibleLineCount - 1)
```

The `Changed` callback calls `SetFirstVisibleLine`; it preserves caret and
selection state. C137 wheel and keyboard paths synchronize the bar after the
TextArea changes. Short content produces an inert full-track thumb.

### ListBox

The ListBox uses the same scrollbar class:

```text
Minimum = 0
Maximum = ListBox.MaximumFirstVisibleIndex
Value = ListBox.FirstVisibleIndex
PageSize = ListBox.VisibleRowCount
```

The callback only changes `FirstVisibleIndex`; selected index is independent.
Pointer selection continues to translate visible rows to logical item indices
after scrollbar movement.

## Managed Notes and rendering

Managed Notes registers two persistent scrollbar controls in the C138 proof:
one beside the TextArea and one beside the C137 ListBox. Registration changes
from 8 to 10. Both render through the existing managed `FillRect` primitive
with distinct track/thumb colors for normal, focused, and disabled states.
No Panel ownership or layout engine was added; the existing Panel remains
single-level and non-owning.

## Focused tests and production proof

`GuideXosScrollBarC138Tests` covers 22 API cases, 20 host/drag cases, 10
TextArea binding cases, and 10 ListBox binding cases (62 total). The production
runner also retains the C137 46-case wheel suite and the C134/C135 popup and
C136 secondary-pointer paths.

The C138 NativeAOT runner uses physical QEMU input: relative PS/2 motion,
primary button down/up, secondary popup input, Escape, wheel, and relaunch.
It proves thumb dragging outside the original bounds, track paging, popup
blocking, ListBox viewport movement with selection preservation, logical row
selection after scrolling, wheel after drag, and deterministic close/relaunch.
The final proof state requires transient popup capture and pointer-drag owner
both to be none.

Proof evidence and exact hashes are recorded in the generated
`out\dotnet\c138-managed-scrollbar\c138.manifest.json` and boot serial files
when the runner is executed. The canonical ordinary-kernel restoration is
performed after proof execution; the protected `ESP\ramdisk.img` is not a
proof output and must retain its baseline hash.

## Compatibility guarantees

NativeAOT, GC, VFS, the App Model, C129 keyboard semantics, C134 one-owner
popup capture, C135 PopupMenu, C136 primary/secondary semantics, C137 wheel
transport, ABI v1, and host table size 104 remain intact. Pointer motion uses
semantic input kind 0 and the existing 24-bit coordinate payload; it adds no
callback field or ABI version change. Tab remains KeyDown-only: C138 adds no
synthetic Tab `KeyChar`.

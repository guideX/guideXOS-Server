# NativeAOT C139 — Shared Managed Vertical Scroll Viewport

## Scope and outcome

C139 extracts the bounded logical vertical viewport state used by the C137 and
C138 TextArea and ListBox controls. The implementation is Outcome A: both
controls use one reusable `GuideXosVerticalViewport` for content extent,
visible extent, offset, maximum-offset calculation, clamping, small movement,
page movement, and logical item reveal. Caret, selection, rendering, pointer
hit testing, focus, lifecycle, modal routing, and popup behavior remain
control/host responsibilities.

Horizontal scrolling, pixel/smooth/kinetic scrolling, nested scrolling,
virtualization, layout, scroll chaining, and a general child-control
`ScrollView` remain deferred.

## Pre-C139 inventory

Before extraction, both controls independently stored and calculated a first
visible logical position. Each had a content-minus-visible maximum, clamping in
wheel and direct setters, three logical units per normalized wheel notch,
caret/selection reveal, and reset/shrink handling. C138 also repeated the
range/page/value synchronization recipe at each ScrollBar binding site.

The shared part was the pure bounded state machine. TextArea retains line
count and caret reveal decisions; ListBox retains item count, selected index,
row hit testing, and selection reveal decisions. ScrollBar retains drawing,
pointer drag ownership, track paging, arrow stepping, keyboard handling, and
wheel handling.

## Viewport API and invariants

`GuideXosVerticalViewport` is a small managed state object:

```text
ContentExtent   non-negative logical content size
VisibleExtent   non-negative visible logical size
Offset          bounded viewport origin
MaximumOffset   max(0, ContentExtent - VisibleExtent)
SmallChange     positive logical step multiplier, default 1
LargeChange     positive page multiplier, default max(1, VisibleExtent - 1)
Changed         offset-only callback
```

It exposes `SetContentExtent`, `SetVisibleExtent`, `SetOffset`,
`ScrollSmall`, `ScrollLarge`, `ScrollPageForward`, `ScrollPageBackward`, and
`EnsureVisible`. All arithmetic uses `long` intermediates and clamps to the
representable integer range. Negative extents normalize to zero. Empty,
undersized, equal-sized, and zero-visible ranges therefore have deterministic
zero or bounded offsets without underflow, overflow, divide-by-zero, or
negative effective viewport state.

`Changed` fires only for an effective offset change. Same-value assignments,
no-op boundary movement, and extent changes that leave the offset valid do not
fire it. An internal narrow state hook lets a bound ScrollBar refresh its
range/page geometry when an extent changes without an offset change; this is a
binding synchronization hook, not a general property-change framework.

Notifications are guarded by one bounded dispatch flag. If a callback mutates
the viewport, the mutation is applied and same-value suppression preserves the
final state, but nested notification dispatch is suppressed. This gives one
callback per completed outer mutation and prevents callback-driven recursion.
The ordinary scroll, step, page, direct-set, and synchronization paths create
no temporary managed objects.

## ScrollBar binding

`GuideXosScrollBar.BindViewport` connects the existing C138 bar to the
authoritative viewport:

```text
viewport state -> Minimum=0, Maximum=MaximumOffset,
                  PageSize=VisibleExtent,
                  SmallChange=SmallChange,
                  LargeChange=LargeChange,
                  Value=Offset
ScrollBar.Changed -> viewport.Offset
```

`SynchronizeViewport` is also available for an explicit refresh after model
replacement. Same-value suppression in both objects prevents binding loops or
unnecessary bar churn. The bar does not duplicate maximum-offset arithmetic.
Pointer drag ownership remains in `GuideXosControlHost`; the viewport owns no
pointer capture, controls, registrations, focus, modal state, or popup state.

## TextArea migration

TextArea now owns one `GuideXosVerticalViewport` configured in logical lines.
`FirstVisibleLine` and `MaximumFirstVisibleLine` delegate to it. Wheel input
still uses the C137 normalized signed delta and three logical lines per notch;
the viewport changes without moving the caret. Text editing updates
`ContentExtent`, while `EnsureCaretVisible` calls the shared `EnsureVisible`
operation. Rendering reads the shared offset. The line buffer, caret,
selection, editing results, and lifecycle behavior remain TextArea-specific.

## ListBox migration

ListBox uses the same viewport configured in logical item rows.
`FirstVisibleIndex` and `MaximumFirstVisibleIndex` delegate to it. Wheel
movement never changes `SelectedIndex`. Keyboard selection still calls the
shared reveal operation; pointer hit testing remains the local visible-row plus
viewport-offset mapping. Clear/repopulation updates content extent and clamps
the viewport without merging selection and scroll state.

## Focused tests

`GuideXosSharedScrollViewportC139Tests` covers 54 cases:

- 30 viewport cases: construction, zero/undersized/equal/larger content,
  direct clamping, small/page movement, growth and shrink, visible-extent
  changes, maximum recalculation, exact callback behavior, bounded reentrancy,
  repeated boundary movement, large extents, and invalid-state normalization;
- 10 TextArea migration cases: shared state, initial offset, wheel, ScrollBar
  binding round-trip, rendering-visible state, caret independence, caret
  reveal, shrink clamp, bar synchronization, and relaunch defaults;
- 10 ListBox migration cases: shared state, wheel, ScrollBar binding
  round-trip, selection independence, keyboard reveal, pointer mapping, shrink
  clamp, selection validity, bar synchronization, and relaunch defaults;
- 4 cross-control cases proving identical viewport mathematics for equivalent
  logical dimensions.

The existing C138 focused suite remains 62/62 after binding migration. The
C137 wheel suite remains 46/46. Earlier managed suites are retained by the
C139 composite mode and are not weakened or removed.

## Managed Notes and architecture preservation

Managed Notes continues to register 10 controls in the C138/C139 proof: the
same two ScrollBars are now directly bound to the TextArea and ListBox
viewports. No registration is added by the extraction. The Panel remains a
single-level, non-owning control. No second focus system, capture stack,
viewport-owned capture, synthetic Tab `KeyChar`, ABI callback, or ABI version
change is introduced. ABI v1 and table size 104 remain unchanged.

## NativeAOT and QEMU proof record

The C139 production mode is `C139Composite`. The completed proof record is in
`out/dotnet/c139-shared-scroll-viewport/c139.manifest.json`. The NativeAOT
composite built successfully with the repository's existing warning set (65
warnings, no errors), producing:

```text
composite HostLogProof.elf: 3DB4DC1BA39FF7EA326C91A7CD9A737A20F7CFE2F13B4EFDA1BCBC8A1644E81F
proof kernel.elf:            62B50E1F7E90C4B5295533CBEF397069A6AA109A22CE01E007B9E23041C0F8FA
```

Three independent fresh QEMU boots passed. Each boot emitted
`C139-FOCUSED ... total=54 result=PASS`,
`C139-TESTS ... cases=54 result=PASS`, and the retained
`C138-PROOF ... result=PASS` marker. The proof serial logs are under
`out/dotnet/c139-shared-scroll-viewport/boot-01` through `boot-03`; the
manifest records their individual hashes and the final capture state.

After proof execution, the canonical ordinary kernel is restored and rebuilt.
The ordinary kernel was rebuilt with the default kernel configuration, and the
ordinary validator passed three additional fresh QEMU boots. Both the build
kernel and `ESP\kernel.elf` now have SHA-256
`9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203`.
The ordinary serial logs and manifest are under
`out/dotnet/c139-shared-scroll-viewport/ordinary-boot`.
`ESP\ramdisk.img` remained unchanged at:

```text
E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE
```

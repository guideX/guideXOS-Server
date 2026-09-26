# NativeAOT C140 — Reusable Managed Vertical ScrollView

Phase C140 adds `GuideXosScrollView`, a bounded vertical viewport for ordinary
managed controls. It is a single-level, non-owning membership container plus
one `GuideXosVerticalViewport`, a reusable clip/translation boundary, and an
optional explicit `GuideXosScrollBar` binding. It is deliberately not a
retained UI tree or a general layout engine.

## Architecture

The ScrollView owns only fixed metadata, bounds, viewport state, and the
optional scrollbar relationship. The application still owns every member's
lifetime. Membership is an explicit fixed array of at most eight direct
ordinary controls:

`Button`, `CheckBox`, `Label`, `Separator`, `RadioButton`, `ProgressBar`, and
`ComboBox`.

`TextArea`, `ListBox`, `Panel`, and another `ScrollView` are rejected. This
avoids nested-scroll ambiguity and keeps the first general scrolling phase
single-level. Duplicate members, Panel members, members already attached to a
ScrollView, invalid coordinates, and capacity overflow return deterministic
failure results. Removal detaches only the membership metadata; it does not
destroy the caller-owned control.

The existing `GuideXosPanel` remains separate. Panel keeps its four-entry,
local-coordinate, non-owning direct-child contract and does not gain viewport
semantics. A ScrollView member cannot also be a Panel child, and a Panel cannot
become a ScrollView member, so ownership and coordinate rules do not overlap.

## Coordinates and extent

Members retain stable logical content coordinates. For a member with logical
position `(logicalX, logicalY)` the screen position is:

```text
screenX = ScrollView.InnerX + logicalX
screenY = ScrollView.InnerY + logicalY - Viewport.Offset
```

The member's application geometry is not changed by wheel movement. Bounds are
translated when membership is added or when the outer ScrollView is resized;
rendering and input apply the presentation offset at the container boundary.

`ContentExtent` is derived as the maximum visible member bottom. Hidden members
contribute no extent, while disabled members still contribute extent. The
viewport's `VisibleExtent` is the inner height, and its clamped `Offset` is the
only scroll position. Removing a bottom member recalculates the extent and
immediately clamps the same shared offset. An explicit `RecalculateContentExtent`
call is available for bounded geometry-change workflows.

## Viewport and ScrollBar

ScrollView owns exactly one `GuideXosVerticalViewport`; it does not duplicate
the offset or range. Wheel, direct scrolling, page scrolling, focus reveal, and
ScrollBar changes all converge on `Viewport.Offset`.

The application creates and owns a `GuideXosScrollBar`, then calls
`BindScrollBar`. The established C138/C139 binding path derives minimum,
maximum, page size, value, thumb geometry, and inert all-content-visible state
from the shared viewport. There is no automatic ScrollBar creation or child
ownership. Rebinding is idempotent, and unbinding releases the callback path.

## Rendering and clipping

The frame is drawn first. Member rendering then saves the reusable
`GuideXosSurface` render state, sets a clip rectangle to the inner viewport,
translates Y by `-Offset`, renders members in insertion order, and restores the
previous state in a `finally` block. This makes nested use of the surface state
safe without introducing a retained compositor tree.

Rectangles are intersected exactly with the clip rectangle. The existing native
text primitive is row-oriented; C140 therefore suppresses a text row when the
18-pixel row crosses a vertical clip edge, while applying exact horizontal
glyph-span clipping. This conservative granularity is explicit and
deterministic: partially visible text rows are omitted rather than allowed to
paint outside the viewport. Partially visible rectangle-backed controls retain
their visible intersection.

## Input and focus

Hit testing first requires the pointer to be inside the visible inner rectangle.
It maps screen coordinates back into content space, then searches visible
members in reverse insertion order. The visible portion of a clipped member is
interactive; the clipped-off region is not. Hidden members are skipped.
Disabled members may be targeted for deterministic disabled feedback but never
activate and never receive focus.

ScrollView consumes wheel input inside its visible content area. It does not
bubble wheel events through arbitrary members and does not change focus. A
transient popup/drag owner remains the host's first-priority routing decision;
ScrollView does not create a second capture lease or capture stack.

Focusable members participate in the existing host scope. Forward and reverse
Tab movement skips hidden and disabled members. When focus moves to an offscreen
member, ScrollView performs the minimum reveal needed to expose the complete
member rectangle. Clicking a newly visible member focuses and routes that
member after translation. Wheel movement can temporarily clip the focused
member without changing focus; a subsequent keyboard focus reveal restores
visibility when needed.

The production host registers the ScrollView as one focus scope item and a
bound ScrollBar as the second item. Tab entering the ScrollView first focuses
its eligible member; Tab leaving the last eligible member returns to the host's
normal traversal. No synthetic Tab `KeyChar` is generated.

## Lifecycle and modal policy

Visibility and enabled state are local gates. Hiding or disabling the
ScrollView blurs its member and rejects wheel/pointer input. Removing a focused
member blurs it before detaching. Clearing members detaches all fixed entries,
resets extent, and leaves caller-owned controls alive. `Reset` restores visible,
enabled, no-focus, offset-zero state without inventing ownership semantics.

Existing modal isolation and C134/C135 transient capture remain authoritative.
ScrollView has no modal state and no popup exception: while another host scope
owns a popup or drag, background ScrollView input is not allowed to steal it.
Popup controls are not ScrollView children. Nested ScrollViews, nested Panels,
scroll chaining, and TextArea/ListBox-in-ScrollView policies are intentionally
deferred rather than guessed.

## Allocation and ABI discipline

Membership uses a fixed eight-entry array. Wheel, offset changes, hit testing,
focus reveal, and ScrollBar synchronization use bounded scalar state and do not
allocate per event. Rendering uses the existing surface primitives and a small
value-type render state. The implementation uses no reflection, dynamic tree,
virtualized list, smooth/kinetic motion, horizontal axis, or new native host
callback.

The NativeAOT host ABI remains version 1 with table size 104. Existing pointer,
wheel, keyboard, popup-capture, drag-owner, C138 ScrollBar, and C139 shared
viewport transport remain unchanged.

## Implementation and proof surface

The reusable control is in
`samples/managed/HostLogProof/GuideXos/GuideXosScrollView.cs`. The reusable
surface clip/translation state is in `GuideXosSurface.cs`. Ordinary controls
carry only small ownership guards so Panel and ScrollView membership cannot
overlap. `GuideXosControlHost` registers ScrollView as a distinct scope kind.

`ManagedScrollViewDemo` is a small selector-5 production composite sample. It
registers two host entries: one ScrollView and one bound ScrollBar. Eight
ordinary controls span logical Y positions 0 through 220 in a 118-pixel outer
surface, so the initial offset is zero and the content is genuinely taller than
the viewport. The C140 app-model record is conditional on the C140 proof-kernel
define; ordinary builds do not advertise a selector that is absent from their
managed registry.

The proof runner is
`scripts/dotnet/run-c120-managed-control-host.ps1 -ProofPhase C140`, using the
`C140Composite` NativeAOT build mode. Its manifest includes source hashes,
composite/kernel/ramdisk provenance, focused totals, retained C137/C138/C139
source coverage, three boot records, and the documentation path.

## Focused tests and retained regressions

`GuideXosScrollViewC140Tests` contains 58 bounded cases:

| Area | Cases |
| --- | ---: |
| Core membership, extent, clamps, direct/page scrolling, shrink | 20 |
| Clipping and render-state boundaries | 9 |
| Translated hit testing | 10 |
| Focus, reveal, visibility, enabled state, lifecycle reset | 9 |
| ScrollBar binding, drag, page, shrink, unbind | 10 |
| **Total** | **58** |

C139 shared-viewport tests remain in place, as do the C138 ScrollBar and C137
wheel suites. Earlier Panel, lifecycle, keyboard transport, control, popup,
transient-capture, and secondary-pointer fixtures are not rewritten by C140.

## NativeAOT/QEMU and restoration evidence

The production proof launches the conditional `Managed ScrollView` app-model
record through `/system/apps/GXOSAPP.ELF`, then drives real PS/2/QEMU input:
wheel over the viewport, translated member pointer activation, ScrollBar thumb
drag/release, and Tab focus reveal. The native proof marker records the logical
target geometry; managed markers record initial registration, test totals,
wheel/thumb synchronization, translated activation, focus reveal, and final
drag release.

Acceptance requires three independent fresh C140 boots, each with PASS serial
classification, and final `capture=none`, `drag owner=none`, and an in-range
viewport. The same runner records the composite ELF SHA-256, proof-kernel
SHA-256, and each serial SHA-256. The dedicated C137/C138/C139 proof phases
and their source/tests remain retained; C140 does not replace them with a
direct setter test.

After proof work, restore/rebuild the canonical ordinary kernel and verify both
`kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`, then run the ordinary
fresh-boot validation. The protected `ESP/ramdisk.img` is independently hashed
and must remain the accepted baseline unless a pre-existing legitimate change
is proven.

## Deferred work

C140 intentionally defers nested ScrollViews, arbitrary nested container trees,
horizontal or two-axis scrolling, smooth/kinetic/touch scrolling, virtualization,
automatic layout, scroll chaining, event bubbling, overlay or automatically
owned ScrollBars, generalized nested TextArea/ListBox scrolling, and compositor
transforms beyond the clip/translation boundary needed here.

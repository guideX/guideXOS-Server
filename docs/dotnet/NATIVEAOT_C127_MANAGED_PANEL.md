# NativeAOT C127 — Managed bounded Panel and child membership

## Purpose

C127 adds `GuideXosPanel`, a reusable fixed-capacity, single-level composition
boundary for existing managed controls. It stores a bounded rectangle and a
fixed insertion array of non-owning child references. It does not become a
widget tree, a focus scope, a renderer, or a lifetime owner.

Managed Notes uses one Panel for the existing `Path Display` radio pair. The
C126 `GuideXosGroupBox` remains a decorative sibling frame with its original
caption, bounds, and non-owning contract. The Panel is what owns the two radio
memberships and their relative positions.

## Public API and supported children

`GuideXosPanel` exposes:

* `X`, `Y`, `Width`, `Height`, `Visible`, `Focusable`, `ChildCount`,
  `MaximumChildCount`, and `RejectedInputCount`;
* `TrySetBounds`, `SetVisible`, `TryAddChild`, `TrySetChildPosition`,
  `TryRemoveChild`, `Clear`, `ContainsChild`, `GetChildAt`, `ContainsPoint`,
  `TryResolvePoint`, `Render`, and `Reset`;
* `GuideXosPanelResult` values for `Added`, `Moved`, `Removed`, `Cleared`,
  `Empty`, `Duplicate`, `CapacityReached`, `InvalidChild`,
  `MembershipConflict`, `InvalidPosition`, `OutOfBounds`, and `NotMember`.

The default and maximum capacity in C127 are four children. Supported child
types are `GuideXosButton`, `GuideXosCheckBox`, `GuideXosLabel`,
`GuideXosSeparator`, `GuideXosRadioButton`, and `GuideXosProgressBar`. Each
supported control now exposes `ParentPanel`, `Visible`, and
`EffectiveVisible` where applicable. `GuideXosGroupBox`, text input, text
area, list box, and another Panel are intentionally not supported children in
this phase.

## Membership and lifetime rules

Membership is fixed-array, non-owning, and insertion ordered. A child can be a
member of at most one Panel. Adding a duplicate or a child already owned by a
different Panel rejects before changing either object. Capacity, invalid child
types, negative local positions, and out-of-bounds child rectangles reject
without partial membership mutation.

The Panel stores the child reference and local position only. It never
disposes, resets, or owns application state. `TryRemoveChild` and `Clear`
detach children, invalidate their transient focus state, restore standalone
parent visibility, and leave the child instance alive for reuse. The caller
must ask the existing `GuideXosControlHost` to `RefreshVisibility` after a
membership or parent-visibility change when host focus may be affected.

While a child is a member, its public bounds setter is rejected; position is
changed through `TrySetChildPosition` or by moving the Panel. This removes
ambiguous competing geometry authorities. Removing the child returns control
of its bounds to the child.

## Coordinates, bounds, and rendering

Panel bounds use pixel coordinates with the established half-open convention.
The coordinate domain is `0..4095`; width is 8-pixel aligned from 8 through
504, height is 18-pixel aligned from 18 through 288, and endpoint overflow is
rejected before mutation. Every member's complete rectangle must fit inside
the Panel. A Panel resize that would place any existing child outside rejects
and preserves the previous geometry.

Each child position is stored as local `LocalX`/`LocalY`. Panel moves and
resizes recompute absolute child positions from the stored local values, so
repeated movement does not accumulate drift. Rendering delegates to the
existing child renderers in insertion order. The Panel has no frame of its
own and makes no clipping claim; out-of-bounds children are rejected rather
than clipped.

## Visibility, focus, and input

Effective child visibility is the conjunction of child-local visibility and
Panel visibility. Hiding a Panel preserves each child's local visibility and
state, but the effective child is not rendered, pointer-activatable, or
eligible for Tab/Shift+Tab. Showing the Panel restores only children that were
locally visible; it never steals focus.

`GuideXosControlHost` remains the sole input and focus authority. C127 adds
only `RefreshVisibility`, which normalizes an invalid active entry through the
existing registration order. Hiding or removing a focused Panel child blurs
that child; the next eligible registered control receives focus, or the host
ends with no focus if none exists. Modal entry, isolation, saved-ID
restoration, disabled-control skipping, and exactly-once activation remain
host behavior. The Panel is never registered and never dispatches input.

## Managed Notes integration

The C127 Notes layout keeps the C126 frame at `(12,264)` with size `456 x 90`
and adds a Panel with the same bounds:

* Full path radio: local `(8,14)`, resolved `(20,278)`;
* File name radio: local `(148,14)`, resolved `(160,278)`.

The existing seven host registrations and order are unchanged:
`Open`, `Save`, `Save As`, `Show path`, `Full path`, `File name`, `Document`.
Panel moves, visibility changes, radio selection, path presentation, document
usage, Save/Open, picker modal isolation, and native Reload remain separate.
The GroupBox frame and Panel are synchronized by the small Notes proof
commands; the GroupBox does not acquire child ownership.

## Validation and limitations

`GuideXosPanelTests` covers construction, invalid geometry, capacity,
duplicates, cross-panel conflict, removal, clear/reuse, parent lifetime,
relative coordinates, repeated movement, resize rejection, local visibility,
rendering, hidden pointer/focus exclusion, focus recovery, exactly-once radio
input, and modal isolation. The dedicated runner is
`scripts/dotnet/run-c127-managed-panel.ps1`; its evidence root is
`out/dotnet/c011ec127-managed-panel/` and it requires three fresh QEMU boots.
The C127 image also runs the established C126 and earlier managed regression
paths.

Validation completed on 2026-09-20:

* `dotnet build samples/managed/HostLogProof/HostLogProof.csproj -c Release
  -p:HostLogProofMode=C127Composite -p:PublishAot=false --no-restore` passed
  with zero errors.
* The C127 NativeAOT/QEMU runner passed three fresh boots. The result and
  source/artifact hashes are recorded in
  `out/dotnet/c011ec127-managed-panel/c127.manifest.json`. Serial evidence
  hashes were boot 1 `E59FBCE00C1A20B852A872A9361AAAAB8C653751911F7BC50693DEE13300DC17`,
  boot 2 `DAE1731B53677A4EF49FCEFA299B0F808C8BE58EECE2A41F33CF0EE94DB39FCB`,
  and boot 3 `0A185EAE95FAFE11879A31F5702EB3288CA1980FCA2BE0105B2FC77F793C687F`.
  Each boot reported `C127-RESULT outcome=PASS`, including the 60-case
  managed panel suite and the C126 regression path.
* After proof testing, the ordinary kernel was rebuilt from current sources
  and restored to `ESP/kernel.elf`. The ordinary three-boot validator passed;
  its manifest is
  `out/dotnet/c011ec127-managed-panel/ordinary-boot/run-20260920-143000877-cfc6fa34/ordinary-boot.manifest.json`.
  The ordinary kernel and restored ESP copy both hash to
  `A3325E6C5E62C4ECD9C463EE38DB0763DDA2A67F54D2A1AE1F86673B37457CDF`.

Host ABI v1/table 104, capabilities, pointer/keyboard transport, NativeAOT
runtime, GC, and VFS are unchanged. Ordinary kernel/ESP artifacts are
restored after proof testing by the same non-destructive workflow used by
earlier phases; the final evidence records their paths and SHA-256 hashes.

Nested containers, clipping, scrolling, richer layout, docking, anchoring,
automatic layout, and an in-OS managed build/run/debug loop remain open work.
They are deliberately outside C127.

# C123 — Reusable Managed Separator / Divider

## Purpose

C123 adds `GuideXosSeparator`, a small reusable managed presentation primitive
for a bounded horizontal divider. It gives ordinary managed guideXOS UI code a
way to mark a real visual grouping boundary without adding a layout engine,
new native drawing ABI, or interactive widget semantics.

Managed Notes uses one instance between its document/status content and the
command area. The divider is structural presentation: it is not part of the
path label and is not controlled by the `Show path` checkbox.

## API and state model

`GuideXosSeparator` is a sealed per-instance control with the following public
surface:

- `GuideXosSeparator(int x, int y, int width)` constructs a visible divider.
- `TrySetBounds(x, y, width)` atomically validates and updates geometry.
- `SetWidth(width)` updates only the width through the same validation path.
- `SetVisible(bool)` changes visibility without changing geometry.
- `Render(GuideXosSurface)` emits the divider when visible.
- `Reset()` restores visible state and clears the rejected-input diagnostic
  while retaining valid geometry.
- `X`, `Y`, `Width`, `Height`, `RenderWidth`, `Visible`, and
  `RejectedInputCount` expose bounded state.

The instance stores only scalar presentation state: `X`, `Y`, `Width`, a
visibility bit, and a rejected-update counter. There is no global or static
geometry. The counter is diagnostic state and has no rendering or application
meaning.

## Geometry and bounds

C123 is horizontal-only. The surface uses an 8-pixel character cell and an
18-pixel text row, so the contract is:

- minimum width: 8 pixels (one glyph column)
- maximum width: 504 pixels (63 glyph columns)
- valid X and Y coordinates: 0 through 4095
- height: fixed at 18 pixels
- width: a positive multiple of 8 pixels
- X plus width and Y plus height must remain within the supported coordinate
  range

Validation occurs before mutation. Negative coordinates, unaligned or
out-of-range widths, and overflowing bounds are rejected, and the previous
valid state remains intact.

## Rendering choice

The existing managed surface text contract is used. The separator renders a
single deterministic line of `-` glyphs. A native rectangle/line primitive
was not added because the C119 frame/rectangle audit showed that some geometry
primitives can replace previously submitted frame geometry rather than safely
compose with it. Text submission is already a supported managed surface
operation and gives the required result without changing the compositor.

`Render` uses a fixed `stackalloc` buffer of 64 bytes: at most 63 hyphens plus
the bounded terminator capacity. Hidden separators make no surface text call.
The compositor redraws the complete frame before each managed render cycle;
therefore a shorter subsequent line replaces the prior frame and cannot leave
stale trailing divider glyphs. No history buffer, collection, reflection, or
per-frame growing allocation is introduced.

## Presentation-only behavior

The separator has no keyboard handler, pointer handler, focus state, action ID,
or application semantics. It is deliberately not registered with
`GuideXosControlHost`, is absent from the host registration count, and cannot
appear in Tab or Shift+Tab traversal. Hiding, showing, moving, or resizing it
does not mutate the active control or modal scope.

The focused host tests create the five existing Notes controls—Open, Save,
Save As, Show path, and Document—and register exactly those five. The
separator is only rendered beside them. The proven order remains:

`Open -> Save -> Save As -> Show path -> Document`

with the corresponding reverse order and existing wrapping behavior.

## Managed Notes integration

Notes constructs one separator at `(20, 200)` with a configured width of 480
pixels, rendered as 60 hyphens. It sits below the editor/status region at
`y=174` and above the command controls at `y=220`, giving the existing layout
a meaningful document-to-command boundary without moving the controls.

The divider remains visible when `Show path` is unchecked. The path label is
independently hidden and shown, and continues to update on Open and Save As.
The same divider remains passive through the file picker/modal scope, and
focus is restored by the existing host logic when the picker closes. Editing,
Save As, and reopen behavior are unchanged.

## Proof and regression strategy

The C123 focused separator suite covers construction, default state, minimum
and maximum widths, invalid-width and invalid-geometry rejection,
prior-state preservation, visibility, reset, dynamic growth and shrinkage,
repositioning, hidden updates, deterministic glyph output, bounded temporary
storage, and two-instance isolation. It reports 48 checks.

The focused host suite covers the five-control registration count, initial
focus, forward and reverse traversal with wrapping, active-focus preservation,
pointer and keyboard isolation, modal-scope isolation, and hidden geometry
updates. It reports 33 checks.

The production Notes proof covers initial rendering, path-label coexistence,
unchanged traversal, hide/show while Document is active, expansion to 63
columns, contraction to 12 columns with no stale tail, Show path independence,
Open and Save As path updates, picker isolation, focus restoration, edit,
save, and reopen. C123 uses the bounded proof image and retains dedicated
runner coverage for allocation-heavy C120, C121, and C122 suites rather than
forcing every historical fixture into one image.

## ABI, runtime, and allocation impact

C123 is entirely above the existing managed surface contract. It adds:

- no Host ABI table entry or version change
- no capability bit
- no input-transport change
- no NativeAOT runtime change
- no GC change
- no VFS interface change
- no lifecycle change

The Notes application owns one separator instance. Each separator has fixed
scalar state and a maximum 64-byte temporary render buffer. Rendering,
visibility, and geometry changes do not initialize the runtime, reinitialize
GC, remap the executable, or reset the heap; the resident composite reuse
path remains in use.

## Limitations and out of scope

C123 intentionally does not provide automatic layout, vertical orientation,
colors, styles, themes, borders, GroupBox behavior, titled separators, resize
handles, interactive splitters, draggable dividers, animation, or additional
accessibility semantics. Those features require separate design and proof and
are not implied by this control.

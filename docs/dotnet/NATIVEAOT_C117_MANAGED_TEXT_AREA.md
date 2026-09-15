# NativeAOT C117: Reusable Managed Multiline Text Area

## Outcome and scope

C117 adds `GuideXosTextArea`, a reusable bounded managed multiline editing
control. Managed Notes uses the control for its actual loaded document; Notes
does not interpret PS/2 scan codes or own a second editor implementation.

The control is intentionally a small document editor, not a rich-text or
Notepad replacement. Its contract is deterministic, allocation-bounded, and
usable by ordinary managed applications through the existing App Model input
bridge.

## Architecture

The input path remains:

`PS/2 -> desktop compositor -> NativeAotManagedSurface -> launch-flags input -> GuideXosInputEvent -> application control`

`GuideXosApplication.HandleInput` receives the platform-neutral
`GuideXosInputEvent`. The focused application routes the event to its control.
`GuideXosTextArea` owns document, caret, selection, focus, and viewport state;
the application only chooses where to host and render it.

The existing `GuideXosTextInput` remains the smaller single-line primitive
used by the Save As filename field. Shared key values were extended with
`Up`, `Down`, `Home`, and `End`, while the C116 single-line behavior remains
separate and bounded.

## Reusable API

The control is `HostLogProof.GuideXosTextArea` in
`samples/managed/HostLogProof/GuideXos/GuideXosTextArea.cs`.

Important public state and operations are:

- `MaximumCharacters`, `MaximumLines`, `MaximumRenderableColumns`, and
  `VisibleLineCount` expose the configured limits.
- `SetText`, `SetUtf8`, `ToUtf8`, and `TryCopyUtf8To` provide bounded document
  load/save operations.
- `HandleCharacter`, `HandleKey`, and `HandlePointerDown` are the editing and
  pointer-focus entry points.
- `CaretIndex`, `CaretLine`, `CaretColumn`, `AnchorIndex`,
  `SelectionStart`, `SelectionEnd`, `SelectedText`, and `HasSelection` expose
  the editing model.
- `FirstVisibleLine` exposes the vertical viewport.
- `Render` uses the existing `GuideXosSurface.TrySetText` path.

Each edit returns `GuideXosTextAreaEditResult`. Capacity violations and
unsupported input return `Rejected` and increment `RejectedInputCount` without
changing the document or its editing bounds.

## Storage and bounds

The default Notes instance is configured as:

- maximum document characters: 256 UTF-16 code units;
- maximum logical lines: 32;
- maximum renderable columns per line: 48;
- visible lines: 4.

The implementation stores one fixed `char[256]` buffer and uses LF (`0x0A`)
as the logical line separator. The configured maximums are constructor
parameters within compile-time supported ceilings (1024 characters, 128
lines, 16 visible lines, and 56 renderable columns). There is no unbounded
document growth and no runtime, GC, or VFS change is needed for editing.

The accepted character set is printable ASCII (`0x20` through `0x7E`) plus
LF. `SetText` and `SetUtf8` validate the complete replacement before copying,
so a rejected load is atomic. Insertions validate the prospective character
and line counts before shifting the buffer.

## Caret, navigation, and selection

The caret is a bounded linear buffer index in the inclusive range
`0..Length`. Line and column are derived from LF separators. Left and Right
move one logical character and cross line boundaries; Home and End use the
current logical line. Enter inserts LF. Backspace and Delete remove one
character, with the newline at a line boundary joining adjacent lines.

Up and Down use a `_preferredColumn` retained while moving vertically. A
short line temporarily clamps the actual caret column, but subsequent vertical
movement restores the preferred column when a longer line is reached. A
horizontal move, Home, End, edit, or pointer placement resets that preference.

Selection is an anchor plus active caret. The public normalized range is
`SelectionStart..SelectionEnd`, and `HasSelection` is true when the two
indices differ. Shift navigation changes only the active caret, allowing
forward, reverse, expansion, contraction, and direction reversal. Ordinary
Left/Right/Up/Down/Home/End collapses a selection to the directionally
appropriate edge. Typing replaces the range; Backspace and Delete remove it.

## Input transport and focus

The current compositor already tracks keyboard Shift state. C117 carries that
state in the high bit of the existing 24-bit input payload:

- `LaunchFlagInputShift = 0x00800000`;
- key/character value mask becomes `0x007FFFFF`;
- pointer coordinates remain in their existing 12-bit fields;
- the Host ABI version remains 1 and the Host ABI table remains 104 bytes.

Native surface key events set the bit from the existing PS/2 modifier state.
The managed launch context exposes it as `GuideXosInputEvent.Shift`. No Host
ABI function, table slot, capability bit, or raw keyboard API was added.

Pointer-down in the text-area rectangle maps to the nearest valid column on a
visible line, focuses the control, clears the prior selection, and preserves
the current bounded viewport. Events sent while the control is unfocused are
ignored. Managed Notes blurs the text area while its picker is active; picker
input is routed only to the filename field, and completion/cancellation
restores text-area focus.

## Viewport and rendering

The viewport is a single bounded `FirstVisibleLine` plus the configured visible
line count. Every caret move and edit calls the caret-visibility invariant:
the caret line is moved into the visible range, and the first line is clamped
to `0..LineCount-VisibleLineCount`. Adding or removing lines therefore cannot
leave an invalid viewport.

Rendering uses the existing managed surface text primitive. Each visible line
is clipped to 48 columns and is prefixed with `> ` on the caret line. A focused
caret is rendered as `|`; selected character ranges are rendered with `[` and
`]`. This provides deterministic serial/widget evidence without adding a
graphics subsystem. Horizontal scrolling and word wrapping are deliberately
not hidden in the control.

## Managed Notes integration

Managed Notes loads `/system/apps/NOTES.TXT` into `GuideXosTextArea`, places the
caret at the document start, and saves `GuideXosTextArea.ToUtf8()` through the
existing managed file services. Open picker, Save, Save As, filename
validation, overwrite confirmation, cancellation, directory enumeration, and
stat/read/write/reopen behavior remain in the C115/C116 picker and VFS layers.

The C117 fixture starts as:

```text
First line
Second line
Third line
Fourth line
Fifth line
Sixth line
```

The proof focuses the area, inserts and splits text, navigates vertically,
selects and replaces `line`, edits the last line to force scrolling, saves,
uses typed Save As, reopens, reloads, cancels a picker, and exercises overwrite
decline/confirmation. The expected final document is:

```text
AFirst!
 line
Second? ROW
Third line
Fourth line
Fifth line
Sixth line!
```

The dedicated runner is
`scripts/dotnet/run-c117-managed-text-area.ps1`. It stages a fresh ramdisk,
builds the C117 composite and kernel, and accepts only three fresh QEMU boots
whose serial evidence contains the C117 control tests, Shift transport,
selection, viewport, Notes Save/reopen, C116 filename regression, and
application regressions. Evidence is written under
`out/dotnet/c011ec117-managed-text-area/`.

## Runtime, ABI, and boundedness impact

C117 adds no NativeAOT runtime-source changes, GC changes, VFS changes, Host
ABI table growth, ABI version change, capability bit, or executable mapping
model change. Managed re-entry uses the established resident image path. The
runtime/PAL/GC/code-manager/module/mapping/heap initialization counters retain
their existing one-time initialization and preserve/reuse behavior.

The control uses fixed primary storage. Normal document I/O allocates only
bounded result/path objects already allowed by the managed application model;
it does not require a new allocator or collection policy.

## Known limitations and out of scope

- Unicode beyond the current bounded ASCII character contract and IME input.
- Clipboard integration.
- Undo/redo.
- Rich text and syntax highlighting.
- Proportional-font precision.
- Word wrapping and horizontal scrolling.
- Mouse drag selection and mouse-wheel scrolling.
- Very large documents.

These omissions are intentional for C117. The reusable editing architecture,
keyboard selection, vertical viewport following, and existing managed file
services are the proven phase boundary.

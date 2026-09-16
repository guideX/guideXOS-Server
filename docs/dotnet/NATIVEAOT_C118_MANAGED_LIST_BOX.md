# NativeAOT C118: Reusable Managed List Box

## Scope and architecture

C118 adds `GuideXosListBox`, a small platform-neutral managed control for a
bounded sequence of printable ASCII labels with one selected item. It is
implemented beside `GuideXosTextInput` and `GuideXosTextArea` in the managed
HostLogProof surface and uses the existing `GuideXosInputEvent` path:

`PS/2 / pointer -> compositor -> NativeAotManagedSurface -> managed input event -> application/control`

The control does not inspect scan codes or pointer hardware. An ordinary
managed application can host an instance, populate it with `TryAdd`, route
the existing managed input event to it, and consume its selected index or
activation result.

## Public/shared API

The main API is `GuideXosListBox`:

- `TryAdd(string)` appends a bounded label and returns `Added` or `Rejected`.
- `Clear()` removes all items and resets selection and viewport.
- `Reset()` clears items and transient focus state.
- `ItemCount`, `GetItemLabel(index)`, `SelectedIndex`, `SelectedLabel`, and
  `HasSelection` expose bounded application-facing state.
- `Focus`, `Blur`, `HandleKey`, `HandlePointerDown`, and `Render` provide the
  control lifecycle and input/render operations.
- `GuideXosListBoxResult` distinguishes `Ignored`, `Focused`,
  `SelectionChanged`, `Activated`, and `Rejected`.

The control stores only labels and an index. Applications map the selected
index back to their own file or record data; the control does not own
arbitrary application objects.

## Bounds and storage

All ceilings are explicit and fixed at construction:

| Property | Default | Supported ceiling | C118 picker |
| --- | ---: | ---: | ---: |
| Item count | 16 | 64 | 64 |
| Label length | 48 UTF-16 code units | 127 | 127 |
| Visible rows | 4 | 16 | 4 |
| Render width | 48 bytes | 61 bytes | 56 bytes |

Labels are copied into a fixed `char[]` slab with a fixed per-item length
array. Only printable ASCII (`0x20` through `0x7e`) is accepted, keeping the
existing byte-oriented surface path deterministic. Capacity and label
overflow are rejected before any item, selection, or viewport state changes.
The picker uses the full 64-item control capacity, matching the existing
bounded directory enumeration ceiling.

## Selection, focus, keyboard, and pointer behavior

`SelectedIndex` is `-1` for an empty list. The first accepted item selects
index `0`. A non-empty list always keeps its selected index in range.

Only a focused instance handles keys. Up and Down move by one and clamp at
the first and final item. Home selects the first item; End selects the final
item. Enter returns `Activated` for the current selection. Changing selection
returns `SelectionChanged`; Enter does not masquerade as a selection change.
Escape remains the containing picker/dialog application's cancellation key.

`HandlePointerDown` checks a caller-provided control origin, render width,
visible row count, and line height. A click inside a valid row focuses and
selects that row. A click in an unused row focuses without changing selection.
Clicks outside the control are ignored. Pointer selection alone does not
activate an item; the Open picker uses Enter or its existing Open button.

Each instance owns its own focus, selection, and viewport state. Blurring one
list cannot change another list, and an unfocused list ignores navigation.

## Viewport and rendering

`FirstVisibleIndex` is bounded to `0..max(0, ItemCount-VisibleRowCount)`.
Whenever selection moves above or below the viewport, the control adjusts
the first visible index until the selected row is visible. Home returns the
viewport to the first item and End brings the final item into view. Empty
lists reset the viewport to zero; clear and repopulation cannot leave stale
viewport state.

Rendering uses the existing managed surface text API. Each visible row is
drawn as `> label` for the selected item and `  label` otherwise. Labels are
clipped to the configured render width, and unused visible rows are blank.
No graphics subsystem or scrollbar chrome was added.

## Open picker integration

`GuideXosFilePicker` now owns one `GuideXosListBox` for its enumerated
candidates. `BuildCandidates` filters directory entries and populates the
control; it no longer owns a separate selected-index navigation algorithm.
The picker still owns enumeration, extension filtering, path validation,
stat/read behavior, dialog state, and result semantics. The list box owns
candidate display, selection movement, pointer hit testing, viewport state,
and activation reporting.

The Save picker renders the same reusable candidate list where present but
continues to route Save As keyboard input to `GuideXosTextInput`, preserving
the C116 filename workflow and overwrite state machine without a dialog
redesign.

The C118 fixture contains eight `.TXT` candidates, while four rows are
visible. The proof selects `02-POINT.TXT` by pointer, exercises Down, Up,
Home, End, scrolls down and back up, activates `SECOND.TXT`, edits its loaded
text area document, saves it as `THIRD.TXT`, and reopens the saved file
through the list box.

## C116/C117 interoperability and runtime impact

The list box consumes the same `GuideXosTextInputKey` values already used by
the text input and text area controls. No launch-flag encoding was widened.
The C118 composite retains the C115, C116, and C117 managed feature defines,
so the proof also exercises text input, multiline editing, selection,
viewport behavior, Save As, overwrite decline/confirmation, and reopen.

C118 does not change:

- Host ABI version `1`;
- Host ABI table size `104`;
- capability bits;
- NativeAOT runtime source;
- GC source or initialization;
- VFS interfaces or implementation;
- the resident managed lifecycle or executable mapping path.

The list and its tests allocate only fixed-size managed arrays determined by
explicit constructor bounds. There is no unbounded growth, reflection, or
runtime/GC special case.

## Validation evidence

The dedicated runner is
`scripts/dotnet/run-c118-managed-list-box.ps1`. It requires at least three
fresh accepted QEMU boots and writes its manifest and serial-derived evidence
under:

`out/dotnet/c011ec118-managed-list-box/`

The serial channel is authoritative. The manifest records repository identity,
binary hashes, list bounds, population and transition evidence, pointer and
keyboard events, viewport transitions, activation, canonical paths, exact
VFS byte/content verification, Notes integration, C116/C117/application
regressions, lifecycle markers, and per-boot results. The expected edited
document hash is recorded alongside byte-for-byte VFS comparisons.

Focused control tests cover empty and one-item lists, population/clear/
repopulation, item and label capacity, initial/no selection, all required
navigation and activation cases, pointer hit testing, viewport movement,
selection visibility, clear/repopulation validity, focus isolation, reset,
and repeated activation.

## Limitations and out of scope

C118 intentionally does not provide multiple selection, check-box rows,
arbitrary item templates, icons, columns, tree hierarchy, drag reorder,
type-ahead search, unbounded-data virtualization, double-click timing,
mouse-wheel support, or rich accessibility metadata.

# NativeAOT C125 — Managed determinate ProgressBar

## Purpose

C125 adds `GuideXosProgressBar`, a reusable managed presentation control for
bounded determinate progress. It is implemented above the existing managed
surface text API and does not change the Host ABI, capabilities, input
transport, NativeAOT runtime, GC, or VFS.

Managed Notes uses one instance to show document-buffer usage:

`GuideXosTextArea.Length / GuideXosTextArea.MaximumCharacters`

The live Notes text area is configured with a 256-character capacity, so the
production bar is range `0..256`, with its value always synchronized from the
authoritative `GuideXosTextArea.Length` property. No second document-length
counter is maintained.

## Reusable API and state

`GuideXosProgressBar` stores only bounded presentation state:

* `X`, `Y`, `Width`, and fixed `Height` of 18 pixels;
* inclusive `Minimum`, `Maximum`, and `Value` integers;
* `Visible`, `Percentage`, `FillCellCount`, and `FilledCells` derived state;
* one bounded rejection diagnostic counter.

The API consists of the constructor, `TrySetRange`, `TrySetValue`,
`TrySetBounds`, `SetWidth`, `SetVisible`, `Render`, and `Reset`.

The supported numeric domain is non-negative `0..65535`. A range requires
`Maximum > Minimum`. Range changes that are invalid or do not contain the
current value are rejected before mutation. Values outside the current range
are rejected before mutation. Negative values, above-ceiling values, reversed
ranges, equal endpoints, and geometry arithmetic that would exceed the
bounded surface contract are rejected without corrupting prior state.

Progress uses integer floor arithmetic with `long` intermediates:

```text
normalized = Value - Minimum
span       = Maximum - Minimum
percentage = normalized * 100 / span
filled     = normalized * fillCellCount / span
```

The minimum is exactly 0% and zero filled cells; the maximum is exactly 100%
and every fill cell. No floating point or formatting allocation is used.

## Geometry and rendering

The bar is a single fixed text row. X and Y are bounded to `0..4095` with
overflow-safe endpoint checks. Width is pixel-aligned to the existing 8-pixel
text grid, from 64 through 440 pixels. The representation reserves seven
columns for brackets, a separating space, and the percentage, leaving a
maximum of 48 fill cells. Notes uses width 312 pixels, giving 32 fill cells:

```text
[--------------------------------] 0%
[########------------------------] 26%
[################----------------] 50%
[################################] 100%
```

The exact fill count is derived state and is covered by the focused suite.
Rendering uses one 64-byte stack buffer, within the existing 63-byte surface
text limit. The compositor redraws the complete frame before managed draw
calls, so a width decrease cannot leave stale trailing characters.

Width changes and repositioning preserve range and value. Hidden bars issue no
text call, but retain their range, value, and geometry. Value changes while
hidden are accepted; showing the bar renders the latest value. `Reset`
restores visibility and clears rejection diagnostics while retaining the
configured presentation and progress state.

The control has no focus state, keyboard or pointer handlers, callbacks,
timers, task binding, or application operations. It is intentionally absent
from `GuideXosControlHost`; Notes registration remains seven controls in the
C124 order:

`Open, Save, Save As, Show path, Full path, File name, Document`.

The bar therefore cannot enter Tab or Shift+Tab traversal and progress updates
preserve the active interactive control. Picker modal ownership and focus
restoration remain controlled by the existing host.

## Notes integration

The bar is rendered on the document-usage row above the existing separator.
`RenderMain` calls a small synchronization method that sets the bar value from
`_textArea.Length` before drawing. This keeps it correct after initial load,
Open, insertion, LF insertion, Backspace, Delete, selection replacement,
reload, Save, and Save As. Path-only operations do not alter the value.

The C125 proof fills the real text area to its configured 256-character
capacity through a bounded proof path, verifies 100%, attempts one additional
character, and verifies that the text area rejects it and the bar remains at
256/256. Save and reload preserve the full-capacity value.

Full-path/file-name radios and Show path remain presentation-only with respect
to progress. They can update or hide the path label without changing the bar.

## Interoperability and proof strategy

C125 retains C116–C124 managed controls and uses the existing lightweight
proof-image strategy. The dedicated runner is
`scripts/dotnet/run-c125-managed-progress-bar.ps1`; it requires three fresh
accepted QEMU boots and records serial, build, source-hash, lifecycle, and
manifest evidence under `out/dotnet/c011ec125-managed-progress-bar/`.

The focused suite covers construction, valid and invalid ranges, ceiling and
overflow rejection, state preservation, exact boundary and intermediate
arithmetic, deterministic rounding, geometry rejection, rendering, hidden
updates, resizing, stale-tail safety, reset, and independent instances.
The production sequence covers Notes editing, selection replacement, path
presentation, focus order, modal isolation, capacity, overflow, Save, Save As,
reload, and resident lifecycle behavior.

## Allocation, bounds, and runtime impact

Each bar contains scalar state only. Its render buffer is fixed at 64 bytes and
its fill-cell count is capped at 48. Notes owns one bar. There are no dynamic
collections, histories, event subscriber lists, per-frame growing buffers, or
GC/runtime changes. C125 adds no Host ABI call or table entry; Host ABI remains
version 1 with table size 104.

## Limitations and out of scope

C125 does not provide indeterminate progress, spinners, animation, timers,
cancellation, task binding, background jobs, progress callbacks, gradients,
colors, native filled rectangles, vertical or nested bars, automatic layout,
or accessibility metadata. It is a bounded text-backed determinate control
only.

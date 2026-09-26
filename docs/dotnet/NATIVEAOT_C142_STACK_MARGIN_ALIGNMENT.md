# NativeAOT C142 — Vertical Stack Per-Member Margins and Alignment

## Scope and baseline

C142 extends the C141 `GuideXosVerticalStack` with bounded per-member layout
metadata. The C141 baseline remains the authority for fixed capacity 8,
non-owning membership, leaf-control-only support, visibility-aware layout,
padding, spacing, `ContentHeight`, ScrollView composition, focus reveal, and
popup behavior. C142 does not change ABI v1, the native table size 104, the
runtime, input transport, ownership, focus, or capture architecture.

The Stack remains a geometry-only one-dimensional engine. Applications still
create, register, render, focus, and own the controls. A control may remain a
direct `GuideXosScrollView` member while the same object is referenced by one
Stack for placement.

## Member metadata

The existing `IGuideXosVerticalStackMember` contract now exposes four integer
margins and `GuideXosVerticalStackHorizontalAlignment`. The concrete leaf
controls expose the same metadata publicly, plus bounded `TrySetMargins` and
`TrySetHorizontalAlignment` methods. Margins are non-negative integers capped
at 1024. Property assignment clamps to that range; the `TrySet*` methods reject
invalid input without changing the prior value. No style dictionary,
reflection, floating-point value, or observer system is used.

The alignment enum is deliberately small:

```text
Stretch (default), Left, Center, Right
```

`WidthPolicy.KeepWidth` remains source-compatible as the C141 compatibility
policy. For a default Stretch member it retains the current arranged width.
Explicit Left/Center/Right metadata uses the desired width captured before
Stack arrangement, so changing `Stretch -> Center -> PerformLayout()` cannot
turn a previous stretched width into the new requested width.

## Horizontal arrangement

The inner rectangle is the Stack frame after Stack padding. For Stretch:

```text
availableWidth = innerWidth - MarginLeft - MarginRight
member.X       = innerLeft + MarginLeft
member.Width   = availableWidth
```

Character-aligned controls floor the resulting width to the existing 8-pixel
cell boundary. A negative available width is clamped to zero for calculation;
because supported controls have a positive minimum width, the layout is then
rejected atomically and no negative rectangle is written.

Left preserves the desired width and places the member at
`innerLeft + MarginLeft`. Center preserves the desired width and centers it in
the usable area after both margins. Integer division floors the half remainder,
so an odd spare pixel is biased to the right. Right preserves the desired width
and places the member's right edge at `innerRight - MarginRight`. Fixed-width
members may extend outside the usable region only while their existing bounded
control coordinates remain valid; no wrapping or silent negative coordinate is
allowed.

## Vertical arrangement

For each visible member in insertion order:

```text
currentY = Stack.Y + TopPadding
currentY += MarginTop
member.Y = currentY
currentY += member.Height
currentY += MarginBottom
if another visible member follows:
    currentY += Spacing
```

`ContentHeight` includes top and bottom Stack padding, every visible member's
top/bottom margin, every visible height, and inter-visible-member spacing.
There is no margin collapsing. Hidden members consume no height, margin, or
spacing. Consecutive hidden members therefore cannot leave dangling margins or
double spacing. Disabled members remain laid out normally.

Metadata may be changed after insertion. A subsequent explicit
`PerformLayout()` or `PerformLayout(scrollView)` reads the new values. Ordinary
layout uses fixed arrays and bounded loops; it performs no LINQ, enumeration,
reflection, boxing-heavy metadata work, or per-layout allocation.

## ScrollView, focus, and popup integration

`PerformLayout(scrollView)` first writes the controls' current logical bounds,
then asks the existing ScrollView to synchronize its derived extent. ScrollView
still owns clipping, translation, hit testing, wheel routing, viewport offset,
ScrollBar synchronization, focus traversal, and focus reveal. Margins are
layout space, not a reveal target: focus reveal uses the actual control
rectangle, including the current arranged Y after its top margin.

ComboBox popup geometry continues to originate from the ComboBox's current
arranged bounds. Popup follow-up, commit, close, and transient capture release
remain control/host behavior. PopupMenu, Button, RadioButton, CheckBox,
ProgressBar, and Separator remain outside the Stack ownership boundary.

## Production demonstration

`ManagedVerticalStackDemo` keeps eight direct ScrollView members and two host
registrations. It intentionally mixes Left, Center, Right, and Stretch, uses
horizontal insets and varied vertical margins, and makes the surface taller
than the viewport. The registered CheckBox toggles ProgressBar visibility
through a real callback and calls `PerformLayout(scrollView)`. The resulting
ProgressBar margins disappear, the right-aligned Button moves upward, the
content extent shrinks, the viewport/ScrollBar clamps and synchronizes, and a
real pointer can activate the Button at its new arranged position.

## Focused and retained tests

The C142 focused suite covers 20 metadata cases, 12 vertical-spacing cases,
12 horizontal-layout cases, 14 ScrollView integration cases, and 10 popup/
control integration cases: 68 total. It covers default and invalid metadata,
all four alignments, odd-width center rounding, desired-width preservation,
visibility and content-height shrink/grow, margins in focus reveal, translated
hit tests, ComboBox relocation, Button placement, RadioButton group behavior,
CheckBox state, ProgressBar value preservation, and Separator insets.

C141 remains 56/56. The retained regression targets are C140 58/58, C139
54/54, C138 62/62, C137 46/46, C136 36/36, C135 40/40 plus host 40/40,
C134 30/30, C133 44/44 plus host 24/24, C132 38/38 plus host 12/12, C131
34/34 plus host 23/23, C127 60/60, C128 46/46, C129 31/31, and C130's
explicit deterministic skip.

## NativeAOT proof and restoration

The C142 runner builds the production NativeAOT composite with the existing
runtime pack, stages a phase-specific proof kernel, and performs three fresh
QEMU boots. Each boot must prove launch, registration count, initial/final
content geometry, real wheel movement, ScrollBar drag, Left/Center/Right/
Stretch pointer hits, dynamic visibility relayout, ComboBox popup follow-up,
focus reveal, final viewport validity, and `capture=none` / `drag=none`.

After the proof, the canonical ordinary kernel is rebuilt and copied to both
`kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`. The protected
`ESP/ramdisk.img` is not a source input and must retain its authoritative
starting hash. Generated proof artifacts stay under `out/dotnet` and are not
staged.

The final verification record is emitted by the runner manifest at
`out/dotnet/c142-stack-margin-alignment/c142.manifest.json`; hashes and serial
digests are recorded there after the fresh boots and ordinary restoration.

Final verification on 2026-09-26 completed with three fresh physical-QEMU
boots, all `PASS`. The C142 composite NativeAOT ELF SHA-256 is
`1DAAEDBD0CD5E50C2B83BA8A5EA5CD7A2865C8965EA30FCD2BA77C4EDA61FD5F`; the
staged C142 ramdisk SHA-256 is
`98136A3B6B458DDD7F99B2269375D39118727523B945543542D9061613B5E57D`; and
the restored ordinary kernel SHA-256 is
`34C0A2C7ED64FD43907F77F0428437D74BDC6740A12A56D974A5522F54DD32AB`.
The protected `ESP/ramdisk.img` retained its authoritative SHA-256
`E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.
Boot serial digests and per-boot outcomes are recorded in the manifest.

## Deferred capabilities

C142 intentionally does not add Grid, horizontal stacking, wrapping, Dock or
Flex behavior, weighted or percentage widths, min/max measurement, text
measurement, vertical alignment, margin collapsing, nested layouts, recursive
measurement/arrange passes, style inheritance, responsive rules, or a second
focus/capture system.

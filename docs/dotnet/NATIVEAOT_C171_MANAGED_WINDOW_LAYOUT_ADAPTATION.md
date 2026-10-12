# NativeAOT C171 Managed Window Layout Adaptation

## Decision

C171 uses creation-time adaptive sizing for Managed Settings Center. The native
desktop window manager can drag-resize a native window, but the managed window
bridge does not report later geometry changes to the app or request a managed
surface refresh. The Settings surface therefore keeps native drag-resize
disabled and offers supported sizes in its Options menu. Choosing a size
recreates the Settings surface at the requested dimensions and recomputes its
managed layout. This is the bounded Path B design; it does not change the host
ABI.

The Options menu provides the existing Apply, Reset, and Close actions plus:

- Compact: outer window 512 × 260 pixels.
- Expanded: outer window 720 × 420 pixels.

The normal initial outer size remains 560 × 340 pixels. The native title bar is
24 pixels high, so client heights are 236, 396, and 316 pixels respectively.
The supported outer bounds are 512 × 222 through 720 × 420 pixels; calculated
client bounds are 512 × 198 through 720 × 396 pixels.

## Layout and state handling

`ManagedSettingsCenter.TryCalculateLayout` validates the requested bounds and
derives the client width, height, Settings view rectangle, group widths, and
footer position. At widths below 560 pixels the view begins lower to fit the
compact navigation chrome. The existing GroupBox, VerticalStack, ScrollView,
scroll bar, and control host are reused. Layout recomputation recalculates the
view extent, rebinds the scroll bar, clamps the prior scroll offset to the new
maximum, and reattaches the existing controls.

Before recomputation, transient menus and ComboBox popups close and their
capture is released. The surface replacement path retains the app and window
identity while advancing the surface generation. It restores focus to the
Options menu owner after replacement; focus within a reflow retains the
previous active control and member where they still exist. Pending Default Apps
selection stays in the existing page model and is not written to association
storage by resizing or layout work.

At Compact size, the Apps view exposes a 96-pixel viewport over 198 pixels of
content, with a maximum scroll offset of 102 pixels. Scrolling and keyboard
focus traversal reveal the lower controls. A ComboBox opened near the viewport
edge remains owned by the control host and closes cleanly on layout changes.
The maximum-width ComboBox text path is covered by the C170 focused rendering
suite. Rendering retains the existing fill-rectangle limit of eight; no resize
indicator or extra fill operation was added.

The managed view capacity remains 24. Settings composes four section groups
with 18 leaf controls; its control host capacity is 12. The Default Apps group
and stack capacities remain eight. Text cells use the existing 8 × 18 pixel
metrics. No association API or storage logic participates in geometry changes.

## Host regressions and stress

The combined C169/C170/C171 host gate passed 18 suites and 603 cases. The C171
focused suite passed 35 cases and its stress suite passed 7 cases. Stress
coverage records:

| Operation | Iterations |
| --- | ---: |
| Layout recomputations | 1,000 |
| Alternating geometry changes | 500 |
| Focus transitions | 500 |
| ScrollView movements | 250 |
| ComboBox cycles | 250 |
| Lifecycle iterations | 100 |

Measured hot-path allocation was zero bytes for both layout and the general
page, with zero geometry drift. Default Apps attach validation was `valid`.
The independent C166 association prerequisite passed 604 native cases with
zero failures; its persistence stress ran 1,000 iterations.

## Guest proof

Three focused guest boots used QEMU 11.0.0 and a 1280 × 800 framebuffer. The
focused run is `20261012-142017-c18ea030` under
`out/dotnet/c171-window-layout/settings-ui-proof-runs/`:

1. Baseline 560 × 340 to Expanded 720 × 420, preserving a pending Default Apps
   choice through replacement and applying it afterward.
2. Compact 512 × 260, scrolling through Settings, Tab and Shift+Tab traversal,
   keyboard and pointer ComboBox selection, Apply, and Reset.
3. Repeated Compact and Expanded transitions across Settings page and window
   lifecycles.

The guest screenshots and serial logs are indexed by
`out/dotnet/c171-window-layout/c171-focused-proof-manifest.json`. The independent
settings proof record is
`out/dotnet/c171-window-layout/c167-settings-ui-proof-manifest.json`; the C170
focused manifest is
`out/dotnet/c171-window-layout/c170-focused-proof-manifest.json`. The C171
focused manifest records the actual outer and client sizes and SHA-256 values
for its referenced screenshots and logs. The evidence covers the stated QEMU
framebuffer configuration.

The proof-disabled production candidate passed three more ordinary boots in
`out/dotnet/c171-window-layout/production-acceptance/ordinary-boots/ordinary-run-20261012-145603-936ab0b0/`:
Boot 1 used Expanded, Boot 2 used Compact with scrolling and focus traversal,
and Boot 3 repeated Compact and Expanded through Settings lifecycle changes.
Default Apps Apply and Reset returned association storage to `NoOverride`.
The C166A2 manifest records candidate installation and all three boot results;
the C170 ordinary production manifest independently verifies its 59 referenced
artifact hashes and confirms `PROOF1` output is absent.

## Production boundary and compatibility

The ordinary acceptance lane builds a clean production candidate with
`PROOF1`-only diagnostic output excluded and installs it only after all
ordinary boots pass. Candidate and installed hashes match. The original dirty
C170 `ESP/ramdisk.img` was backed up byte-for-byte before installation; its
SHA-256 is `A413E61963C4B233EBEA330CF3713D2978D9A098A769582269972242E575991B`,
while the installed production ramdisk SHA-256 is
`2DD5D7195C1A18098B3DDEAB0B680AC8B0B2C861ED45C5C924BFC684F9C06334`. The
original bytes remain in `out/dotnet/c171-window-layout/protected-c170-ramdisk.img`
and `out/dotnet/c171-window-layout/protected-media-before/esp-ramdisk.img`.
The C166A2 acceptance manifest records the installation, ordinary boot
results, final `NoOverride` association state, and protected-media baseline.

The host ABI remains version 4 with 128-byte association requests. The
association response remains 328 bytes and the settings record remains GXSETT
version 2 at 26 bytes. There are no storage format or host ABI changes. C128
remains explicitly unverified.

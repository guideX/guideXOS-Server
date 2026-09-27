# NativeAOT C143 Managed GroupBox

## Architecture

`GuideXosGroupBox` is a bounded, non-owning titled section. It stores a frame, a bounded ASCII caption, visibility and enabled state, and up to eight associations to leaf controls. It does not register, construct, destroy, focus, route input to, lay out, clip, or translate its associated controls. Removing or clearing a member only removes the association; registration, callbacks, and lifetime remain with the application.

Supported members are `Button`, `CheckBox`, `Label`, `Separator`, `RadioButton`, `ProgressBar`, and `ComboBox`. `Panel`, `ScrollView`, `TextArea`, `ListBox`, `ScrollBar`, and another `GroupBox` are rejected. Duplicate association, a second GroupBox association, and capacity overflow are reported explicitly. The fixed capacity is eight. No dynamic member collection is used.

The caption accepts empty text or up to 48 printable ASCII characters. Null, control characters, non-ASCII characters, and overlong captions are rejected atomically. Rendering clips the displayed caption to the frame width without changing the stored caption.

## Geometry and rendering

Bounds use integer coordinates. Width is 96–504 pixels in multiples of eight; height is 54–288 pixels in multiples of 18. The default content padding is eight pixels and can be set from zero to 64. The content rectangle is computed live:

```text
ContentLeft   = X + 8 + ContentPadding
ContentTop    = Y + 18 + ContentPadding
ContentWidth  = Width - 2 * (8 + ContentPadding)
ContentHeight = Height - 36 - 2 * ContentPadding
```

The frame uses text rows: `+--- title ---+` across the first row, vertical sides on interior rows, and a bottom border. The surface's existing clipping and translation state applies when the frame is rendered through a ScrollView. Disabling a section leaves the frame appearance unchanged; supported member controls render and route input using their effective disabled state. The section background is not an input target. GroupBox does not add a clipping subsystem or consume ordinary clicks.

`ValidateMembershipGeometry` checks current live leaf bounds against the current content rectangle. It does not cache member geometry, so VerticalStack can move controls and the application can validate the resulting layout afterward. GroupBox has no automatic sizing or layout pass.

## Composition and state

Panel remains a single-level membership/lifecycle grouping API. GroupBox is a visual and effective-state association. VerticalStack remains geometry-only and arranges leaf controls explicitly. ScrollView remains responsible for logical coordinates, translation, clipping, hit testing, focus reveal, wheel input, and ScrollBar integration.

A leaf may simultaneously belong to one GroupBox, one VerticalStack, and one ScrollView. Each association has a distinct responsibility. C143 rejects a leaf that already belongs to a Panel, keeping Panel lifecycle grouping separate from GroupBox section state. The GroupBox frame itself may also be a direct ScrollView member so its frame and associated leaves translate and clip together. ScrollView capacity is nine to represent one frame and up to eight leaf controls. GroupBox is not a recursive container and cannot be added to a Stack or another GroupBox.

Applications set Stack bounds from `ContentLeft`, `ContentTop`, and `ContentWidth`, then call `PerformLayout`. C142 member margins and alignment remain in effect. Dynamic leaf visibility causes Stack relayout; it does not resize the GroupBox.

GroupBox is non-focusable. Its `Visible` and `Enabled` values gate associated controls through each control's effective state, while preserving the control's own state. Hiding or disabling the section makes its members unavailable; restoring the section restores each member's own visibility and enabled value. Host focus recovery and transient popup cancellation use existing control lifecycle paths.

GroupBox membership does not create radio exclusivity. `GuideXosRadioGroup` remains the explicit grouping mechanism. ComboBox and PopupMenu continue using the existing single transient capture lease. If a section is hidden or disabled while a transient popup is open, its owner becomes unavailable and the existing lifecycle handling closes the popup and releases capture.

## Focused and retained regressions

The C143 focused suite contains 68 cases:

| Area | Cases |
| --- | ---: |
| Core GroupBox behavior | 20 |
| GroupBox, Stack, and ScrollView composition | 10 |
| Rendering and scroll behavior | 10 |
| Focus and lifecycle | 10 |
| Popup behavior | 8 |
| Dynamic relayout | 10 |

The C143 launch runs the C143 focused suite and the retained C126, C141, and C142 focused suites. Evidence for C127–C140 remains in their original phase artifacts. C130's retired direct managed Shift/Tab helper is recorded as a deterministic `SKIP`; production Shift/Tab input remains covered by C129. C141 and C142 each report their original focused totals.

## Production NativeAOT proof

The production `Managed GroupBox` application composes a `Server Options` GroupBox, a VerticalStack, eight leaf members, and a ScrollView. Six controls are host-registered; the ScrollView holds the GroupBox frame plus eight leaves. Stack bounds are taken from the GroupBox content rectangle. C142 margins and Left, Center, Stretch, and Right alignment are used in the section.

The production input proof covers launch and registration, frame rendering, wheel scrolling, translated pointer hit testing, ComboBox open and commit, explicit RadioGroup selection, CheckBox activation, ScrollBar drag and release, offscreen Tab and Shift+Tab reveal, dynamic member hiding and Stack relayout, moved Button activation, PopupMenu invocation and commit, and section hide/disable while transient capture is active. The final state requires a valid viewport and layout, no popup capture owner, and no drag owner.

The proof runner records the NativeAOT composite hash, proof-kernel hash, each fresh-boot serial hash, the C143 manifest, and the registration/layout/final-state markers under `out/dotnet/c143-managed-groupbox`. Three independent fresh QEMU boots are required.

## Ordinary boot and compatibility

After proof-kernel use, rebuild and restore the canonical ordinary kernel at `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`, verify no C143 diagnostic marker is present, and run three fresh ordinary QEMU boots. Verify the protected `ESP/ramdisk.img` hash against the value captured before the phase. C143 changes no ABI declarations: ABI v1 and call table size 104 remain in force. Panel, ScrollView, and VerticalStack retain their existing non-owning or geometry-only roles. GroupBox owns no controls and introduces no recursive ownership/layout tree, capture stack, second focus system, or synthetic Tab `KeyChar`.

## Validation record

Outcome: **A — reusable GroupBox implemented and validated.** The focused C143 suite passed 68/68. The C143 startup also retained C126 at 60/60, C141 at 56/56, and C142 at 68/68. Existing phase evidence records the earlier focused regressions:

| Phase | Result |
| --- | --- |
| C127 Panel | 60/60 PASS |
| C128 lifecycle | 46/46 PASS |
| C129 Shift/Tab transport | 31/31 PASS |
| C130 retired helper | deterministic SKIP; C129 is the replacement |
| C131 CheckBox | 34/34; host 23/23 PASS |
| C132 RadioButton | 38/38; host 12/12 PASS |
| C133 ComboBox | 44/44; host 24/24 PASS |
| C134 transient capture | 30/30 PASS |
| C135 PopupMenu | 40/40; host 40/40 PASS |
| C136 secondary pointer | 36/36 PASS |
| C137 mouse wheel | 46/46 PASS |
| C138 ScrollBar | 62/62 PASS |
| C139 shared viewport | 54/54 PASS |
| C140 ScrollView | 58/58 PASS |
| C141 VerticalStack | 56/56 PASS |
| C142 margins and alignment | 68/68 PASS |
| C143 GroupBox | 68/68 PASS |

The final C143 manifest reports three independent QEMU boots as PASS. The production composition registers six host controls, associates eight leaf controls with the `Server Options` GroupBox and VerticalStack, and tracks those eight leaves plus the frame in the ScrollView. Its initial frame is `(29,100,272,270)` and content rectangle is `(45,126,240,218)`. The final viewport offset is 86, with valid layout, no popup capture, and no drag owner.

| C143 artifact | SHA-256 |
| --- | --- |
| NativeAOT composite ELF | `C4222C7AA3D0360909ED2CF5163EF99EE54A77B31F43397083DC7AC0F76F5A4E` |
| C143 proof kernel | `E0D9867BDB18B9A0674AF392BE1B10A41D849E665D1DA474A16F5972156BA507` |
| C143 boot 1 serial | `236DBBCD265081A1DD5A0E864B3C6F6CF8FB2C39AAD7773130503E2C1CAF1459` |
| C143 boot 2 serial | `4B54B8F26467410E2C99A8D5D325ADAACCF9B591D2BACECF0D2A059E1420F0DC` |
| C143 boot 3 serial | `3F8626BFEA31830AD64344065E5656DBF52CBDEFAF78F9F27A06C939094AA271` |

The restored ordinary kernel and `ESP/kernel.elf` match at `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203`. Three fresh ordinary boots passed the kernel main-loop and Navigator smoke checks. Their serial hashes are `100849FD8A050330B87883CFA9A161A5114C9537B522AD90D7B03D446361F85B`, `CD5C6CECF3833E52EC88192C82E8B7686C97A23F3E2328E2A7E730148B3C78A3`, and `7DE8A8601FB5A7DE7968091D4B294B5F2C7C5B4EB6A8E988933EC5486ACFA9AC`. The ordinary kernel binary contains no C143 proof markers. `ESP/ramdisk.img` remains at its starting SHA-256, `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.

The C143 manifest is `out/dotnet/c143-managed-groupbox/c143.manifest.json`; the ordinary boot manifest is under `out/dotnet/c143-managed-groupbox/ordinary-validation/`. ABI v1 and call table size 104 are unchanged.

## Deferred

Nested GroupBoxes, collapsible sections, animation, automatic size-to-content, Grid, recursive children, GroupBox-owned Stack or controls, title icons, checkable headers, radio grouping by section, theme/style support, automatic text measurement, and per-section layout engines remain deferred.

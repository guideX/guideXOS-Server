# NativeAOT C144 — Managed Settings Center

## Purpose

C144 is an application-scale composition proof. It builds a user-facing Settings Center from the managed controls, layout primitives, shared scrolling surface, popup routing, and lifecycle rules already proven through C143. It does not add an ownership tree, data-binding framework, persistence layer, or ABI surface.

## Application architecture

`ManagedSettingsCenter` has four vertically arranged sections:

| Section | Controls | Behavior |
| --- | --- | --- |
| Appearance | density ComboBox, status CheckBox | updates the working snapshot and controls the System preview |
| Input | standard/natural RadioGroup, speed ComboBox, keyboard-tips CheckBox | one external CheckBox enables or disables the full section |
| System | summary/detail RadioGroup, report-format ComboBox, ProgressBar, explanatory Label | independent choices update the preview text and applied progress value |
| Advanced | Apply and Defaults Buttons | shown or hidden by the header toggle |

The header keeps `Advanced settings` available while the Advanced GroupBox is hidden or below the viewport. The Options button opens a bounded Apply / Reset to defaults / Close PopupMenu.

The application owns every control. Each leaf has at most one GroupBox association. The GroupBoxes remain non-owning and do not arrange or clip controls. Four VerticalStacks arrange the 17 section leaves with C141/C142 padding, margins, and alignments. One ScrollView contains four GroupBox frames and the 17 ordinary leaf controls as 21 direct, non-owning members; one bound ScrollBar controls its viewport. RadioGroups coordinate selection without taking control lifetime. Popup capture is transient and has one owner.

## Capacity audit

The Settings Center uses:

- 4 GroupBox frames;
- 17 grouped leaf controls, distributed 3 / 5 / 6 / 3;
- 21 direct ScrollView members;
- 9 registered controls: ScrollView, ScrollBar, three ComboBoxes, PopupMenu, Options Button, and two header CheckBoxes;
- a host capacity of 10, leaving one registration slot;
- a ScrollView capacity of 24, leaving three member slots at the application’s steady composition.

C144 raises the ScrollView’s fixed maximum from 9 to 24 after auditing the 4-frame + 17-leaf composition. The default capacity remains 8, and the existing eight-member rejection behavior remains covered. Each view allocates its fixed-length membership array at its validated constructor capacity; it never grows or resizes that array. `MemberEntry` has an object reference, kind, and two integer coordinates; on the current 64-bit layout this is 24 bytes after alignment. The C144 view’s 24 entries require 360 additional bytes over the former nine-entry maximum. Small-capacity and default-capacity views retain arrays sized to their own bounds. Capacity tests fill to capacity − 1, accept capacity, and reject capacity + 1 for ScrollView and host registration arrays.

## Settings state

The application keeps a bounded `working` snapshot and an `applied` snapshot. CheckBox, RadioButton, and ComboBox callbacks update the working copy. Dirty state is exactly `working != applied`; the header reports it and Apply is enabled only while dirty. Apply copies the working snapshot to the applied snapshot and does not move the viewport. Defaults updates multiple controls under a callback-suppression guard, recalculates layout, and leaves the viewport alone unless hiding content requires the existing viewport clamp. No setting is persisted to disk.

The System preview text is derived from the selected report format and detail RadioGroup. The ProgressBar summarizes the applied snapshot, so working edits do not masquerade as applied state. Defaults restores both independent RadioGroups and all three ComboBoxes deterministically.

## Visibility, enabled state, and lifecycle

The Advanced toggle changes GroupBox visibility without overwriting leaf visibility or enabled state. Showing Advanced grows content extent and updates the ScrollBar; hiding it removes its extent and clamps the viewport when needed. The Input toggle changes only the Input GroupBox enabled state. A member that is individually disabled remains disabled after the group is restored. Disabling the section while its speed ComboBox owns popup capture closes the popup and releases that capture.

The close/relaunch proof uses the ordinary managed App Model surface lifecycle. Each launch reconstructs the bounded settings composition from defaults, re-registers exactly nine controls, and restores empty popup and drag state. The Options menu Close command closes its popup capture before closing the surface.

## Input and focus

The Settings Center uses the existing single focus system. Labels, ProgressBars, and GroupBox frames do not become focus targets. The ScrollView traverses the visible section members in insertion order, skips hidden or disabled members, and reveals offscreen members when focus reaches them. Options and the header toggles remain host-level focus targets. The ScrollBar remains outside keyboard focus while still accepting pointer routing. Enter activates the currently focused Apply or Defaults member; it cannot repeat an older pointer activation after focus moves. Keyboard interaction exercises forward Tab, reverse Shift+Tab, Space, RadioButton arrows, ComboBox open/highlight/commit, and Button activation. Tab remains a KeyDown transport; C144 does not synthesize a Tab KeyChar.

Pointer input exercises CheckBoxes, independent RadioGroups, multiple ComboBoxes, translated hit testing, popup item selection and outside-click cancellation, the Options PopupMenu, the ScrollBar thumb and track-page clicks in both directions, and wheel scrolling. The ComboBoxes in Input and System sit below Appearance in the logical surface, so their dropdown routing is checked after scrolling. The ScrollView continues to translate member coordinates and own clipping; the popup keeps its existing transient-capture behavior.

## Focused tests and retained evidence

C144 focused coverage is 56 cases: 22 application-state cases, 21 composition cases, and 13 lifecycle cases. The application-state stress case performs 20 Advanced toggles, 10 ComboBox open/commit/cancel cycles, 10 radio changes, 20 wheel changes, four scrollbar-value transitions, and four Defaults/Apply cycles. The composition coverage checks independent GroupBox membership, one Stack per section, 21 direct members, capacity edges, growth/shrink/clamp, scrollbar synchronization, offscreen focus reveal, translated hit testing at different offsets, and registration boundaries. Lifecycle coverage checks effective state, own-state preservation, pointer-only ScrollBar drag routing without keyboard focus, popup invalidation, repeated composition reset, and final capture/drag state.

The C144 NativeAOT image reruns the 56 C144 application-state, composition, and lifecycle cases. Those cases reuse the same Settings Center instance and reset its bounded composition between phases. The C143 (68), C142 (68), C141 (56), C140 (58), C139 (54), C138 (62), C137 (46), C136 (36), and earlier C126 (60) focused suites remain prior evidence from the completed C143 baseline, referenced by the C144 manifest; this image does not claim those suites reran. Earlier C127–C135 suite totals, including the deterministic C130 `SKIP`, remain documented in the [C143 phase write-up](NATIVEAOT_C143_MANAGED_GROUPBOX.md); C144 does not claim those suites reran. The manifest records the exact rerun set and each boot’s serial SHA-256.

## NativeAOT and QEMU evidence

The production runner is `scripts/dotnet/run-c144-managed-settings-center.ps1`. It builds the managed NativeAOT composite and proof kernel, then performs three independent fresh QEMU boots. The QMP driver sends physical pointer, wheel, and keyboard events and waits for the corresponding managed serial markers before continuing. Each boot is required to finish with valid layout, membership, and viewport state and with no popup capture or ScrollBar drag owner.

The C144 evidence directory is `out/dotnet/c144-managed-settings-center`. Its manifest records the composite ELF hash, proof kernel hash, serial hashes, test totals, registration and capacity counts, content and viewport geometry, dirty transitions, Apply/Defaults evidence, input markers, and final lifecycle state.

## Completed proof results

The C144 NativeAOT composite build succeeded. The composite ELF SHA-256 is `FEFDE586AE4EB966404E7035E59747F1CC7B524DBC955A653652CA0263ACB103`; the C144 proof kernel SHA-256 is `120A23412319CB5E286FC11976A4A90A41A7664FE95847EB4D0BCDCED28F2302`. The proof kernel is preserved inside each boot’s `ESP` evidence directory. ABI v1 and the 104-byte host table are unchanged.

All three physical QEMU boots passed, each rerunning the 56 C144 cases (state 22/22, composition 21/21, lifecycle 13/13):

| Fresh boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 | PASS | `B63DE4C29415D150BC78780BF6592B44FA9248ED09006A4DE171BA2B5AE78252` |
| 2 | PASS | `DA1CB59906F89B3A73F5C00631BF0C4E3DA7DD06BAF6FF596DFDE5B576A63084` |
| 3 | PASS | `3F6A1FADBA5511EC55F3CFC33F4DE3B60B44FCC86DDA3C64C45F17DA92D2D4B9` |

The manifest records the initial content extent as 632 pixels and the final extent as 792 pixels, with a 206-pixel viewport and offset moving from 0 to 61. Each physical run recorded 68 effective wheel transitions, four ScrollBar drags, two track-page clicks (up and down), five outside-popup cancellations, 26 Advanced reveals, and 22 Advanced hides. Keyboard ComboBox commit, Space activation, RadioButton arrow selection, Button Apply, Shift+Tab, menu Close, and final empty capture/drag state all passed. Pointer and menu Apply/Defaults both passed while preserving the viewport where required. The C143 68, C142 68, C141 56, C140 58, C139 54, C138 62, C137 46, C136 36, and C126 60 focused suites are historical evidence referenced by the manifest; this C144 run does not claim they reran.

**Outcome A:** the multi-section application passed using the existing direct-member, single-focus, single-capture architecture. C144 repaired two bounded issues found during integration: non-focusable ScrollBars now accept pointer routing without joining Tab order, and keyboard Enter checks the currently focused button instead of a stale previous pointer activation. Neither repair adds a new primitive or ownership/focus model.

## Ordinary kernel restoration

After C144 proof work, restore the canonical ordinary kernel from the preserved ordinary `ESP/kernel.elf` and verify both hashes against the baseline. Confirm the protected ramdisk hash is unchanged, then run three fresh ordinary QEMU boots. The ordinary boot manifest and serial hashes are kept separate from the C144 NativeAOT evidence.

The ordinary build artifact and ESP kernel were restored to the preserved baseline SHA-256 `9C2AF5EFE44BA26DE678B3749B98BEC7180ED5A4F97A033C3976AF5CB06E3203`. The protected `ESP/ramdisk.img` remained `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`. The dedicated ordinary validator passed 3/3 fresh boots with the kernel main-loop and Navigator smoke markers:

| Fresh boot | Result | Serial SHA-256 |
| --- | --- | --- |
| 1 | PASS | `C5A8C71DDA91D03485D024C450DA1301BC06C3176681F0250EECA4C17AEE2FE7` |
| 2 | PASS | `8FFC5F81A29112AA6C44226AD52208BAEC34E6FE4AF992ACBE28B96E6CDAE443` |
| 3 | PASS | `37E1E83836CF3F7E09B0092E226BFF2DC9FFBA509D003D9EF36DBBCD15A07C5F` |

The ordinary boot manifest is written under `out/dotnet/c144-managed-settings-center/ordinary-boot/`.

## ABI and architecture

C144 preserves ABI v1 and the 104-byte ABI table. Panel remains non-owning; ScrollView remains a fixed-capacity, direct-member, non-owning viewport; VerticalStack remains geometry-only; GroupBox remains non-owning and non-layout; the application owns the controls. C144 adds no recursive ownership/layout tree, capture stack, second focus system, or synthetic Tab KeyChar.

## Deferred

Settings persistence, configuration databases, data binding, generated forms, nested containers, nested ScrollViews, grid/dock layout, themes, localization, search, profiles, and undo/redo remain outside C144.

# C124 — Reusable Managed RadioButton and Bounded RadioGroup

## Purpose

C124 adds two reusable managed guideXOS primitives:

- `GuideXosRadioButton`, a bounded mutually-exclusive choice with a label,
  geometry, selected state, enabled state, focus state, pointer handling,
  keyboard handling, and deterministic text rendering.
- `GuideXosRadioGroup`, an explicitly bounded registration and selection owner.

The group is fixed-capacity and uses registration order for local arrow
navigation. It has no reflection, dynamic discovery, event-subscriber list,
or unbounded collection.

## RadioButton API and bounds

`GuideXosRadioButton(x, y, width, height, label, maximumLabelLength)` stores a
fixed character buffer. The default label limit is 32 characters and the
supported maximum is 48 printable ASCII characters. Empty, non-printable, and
overlong labels are rejected without changing the previous label.

Geometry uses the existing surface coordinate model: width is 32 through 512
pixels, height is 18 through 128 pixels, and the bounded rectangle must remain
within coordinates 0 through 4095. `TrySetBounds` validates before mutation.

The public state is `Label`, `Selected`, `Enabled`, `IsFocused`, geometry,
`Group`, `GroupIndex`, and `RequestedGroupIndex`. `TrySelect` routes selection
through the group when registered; an ungrouped instance can be selected as an
independent choice. `SetEnabled`, `Focus`, `Blur`, `Reset`,
`HandlePointerDown`, `HandleKey`, `HandleCharacter`, and `Render` complete the
small control surface.

## RadioGroup API and boundedness

`GuideXosRadioGroup` defaults to and supports at most four members. It stores a
fixed `GuideXosRadioButton[]` registration array, a member count, a selected
index, and a rejection counter. Registration is explicit and ordered.

`TryRegister` rejects null, duplicate, already-grouped, and capacity-overflow
registrations. `TrySelect`, `TrySelectIndex`, `GetMember`,
`TryMoveNext`, `TryMovePrevious`, `Reset`, `Capacity`, `MemberCount`,
`SelectedIndex`, `SelectedMember`, and `HasSelection` provide the bounded API.
The no-selection representation is `SelectedIndex == -1` and
`SelectedMember == null`.

The invariant is zero or one selected member. Selecting a member clears every
other member in that group. Selecting the current member again leaves it
selected; radio activation never toggles the current choice off. Groups have
independent storage and selection state.

The generic initial policy is no selection. Applications explicitly select a
default. Managed Notes selects `Full path` on launch.

## Input policy

Pointer-down is accepted only inside an enabled button. The existing
`GuideXosControlHost` first verifies the bounded hit, synchronizes its active
control, and then routes the event once. Outside clicks are ignored; disabled
members return `Disabled` and do not change selection.

Focused Space is committed by `KeyChar`, while the matching `KeyDown` is
ignored. This preserves the existing transport’s exactly-once behavior. Space
on an already-selected member retains the selection. Unfocused, disabled,
Enter, and unrelated input are ignored or rejected according to the control
result. Enter intentionally does not activate a radio.

Left/Up moves to the previous enabled group member. Right/Down moves to the
next enabled member. Navigation wraps in registration order and selects the
target. Disabled members are skipped. The radio reports the target to
`GuideXosControlHost`, which performs the actual focus transfer, so C120
remains the only focus authority. Tab and Shift+Tab continue to use global
host registration order; each enabled radio is an ordinary focus target.

If the selected member is disabled, the group selects the next enabled member
and wraps. If none is enabled, selection becomes `-1`. Focus is cleared by the
button and normalized by the existing host rules. Reset clears selection and
detaches members so the same instances can be registered again.

## Rendering

Rendering uses the existing bounded text surface and a fixed temporary buffer:

- unselected: `( ) Label`
- selected: `(o) Label`
- focused unselected: `>( ) Label`
- focused selected: `>(o) Label`
- disabled unselected: `x( ) Label`
- disabled selected: `x(o) Label`

The label is clipped to the button’s bounded width; stored text is not
modified by clipping.

## ControlHost integration

`GuideXosManagedControlKind.RadioButton` and
`TryRegisterRadioButton` extend the existing explicit kind dispatch. The host
handles registration, capacity, focus transfer, Tab/Shift+Tab traversal,
pointer synchronization, disabled skipping, keyboard routing, and modal
scope. No second focus manager was added.

The host remains fixed at eight registrations. C124 Managed Notes uses seven:

`Open -> Save -> Save As -> Show path -> Full path -> File name -> Document`

The C122 path label and C123 separator remain presentation-only and are not
registered. The native Reload button remains outside this managed focus order.

Modal picker scopes receive input exclusively. Radio controls do not consume
picker input, and the existing saved-ID focus restoration returns to Open or
Save As after the picker closes.

## Managed Notes integration

Notes keeps `_currentPath` as the full canonical VFS path. The shared C122
`GuideXosLabel` instance is updated from the selected presentation mode:

- `Full path`: `Path: /system/apps/THIRD.TXT`
- `File name`: `Path: THIRD.TXT`

The radio group changes only the label’s presentation. The C121 `Show path`
checkbox independently controls label visibility. When unchecked, the label is
hidden regardless of the radio selection; changing the radio while hidden does
not reveal it. Re-showing the label uses the current radio mode. Open and Save
As update the same label instance using the new canonical path and selected
mode. VFS storage, file reads, writes, and canonical path ownership are
unchanged.

## Allocation and runtime impact

Each radio owns one fixed label buffer (at most 48 `char` values), scalar
geometry/state, and a scalar rejection counter. Each group owns one fixed
member array of at most four references plus scalar selection state. Host
registration remains a fixed array of at most eight entries. Arrow routing
uses bounded linear scans. No input history, reflection, dynamic event list,
selection framework, or per-input growing allocation was added.

C124 does not change Host ABI version 1 or the 104-byte host table. It adds no
capability bit, no input-transport field, no NativeAOT runtime change, no GC
change, and no VFS change. Resident lifecycle and heap-preservation behavior
remain the established C120 path.

## Proof and regressions

The focused suites cover construction and label/geometry rejection, all six
rendering states, pointer and Space selection, exactly-once input, disabled
behavior, bounded capacity, duplicate policy, no-selection and explicit
selection, exclusivity, selected-member normalization, enabled-only forward
and reverse wrap, independent groups, reset/rebuild, host registration,
global traversal, pointer-host synchronization, arrow-host synchronization,
and modal isolation.

The dedicated runner is
`scripts/dotnet/run-c124-managed-radio-button.ps1`, with evidence under
`out/dotnet/c011ec124-managed-radio-button/`. It requires three fresh accepted
C124 QEMU boots and records serial, input, hash, repository, manifest, and
lifecycle evidence. C120 through C123 remain covered by their dedicated
runners; C116 through C119 and the existing Workspace, Status, Counter, native
Notepad, and `[ThreadStatic]` checks remain regression lanes.

## Limitations and out of scope

C124 intentionally does not provide tri-state choices, automatic data
binding, unbounded groups, nested option trees, dropdowns, combo boxes,
automatic layout, mnemonic keys, accessibility metadata, theme animations,
persistent settings, or a general forms/selection framework.

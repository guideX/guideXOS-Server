# C121 — NativeAOT managed CheckBox / Boolean Toggle

## Purpose

C121 adds `GuideXosCheckBox`, a reusable bounded managed control for one
labeled boolean choice. The control owns label storage, two-state value,
enabled/focused state, pointer-down handling, Space handling, and text
rendering. An application supplies the meaning of the boolean; the control
does not provide persistence, binding, or a settings framework.

## Reusable API and bounds

The control is constructed with `(x, y, width, height, label, isChecked)`.
`SetLabel`, `TrySetBounds`, `SetChecked`, `Toggle`, `SetEnabled`, `Focus`,
`Blur`, `Reset`, `HandlePointerDown`, `HandleKey`, `HandleCharacter`, and
`Render` provide the lifecycle and input surface. `Checked`, `Enabled`,
`IsFocused`, `Label`, geometry, and `RejectedInputCount` expose bounded state.

Each instance owns a fixed `char[]` label buffer. The default maximum label is
32 characters; the supported maximum is 48 characters. Labels must be
non-empty printable ASCII and are rejected before mutation. The production
instance uses the default 32-character bound. There is no shared/static label
or checked state.

Geometry is bounded to coordinates 0–4095, widths 32–512, and heights 18–128;
coordinate-plus-size overflow is rejected before mutation. Rendering is
clipped to the control width and uses a fixed stack buffer. Invalid labels or
bounds preserve the prior label, geometry, checked state, enabled state, and
focus state.

The value is exactly two-state: `false` (unchecked) or `true` (checked).
Programmatic `SetChecked` changes only the value and never focuses the control.
`Toggle` returns `Toggled` only for a focused enabled instance; disabled and
unfocused calls return their corresponding non-toggle result.

## Focus and input policy

The checkbox has ordinary enabled/focused state. Disabling it clears focus;
the existing `GuideXosControlHost` then normalizes the active control using the
same eligibility policy as the other managed controls.

An enabled pointer-down inside the half-open bounds focuses the checkbox and
toggles it exactly once. Outside input is ignored. Disabled pointer input is
rejected and does not transfer focus. No pointer-up or native input transport
was added.

When the host routes input to the active checkbox, a `KeyChar` Space toggles
exactly once. `KeyDown` Space is intentionally ignored by the checkbox, so a
physical press that produces both key-down and character events cannot toggle
twice. Enter and unrelated keys are ignored. This is distinct from buttons,
which retain Enter/Space activation, and from text controls, where Space is
content.

## Rendering

The managed surface receives a bounded textual representation:

| State | Representation |
| --- | --- |
| normal unchecked | `[ ] Show path` |
| normal checked | `[x] Show path` |
| focused unchecked | `>[ ] Show path` |
| focused checked | `>[x] Show path` |
| disabled unchecked | `x[ ] Show path` |
| disabled checked | `x[x] Show path` |

The label is clipped to the render width. No graphics, theme, icon, animation,
or new surface/graphics ABI was introduced.

## ControlHost integration

`GuideXosControlHost` adds one explicit `CheckBox` kind and one bounded
registration adapter. Existing registration, one-focused-control invariant,
pointer synchronization, active-control routing, Tab, reverse Shift+Tab,
wrapping, disabled skipping, modal suspension, and saved-focus restoration are
shared unchanged. The host remains bounded at capacity 8 and continues to use
explicit control-kind dispatch; the checkbox contains no parallel focus system.

The focused Managed Notes order is:

`Open → Save → Save As → Show path → Document`.

## Managed Notes integration

Managed Notes uses one real checkbox, initially checked, to control the existing
display-only `Path: ...` line. Checked state renders the current path/status
line; unchecked state omits that optional presentation line. This is a local
presentation preference only: it is not persisted, does not change VFS
behavior, and does not alter document contents or save/reopen semantics.

Open and Save As continue to enter the existing picker modal scope. While the
picker owns input, the background checkbox, buttons, text area, and text input
cannot consume input. On modal exit, the host restores the prior main-scope
focus using the established C120 policy.

## Boundedness and runtime impact

The checkbox contributes one fixed label buffer and bounded scalar state per
instance. It has no event subscription list, reflection, growing history, or
per-toggle allocation. Host capacity remains fixed. There were no Host ABI
table changes: ABI version remains 1 and table size remains 104. No capability,
input-transport, NativeAOT runtime, GC, executable-mapping, lifecycle, or VFS
change is required.

## Validation evidence

Focused managed tests cover construction, label and geometry rejection, both
checked states, programmatic changes, focus, pointer boundaries, disabled
behavior, exactly-once Space, Enter/unrelated-key rejection, all six render
states, reset, rejected-operation preservation, and two independent instances.
Host tests cover registration, forward/reverse traversal, wrapping, disabled
skip, active routing, pointer focus synchronization, modal isolation, and
restoration (17 assertions). The checkbox focused suite and checkbox host
suite run in separate bounded launch contexts; the legacy 50-assertion C120
host fixture remains in the dedicated C120 runner. This keeps the C121 image
within its existing bounded proof budget without weakening the C120 regression
requirement.

The dedicated runner is
`scripts/dotnet/run-c121-managed-checkbox.ps1`. Its evidence root is
`out/dotnet/c011ec121-managed-checkbox/`; serial output is authoritative. The
runner includes the C116–C120 regression markers, lifecycle counters, missing-
image and independent-image Busy checks, Notes save/reopen checks, and three
fresh accepted QEMU boots. The dedicated C120 runner was also run for three
fresh accepted boots. The final validation report records the per-boot results
and evidence paths.

## Limitations and out of scope

C121 intentionally does not provide tri-state values, radio groups, switch
animations, persistent preferences, automatic data binding, a validation
framework, arbitrary child content, icons, mnemonics, accessibility metadata,
or theme-engine changes.

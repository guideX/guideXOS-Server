# C120 Reusable Managed Control Host Design

**Status:** Approved for implementation

## Goal

C120 adds one bounded managed focus owner for the reusable controls introduced by C116–C119. It makes focus, traversal, keyboard routing, pointer focus synchronization, and one shallow transient scope deterministic without turning the managed UI layer into a widget framework.

## Current-state audit

The live checkout is clean at `b9eb1b1739985100030eb7e075e5a9320129befa` (`Add reusable managed button control`). The four reusable controls currently each store their own `IsFocused` state and expose control-specific `Focus`, `Blur`, pointer, and keyboard methods. `ManagedNotes` manually blurs buttons and the text area, chooses a button by trying each pointer handler, and dispatches ordinary input directly to buttons or the text area. `GuideXosFilePicker` directly focuses and dispatches to its list box or filename text input.

The native input audit found that the existing compositor path sends printable characters through `KeyChar` and control/special keys—including Tab (`9`)—through `KeyDown`. Shift is already transported in the reserved input-payload bit. C120 therefore adds no native transport field, Host ABI field, capability, or runtime service.

## Architecture

### `GuideXosControlHost`

`GuideXosControlHost` is an explicitly bounded per-scope host with a fixed array of eight registration entries. Eight is sufficient for the four-control Notes scope and the picker scope while leaving bounded room for transient picker controls. The host exposes:

- reset/clear and registration count;
- registration in explicit order;
- stable integer control IDs and control kind inspection;
- active index and active control ID;
- explicit focus and focusability changes;
- pointer focus synchronization followed by pointer-down forwarding;
- `KeyDown`/`KeyChar` routing;
- one-level modal entry/exit with restoration of the initiating control.

Each registration entry contains a stable ID, explicit `GuideXosManagedControlKind`, a control reference, and a focusable flag. The adapter is a narrow explicit kind switch for `GuideXosButton`, `GuideXosTextInput`, `GuideXosTextArea`, and `GuideXosListBox`. It uses no reflection, type scanning, event bubbling, or unbounded collection.

Registration rejects invalid IDs, duplicate IDs, unsupported/null controls, and capacity overflow without changing existing entries. Registration order is immutable until reset/rebuild. IDs are application-owned and need only be unique within one host.

### Focus ownership

The host stores `ActiveIndex = -1` for no focus. A successful focus transfer blurs the previous active entry, updates the host index, then focuses the new entry. The host normalizes an active entry that becomes disabled or non-focusable by moving forward to the next eligible entry, wrapping once, or entering no-focus if none remain. A disabled button is ineligible through its existing `Enabled` property; other controls can be made non-focusable through the host entry.

The host guarantees the one-active-control invariant for operations performed through the host by blurring every registered entry before selecting the new active entry. Existing control behavior remains responsible for its own editing, selection, and activation semantics; applications must use the host for shared-scope focus changes.

The initial Notes policy is explicit no-focus after launch. The first forward Tab selects Open, and the first reverse Shift+Tab selects Document. This leaves reset safe while preserving deterministic keyboard entry.

### Keyboard routing

`GuideXosTextInputKey.Tab` is the managed representation of key code `9`. `KeyDown(Tab)` belongs to the host and traverses exactly once. Shift reverses the direction. The host wraps and skips ineligible entries. A `KeyChar` containing `\t` is ignored and never forwarded, providing a double-delivery guard without a second traversal.

All non-Tab key events are routed only to the active adapter. The host maps the control-specific result into a small common host result for application dispatch, but does not reinterpret the key or move command behavior into the host:

- buttons still activate on Enter through `KeyDown` and Space through `KeyChar`;
- text input and text area retain their editing/submission behavior;
- list box retains Up/Down/Home/End navigation and Enter activation.

### Pointer synchronization

The application or picker continues to identify the target from its existing bounded geometry. It then calls `FocusAndRoutePointer(controlId, x, y)`. The host performs focus transfer first and forwards the pointer-down to the selected control. A button may therefore both focus and activate on pointer-down, preserving C119 semantics, while text controls and list boxes retain their pointer behavior.

### Modal/transient scope

The main Notes host owns one bounded modal-host link and one saved initiating control ID. Entering a picker scope blurs/suspends the main host and records the initiating main control. The picker owns a separate `GuideXosControlHost` for its active reusable control:

- Open registers the candidate list box;
- Save As registers the filename text input first and can register the candidate list second.

While the picker is active, `ManagedNotes` routes input only through the picker host and picker state machine. Main buttons, document text area, and main traversal cannot consume input. On cancellation or completion, the picker host is cleared and the main host restores the initiating control if it is still eligible; otherwise it selects the next eligible control, or no-focus if none remain. The model is deliberately one level deep and does not implement a general modal/window stack.

## Managed Notes integration

The C120 Notes main scope registers, in order:

1. Open button
2. Save button
3. Save As button
4. Document text area

The application keeps command mapping in `ManagedNotes`: host activation of IDs 1–3 maps to the existing Open, Save As, and Save actions, while ID 4 routes editing to the text area. The main host replaces `BlurButtons`, `FocusedButton`, and direct text-area focus transitions. The picker retains file validation, VFS operations, and transient rendering but delegates focus and ordinary input ownership to its picker host.

The production proof starts with no focus, traverses forward and reverse with wrapping, disables and re-enables Save, pointer-focuses the document, verifies Space editing without button activation, activates a button through Enter, enters Open, proves picker isolation and C118 list behavior, restores main focus, enters Save As, proves filename ownership and isolation, restores focus, then saves and reopens exact document content.

## Results and error policy

Host operations return explicit bounded results. Rejected registration/focus operations leave prior host state unchanged. Empty and all-ineligible scopes ignore Tab/Shift+Tab without allocation or state corruption. A one-control scope keeps that control active on repeated traversal without unnecessary blur/focus churn.

## ABI, runtime, and allocation impact

C120 preserves Host ABI version `1`, Host ABI table size `104`, all existing capabilities, the current launch/input payload, NativeAOT lifecycle, GC, executable mapping, and VFS interfaces. The only managed input addition is the symbolic Tab value `9`; the native transport already carries it.

The host's registration array is fixed at eight entries. Each picker scope has its own fixed host and one saved restoration ID. There is no focus history, recursive scope stack, reflection, or runtime type discovery. Existing control storage remains authoritative for text, selection, labels, and bounds.

## Validation

Validation will include a focused host suite covering empty/single/multiple registration, order, capacity, duplicates, reset, explicit focus, invalid operations, one-focus invariants across all four control kinds, forward/reverse traversal and wrapping, disabled skipping and normalization, routing and Tab suppression, pointer transfer, modal isolation/restoration, restoration fallback, and independent host instances. Managed Notes and C116–C119 regressions will run through a dedicated C120 composite proof runner with three fresh accepted QEMU boots. Serial evidence will record active IDs, traversal direction/wrap, disabled skips, pointer transfers, modal entry/isolation/restoration, routing, rendering focus markers, lifecycle counters, missing-image behavior, Busy behavior, and per-boot results.

## Out of scope

C120 does not provide automatic geometric or spatial focus order, a layout engine, arbitrary nested widget trees, DOM-style bubbling, focus groups, accessibility metadata, mouse-hover focus, mnemonic/Alt-key navigation, default/cancel dialog policy, arbitrary deep modal stacks, a general window manager, application-global focus across processes, command binding, dependency properties, or UI automation metadata.


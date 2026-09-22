# NativeAOT C133: reusable managed ComboBox

Primary outcome: **Outcome B — control works but popup interaction exposes a
bounded host defect.** The managed ComboBox API, state machine, focused host
fixture, Panel behavior, Notes integration, and bounded storage all validate.
In the production NativeAOT/QEMU path, all three fresh boots consistently
open the transient list through the real pointer route, then stop at the first
follow-up popup interaction. The failure is isolated to transient popup
follow-up routing/capture; it does not require or justify a second windowing
hierarchy.

Phase C133 adds a bounded, non-editable `GuideXosComboBox` to the managed
control host. The drop-down is transient state owned by the ComboBox; it is
not a registered child, a nested Panel, or a second window hierarchy.

## Storage and API

The default and proof configuration is:

- maximum items: 16;
- maximum item text: 48 characters;
- visible rows: 4;
- host registration capacity: the existing 8-entry capacity.

The implementation uses fixed `char[]` and `int[]` storage. `TryAddItem` and
`AddItem` reject item-count and text-length overflow without modifying existing
items. Empty item text is valid. `SelectedIndex` starts at `-1`; invalid
programmatic indexes are ignored and counted as rejected input. The public
surface includes `ItemCount`, `SelectedIndex`, `SelectedText`, `AddItem`,
`TryAddItem`, `ClearItems`, `IsOpen`, `ActiveIndex`, and bounded `Changed`.

## Selection and state machine

The committed selection is the public `SelectedIndex`. On open, the private
active highlight is initialized to the committed item, or item zero when there
is no committed selection. Up/Down/Home/End move only the active highlight.
Enter, Space, or a pointer row choice commits it, closes the drop-down, and
invokes `Changed` exactly once only when the committed index changed. Escape,
outside click, visibility/enabled/membership/focus interruption, clear, and
programmatic selection close without committing pending navigation. Re-selecting
the current item closes without a duplicate callback.

The closed rendering is a bounded selector with the current text and `v`
indicator. The open rendering draws rows below the control, marks the active
row with `>`, and marks the committed row with `*`. The host retains focus;
the active row is not a second focus target.

## Pointer, keyboard, and transient capture

Pointer activation focuses and opens the closed control. A popup row commits a
choice. While open, the host routes the entire pointer gesture to the open
ComboBox before normal hit-testing, so an outside click closes and is consumed
and cannot activate the control underneath. Disabled, hidden, non-member, and
stale gestures are rejected or cancelled through the existing host lifecycle
paths.

Closed Space follows the existing KeyDown/KeyChar split used by Button and
CheckBox. Enter opens. Open Up/Down changes the highlight; Enter/Space commits;
Escape cancels. Tab and Shift+Tab close first, then use the host's ordinary
forward/reverse traversal. No synthetic Tab `KeyChar` is emitted.

The managed host fixture validates the full transient interaction contract,
including row selection, current-row close, outside-click suppression, stale
pointer/key cancellation, modal transitions, and relaunch. The production
proof reaches pointer-open through the compositor path, but the first native
popup follow-up is repeatably blocked before the managed return path; this is
the bounded defect classified below.

## Lifecycle, mutation, Panel, and Notes

The host and control cancel open state when visibility, enabled state,
membership, Panel visibility, focus, modal context, unregister/reset, or
application close interrupts the gesture. Reopening initializes a clean active
highlight. Selection changes while open are authoritative and close the
popup. Clearing items closes the popup and returns selection to `-1`.

`GuideXosPanel` remains single-level and non-owning. It can contain the
ComboBox as an ordinary supported child, but the transient list never becomes
a Panel child. Managed Notes replaces the C132 Full Path/File Name RadioButton
pair with one ComboBox containing `Full Path` and `File Name`. Its registration
count changes from 8 to 7; RadioButton and its C132 fixtures remain supported.

## Validation

Focused C133 suites cover bounded storage, selection/callback semantics,
pointer and keyboard interaction, committed/highlight separation, outside
click suppression, traversal, lifecycle interruption, stale gesture
cancellation, mutation while open, Panel restoration, and relaunch. The
production NativeAOT proof runs the same control through compositor pointer
and keyboard entry paths, Managed Notes integration, modal isolation, and
relaunch setup. The native follow-up limitation is recorded rather than
treated as a control-level pass.

The evidence below is filled from the completed run:

- focused API suite: 44/44;
- focused host/lifecycle suite: 24/24;
- C127 Panel: 60/60;
- C128 lifecycle: 46/46;
- C129 Shift/Tab: 31/31;
- C130 legacy wrapper: explicit deterministic `SKIP`;
- C131 CheckBox: 34/34 API and 23/23 host/lifecycle;
- C132 RadioButton: 38/38 API and 12/12 host/lifecycle;
- managed NativeAOT build: PASS;
- C133 focused API: 44/44 PASS;
- C133 focused host/lifecycle: 24/24 PASS in an independent fresh host-context
  proof boot;
- C133 production QEMU: three fresh boots, each classified
  `BOUNDED-HOST-DEFECT` after `pointer-open=PASS` and
  `row-followup=BLOCKED`;
- C133 composite ELF SHA-256:
  `7D79737F6B1FCCB33D2E98C19C25E1F630751DEF552560C5E96026CE4601B198`;
- C133 proof-kernel SHA-256:
  `BBD82E9FEF7AB09C93BE99584303387C1CB0FE1F82C70EDDCCFE96B06863BBCC`;
- C133 serial SHA-256 values:
  boot 1 `E2348C1C93569278365BF9D4A837684678E4A36E26F3C3F235068DD9682341C0`,
  boot 2 `24DD19C1B0C9134078482999811B041076F775F65C6DA81FF5ED42CDD9E4014C`,
  boot 3 `A74E07B0057EB9209E63D3EC28F3A0E4812D9EC022E2245CC271CFC906D044C0`;
- C133 manifest:
  `out/dotnet/c133-managed-combobox-final27/proof/c133.manifest.json`;
- ordinary kernel restored and verified at both
  `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf`, SHA-256
  `1EA9C48EC631EAECA2EA71BBAF7CDA711600697E584EBCA3557A672D4AD0BADE`;
- ordinary QEMU: three fresh boots PASS;
- ordinary serial SHA-256 values:
  boot 1 `B5118888161E4510E825D349F51BA7F4F2BA51E32FCE9C559A102F680991A976`,
  boot 2 `BA2C6812845403FCEAE7B4F438F62A7A446D4654BA00051FF0041D2192E131AA`,
  boot 3 `B69D01E200D202D3F0940F20623FFFC5AB0CF3EDE15599A5FD4306F9ADAE3A20`;
- ordinary manifest:
  `out/dotnet/c133-ordinary-boot-final/run-20260922-004415198-69126efa/ordinary-boot.manifest.json`.

The independent host fixture evidence is in
`out/dotnet/c133-managed-combobox-final11/proof/boot-01/serial.log`.
The three production bounded-defect logs are under
`out/dotnet/c133-managed-combobox-final27/proof/boot-01` through `boot-03`.

## Architecture and limits

The ComboBox uses the existing managed registration, focus, input, drawing,
Panel, lifecycle, and modal primitives. Its popup is transient state with
bounded rows and no child registration. ABI v1 and table size 104 are
unchanged; no runtime, GC, VFS, App Model, capability, or input-transport
changes were made. RadioButton remains a supported reusable control with its
C132 fixtures intact. Panel remains single-level and non-owning. Notes now
has seven registrations instead of eight because two RadioButtons were
replaced by one ComboBox.

The deferred scope is editable text, autocomplete, incremental search,
dynamic binding, rich/template items, icons, multiselect, nested drop-downs,
animation, general popup windows, and unbounded scrolling. The next repair
for Outcome A is a narrow host fix for the native transient popup
follow-up/capture route.

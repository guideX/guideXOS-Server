# NativeAOT C136: Secondary Pointer Transport and Context Menu

## Outcome

Outcome A — secondary pointer transport and context-menu invocation validated.

Three fresh C136 QEMU boots reached the real NativeAOT input path, opened the
existing C135 `GuideXosPopupMenu` from a physical/QEMU secondary-button
gesture, activated the real `Save` command, and terminated with `menu=closed`
and `capture=none`.

The C136 production manifest is
`out/dotnet/c136-run21/c136.manifest.json`.

## Pointer transport audit

Before C136, pointer coordinates were carried in the existing bounded x/y
payload in `kernel/core/nativeaot_application.cpp`. The compositor already
received native button identity (`1 = primary`, `2 = secondary`, `4 = middle`)
and maintained aggregate PS/2 button state, but the managed bridge surfaced
only primary pointer-down. Pointer-up and secondary identity were discarded at
the native-managed seam. The Windows compositor/test path also lacked
secondary-button-up forwarding.

C136 surfaces the existing identity in the existing launch-flags semantic
field. ABI version 1 and the 104-byte table are unchanged:

| Event | Semantic kind |
| --- | --- |
| Primary down | `0x01000000` (existing) |
| Key down | `0x02000000` (existing) |
| Key char | `0x03000000` (existing) |
| Primary up | `0x04000000` |
| Secondary down | `0x05000000` |
| Secondary up | `0x06000000` |

The managed decoder maps these to the existing pointer lifecycle with
`PointerButton.Primary` or `.Secondary`. No ABI expansion, second input
subsystem, or generalized HID layer was required.

## Native and managed button semantics

The compositor now keeps an independent active-button bitmask. Down ORs only
the corresponding bit; up clears only that bit. Therefore primary-held plus a
secondary press/release preserves primary state, secondary-held plus a
primary press/release preserves secondary state, and either release ordering is
safe. Primary-only focus acquisition, title-bar drag, and widget activation are
unchanged. Secondary content events do not acquire host focus and cannot reach
a primary activation callback.

The host exposes one pointer API carrying coordinates, lifecycle, button
identity, and the existing routing/consumption result. A secondary gesture
records the eligible target and original press coordinates on down. Up opens a
menu only when the same registration remains eligible in the same modal and
capture context. Hidden, disabled, removed, unregistered, focus-invalidated,
modal-invalidated, closed, or stale gestures are consumed/cancelled and never
retarget another control.

## Managed Notes integration

The target is the existing Notes `Document` TextArea region. The existing C135
popup registration is reused: Managed Notes remains at 8 registrations, with
one non-focusable PopupMenu transient owner and no popup-row registrations.
Both the Options button and a valid secondary editor click invoke that same
menu. The menu uses real bounded Notes commands; the production proof executes
`Save` exactly once per activation.

The menu origin is the secondary-click coordinate, subject only to the
existing bounded screen-edge clamp. The production target marker was:

```text
localX=0x50 localY=0x64
screenX=0x1A4 screenY=0x158
menuItemX=0x1B8 menuItemY=0x173
```

The same rendered geometry is used for hit testing. The underlying editor
focus remains the host focus; the highlighted menu row is internal popup state.
Closing restores normal input and traversal. Up/Down, Enter, Space, Escape,
Tab, and Shift+Tab retain C135 semantics, including no synthetic Tab
`KeyChar`.

## Capture and conflict policy

The popup acquires the C134 one-owner transient capture lease. There is no
capture stack and no context-menu-specific lease. An existing ComboBox or
PopupMenu owner remains authoritative; a second popup acquisition is rejected
according to the C135 policy. After release, a later secondary click opens the
context menu normally.

With the menu open, both primary and secondary outside clicks close and consume
the current menu. A secondary outside click does not click through or open a
second menu. The final production state is `menu=closed capture=none`.

## QEMU proof and runner safety

The runner command was:

```powershell
.\scripts\dotnet\run-c136-secondary-pointer-context-menu.ps1 `
  -EvidenceRoot out\dotnet\c136-run21 -FreshBootCount 3
```

It used explicit QMP `input-send-event` phases: bounded relative pointer
movement, explicit `btn/right/down`, explicit `btn/right/up`, primary follow-up
events, and explicit keyboard key-down/key-up events. Each injected phase had
a bounded serial acknowledgement. Outcomes are PASS, FAIL, or TIMEOUT; no
unbounded wait was added.

The proof sequence was: secondary open; primary pointer selection of Save;
secondary reopen; Down/Enter Save; secondary reopen; Escape; secondary reopen;
outside primary close/consume; stale secondary-release cancellation; Options
button invocation; capture release; close/relaunch. The serial evidence
contains `C136-NATIVE-INPUT`, `C136-SECONDARY-DOWN`, `C136-SECONDARY-UP`,
`C136-CONTEXT-OPEN`, `C136-COMMAND`, and final `capture=none` acknowledgements.

## Allocation audit

The new pending state is bounded integer/boolean state for target identity,
press coordinates, and lifecycle validity. The secondary path does not allocate
per event, box button state, or create disposable state. The existing fixed
capacity popup is reused. This avoids reproducing the synchronous-dispatch
allocation failure class found in earlier phases.

## Focused tests and retained suites

The C136 focused managed suite is `36/36 PASS`. It covers primary and
secondary distinction, down/up/click, primary/secondary independence and
release ordering, target eligibility, outside/hidden/disabled targets,
membership/visibility/modal/unregister/close cancellation, stale release,
single-open and follow-up routing, command exactly-once, Escape, both outside
buttons, ComboBox and PopupMenu conflicts, focus/traversal, repeated cycles,
capture leak prevention, and relaunch.

Retained results:

| Suite | Result |
| --- | --- |
| C135 PopupMenu API | `40/40 PASS` across 3 fresh API boots |
| C135 PopupMenu host/lease | direct managed serial acknowledgement `40/40 PASS`; wrapper stopped after that bounded acknowledgement because this focused context does not emit the wrapper's separate C134/focused-result markers |
| C134 transient capture | `30/30 PASS` in the C136 composite and retained host evidence |
| C133 ComboBox | API `44/44 PASS`; host `24/24 PASS` |
| C127 Panel | `60/60 PASS` |
| C128 lifecycle | `46/46 PASS` |
| C129 Shift/Tab | `31/31 PASS` |
| C130 legacy wrapper | explicit deterministic `SKIP` |
| C131 CheckBox | API `34/34 PASS`; host/lifecycle `23/23 PASS` |
| C132 RadioButton | API `38/38 PASS`; host/lifecycle `12/12 PASS` |

Button, TextArea, Panel, pointer/control-host regressions, and the managed
NativeAOT composite build also passed in the retained production image. The
C136 manifest records the C135/C134 retained totals without changing their
contracts.

## C136 production artifacts

| Artifact | SHA-256 / result |
| --- | --- |
| Composite ELF `out/dotnet/c136-debug-build5/build/composite/artifacts/HostLogProof.elf` | `893D94E822DA449FD30EEA73A4FAD203780B0E944C7CCC1DB61526A8B052994F` |
| C136 proof kernel | `06BB15C1261CBEDAFBB10365D705BB14D9E65BF266D8ADBE18C6245504BEC617` |
| Boot 1 serial | `51948DEAC792A93F10C097349E672A262AF124AFE472BAB43D1D227AC6274BF2` |
| Boot 2 serial | `370CC35C215CD30F73045996AD2B0DE924361B12FBEF0FE01D4BF9D11F1119FA` |
| Boot 3 serial | `50CA0CAB62A8F9623214368F86918DF1730296F79098D63DA905DE5166A75602` |
| C136 runner | `PASS / PASS / PASS`, QEMU executed, ABI changed `false` |

The real production command was `Save`. Each successful run ended with the
popup closed and transient capture released. The proof image was not left in
the canonical ESP after validation.

## Ordinary restoration

After the proof, the ordinary kernel was rebuilt without C136 proof flags and
copied to `ESP/kernel.elf`. The final ordinary validator manifest is:

`out/dotnet/c136-ordinary-final/run-20260923-090818050-6c1d7974/ordinary-boot.manifest.json`

It reports three fresh ordinary boots `PASS / PASS / PASS`, main-loop and
navigator markers present, and semantic proof markers absent from boot output.
The final hashes are:

| Artifact | SHA-256 |
| --- | --- |
| `kernel/build/amd64/bin/kernel.elf` | `8DAEC1239FD48236A01C5C477C66A589207BA6EDF8F706EBF7BB4ADE21BAAA5A` |
| `ESP/kernel.elf` | `8DAEC1239FD48236A01C5C477C66A589207BA6EDF8F706EBF7BB4ADE21BAAA5A` |
| Ordinary boot 1 serial | `B493E87BDC55CAE3FA88AADEEB2913939CE8040BBA14D0E7BCDAEF81C7F5568B` |
| Ordinary boot 2 serial | `1AA754E9059483FEF7D4B3E21C36E750256E3C30130958C1076A79E34905B7B0` |
| Ordinary boot 3 serial | `8950D24FD04314F85BBB1760B1280AE46682386D679F5D610BA910D4AE25F456` |
| `ESP/ramdisk.img` | `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE` |

`ESP/ramdisk.img` was preserved byte-for-byte and was not staged or committed.
The ordinary runtime source still contains the production secondary transport
code, but no C136 proof marker appears in ordinary boot serial output.

## Preserved architecture and deferred work

ABI v1/table 104, NativeAOT, GC, VFS, App Model, capabilities, keyboard
transport, independent Shift state, key-down-only Tab, and no synthetic Tab
`KeyChar` are preserved. Panel remains single-level and non-owning. Popup rows
remain unregistered children. There is no capture stack or second focus system.

Middle-button UI, wheel, drag-and-drop, double-click, pointer-drag capture,
touch long-press, nested menus, submenus, menu bars, Clipboard, undo/redo, and
the keyboard context-menu key remain deferred.

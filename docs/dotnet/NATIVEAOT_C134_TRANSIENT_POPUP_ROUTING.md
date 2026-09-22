# NativeAOT C134 — Transient Popup Follow-Up Routing

## Result

C134 is **Outcome A — transient follow-up routing repaired**.

The production failure was bounded and reproducible. The first popup follow-up was a pointer item-selection event. Native input delivery began, but repeated managed dispatch stopped before `GuideXosControlHost` and before the ComboBox handler could run. The repair therefore has two bounded parts: reusable managed dispatch state removes the re-entry allocation dead end, and `GuideXosControlHost` now owns a single explicit transient-input lease for an open ComboBox. No native acknowledgement defect was found or changed.

## C133 reproduction

The authoritative starting point was commit `ac97ecb83bc683b76c4a025a9d0167c65622c3b2`, `Add reusable managed ComboBox for C133`, on `v1.1_DOTNET_SUPPORT`.

Three fresh C133 NativeAOT/QEMU boots reproduced:

```text
Notes registration = 7
C133 API = 44/44 PASS
pointer open = PASS
[C133-POINTER] open=PASS row-followup=BLOCKED result=PASS
committed selection = FullPath
```

The failing sequence was:

```text
Tab -> pointer open -> pointer item row follow-up -> bounded BLOCKED
```

The blocked event was pointer item selection, not Down, Enter, Space, Escape, outside click, or traversal.

## Trace and root cause

The event trace crossed native pointer receipt and entered `invokeManagedInput`. TLS/heap re-entry began, but the managed input handler did not run. The managed trace then identified the corresponding boundary as:

```text
DispatchC107Composite
 -> GuideXosApplicationRegistry.Dispatch
 -> GuideXosHost.TryCreate
 -> GuideXosLaunchContext.TryCopy
 -> bounded failure/stall before host-create-context-ready
```

`TryCopy` allocated a new managed byte array for every launch context. The ordinary production input context is often empty, so the repeated `new byte[0]` path became the re-entry dead end after the first popup event. The native side was not waiting on a missing completion token: it had entered the managed bridge and the proof stayed bounded while the managed dispatch did not return.

The managed transport now uses one fixed 48-byte launch-context buffer and one synchronous dispatch host. Context fields are overwritten for each synchronous dispatch. This is intentionally non-recursive and single-threaded, matching the existing resident input contract; it is not a second runtime or focus system.

The host-side audit also showed that the existing “find any open ComboBox” behavior was not a reusable ownership contract. C134 replaces it with an explicit, bounded lease so the registered ComboBox remains the focused control while its open transient state gets first refusal on follow-up input.

Diagnosis:

- native input receipt: reached;
- native-to-managed transport entry: reached;
- managed dispatch entry: reached on the failing re-entry;
- `GuideXosControlHost` routing: not reached in the original failure;
- ComboBox handler/state mutation/redraw: not reached in the original failure;
- native acknowledgement: no defect found;
- proof runner: only bounded the observation; it was not the cause.

## Transient-capture contract

`GuideXosControlHost` has one `_transientCaptureIndex`.

- A lease can be acquired only for a registered, enabled, visible, focused, open ComboBox.
- The registered control remains the focus owner; the lease is event priority, not a second focus target.
- One owner is supported. A second attempted owner is rejected deterministically; there is no capture stack or implicit transfer.
- Pointer, KeyDown, and KeyChar routing consult the lease before ordinary focused-control routing.
- The opening event establishes the lease before returning to native input.
- Commit, cancel, lifecycle invalidation, modal transition, unregister, membership loss, disable, hide, reset, and application close release it.
- Reconciliation rejects a dead, stale, unregistered, unfocused, disabled, hidden, or closed owner.

Outside pointer input is consumed by the open ComboBox: it closes the popup and preserves the committed selection. The underlying control does not receive the same click.

While open, Down and Up move the active highlight; Enter and Space commit; Escape cancels. Tab and Shift+Tab close the popup, release the lease, and then use ordinary forward/reverse traversal. Tab remains KeyDown-only; no synthetic Tab `KeyChar` was added.

Callbacks remain bounded. A callback may hide, disable, refocus, or otherwise invalidate the ComboBox; host reconciliation releases the lease before a stale event can recommit. Nested transient owners are not supported and are rejected rather than creating nested capture.

Modal routing keeps priority over the transient lease. Entering a modal scope cancels the background ComboBox capture. Panel remains single-level and non-owning: hiding a Panel or removing the ComboBox closes the popup, releases capture, preserves the committed value, and consumes stale follow-up input.

The ComboBox also tracks whether popup rows have actually been rendered. Closed-state rendering no longer inserts four empty popup labels into the fixed native 32-widget frame; rows are added only while open and are cleared on close. This preserves the existing GroupBox/Panel rendering in the production C134 image without changing the widget-capacity contract.

## Tests

The new focused host suite has 30 cases covering closed/open ownership, deterministic owner selection, pointer item commit, outside-click consumption, Down/Up/Enter/Escape, Tab/Shift+Tab, hide, disable, membership loss, modal transition, unregister, application close, stale pointer and keyboard completion, dead owners, normal routing recovery, disabled acquisition, second-owner rejection, callback mutation, programmatic selection, and repeated open/close cycles.

```text
C134-TRANSIENT-HOST-TESTS cases=30 result=PASS
```

Retained regression evidence:

```text
C127 Panel                 60/60 PASS
C128 lifecycle             46/46 PASS
C129 Shift/Tab             31/31 PASS
C130 legacy wrapper        deterministic SKIP
C131 CheckBox API          34/34 PASS
C131 host/lifecycle        23/23 PASS
C132 RadioButton API       38/38 PASS
C132 host/lifecycle        12/12 PASS
C133 ComboBox API          44/44 PASS
C133 host/lifecycle        24/24 PASS
```

The managed NativeAOT composite build succeeded. Relevant Button, ListBox, control-host, Panel, and keyboard/input coverage remained green in the retained suites.

## Exact original failure retest and C134 proof

The exact C133 boundary now emits:

```text
C134-POPUP-OPEN pointer=production result=PASS
C134-FOLLOWUP-ROUTED pointer-item=managed-host result=PASS
C134-COMMIT selection=FileName changed=once result=PASS
```

The full production sequence then passed pointer commit, Down/Up highlight navigation, Enter/Space commit, Escape cancellation, outside-click consumption, Tab, Shift+Tab, lifecycle cancellation, modal isolation, close/relaunch, and final capture release.

Final C134 evidence:

```text
manifest: out/dotnet/c134-final-proof/c134.manifest.json
composite ELF SHA-256: 75E28202AE83FB81A1F42D150B90C8C6FF37024662CCFB70EA21D5EB85C8905E
proof kernel SHA-256:  DFA9FF8CBAA0B9B4E05E97FAE079430C07BCF9CCDE6D06655ED203DC2E93F461
serial boot 1:       F0ACD4567DE4712B26BFC7228431A75A1FDFE5D223AFD001FD0F3B5C9D89293D
serial boot 2:       D9657D465BE48A3BC4C92C73E05E3877A08EAF4B7DEFDF9838C61BE06216EC5A
serial boot 3:       05D87515708E3E0D0C05A8F19C8DB6A890A26BD5FB86DD333E9D1D77238A23B8
boots:               PASS / PASS / PASS
Notes registration:  7
initial selection:   FullPath
final selection:     FullPath after close/relaunch
final capture:       none / closed
```

## Ordinary kernel restoration

After proof testing, the canonical ordinary kernel was restored to both:

```text
kernel/build/amd64/bin/kernel.elf
ESP/kernel.elf
```

Both hashes are:

```text
1EA9C48EC631EAECA2EA71BBAF7CDA711600697E584EBCA3557A672D4AD0BADE
```

The ordinary kernel proof-marker scan was clean. Three fresh ordinary QEMU boots passed the main-loop and navigator smoke checks:

```text
manifest: out/dotnet/c134-ordinary-final2/run-20260922-131754979-6d15ff5d/ordinary-boot.manifest.json
serial boot 1: 67C2A5F974AF8B5A5E6A8F14238E0483F72ED6C41443072A0BC1D392A73104BE
serial boot 2: 46AE13073F2636872CB33051C499BC24F2693881590BFBF0B81C7C8601F69FD5
serial boot 3: 8FCFAAC385D08528F6D809FDEA8DF894292668E2241507EEBD4331F28EC00612
boots: PASS / PASS / PASS
```

## Compatibility and limitations

- Host ABI remains version 1 with table size 104; no ABI or native popup/window ABI was added.
- NativeAOT runtime, GC, VFS, App Model, capability model, and production input transport remain unchanged.
- Panel remains single-level and non-owning.
- No popup child registration, nested Panel, general owned-control tree, second focus subsystem, or synthetic Tab `KeyChar` was introduced.
- The transient primitive intentionally supports one active owner and synchronous, non-recursive managed dispatch. Future menus or date pickers can use the same bounded contract, but nested capture is outside C134.

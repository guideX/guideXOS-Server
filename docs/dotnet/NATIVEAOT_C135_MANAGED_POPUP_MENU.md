# NativeAOT C135 — Reusable Managed Popup Menu

## Outcome

C135 is Outcome A: `GuideXosPopupMenu` is a second independently useful popup control using the C134 `GuideXosControlHost` transient-capture lease. No parallel popup router, capture stack, popup child ABI, second focus system, or windowing subsystem was added.

## Architecture

`GuideXosPopupMenu` is a fixed-capacity managed object registered once in the existing host as `GuideXosManagedControlKind.PopupMenu`. It is non-focusable, does not become a Panel child, and keeps the invoking control's ordinary host focus unchanged. Its open rows, active row, hit testing, rendering, callbacks, and cancellation state are internal bounded state.

- Maximum entries: 8.
- Maximum item text: 48 printable ASCII characters.
- Storage: fixed `char[]`, length, command-ID, enabled, and separator arrays allocated at construction.
- Overflow: rejected deterministically; existing entries remain unchanged.
- Commands: one bounded `Action<uint>` receives a per-row command ID exactly once after the menu closes. Notes maps `Open`, `Save`, and `Reload` to existing application actions.
- Separators: supported as non-selectable rows; they render as a bounded rule and are skipped by pointer/keyboard activation and navigation.

## Invocation and interaction

Managed Notes renders an `Options` button through the existing surface action path. The current production input contract proves pointer-down, KeyDown, KeyChar, and Shift payloads but does not expose a proven secondary-button event, so right-click invocation is explicitly deferred. The Options action is the real invocation path used by NativeAOT/QEMU proof.

Opening acquires the same single transient lease used by ComboBox. A second owner is rejected safely and the existing owner remains authoritative. If acquisition fails, the attempted menu is closed. There is never more than one transient capture owner.

Pointer selection of an enabled row closes the menu, dispatches one command ID, and releases capture. A disabled row does not invoke and leaves the menu open. An outside pointer closes and consumes the gesture, so the underlying control cannot activate. Escape cancels. Down/Up move over enabled command rows only; Enter and Space activate the active row. Tab and Shift+Tab close the menu before the existing forward/reverse host traversal. No synthetic Tab `KeyChar` is generated, and menus do not use mnemonic or Alt-key behavior.

## Lifecycle, Panel, modal, and focus behavior

Visibility, enabled state, invoker availability, host unregister/reset, modal entry, and application close all close the menu and invalidate its lease. Stale pointer or keyboard input after cancellation cannot dispatch a command. Callback dispatch is guarded against re-entry; callback-driven menu reopening is rejected while the command callback is in progress.

Panel remains single-level and non-owning. The menu is not a Panel child or nested hierarchy. An invoker/member invalidation is represented by `SetInvokerAvailable(false)` and closes the menu; Notes retains its existing Panel membership behavior. Modal routing remains owned by `GuideXosControlHost`; entering a modal scope closes the background menu and isolates modal input.

The host registration count for C133 Managed Notes was 7. C135 is 8: the eighth entry is the non-focusable popup menu. Repeated open/close does not change this count, and relaunch reconstructs the same count. No popup child registration is created.

## Allocation and C134 protection

The follow-up routing path uses the existing bounded synchronous dispatch state repaired by C134. Menu hit testing, navigation, lease lookup, and callback dispatch do not allocate per event. Menu rendering uses stack-backed line storage. The fixed item arrays are allocated once when the menu object is constructed; diagnostic text accessors are not used by the production hot path.

## Validation

Focused C135 coverage is split into 40 API/state cases and 40 host/lease cases. It covers construction, empty/capacity/overflow/text bounds, disabled rows, separators, open/close, pointer and keyboard activation, exact-once callbacks, outside consumption, traversal, stale input, lifecycle interruption, modal isolation, registration stability, and ComboBox/menu conflict policy.

The production NativeAOT proof uses the Notes Options action, pointer Save selection, keyboard Down/Enter activation, Escape, outside-click consumption, Tab, Shift+Tab, lifecycle cancellation, close/relaunch, and a retained ComboBox pointer open/follow-up/commit. The C135 runner records the composite ELF, proof kernel, serial hashes, manifest, registration count, command proof, and final capture state for three fresh QEMU boots.

After proof, the ordinary kernel is rebuilt/restored and three ordinary fresh boots are run. `ESP/ramdisk.img` is protected pre-existing work and is not staged or committed by C135.

The ABI remains v1 with table size 104. NativeAOT, GC, VFS, App Model, capabilities, pointer/keyboard transport, independent Shift handling, C128 lifecycle semantics, C129 Tab semantics, C130 bounded validation, C131 CheckBox, C132 RadioButton, C133 ComboBox, and C134 one-owner transient capture remain preserved.

Deferred: secondary-click transport, nested/cascading submenus, menu bars, mnemonics, Alt navigation, icons, checked items, embedded controls, scrolling, animation, themes, drag interaction, and multiple simultaneous popups.

## Recorded validation

The retained focused baselines were C127 Panel `60/60 PASS`, C128 lifecycle `46/46 PASS`, C129 Shift/Tab `31/31 PASS`, C130 legacy wrapper `SKIP` (explicitly classified), C131 CheckBox API/host `34/34 PASS` and `23/23 PASS`, C132 RadioButton API/host `38/38 PASS` and `12/12 PASS`, C133 ComboBox API/host `44/44 PASS` and `24/24 PASS`, and C134 transient capture `30/30 PASS`. The C134 baseline was rerun independently before C135 changes; its production sequence remained `POPUP OPEN`, `FOLLOWUP ROUTED`, `COMMIT`.

C135 focused managed coverage was `40/40 PASS` for API/state and `40/40 PASS` for host/lease behavior. The managed NativeAOT composite build completed successfully. Focused API, focused host, and production proofs each completed three fresh QEMU boots: `PASS / PASS / PASS`.

The production composite ELF SHA-256 was `CA762436C407A81C9E56A7749D019ECA91C34004862A749A6961366B84789B31`. Proof-kernel SHA-256 values were `F8F0FC6A61823601E99AC15DBE9BD0EFB33936F5EA4885027C8C9F70AC85E37C` for focused API, `E58EFD7C3B81B765D8B94B055963888737A6B4A426FCE422EC784F06501B5AD3` for focused host, and `D99B74A282A9F559080430FF0EF54F59ED2807F3C9AB070DDA9EF74E647F6F60` for production. Production serial SHA-256 values were, in boot order: `84F0FBC67628EC8B6213357A48A7973BF2EF41AEAC6C7B38A5244F1A9A1C9C16`, `3BFDAA87BD157D6D40F336F10D04F50EA1469221FE3BB37AF663D716EF8C5E5E`, and `FEB5BB0F7892EC030C499642EC04218023290BA72D2654DD7BF40A9539688D76`.

The production proof ended with the menu closed and transient capture `none`, then the canonical ordinary kernel was rebuilt. Both `kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` have SHA-256 `1EA9C48EC631EAECA2EA71BBAF7CDA711600697E584EBCA3557A672D4AD0BADE`. The three ordinary-boot serial hashes were `81F94E08D75E5F8DB27B24FFAC5C375C64A9C62AD9FA2126775D646F2263AC23`, `65C20CC681E0B60B55B56936E6E4EA65BF0AB2171FEFED6B6198D153A2FC71EE`, and `00D0E18B1CE23C656BE71BF57C3A8FEE687C585184FAB8E7215C339D550EE891`; all were `PASS` with no canonical kernel mutation. The protected `ESP/ramdisk.img` hash remained `E26D6D7C54CBFF416CC59B8FE0E4A2B9914D15B9A74A0368EAEA27A3E103BBAE`.

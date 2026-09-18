# C120: Reusable Managed Control Focus Host

C120 adds `GuideXosControlHost`, a bounded managed owner for the reusable
controls introduced by C116–C119. It owns registration, focus, traversal,
pointer focus synchronization, ordinary input routing, and one shallow modal
scope. It does not become a general widget framework and does not change the
GuideXOS Host ABI.

## Contract

`GuideXosControlHost` has a fixed maximum of eight registered controls. The
picker uses a two-entry host. Registration is explicit and records an integer
ID, a `GuideXosManagedControlKind`, a managed control reference, and a
focusable flag. Dispatch is an explicit kind switch; there is no reflection,
unbounded collection, or hidden control discovery.

The host exposes one active control or no focus. Notes registers controls in
this order:

1. Open button
2. Save button
3. Save As button
4. multiline document text area

Notes starts with no focus. `Tab` and `Shift+Tab` traverse this order,
wrap, and skip disabled or non-focusable entries. Tab is consumed by the host;
the text controls do not receive it as a character. The native route already
forwards the existing KeyDown event and Shift payload, so C120 does not add a
Host ABI table slot or alter keyboard transport.

Pointer targeting remains application-owned geometry. After a bounded hit
test, the application calls `FocusAndRoutePointer`; the host transfers focus
and forwards the pointer-down. Pointer-up is not part of the managed contract.

The button keeps C119 activation semantics: pointer-down inside an enabled
button and Enter activate, while Space KeyDown is ignored and the following
Space KeyChar activates exactly once. A Space KeyChar routed to the C117 text
area remains document text. The managed frame primitive is not used to add
button fill rectangles; focused buttons render only their text marker.

`EnterModal` clears the main scope and routes all subsequent input to the
picker host. `ExitModal` clears the picker scope and restores the saved main
control when it is still eligible; otherwise it selects the next eligible
control in registration order, with no-focus as the final bounded state.

## Managed Notes and picker

Managed Notes uses the host for Open, Save, Save As, and the C117 document
area. Its application-owned command mapping and native Reload action remain
unchanged. The managed file picker uses a two-entry scope: the Save filename
field (when C116 text input is enabled) and the candidate list. Open keeps the
candidate list focused; Save starts in the filename field, and Tab can move
between the filename and existing-candidate list. Picker completion and
cancellation return through the host restoration path.

## Test-first and proof evidence

`GuideXosControlHostTests` runs 50 bounded cases covering empty and single
hosts, registration order and capacity, duplicates, explicit focus, focus
invariants for all four controls, forward/reverse traversal and wrapping,
disabled normalization, pointer transfer, keyboard ownership, modal
isolation/restoration/fallback, and independent host instances. The managed
proof marker is:

`[C102-MANAGED-OUTPUT] C120-TESTS cases=50 result=PASS`

The native proof additionally records no-focus registration, traversal,
disabled skipping, pointer transfer, routing, picker modal isolation and
restoration, and C116–C119 regressions. It requires exactly one
`C120-SPACE ... exact-once=PASS` marker and exactly one managed Save
activation marker per boot. Serial output is authoritative; screenshots are
supplementary.

Run the complete proof with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/dotnet/run-c120-managed-control-host.ps1
```

The runner requires three fresh QEMU boots by default. Evidence is written
under `out/dotnet/c011ec120-managed-control-host/`, including per-boot serial,
stdout, stderr, categorized evidence, `inputs.json`, repository state, and
`c120.manifest.json`. The manifest records ABI v1/table 104, unchanged
capabilities, no runtime/GC/VFS source changes, the C120 source hashes, and
the three boot outcomes.

## Runtime and allocation boundary

C120 is managed application code. It does not modify the NativeAOT runtime,
PAL, GC, VFS, capability table, ABI table 104, or the existing input bridge.
The host and controls use fixed arrays and bounded control-owned storage;
the runner keeps the established allocating NativeAOT composite proof and
resident lifecycle checks.

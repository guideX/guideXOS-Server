# NativeAOT C111: User-Facing Managed Application

## Result

C111 turns `Managed Workspace` into the first genuinely user-facing managed
guideXOS application. It remains a logical application inside the one
resident NativeAOT composite image. The normal App Model record resolves the
application, `desktop::launch_app_with_context` performs the same activation
work used by the shell, and the managed dispatcher selects the logical C#
entry point.

The managed application uses the existing `KernelCompositor` surface rather
than introducing a second UI framework. Managed code requests a window, sets
the title, draws text and a panel, and creates a button through a small host
callback table. The kernel continues to own the window, compositor, input,
focus, and close semantics.

The acceptance result and exact hashes are recorded in:

`out/dotnet/c011ec111-user-facing-managed-app/c111.manifest.json`

## C110 handoff and scope

C110 established the production shared-composite lifecycle and App Model
routing:

`BuiltInAppMetadata` -> `ManagedNativeAot` -> bounded selector ->
`desktop::launch_app` -> `nativeaot::launchLogicalApplication` -> one resident
`/system/apps/GXOSAPP.ELF` -> managed dispatcher.

C111 keeps that architecture. It does not add a second NativeAOT image,
runtime instance, code-manager registry, module loader, runtime unload path,
GC restart, or per-application runtime state.

## Existing user-facing boundary

The existing production application surface is the kernel compositor. It
supports windows and title bars, application-owned labels and buttons,
rectangles, focus, pointer routing, keyboard routing, and close. Native apps
already use this surface. C111 exposes only the small set needed by the two
managed logical applications:

- request a bounded window with a title and dimensions;
- draw bounded text;
- draw a bounded filled rectangle;
- add a bounded button;
- request a logical window close.

No new widget library or shell UI was added. The managed callbacks are
available only during the synchronous managed dispatch and are backed by the
existing singleton compositor surface.

## Applications and App Model records

Primary application:

- ID: `com.guidexos.apps.managed.workspace`
- display name: `Managed Workspace`
- launch kind: `ManagedNativeAot`
- selector: `1`
- image: `/system/apps/GXOSAPP.ELF`

Secondary application:

- ID: `com.guidexos.apps.managed.status`
- display name: `Managed Status`
- launch kind: `ManagedNativeAot`
- selector: `2`
- image: `/system/apps/GXOSAPP.ELF`

`Managed Status` remains an independent logical application and regression
target. It has a distinct title, panel color, status content, launch counter,
and button. Selector `0`, duplicate selectors, unknown IDs, and invalid
records remain rejected by the existing catalog rules.

The records are emitted through the existing Start menu and All Programs
surfaces. C111 does not change their ordering or redesign either surface.

## Production launch path

The acceptance path is:

`App Model record / shell activation`
-> `desktop::launch_app_with_context`
-> `appmodel::resolveLaunchTarget`
-> `ManagedNativeAot`
-> `nativeaot::launchLogicalApplication`
-> `launchLogical`
-> one resident composite image
-> managed dispatcher
-> selector `1` or `2`
-> managed compositor callbacks.

The C111 kernel validation invokes `desktop::launch_app_with_context` with the
resolved normal App Model IDs. This is the same activation handler used by a
shell record; it does not call the NativeAOT launcher directly for the primary
acceptance proof.

## Launch-context ABI and ownership

`NativeGxAppContext` retains the C110 prefix and extends it with:

```text
uint32_t size
uint32_t apiVersion
NativeHostCallTable* host
void* userData
const uint8_t* launchContext
uint32_t launchContextLength
uint32_t launchFlags
```

The C111 host table retains `log` at its existing offset and appends the
surface callbacks:

```text
requestWindow, drawText, drawRect, addButton, closeWindow
```

The compatibility prefix is 24 bytes. A full C111 context is 56 bytes and a
full C111 host table is 56 bytes on the target ABI. Legacy selector-only
contexts with the 24-byte prefix remain accepted.

The launch context contract is deliberately bounded:

- encoding: UTF-8 bytes (the acceptance values are ASCII);
- maximum: 48 bytes, excluding no hidden terminator because the explicit
  length is authoritative;
- native validation: rejects a nonzero length with a null pointer, lengths
  greater than 48, and embedded NUL bytes;
- allocation: the desktop/native launcher copies the validated bytes into a
  49-byte stack buffer for the synchronous managed call;
- managed ownership: the managed dispatcher copies the bytes into a managed
  `byte[]` before using them for rendering and logging;
- lifetime: the native stack copy is valid only during the managed dispatch;
  managed code never retains the native pointer;
- empty context: null pointer plus length zero; it is rendered as `<empty>`;
- after return: no context pointer remains live and the next launch receives
  a fresh validated value.

This is a bounded extension, not a general marshalling framework. The
existing selector-only path remains compatible when no context is supplied.

## Managed Workspace behavior

Each launch requests a 520x300 window titled `Managed Workspace`. It displays
managed-rendered content containing:

- the application title and NativeAOT identity;
- a persistent resident launch count;
- the current copied launch context, or `<empty>`;
- a managed allocation proof (`byte[] copied`);
- an application status line.

The managed code creates an `Activate action` button. The existing compositor
pointer path activates it and the application updates its persistent action
count and button label to `Action complete`. The kernel validation exercises
the same compositor mouse-down/mouse-up routing and records the interaction
result. This is one bounded interaction, not a new input framework.

## Managed Status behavior

Each launch requests a distinct 520x300 window titled `Managed Status`. It
displays its own persistent launch count, copied launch context, an allocation
proof, the continuing managed-thread status, and a distinct logical-app
status. Its `Refresh status` button keeps the two logical applications
observably separate while using the same resident image and host surface.

## Persistent and per-launch state

The ordinary managed static launch counters intentionally survive logical
relaunch because the composite image, managed heap, and managed thread stay
resident. The per-launch context is copied afresh on every entry and is not
stored in persistent application state.

The required Workspace values are:

```text
Workspace launch 1: count=1, context=first-launch
Status launch 1:    count=1, context=status-after-native
Workspace launch 2: count=2, context=return-launch
Status launch 2:    count=2, context=status-relaunch
Workspace launch 3: count=3, context=<empty>
```

The managed serial records are emitted by the application itself as
`C111-WORKSPACE` and `C111-STATUS` records. The same contexts are also
rendered into the managed window text. Thus the argument proof is not native
logging alone.

## Runtime and regression behavior

The C111 application naturally allocates managed `byte[]` context copies and
small rendering/ABI objects on normal behavior. The existing C107 composite
proof remains in the dispatcher, so each launch also verifies ordinary static
state, `[ThreadStatic]` state on the continuing managed thread, and a
deterministic allocation check. `[ThreadStatic]` remains runtime/thread-local
state and is not part of the user-facing state model.

The existing once-per-boot lifecycle markers remain required:

- runtime initialization: 1;
- PAL initialization: 1;
- GC initialization: 1;
- code-manager registration: 1;
- module initialization: 1;
- executable mapping: 1;
- managed heap initialization: 1;
- resident heap preservation: one record per managed entry (5 in C111).

The resident identity and TLS records must remain stable across all five
managed entries. The C111 sequence also launches native Notepad between the
first Workspace and first Status entries, proving native routing and the
resident managed domain coexist.

## Close and return

After the first Workspace launch and interaction, C111 uses the existing
`KernelCompositor::requestCloseWindow` path. The logical window is closed and
the application returns a deterministic success result while the composite
image, heap, TLS, and managed statics remain resident. Later managed launches
create a fresh user-facing window and continue the persistent counters.

## Negative coverage

C111 validates the relevant bounded-context failures:

- 49-byte context: rejected before managed dispatch;
- null pointer with nonzero length: rejected deterministically;
- selector `0`: invalid record rejected;
- unknown managed record ID: rejected by App Model lookup;
- missing composite image: `not-found` without creating resident state;
- independent `/system/wall/C104A.ELF`: existing `busy` or `base-collision`
  guard remains active after the shared image is resident.

Every negative case is followed by valid managed activity in the same boot,
so the check includes no-crash, no-state-corruption, and later relaunch
behavior.

## QEMU evidence

The dedicated runner is:

`scripts/dotnet/run-c111-user-facing-managed-app.ps1`

It stages one production composite as `/system/apps/GXOSAPP.ELF`, builds the
C111 kernel, and performs three fresh QEMU boots by default. Each boot keeps:

- `serial.log`, including App Model, managed-output, surface, interaction,
  context, state, lifecycle, and negative-test markers;
- QEMU stdout/stderr logs;
- the ESP staging tree;
- serial and input/image hashes in the manifest.

The visual proof uses the existing framebuffer surface: the kernel draws all
windows, checks the focused title and widget count, and samples the managed
panel color. This preserves deterministic evidence in the headless QEMU
harness without making serial output the entire application experience.

The final clean run completed three fresh boots, all classified `PASS`. Each
boot recorded five managed launches, five visible surfaces, one image mapping,
four resident reentries, five TLS bridge installations, one heap
initialization, and five heap-preservation records. The per-boot serial hashes
and the full classifications are in the manifest; the serial files are
`boot-01/serial.log`, `boot-02/serial.log`, and `boot-03/serial.log` below the
dedicated evidence root.

The expected full sequence is:

`Workspace(first-launch) -> Notepad -> Status(status-after-native) ->
Workspace(return-launch) -> Status(status-relaunch) -> Workspace(empty)`

The exact per-boot classification is in
`out/dotnet/c011ec111-user-facing-managed-app/c111.manifest.json`.

## Files and change boundaries

Production changes are limited to the managed launch-context extension, the
existing desktop activation handler, the thin compositor host bridge, the
C111 dispatcher behavior, and the validation runner/documentation. The
NativeAOT runtime-pack source and runtime singleton behavior are unchanged.
No GC algorithm, GC root enumeration, TLS runtime investigation, process
isolation, package management, or GUI-framework redesign is part of C111.

## Outcome and next phase

Outcome A is achieved. One normal user-visible guideXOS App Model record
launches the meaningful managed `Managed Workspace` C# application through
the production resident composite, accepts fresh bounded context, preserves
intentional state, closes and relaunches, and coexists with native
applications. The independent `Managed Status` record remains a distinct
managed regression target. Three fresh QEMU boots passed the full C111
acceptance parser.

The smallest recommended next phase is C112: extract the proven bounded
managed application contract into a reusable application-development model
(shared managed application base/helpers, documented host-surface capability
versioning, and one additional small managed app or sample) while preserving
the one composite/one runtime architecture. C112 should not broaden into a
general GUI framework or runtime isolation effort.

# Developer Studio Phase 27X — Compiler-Built GUI Application

## Result and boundary

Phase 27W ended at the application boundary: the bare-metal compiler/runtime
could execute the bounded `log`/return subset, but it could not construct a
live App Model GUI application. Phase 27X adds the smallest production-facing
bridge needed for one window. The compiler-generated NativeElf now creates a
real `KernelApp` window, gives it compositor-owned widgets, renders through the
normal compositor callback, remains alive while the desktop is pumped, and
closes through the normal application close path.

The GUI runtime proof is complete. The remaining limitation is the existing
bare-metal `NativeElfRunService::start` call shape: it invokes the loader
synchronously. The target cooperatively pumps the desktop and scheduler, but
an external caller cannot independently poll or request-close the same run
operation until `start` returns. Therefore the full Developer Studio
non-blocking owner contract remains **Outcome B** even though the compiler-built
window and its normal lifecycle are QEMU-proven. The first unsupported boundary
is run-service scheduling/ownership, not compiler code generation, NativeElf
loading, App Model identity, or compositor rendering.

No debugger functionality was added.

## Objective and compiled fixture

The fixture is staged from `scripts/fixtures/phase27x` by
`smoke-compiler-bootstrap.ps1 -Phase27XOnly`. Its project metadata is a real
`native-gui-application` project with output
`/P27X/build/bin/amd64/p27x.elf`. The source is intentionally small:

```c
#include "guidexos_app.h"

int gx_main(gx_app_context* ctx)
{
    int window = gx_window_create(320, 180, "Developer Studio Phase 27X");
    if (window == 0) return -1;
    if (gx_window_set_text(window, "27X GUI 27") != 0) return -2;
    int result = gx_window_run(window);
    if (gx_window_destroy(window) != 0) return -3;
    return result;
}
```

The smoke controller edits the same project source to `27X GUI 28` for the
second run. The loader does not create a canned window or inject either text.
The compiler emits the string literals into the NativeElf read-only data
segment and the generated call sites load the runtime table entries.

## Chosen native App Model ABI

The append-only `gx_host_calls` table now exposes four bounded application
operations:

```text
native_window_create(ctx, width, height, title) -> handle
native_window_set_text(ctx, handle, text)         -> result
native_window_run(ctx, handle)                   -> result
native_window_destroy(ctx, handle)               -> result
```

The table is versioned by the existing NativeElf application ABI. The new
slots are at offsets 352, 360, 368, and 376; the full table is 384 bytes.
`native_elf_runtime.h`, `native_app_runtime.h`, the SDK ABI header, and
`native_abi_layout_test.cpp` assert these offsets and size.

This shape is deliberately narrower than exposing compositor objects. The
kernel validates the application context and host table, copies title/text
through bounded string validation, enforces the existing compositor minimum
and maximum dimensions, and only accepts the current generation's window
handle. The kernel allocates the `NativeElfGuiApplication` and its bounded
label storage; compiled code never receives a kernel object pointer. Invalid
arguments, stale handles, duplicate destroy, and incompatible declarations
return deterministic errors.

The compiler recognizes the four exact runtime declarations through its
existing external-call path. String literals are a new bounded read-only-data
expression type; `char*` parameters are a narrow compiler type, not a libc
surface. The compiler rejects a recognized function with the wrong signature
and rejects an object compiled against the old compiler object ABI version.

## Application and window lifecycle

`NativeElfRunService::prepare` still validates the exact successful
`BuildResult`, registers one in-memory `DevelopmentTemporary` App Model record,
and assigns a generation-bound `dev.guidexos.*` identity. `start` resolves and
revalidates that record, binds the identity into the NativeElf loader, and
launches the exact artifact through the production loader/trampoline path.

The runtime bridge constructs a `NativeElfGuiApplication`, which derives from
the existing `KernelApp` base. `KernelApp::createWindow` registers a normal
`KernelWindow` with `KernelCompositor`; the application adds one normal label,
copies the text into its owned buffer, and requests redraw. The compositor
calls the application's ordinary `draw` override. This is the same window
registration, widget, repaint, and close machinery used by hand-written native
applications; no second compositor or fake window system was introduced.

`gx_window_run` performs an initial desktop draw and then repeatedly calls the
existing `desktop::cooperative_yield` and `desktop::tick` path. It then halts
until the next iteration, so it does not use a tight busy loop. In automated
QEMU proof mode, after at least two pumps and a rendered frame, the controller
requests `KernelCompositor::requestCloseWindow`. That call invokes
`KernelApp::requestClose`, which runs `onWindowClose`, `shutdown`, compositor
unregistration, window destruction, and the terminal application state change.
The compiled program then exercises the bounded destroy call; destruction is
idempotent after the normal close.

Physical close-button input uses the same compositor request path. The QEMU
automation uses a production API-driven close request because reliable mouse
click injection is not part of this harness. The application cannot be
mistakenly marked normal when loader teardown is forced after a fault: forced
cleanup suppresses the normal-close flag.

## Rendering proof

The render marker is emitted from `NativeElfGuiApplication::draw`, not from
`gx_window_set_text`. The callback increments a render count, computes the
bounded FNV-1a content hash, and emits the window ID, text, and hash. The smoke
controller additionally checks that:

- the application and compositor window were created;
- the window ID belongs to the current run generation;
- `draw` ran at least once;
- the cooperative pump ran at least twice;
- the expected text and content hash were observed;
- the compositor has no live window after close.

The first QEMU boot recorded:

```text
NativeElf: app_create generation=1 id=dev.guidexos.phase27x
NativeElf: window_create id=0x000003E8 width=320 height=180
NativeElf: compositor_render window=0x000003E8 text=27X GUI 27 hash=fnv1a64:C7A3F8397310AC36
NativeElf: close_request id=0x000003E8
NativeElf: close_complete normal
...
NativeElf: app_create generation=2 id=dev.guidexos.phase27x
NativeElf: window_create id=0x000003E9 width=320 height=180
NativeElf: compositor_render window=0x000003E9 text=27X GUI 28 hash=fnv1a64:C7A403397310BEE7
```

The compositor callback is the authoritative machine-verifiable render
evidence available on this path. No screenshot-only assertion is used.

## Scheduler, run ownership, and identity

The live target yields through the production desktop pump, allowing input,
compositor repaint, timer progress, and scheduler bookkeeping to continue.
The run owner remains in the kernel and the run state is set to `Running`
before the loader is entered. However, because `start` is synchronous, the
owner cannot receive a separate external `poll` or `request_close` call while
that invocation is on the stack. The Phase 27X smoke proves cooperative OS
progress and normal close; a future phase must move the run body to an owned
scheduled execution context to satisfy the stronger asynchronous Developer
Studio controller contract.

The same Phase 27W generation-bound temporary identity is used for both runs.
The generation is logged with the application creation, the window is owned
by the matching runtime application, and old handles do not match a new
generation's window. The registration is an in-memory development record only;
there is no package installation, Start Menu entry, desktop shortcut, or
persistent compositor record.

## Resource ownership and failure cleanup

One NativeElf generation owns the application object, its one compositor
window, label/text/title backing storage, runtime handles, loader image and
application stack, run-service state, and temporary development registration.
Normal `KernelApp::requestClose` unregisters and deletes the window. Loader
teardown captures the run evidence and then force-closes any remaining window
before deleting the application object. This covers an application that exits
without explicit destroy and an application that fails after window creation.
The run service then unregisters the exact handle/generation/application
identity and permits release.

The ABI rejects a null or out-of-range context/output, malformed or
overlength title/text, dimensions outside the bounded range, non-current
handles, and calls made after close. A duplicate destroy of the just-closed
window is a safe tombstone operation. Unsupported recognized declarations are
rejected during compilation by signature validation; the old ABI version is
rejected during object/build validation. These are bounded negative controls,
not a claim of a complete hostile-process sandbox.

## Rebuild, rerun, and stale protection

The QEMU fixture proves, without rebooting within the boot:

1. cold dependency-aware build and exact artifact deployment;
2. render/close/cleanup of `27X GUI 27`;
3. warm cache hit;
4. shared-header/source edit and recompilation;
5. a new artifact hash and render of `27X GUI 28`;
6. compiler failure after success, with no stale launch or registration;
7. malformed GUI declaration failure with the same stale-launch protection;
8. restored source and a final successful build/export.

The Run service only accepts the successful build snapshot's exact path, size,
and SHA-256 and revalidates them immediately before NativeElf launch. A failed
later build therefore cannot turn the Phase 27P retained last-good artifact
into a new Run session.

## Validation

Host/compiler/runtime checks completed for this working tree:

```text
run-compiler-functions-host-test.ps1       PASS
run-compiler-globals-host-test.ps1         PASS
run-compiler-arrays-host-test.ps1          PASS
run-compiler-pointers-host-test.ps1        PASS
run-compiler-structs-host-test.ps1         PASS
run-compiler-struct-arrays-host-test.ps1   PASS
run-compiler-multifile-host-test.ps1       PASS
run-compiler-object-host-test.ps1          PASS
run-compiler-bootstrap-host-test.ps1       PASS
run-native-abi-layout-test.ps1              PASS
run-native-elf-host-test.ps1                PASS
run-native-elf-trampoline-host-test.ps1    PASS
run-native-elf-runtime-host-test.ps1       PASS
run-native-elf-development-app-model-host-test.ps1 PASS
```

The functions host test also checks the GUI call mapping, generated host-table
slot access, string-pointer handling, and incompatible signature rejection.
The ABI layout and development registration tests cover table compatibility,
generation identity, and temporary cleanup. The compiler-generated final
`x27main.elf` was externally audited as ELF64 AMD64 with two load segments;
its disassembly contains calls through offsets `0x160`, `0x168`, `0x170`, and
`0x178`.

Focused fresh-boot QEMU commands are:

```text
scripts/smoke-compiler-bootstrap.ps1 -Phase27POnly -BootCount 1
scripts/smoke-compiler-bootstrap.ps1 -Phase27UOnly -BootCount 1
scripts/smoke-compiler-bootstrap.ps1 -Phase27VOnly -BootCount 1
scripts/smoke-compiler-bootstrap.ps1 -Phase27WOnly -BootCount 1
scripts/smoke-compiler-bootstrap.ps1 -Phase27XOnly -BootCount 1
```

The final Phase 27X fresh boot passed all of:

```text
DEVELOPER_STUDIO_PHASE27X_BEGIN
DEVELOPER_STUDIO_PHASE27X_BUILD_PASS
DEVELOPER_STUDIO_PHASE27X_DEPLOY_PASS
DEVELOPER_STUDIO_PHASE27X_APP_CREATE_PASS
DEVELOPER_STUDIO_PHASE27X_WINDOW_CREATE_PASS
DEVELOPER_STUDIO_PHASE27X_RENDER_27_PASS
DEVELOPER_STUDIO_PHASE27X_RUNNING_PASS
DEVELOPER_STUDIO_PHASE27X_CLOSE_PASS
DEVELOPER_STUDIO_PHASE27X_CLEANUP_PASS
DEVELOPER_STUDIO_PHASE27X_REBUILD_PASS
DEVELOPER_STUDIO_PHASE27X_RENDER_28_PASS
DEVELOPER_STUDIO_PHASE27X_RERUN_PASS
DEVELOPER_STUDIO_PHASE27X_STALE_BLOCK_PASS
DEVELOPER_STUDIO_PHASE27X_NEGATIVE_PASS
DEVELOPER_STUDIO_PHASE27X_PASS
```

The earlier focused phase results remain available in the same source tree:
Phase 27P, 27U, 27V, and 27W are the prerequisite QEMU gates and were not
weakened. The 27X command itself runs the earlier bootstrap/runtime gates
needed to reach the 27X fixture.

## Limitations and deferred work

- Bare-metal Run `start` is still synchronous; asynchronous execution and
  independent external close/poll are the next boundary.
- The automated close is API-driven through the production compositor close
  request. Developer Studio was not physically mouse-driven in this run.
- The proof is one bounded window and one bounded text label; it is not a GUI
  toolkit, libc, dynamic linker, shared-library, or arbitrary launcher.
- No debugger, breakpoints, stepping, symbols, source mapping, watch values,
  attach, or debugger UI was added.

# Developer Studio Phase 28N — Source Editor Debugger Integration

## Outcome

Outcome A is complete. The packaged Developer Studio source editor is now a
first-class client of the Phase 28M debugger: it renders the paused frame-0
source location, renders persistent source breakpoints, toggles breakpoints
through the existing manager, and navigates the real editor from Call Stack
and Breakpoints rows.

Phase 28M remains the prerequisite and is exercised by the Phase 28N focused
diagnostic. Phase 28N adds no debugger backend capability and does not replace
the breakpoint manager, stepping controller, Call Stack, variables, watches,
output queue, or generation/session lifecycle.

## Repository and package provenance

The standalone implementation was branched from final Phase 28M commit
`f50ce42a7a9c602f69b86c1a579952b562aa382b` on local branch
`phase28n-source-debugger-integration`. The implementation commit is
`591c2fa` (`developer studio: integrate debugger with source editor`), followed
by local proof-hardening commit `77d861f` (`developer studio: harden manager
breakpoint proof`).

The server repository started at `766d89673e191ef34477442bee7435cc4fa00c8a`,
branch `v0.5_DEVELOPER_STUDIO`, with upstream
`origin/v0.5_DEVELOPER_STUDIO` and divergence 4 ahead / 0 behind. The server
package/harness integration is local commit `e839e64` (`developer studio:
integrate source editor debugging`). This final package/hash update is recorded
in a separate local closure commit; neither commit was pushed.

The final staged package artifacts are:

| Architecture | Size | SHA-256 | ELF machine |
| --- | ---: | --- | --- |
| AMD64 | 870,864 bytes | `2532A8D98044DC9895DC1E0F01327F34DFB1E3F6B515DB5245C0F38CD72B7629` | x86-64 |
| ARM64 | 1,025,824 bytes | `5B464074A6B9F037E6352C4B25E57984AD2B8104862B388D8E99B25D8098FA68` | AArch64 |

The recorded Phase 28M baseline was AMD64 861,856 bytes,
`BC9D35CB1D9582EB0ABD4CD434E05EA8162B1B1C22F030E4A6D376B4C4546DF3`, and
ARM64 1,015,504 bytes,
`DDED73B4B5D9C683CCC051370E1CEBA6FBB9DE30BF6997B7A7B2331DEBAC5A01`.
Both Phase 28N artifacts were rebuilt through the normal standalone-to-server
staging path and machine-validated; no binary was manually edited.

## Editor architecture

The existing `drawEditor` source renderer remains responsible for tabs,
scrolling, line numbers, syntax spans, caret placement, and visible-line
layout. Phase 28N adds the bounded `DebugEditorModel` in
`src/developer_studio_debug_editor.{h,cpp}` and keeps the integration at the
existing application/controller boundary.

The model contains at most eight persistent breakpoint rows, one execution
location, and one selected-frame inspection location. It is refreshed on
manager/UI state changes and is read by the editor renderer; it is not a
second debugger or an unbounded per-glyph scan.

### Source identity

All editor matching uses the project ID plus canonical project-relative source
path and one-based source line. Matching is case-insensitive in the same way
as the existing path helper and is path-boundary aware. `src/main.cpp` and
`tests/main.cpp` therefore remain distinct. Basename-only matching is not
used.

### Execution marker

The gold execution marker is written only when the real debugger is active and
paused and all of the following agree:

* session generation and stop generation are non-zero;
* Call Stack frame 0 is current and source-mapped;
* frame 0 source path/line agrees with the debugger current location;
* the Call Stack is valid, non-stale, and carries the same generation pair.

The marker is therefore authoritative frame 0 state, never the selected caller,
caret line, clicked breakpoint, or stale Phase 28M panel state. A separate
light-blue inspection marker may follow a selected caller. Selecting frame 1
does not move the gold frame-0 marker.

Execution state is cleared before or while Continue, Step, Running,
cancellation, failure, terminal teardown, and generation replacement are
observed. The source document and persistent breakpoint markers remain open
when the execution marker is cleared.

### Breakpoint gutter and manager authority

The gutter projects the authoritative manager snapshot when the current
debugger UI ABI is available. It carries the manager ID, project/path/line,
enabled state, and action kind. Enabled BREAK rows use the existing blue
visual, disabled rows use a bounded gray visual, and LOG rows use a bounded
orange visual. The Breakpoints pane and editor projection are refreshed from
the same snapshot.

The editor toggle is the existing source-context handler operating on the
active document and caret line. On add it dispatches manager ADD first, waits
for the accepted snapshot/stable ID, then updates the local controller mirror
and editor projection. On remove it resolves the exact canonical manager row
and dispatches manager REMOVE; it does not locally decrement a count or create
a pseudo-breakpoint. Duplicate/rejected/non-executable rows are rejected and
then reprojected from manager state.

Breakpoints-pane Space continues to use the existing enable/disable commands.
The gutter keeps the row while disabled and changes only its visual state.
LOG rows navigate like BREAK rows and remain represented while running.

## Navigation behavior

Call Stack Enter, row selection, and Up/Down selection use the existing
`WorkspaceControllerOpenDocumentAtLocation` path. A mapped frame reuses an
already-open canonical document, opens it through the project workspace path
when necessary, scrolls the target line into view, and updates the caret for
explicit navigation. Locals and Watches continue to use the selected debugger
frame. An unmapped or missing-source frame remains selectable but reports a
bounded source-unavailable state; no guessed file, basename match, or host
filesystem fallback is fabricated.

Breakpoints-pane Enter, mouse row activation, and double activation use the
same navigation helper and the manager's source identity. This applies equally
to BREAK and LOG rows.

The navigation helper never reloads an existing document. Consequently an
already-open document with unsaved in-memory edits keeps its buffer and dirty
state while the caret/visibility changes. No duplicate tab is created.

## Stepping proof

The focused real-application diagnostic drives the same debugger UI routes used
by the normal application:

* Step Into crosses from `src/main.cpp` into `src/helper.cpp`; the helper tab,
  visible line, Locals, and frame-0 marker update together.
* Step Over advances in the caller without selecting a helper location.
* Step Out returns from helper to `src/main.cpp`, refreshes the selected-frame
  data, and moves the frame-0 marker to the caller.

Each new pause clears the old runtime marker before publishing the new mapped
location. The diagnostic asserts editor document/caret agreement at every
step.

## Diagnostic automation and evidence

`scripts/smoke-compiler-bootstrap.ps1` now accepts `-Phase28N` and the focused
`-Phase28NOnly` mode. N-only deliberately reuses the real Phase 28M project,
build, backend, UI panes, and fixture while adding the Phase 28N assertions;
it is not a fake editor harness. The package sentinel is
`/Apps/DeveloperStudio/.phase28n-diagnostic` with contents
`guideXOS-phase28n`.

The focused marker set includes:

* `DEVELOPER_STUDIO_PHASE28N_BEGIN` and `...EDITOR_READY_PASS`;
* `...EDITOR_BREAKPOINT_ADD_PASS`, `...EXECUTION_FRAME0_PASS`, and
  `...CALL_STACK_NAVIGATION_PASS`;
* `...BREAKPOINT_STATE_PASS`, repeated `...STEP_SOURCE_SYNC_PASS`, and
  `...LOGPOINT_PROJECTION_PASS`;
* `...RUNNING_MARKER_CLEAR_PASS`, `...EDITOR_BREAKPOINT_REMOVE_PASS`,
  `...NEW_SESSION_MARKER_PASS`, `...BOUNDS_PASS`, and `...PASS`.

The three fresh isolated packaged QEMU boots passed in one invocation. Evidence
was preserved at:

`C:\Users\guideX\AppData\Local\Temp\guidexos-phase28g-e58be2f03a44446aa1f5755296b1c10a`

Each boot produced the full Phase 28N and Phase 28M guest pass markers. Serial
markers provide widget/state evidence for editor readiness, breakpoint add and
remove, frame-0 source agreement, caller navigation, step source sync,
logpoint projection, running/terminal clearing, and the second generation.
No framebuffer hash was required or captured. Physical mouse injection was
not used; the diagnostic invokes the real application input/selection handlers.

## Verification

Standalone host verification:

* `tests/debug_editor_test.cpp` passed through `build.ps1`;
* CMake configured successfully with MinGW and
  `ctest --test-dir build/phase28n-cmake -R developer_studio_debug_editor_test`
  passed;
* the existing Developer Studio model, project, debugger, step, stack,
  variables, watches, and conditional-breakpoint tests passed;
* AMD64 and ARM64 production ELF builds passed.

Server/backend regressions passed:

* object ABI layout;
* breakpoint manager contract;
* Call Stack controller;
* debug output and policy contracts;
* native debug watches and debugger runtime;
* NativeElf validator, runtime host, trampoline, and development App Model;
* filesystem contract;
* source-step controller;
* PowerShell parse checks for `build.ps1` and
  `scripts/smoke-compiler-bootstrap.ps1`;
* `git diff --check`.

The compiler object ABI remains `COMPILER_OBJECT_ABI_VERSION == 10`. The
debugger C ABI remains `GX_DEVELOPMENT_DEBUG_API_VERSION == 1`, and the
guideXOS C ABI remains `guidexos-c-abi-v1`. No server debugger ABI source was
changed.

## Lifecycle, bounds, and limitations

Breakpoint configuration is retained according to the existing debugger
manager lifetime; runtime execution and inspection markers are not retained
as current after terminal state. A new session reprojects manager rows and
cannot display an old session's address or source marker. The editor model is
bounded at eight persistent rows plus one execution and one inspection marker.

Source/build freshness protections remain those already provided by the
Phase 28M build and debugger path. Phase 28N does not add source-version
diffing, breakpoint persistence across IDE restarts, or a properties dialog.
Completion, hover values, Set Next Statement, Set Value, remote debugging,
attach, and other debugger expansions remain out of scope.

The server worktree retains the pre-existing untracked ESP/P28M and generated
host evidence; those files were not cleaned or committed. The standalone
`main` branch remains at
`33c37e56df6dd70e0963b2caca824e100f5e3d7e`. Neither repository was pushed.

# Developer Studio Phase 28H — Selected-Frame Variables

Phase 28H extends the Phase 28G paused-frame inspection path so a Developer
Studio client can inspect arguments and locals in any validated frame returned
by the current Call Stack query.

## Scope and compatibility

- This phase is server-side only. The standalone Developer Studio repository is
  not changed.
- No debug command was added. `GX_DEVELOPMENT_DEBUG_INSPECT_VARIABLES` remains
  command 20 and `GX_DEVELOPMENT_DEBUG_CALL_STACK` remains command 19.
- The public debug request ABI remains 104 bytes. The existing
  `auxiliaryAddress` field (offset 88) is used only for this query-local
  selection. The object ABI remains version 10.
- `auxiliaryAddress == 0` is exactly the Phase 28G top-frame behavior.
- Values `1` through `GX_DEVELOPMENT_DEBUG_MAX_CALL_STACK_FRAMES - 1` select a
  frame index in the current validated Call Stack. The current limit is 16
  frames. Values outside that range return `INVALID_FRAME` and
  `GX_ERROR_INVALID_ARGUMENT`.

The field is an index, never a caller-supplied instruction pointer, frame
pointer, or memory address. Selection is not stored in the server: every
variables query reconstructs and validates the current Call Stack. This keeps
the feature query-local and means a new pause, Step Out, resume, session
change, or stop-generation change cannot accidentally reuse an old selection.

## Frame authority and evaluation rules

The current validated Call Stack is the single source of truth for selected
frame identity. For frame 0, variables use the paused execution context as in
Phase 28G. For a caller frame, variables use that Call Stack entry's:

- validated frame pointer for RBP-relative variable slots;
- logical instruction pointer, which is the mapped call-site address rather
  than the raw return address;
- function identity and source mapping.

Caller-frame inspection requires the `CALL_SITE_MAPPED` frame flag. The
implementation does not guess a caller's RBP from an unvalidated request and
does not fall back to the caller's raw return address. The selected function
identity must also agree with source metadata at the selected logical RIP.
All reads are performed against the paused snapshot; inspecting variables,
selecting another frame, or polling does not change CPU registers, the active
frame, or the execution state.

If the requested frame is no longer present or the paused context is stale,
the query fails with the existing stale/no-context status instead of evaluating
against a different frame. Control operations (`STEP_*`, `RESUME`, and
`CALL_STACK`) continue to operate on the actual paused execution context;
selection is intentionally not persistent execution state.

## QEMU proof fixture

`scripts/fixtures/phase28h/` contains a two-source native GUI project:

- `src/main.cpp`: `gx_main`, with `root_value = 100`, `before_call = 27`, and
  the post-call `after_call = 200` live-range negative case;
- `src/helper.cpp`: `helper` and nested `helper_tail` with arguments and locals
  used to prove three distinct frames;
- `app/app.json` and `guidexos.project`: the normal Developer Studio project
  and application metadata.

The smoke compiles the fixture twice, checks the warm-cache path, pauses at the
nested `gx_main -> helper -> helper_tail` call chain, then verifies:

| Selected frame | Source | Values asserted |
| --- | --- | --- |
| 0, `helper_tail` | `src/helper.cpp` | `tail_input=28`, `tail_bonus=5`, `tail_value=33` |
| 1, `helper` | `src/helper.cpp` | `input=10`, `delta=4`, `adjusted=14`, `doubled=28` |
| 2, `gx_main` | `src/main.cpp` | `root_value=100`, `before_call=27`; `after_call` absent |

The same paused stack is queried again after frame selection to prove that the
stack is unchanged. Repeated Debug `POLL` calls compare the complete captured
register context, including RFLAGS/RIP/RSP/RBP and RAX–R15. Invalid indices 3
and 16 are rejected. Step Out removes the old leaf frame; the old frame-1
query is then rejected as stale, while the newly selected root frame remains
inspectable. Resume rejects variable inspection while running, and the GUI
render/close/cleanup checks preserve the existing Developer Studio run path.

The smoke emits `DEVELOPER_STUDIO_PHASE28H_FRAME_EVIDENCE` with the validated
RBP and logical RIP for all three runtime frames, plus the required Phase 28H
pass markers. It also exports and externally audits the guest-generated
`h28main.elf`.

Run the focused proof with:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/smoke-compiler-bootstrap.ps1 -Phase28HOnly -BootCount 3
```

`-Phase28HOnly` includes the established Phase 28E, 28F, and 28G prerequisite
chains and their required markers. Each boot uses a fresh isolated ESP. The
expected final line is:

```text
DEVELOPER_STUDIO_PHASE28H_PASS
```

## Host-side checks and known boundaries

The existing ABI-layout, Call Stack controller, Source Step controller, and
native debugger runtime tests remain part of the focused host validation. The
server-side implementation also validates request size, command, stop/session
identity, frame existence, stack-frame shape, metadata identity, and bounded
slot reads before returning a variable.

The current GXSM v1 metadata path supports the validated stack and variable
records used by the fixture. A frame without a mapped call site or without
compatible source-variable metadata is reported as unsupported/stale rather
than guessed. Watches, expressions, persistent selection state, UI changes,
and optimized-register location tracking are intentionally deferred.

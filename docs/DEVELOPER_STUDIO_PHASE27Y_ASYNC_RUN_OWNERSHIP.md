# Developer Studio Phase 27Y — Asynchronous Run Ownership and External Application Lifecycle

## 1. Result and Phase 27X boundary

Phase 27X proved the compiler/runtime/App Model/compositor path: the compiler
generated NativeElf calls, the production host ABI created a real
`KernelApp`, the compositor rendered the window, and the application closed
normally. Its remaining boundary was ownership. `NativeElfRunService::start()`
entered the NativeElf loader directly and did not return until the target had
terminated, so its caller could not independently observe or control the live
application.

Phase 27Y moves the target lifetime behind a bounded cooperative execution
owner. `start()` performs validation and deployment binding, schedules one
target context, gives it one initial slice, and returns while the GUI target is
still alive. The owner can then poll the copied session snapshot, request a
normal close, request safe cooperative cancellation, and release a terminal
session. No debugger functionality is part of this phase.

## 2. Scheduler and execution architecture

The repository already contains the AMD64 voluntary context-switch primitive
in `kernel/arch/amd64/context_switch.cpp`. Phase 27Y uses that primitive as a
single production NativeElf execution owner; it does not add a second
threading subsystem or a busy-wait loop.

The service owns one bounded 32 KiB scheduler stack and one target context.
`host_native_window_run()` reaches `desktop::cooperative_yield()` and then
returns to the owner through `native_elf_scheduler_yield()` at the next
bounded pump boundary. The normal kernel `desktop::tick()` path also gives the
active target one bounded slice when the owner is in the desktop loop. A
target-side tick is guarded so it cannot recursively schedule itself.

This preserves the compiler/runtime ABI: compiled applications still call the
same `gx_window_*` entries, while only the execution boundary changes.

## 3. Run-session model

`NativeElfRunService` owns one bounded `Operation` record. It contains the
project/build identity, temporary App Model registration identity, monotonic
session handle, generation, execution report, close/cancellation requests,
terminal exit result, and cleanup status. Public callers receive only the
generation-bound integer handle and copied `gx_development_run_snapshot`
data; kernel application, compositor, loader, and scheduler pointers stay
private.

The snapshot retains the prior ABI prefix and appends:

```text
closeRequested
cancellationRequested
generation
```

The host-call table also appends
`bare_metal_development_run_cancel` after the existing GUI slots. Existing
host-call offsets remain unchanged. Snapshot result data remains queryable
until `release()` accepts the terminal session.

## 4. State transitions

The externally visible lifecycle is bounded:

```text
Registered -> Launching -> Running -> Closing -> Completed
                                      \\-> Cancelled
Registered -> Cancelled
any launch/runtime/cleanup failure -> Failed
```

`Exited` and `CleaningUp` are short internal terminalization stages. A
terminal state is assigned once, cleanup is guarded by the operation's
cleanup flags, and failure is never converted into success. A second
same-project `prepare()` while the operation is live returns a deterministic
`RuntimeBusy` failure without disturbing the first session.

## 5. Start return and external observation

For a compiler-built GUI target, `start()` returns after the first target
cooperative yield. At that point:

- the copied snapshot is `Running`;
- the generation matches the returned handle;
- a real `KernelApp` and compositor window exist;
- the compiler-produced text has rendered; and
- the target remains active on its own execution context.

`poll()` only copies bounded state and never enters the target context.
`pump()` is an owner-side kernel operation; the desktop scheduler also pumps
automatically at its normal `tick()` boundary. The Phase 27Y smoke increments
an owner heartbeat and polls while the target remains `Running`, proving that
the run owner—not merely unrelated kernel activity—has control.

## 6. Normal close and cancellation

Normal close is not task killing. `request_close()` marks the session
`Closing`, calls `request_native_elf_gui_close()`, and enters
`KernelCompositor::requestCloseWindow()`. That is the production close path:
`KernelApp::requestClose()` invokes the application's close callback and
shutdown, unregisters/deletes the compositor window, and lets the NativeElf
entry return normally. The session then records `Completed` and the exit code.
Duplicate close requests are terminal-idempotent.

Cancellation is deliberately bounded and safe. It never destroys an
arbitrary NativeElf stack asynchronously. `cancel()` requests the same
cooperative compositor close boundary but records `cancellationRequested` and
terminates as `Cancelled`. Cancellation before `start()` is also handled by
cleaning the registered session without launching the artifact. If an
arbitrary target stops yielding, this phase does not claim a safe hard-stop;
that is an explicit limitation.

## 7. Generation and race safety

The service accepts a control operation only when the handle matches the
currently owned operation. App Model resolution also checks handle,
generation, and application ID. Consequently, an old handle cannot close or
cancel a later run. The smoke covers:

- `start()` returning before the first close request;
- completion visible on the first poll after an owner-side close;
- duplicate close/cancel calls;
- a stale handle used while a newer generation is running;
- terminal handle operations after completion;
- cancellation of a registered-but-not-started session;
- busy rejection while the first session remains intact; and
- cleanup before a new generation is prepared.

No broad lock was added. The current bare-metal path is a single cooperative
owner, so the operation and scheduler flags are serialized at explicit
ownership boundaries.

## 8. Resource ownership and cleanup

The run operation owns the temporary development registration, execution
generation, scheduler context, loader report, and terminal result. The
NativeElf generation owns its image, dedicated application stack,
`NativeElfGuiApplication`, `KernelApp`, compositor window, label storage, and
GUI handles.

Normal target return first runs loader teardown, which captures compositor
evidence and reclaims application-owned resources. The owner then clears the
development identity, unregisters the exact temporary App Model record, and
records the terminal snapshot. Failure paths use the same registration and
runtime cleanup accounting. `release()` only clears the operation after the
terminal result and cleanup are observable.

## 9. Rerun and stale-build behavior

The Phase 27Y fixture performs, in one boot:

1. build and run compiler-produced `27Y GUI 27`;
2. observe `Running`, render, owner activity, busy rejection, external close,
   completion, and cleanup;
3. warm-build, edit the shared declaration/source, and produce a new artifact;
4. run and render `27Y GUI 28` under a new generation;
5. reject the first generation's stale handle against the second session;
6. inject a compiler failure and verify no Run session or registration is
   created from the retained artifact; and
7. restore the source, rebuild, and exercise cooperative cancellation.

`prepare()` retains the exact successful BuildResult identity and `start()`
revalidates the artifact path, size, SHA-256, ELF, project, and manifest
immediately before scheduling. A failed later build cannot launch the previous
ELF.

## 10. Validation and QEMU proof

The focused proof is:

```text
scripts/smoke-compiler-bootstrap.ps1 -Phase27YOnly -BootCount 1
```

Its required ordering is:

```text
DEVELOPER_STUDIO_PHASE27Y_START_RETURNED
DEVELOPER_STUDIO_PHASE27Y_RUNNING_PASS
DEVELOPER_STUDIO_PHASE27Y_OWNER_ACTIVE_PASS
DEVELOPER_STUDIO_PHASE27Y_CLOSE_REQUEST_PASS
DEVELOPER_STUDIO_PHASE27Y_CLOSE_COMPLETE_PASS
```

The same boot requires the compiler-built compositor render markers for both
`27` and `28`, busy rejection, cleanup, cancellation, stale-handle rejection,
stale-build blocking, and the final `DEVELOPER_STUDIO_PHASE27Y_PASS` marker.
The script exports and externally audits the generated `y27main.elf`.

Relevant host/compiler/runtime suites and prior focused QEMU gates remain
regression gates. The final validation record belongs in this document after
the fresh-boot run; an unavailable dependency or tool is reported as an
environment limitation rather than a fabricated PASS.

Validation completed on 2026-09-07:

- PowerShell syntax parsing passed.
- The requested compiler host suites passed: functions, globals, arrays,
  pointers, structs, struct arrays, multifile, object format, and bootstrap.
- Native ABI layout, development App Model, NativeElf validator, runtime, and
  trampoline host tests passed.
- Focused QEMU passed on one fresh boot each for Phases 27P, 27U, 27V, 27W,
  27X, and 27Y. The first combined P/U/V/W attempt exposed transient U/V
  fixture misses; isolated fresh reruns passed, and the W smoke was updated to
  assert the new terminal `CANCELLED` state for pre-start `request_close()`.
- Phase 27Y serial evidence placed `START_RETURNED`, `RUNNING`, `RENDER_27`,
  `OWNER_ACTIVE`, `CLOSE_REQUEST`, `COMPLETION_BEFORE_POLL`,
  `CLOSE_COMPLETE`, and `CLEANUP` in that order, followed by rebuilt `28`,
  stale-handle, stale-build, cancellation, negative, and final PASS markers.

## 11. Limitations and deferred debugger work

- One active bare-metal NativeElf session is supported; concurrent project
  ownership is rejected.
- Execution is AMD64-specific because that is the existing integrated context
  switch path.
- Cancellation is cooperative and cannot safely interrupt a non-yielding
  arbitrary NativeElf stack.
- The Phase 27Y proof uses the production API-driven compositor close request;
  it does not add physical mouse injection.
- The session API is lifecycle-only. Breakpoints, stepping, pause-at-
  instruction, register/memory inspection, source maps, symbols, stack traces,
  attach, debugger panes, and exception interception remain out of scope.

Phase 27Y is the lifecycle substrate for a later debugger arc: session
identity, execution ownership, application state, close/cancel control, and
terminal result are now separate concepts without exposing speculative debug
interfaces.

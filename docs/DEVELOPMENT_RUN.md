# Hosted Development Run ABI

The hosted Server exposes Run Project only to the canonical Developer Studio
Native ELF application (`com.guidexos.developerstudio`). It is not a generic
package installer, arbitrary ELF launcher, production sandbox, or bare-metal
filesystem contract.

## ABI

The append-only `gx_host_calls` slots are:

```text
192  development_run_prepare
200  development_run_start
208  development_run_poll
216  development_run_request_close
224  development_run_release
sizeof(gx_host_calls) = 232
```

Phase 27E appends the separate bare-metal Developer Studio build/VFS block
after this hosted-development block; the full table is 312 bytes when those
slots are advertised. The hosted Run contract and its offsets remain
unchanged.

Requests and snapshots carry `size` and `version`. The request contains fixed
caller-owned strings for project root/ID/kind/target, `app/app.json`, the build
artifact path, and the build SHA-256. The snapshot returns a generation-bound
handle, state/error, child process ID, native runtime ID, owned-window counts,
exit code, cleanup status, application identity, display name, artifact hash,
and a bounded error message.

## Preparation contract

Preparation is fail-closed and revalidates all inputs. The current supported
contract is:

- absolute, non-symlink project root with bounded `guidexos.project`;
- `native-gui-application` and `guidexos.amd64.hosted.native` only;
- manifest path exactly `app/app.json` and artifact path exactly
  `build/<manifest entry path>`;
- NativeElf, one amd64 entry, `gx_main`, `guidexos-c-abi-v1`, `native-elf`;
- exactly `log`, `window`, and `draw` permissions;
- no file associations or desktop registry hints;
- non-symlink regular artifact inside the project root, at most 64 MiB;
- SHA-256 equal to the successful Build Project snapshot;
- valid ELF64 AMD64 static `ET_EXEC` image containing `gx_main`.

Installed App Model IDs, reserved Developer Studio-owned namespaces, stale
handles, owner mismatches, malformed manifests, and unsupported claims are
rejected. The temporary `RegisteredApp` uses `AppSourceKind::DevelopmentTemporary`
and remains in the in-memory registry only.

## Lifecycle

The service owns eight bounded deployment slots. A handle encodes the slot and
generation; releasing a terminal deployment increments the generation. Start
resolves the temporary record through the normal AppLaunchResolver and
DesktopService NativeElf path. Poll observes the existing ProcessTable,
NativeAppProcessTable, and compositor window ownership. Request-close publishes
`MT_Close` only for deployment-owned windows.

On child exit, cleanup unregisters the temporary App Model entry, records the
exit status, reports `cleanupComplete`, and permits release. On Developer Studio
runtime cleanup or server shutdown, owner-bound windows and temporary records
are cleaned without terminating an unrelated process. A native child handles
its own close event through the existing runtime cleanup path.

## Validation

The ABI offsets and structure sizes are asserted by
`tests/native_abi_layout_test.cpp`. The experimental full Server build includes
`development_run_service.cpp` and is run with:

```text
cmd /c .\build-native-experimental.bat
```

`nativeapp.capabilities` reports the five development-run calls and the
revalidation policy. The Studio-side controller test and package build are
maintained in the Developer Studio checkout. Interactive F5 driving remains a
separate compositor-input validation step; the experimental shell exposes
`gui.key` for focused-window checks, but it is not a replacement for visual
manual validation.

The bare-metal Developer Studio build/run extension, including its bounded
temporary registration model and Phase 27W QEMU evidence, is documented in
`docs/DEVELOPER_STUDIO_PHASE27W_RUN_PROJECT.md`.

The compiler-built NativeElf GUI extension and its Phase 27X evidence are
documented in
`docs/DEVELOPER_STUDIO_PHASE27X_COMPILER_BUILT_GUI_APPLICATION.md`.

Phase 27Y moves the bare-metal NativeElf target behind a bounded cooperative
execution owner. `start()` returns after the first target yield while the
compiler-built GUI remains alive, and the owner can poll, request production
compositor close, request safe cooperative cancellation, observe completion,
and rerun after cleanup. Its lifecycle, generation, stale-build, race, and
QEMU proof are documented in
`docs/DEVELOPER_STUDIO_PHASE27Y_ASYNC_RUN_OWNERSHIP.md`.

Phase 28A carries compiler-emitted source mappings through object reopen,
linking, and a deterministic final-ELF `GXSM` trailer. A pre-launch request
can select one project-relative source file and executable line; the NativeElf
run owner resolves that line, installs a one-shot `INT3`, reports the source
identity and raw/normalized RIP while paused, then resumes into the normal GUI
and cleanup lifecycle. Its object ABI, mapping policy, stale-source controls,
and focused QEMU proof are documented in
`docs/DEVELOPER_STUDIO_PHASE28A_SOURCE_BREAKPOINT.md`.

Phase 28B adds architectural single-instruction Step Into for a genuine
Phase 28A paused source breakpoint. The target's restored context resumes
with AMD64 `RFLAGS.TF`; vector 1 `#DB` is accepted only for the active
debugger generation, step token, scheduler target, expected code selector, TF
ownership, and validated image/stack ranges. The accepted trap clears TF in
the saved context before exposing `GX_DEVELOPMENT_RUN_PAUSED` again and
returns a `SINGLE_STEP` snapshot. See
`docs/DEVELOPER_STUDIO_PHASE28B_SINGLE_INSTRUCTION_STEP.md` for the exact
fixture, three-step proof, cancellation ordering, and QEMU evidence.

Phase 28C adds bounded source-aware Step Into on top of that primitive. It
uses exact final-ELF `GXSM` address mappings and the trusted
`path/function/line/column` identity, stopping when the executed identity
changes without guessing across unmapped gaps. The request is capped at 128
instructions and stops safely at an application-to-runtime boundary without
exposing runtime source identity. See
`docs/DEVELOPER_STUDIO_PHASE28C_SOURCE_STEP_INTO.md` for the state machine,
snapshot/result contract, direct callee proof, and three-boot QEMU evidence.

Phase 28E adds the distinct source-aware Step Out operation. From a genuine
paused user frame it validates the compiler's AMD64 frame-pointer ABI, recovers
`[RBP]`/`[RBP+8]`, reuses the generation-bound temporary return `INT3`, runs the
remaining callee body without exposing intermediate source pauses, validates
the caller frame, and stops at the first mapped caller source line. It keeps
object ABI 9 and appends the debug snapshot tail. The primary cross-file proof
is `src/helper.cpp:8 helper` to `src/main.cpp:15 gx_main`; root `gx_main` Step
Out is rejected as `NO_CALLER_FRAME`. See
`docs/DEVELOPER_STUDIO_PHASE28E_SOURCE_STEP_OUT.md` for the ABI, frame
contract, live QEMU evidence, focused regressions, and remaining limitations.

Phase 28F adds bounded read-only NativeElf Call Stack inspection. A paused
compiler-built target can expose up to 16 validated user frames from the
current function through the `gx_main` root, with exact GXSM source identity
where available. The query is rejected for Running, Stepping, terminal, stale,
or incomplete contexts and does not mutate registers, stack memory, or
breakpoints. See
`docs/DEVELOPER_STUDIO_PHASE28F_CALL_STACK_INSPECTION.md` for the fixed ABI,
frame policy, nested cross-file proof, Step Out stack shrink, and three-boot
QEMU evidence.

Phase 28G adds compiler-owned persisted arguments/locals metadata and bounded,
read-only paused top-frame inspection through debug command 20. It advances
the compiler-object ABI to 10 and the final ELF `GXSM` metadata to version 2
when variable records are present. See
`docs/DEVELOPER_STUDIO_PHASE28G_ARGUMENTS_LOCALS_INSPECTION.md` for the fixed
record contract, RBP-slot policy, live-range rules, hosted/bare-metal routing,
and QEMU proof sequence.

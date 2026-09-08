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

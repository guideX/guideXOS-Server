# Developer Studio COMP4D — Native Build/Run Qualification

**Outcome:** `COMP4D_GUEST_ACCEPTED`

**Branch:** `v0.5_DEVELOPER_STUDIO`

- **Starting HEAD:** `317253fef87f5585d7e4e71795474cef8d1d7acc`
- **Starting upstream divergence:** 0 behind / 0 ahead
- **Ending state:** this report and the qualification changes are committed locally as a direct child of the starting HEAD. The resulting commit SHA is reported in the closeout response.

The starting tracked worktree was clean. Existing untracked `out/` and `kernel/out/` COMP2/COMP3 artifacts were preserved. The COMP4A, COMP4B, and COMP4C reports and implementation were inspected. No `AGENTS.md` or separate authoritative phase marker was present. No branch switch, reset, stash, clean, rebase, or push was performed.

## Qualification scope

COMP4D checked the guideXOS guest pipeline from project files in VFS, through the production cooperative Build service and compiler, to published NativeElf bytes and the production Run service. Hosted tests covered service behavior that is difficult to exercise deterministically in one boot. This work did not expand the language or alter the Build, Run, or SDK ABI implementations.

## Production path and ownership

The guest Build-start callback in `kernel/core/native_elf/native_elf_loader.cpp` validates the app context and request, copies caller-owned strings into bounded kernel storage, and calls `BareMetalBuildService::request_owned` using the NativeElf runtime identity. Poll, release, and cancel use the same owner identity. The build manager returns generation-bearing handles and keeps job state isolated by owner and generation.

The cooperative service validates the project and manifest through VFS, selects source files, prepares output and object-cache directories, runs the bounded compiler pipeline, validates the resulting NativeElf, and finalizes publication. The compiler reads guest VFS sources and creates objects, links modules, and writes the ELF itself; it does not spawn a hosted compiler or linker. Each poll advances service work by a bounded stage. The compiler invocation itself remains a monolithic, non-preemptible stage; cancellation takes effect at supported cooperative boundaries.

Run preparation consumes the successful Build snapshot's project and artifact identity. `NativeElfRunService::prepare` revalidates the project manifest, artifact path, byte size, SHA-256, architecture, ABI, entry point, and NativeElf image before registration. `start`, `poll`, and `release` then own execution and cleanup. A successful bare-metal run may be observed in the terminal `EXITED` state before release; the guest smoke checks either `EXITED` or `COMPLETED`, no Run error, complete cleanup, expected application exit code, and expected output.

## ABI and publication contracts

The ABI layout test and compile-time assertions passed. `gx_build_snapshot` retains its established layout: handle offset 8, artifact path offset 72, total size 8,800 bytes. The NativeElf host-call table retains the COMP4B cancellation callback at offset 456 and total size 464 bytes. The guest runtime and hosted ABI tests both assert these values.

The Build service writes to its temporary artifact, validates it, calculates the artifact identity, and only then publishes. Existing valid output is backed up during publication. A failed or cancelled job has no success artifact identity. If publication fails, the service restores prior bytes or reports a recovery error rather than claiming that an incomplete file was published. The hosted service regression covers forced rename failure, rollback, and successful recovery.

Run checks the live file immediately before launch. The guest smoke replaced the validated ELF with a four-byte partial ELF and verified that Run preparation rejected the changed size; it then restored and byte-checked the original. After a syntax-failed Build, the smoke verified that Run rejected that failed Build snapshot even though the previous executable remained on disk.

## Hosted qualification

All 14 applicable COMP4A–COMP4C suites passed. The COMP4D production Build-service fixture covers a two-file function call, loops, conditionals, signed division and remainder, repeated generation-distinct Builds, syntax diagnostics, cancellation, prior-artifact preservation, and recovery. The dedicated arithmetic suite executes generated AMD64 code on the host; this is hosted evidence, not guest evidence.

The suites were:

1. `run-compiler-driver-diagnostics-host-test.ps1`
2. `run-compiler-build-job-manager-host-test.ps1`
3. `run-compiler-bootstrap-host-test.ps1`
4. `run-compiler-arrays-host-test.ps1`
5. `run-compiler-functions-host-test.ps1`
6. `run-compiler-globals-host-test.ps1`
7. `run-compiler-pointers-host-test.ps1`
8. `run-compiler-structs-host-test.ps1`
9. `run-compiler-struct-arrays-host-test.ps1`
10. `run-compiler-object-host-test.ps1`
11. `run-compiler-multifile-host-test.ps1`
12. `run-compiler-stack-frame-bound-test.ps1`
13. `run-compiler-build-service-cooperative-host-test.ps1`
14. `run-compiler-signed-arithmetic-host-test.ps1`

`tests/native_abi_layout_test.cpp` compiled and ran successfully. `tests/test-developer-studio-qac-acceptance.ps1` passed, including the COMP4D marker, focused proof-gate, and failure-preservation assertions. `git diff --check` passed.

## Genuine guest acceptance

QAC was used for the guest boot. Immediately before admission, QAC reported online with zero QEMU processes, zero managed sessions, zero queued reservations, and zero blocked reservations. No unmanaged QEMU was launched.

The accepted boot used one fresh QAC-managed boot. Guest evidence:

- Cooperative Build progress and a successful two-module Build passed; the ELF was validated, captured as serial evidence, and parsed by the host harness as `w27main`.
- The fixture exercised a function call, a loop, a conditional, `-17 / 5 == -3`, and `-17 % 5 == -2`. The real guest Run returned `27` and emitted `27W RUN 27`.
- A second owner could not poll the Build. Repeated cancellation of the queued Build was accepted; the terminal snapshot had no artifact identity, and the prior executable bytes remained intact.
- Run rejected the four-byte partial ELF with the expected size-change error, after which the original artifact was restored and byte-checked.
- A guest syntax failure reported `GX_BUILD_ERROR_COMPILER_SYNTAX`, one diagnostic with `[syntax]`, and an empty artifact path, size, and hash. The previous executable bytes were preserved.
- Run rejected the syntax-failed Build snapshot. The saved artifact backup was removed.

The serial log ended with `DEVELOPER_STUDIO_COMP4D_GUEST_PASS`. QAC observed that exact marker 28.34 seconds after QEMU launch, before child exit and within the 120-second runtime deadline. QEMU exited with code 0, QAC verified reservation release, and the coordinator reported `managed_success=1`. The acceptance script exited 0.

Preserved final evidence:

- Directory: `C:\Users\guideX\AppData\Local\Temp\guidexos-comp4d-accepted-d0adb493b2ad48d783c503ad749f04d2`
- Serial: `boot1.serial.log`
- QAC/process lifecycle: `boot1.host-trace.log`
- Captured artifact evidence: `artifacts/`
- Kernel build output: `kernel/out/comp4d-qac/` (left untracked)

One intermediate diagnostic boot reported `VFS_ERR_NOT_SUPPORTED` while creating `/P27W/build/obj/amd64`; it did not reproduce in the subsequent guest boots, including the accepted boot. No production VFS change was made for that single observation.

## Findings and limits

The guest run service correctly reported the successful execution as `EXITED`, while the inherited smoke helper initially required `COMPLETED`. The helper now accepts the service's documented successful terminal state and checks cleanup and application results. The new Build-output matcher also verifies substring matches in longer progress and diagnostic lines. These were qualification-harness corrections; no production Run or compiler defect remained after the focused rerun.

The guest cancellation check covers a queued Build cancelled before its first compiler poll. Hosted tests exercise cancellation at a supported boundary during the non-preemptible compiler call. The guest compiler call itself cannot be interrupted mid-pipeline; it observes cancellation at the next cooperative boundary.

The COMP4D boot runs its focused Build/Run path rather than the entire legacy Phase 27W matrix. It directly exercises the NativeElf Build and Run services. A direct click-through from the Developer Studio UI was not part of this qualification. No division-by-zero or signed-overflow fault was triggered.

No required COMP4D acceptance gate remains. The broader compiler-language limits documented by COMP4A–COMP4C remain unchanged.

## Git closeout

The report, production-path hosted fixture, guest smoke, and QAC acceptance helper changes are committed locally. No push was performed. Existing untracked validation artifacts were left untouched, and the new `kernel/out/comp4d-qac/` build output remains untracked.

# Developer Studio COMP4B: Cooperative Build Lifecycle

**Outcome:** COMP4B_HOSTED_ACCEPTED
**Branch:** `v0.5_DEVELOPER_STUDIO`
**Starting HEAD:** `2ee01bf4592c97aabb50074387cf516943f6fd34`
**Guest runtime qualification:** Pending. This phase used hosted tests only and did not launch QEMU or request a QAC reservation.

## Lifecycle before COMP4B

The bare-metal Build job manager already had one fixed job slot, owner identities, generation-tagged handles, queued/running/completing/terminal internal states, bounded request strings, and a short lock. The Build callback did not use those facilities asynchronously:

1. `host_bare_build_start` copied the caller's request strings and called `BareMetalBuildService::start_owned`.
2. `start_owned` admitted a synchronous job and retained the request in the manager.
3. Before the callback returned, the service validated project and manifest metadata, selected the bounded source set, prepared output and object-cache directories, ran `compile_project_incremental`, validated the output as NativeElf, calculated its hash, and completed the job.
4. A later poll normally observed an already terminal snapshot. There was no Build cancellation callback.
5. `pump_cooperative_test_step` advanced a test-only arithmetic continuation. It did not run the production Build or compiler.

The compiler driver and Build service used static bounded buffers and a 128 KiB compiler-service stack. The full incremental compiler call was synchronous. It processed up to the configured translation-unit limit, including source loading, include expansion, lexing, parsing, code generation, object-cache read/write, linking, ELF generation, and its own file publication/readback checks.

## Lifecycle after COMP4B

The NativeElf Build-start callback now uses `request_owned`. It reserves the same single manager slot, copies request strings into manager-owned storage, creates a generation-scoped handle, initializes a `Preparing` snapshot, and returns without compiling. The caller advances at most one Build stage by polling. The ordinary desktop update boundary can also advance one stage when it is not running inside a NativeElf target. Both paths use the same production stage runner and the existing dedicated compiler-service stack. No worker thread, wait loop, or dynamically growing queue was added.

Polling is itself a cooperative boundary: each successful poll of a live Build advances at most one stage and then returns the latest snapshot. A caller can update its UI between calls. No percentages are reported. Stage text and compiler output use the bounded existing output lines; output remains capped by `GX_BUILD_MAX_OUTPUT_LINES` and reports truncation through the existing field.

### Public snapshot and internal state mapping

| Internal lifecycle | Snapshot/API state | Behavior |
| --- | --- | --- |
| Admitted, waiting for its first step | `GX_BUILD_PREPARING` | The manager's internal state is `Queued`; a cancellation request makes it terminal immediately. |
| Metadata and directory preparation | `GX_BUILD_PREPARING` | Each poll runs one preparation stage. Source object-directory preparation handles at most one source per step. |
| Compiler running | `GX_BUILD_RUNNING` | One compiler pipeline invocation runs on the service stack. Its stage label and source/module output are retained in the snapshot. |
| Artifact validation and finalization | `GX_BUILD_VALIDATING_ARTIFACT` | The service validates a temporary file. The manager enters its internal `Completing` state for this stage. |
| Success | `GX_BUILD_SUCCEEDED` | Set only after publication succeeds; artifact identity fields refer to the final path. |
| Failure | `GX_BUILD_FAILED` | Compiler diagnostic classifications and output are retained. No final artifact is published by this Build attempt. |
| Cancellation | `GX_BUILD_CANCELLED` | A deterministic terminal result with `GX_BUILD_ERROR_BUILD_CANCELLED`; artifact identity fields are cleared. |

No redundant public `Queued` or `Cancellation requested` enum value was added. A live cancellation request is shown in the existing output list while the public state remains active. The final snapshot carries the existing terminal `Cancelled` state.

### Cooperative steps

The production stage sequence is:

```text
Queued / Preparing
        |
        v
Validate metadata and source selection
        |
        v
Prepare output directories
        |
        v
Prepare one source's object-cache directory per step
        |
        v
Run the bounded compiler pipeline (one non-preemptible stage)
        |
        v
Validate temporary NativeElf artifact
        |
        v
Claim finalization -> retain old artifact -> publish validated file
        |
        v
Succeeded
```

Any failed stage terminates as `Failed`. A cancellation accepted before finalization terminates as `Cancelled` and removes the temporary output. The service publishes no intermediate compiler image to the expected final artifact path.

## Yielding and non-interruptible work

The safe return boundaries are after metadata/source selection, output-directory preparation, each source object-directory preparation, the complete compiler invocation, NativeElf validation of the temporary artifact, and final publication. The desktop pump skips nested calls while a NativeElf target is active; the Build poll callback can still advance one stage when the application explicitly polls.

The compiler pipeline remains monolithic for this phase. One poll that enters the compiler stage can still occupy the caller until the entire `compile_project_incremental` call returns. That call includes bounded source reads and parsing/code generation for all selected sources, object cache generation/reopen, linking, ELF generation, and compiler-level output write/readback validation. Per-source compiler preemption was not introduced. Cancellation requested during this call is observed after it returns; cancellation is not immediate within lexer, parser, code generator, linker, VFS, or NativeElf validator internals.

The `Metadata` stage also performs bounded project metadata and source-selection work. It is limited by the existing request/source/text bounds, but its individual VFS and directory operations are not preemptible. No hosted test presents the fake trampoline as an actual guest stack switch.

## Cancellation guarantees

- **Queued:** cancellation uses the owner identity and full generation-tagged handle. It immediately produces `Cancelled`; repeated cancellation of that same cancelled job returns success.
- **Active preparation or compilation:** cancellation records a request and output message. The work already in the current stage runs until its normal return boundary. At that boundary the manager commits `Cancelled`; a compiler-created temporary ELF is removed.
- **After compiler completion or during artifact validation:** cancellation is honored before publication. The expected artifact path and its prior bytes remain unchanged.
- **Finalization-adjacent:** `claim_finalization` and cancellation are serialized under the manager lock. A cancel processed before the claim wins and prevents publication. After the claim, cancellation returns `GX_ERROR_BUSY`; publication's terminal result wins. If a failed stage is committed before cancellation is processed, the `Failed` result wins.
- If cancellation is accepted while a stage is active and that stage later reports a compiler failure, cancellation wins at the return boundary; diagnostic output already emitted by that stage remains in the bounded output lines, while the terminal error code correctly identifies cancellation.
- **Repeated requests:** one cancellation output line is retained; requests do not add unbounded queue entries or additional jobs.
- **Owner close or shutdown:** both request cancellation. An in-flight stage is allowed to reach its return boundary so temporary output can be cleaned and the manager slot can be released.

Cancellation does not terminate a currently executing compiler stage. The API promise is cooperative stage-boundary cancellation, not preemptive cancellation.

## Job ownership and generations

The manager remains single-slot and fixed-capacity. A second Build request while any job occupies that slot returns `GX_ERROR_BUSY` with a zero output handle. There is no waiting queue. The manager copies each request into bounded storage before returning the handle.

Handles retain the existing format: the high 32 bits identify the generation and the low 32 bits identify the fixed slot. Every poll, cancel, release, source-membership update, and stage commit checks both handle and owner identity. A stale callback cannot commit against a later generation. The service has one static stage context, cleared when a job reaches a terminal state or an owner closes; it is never reused while a manager stage is in flight.

## Artifact ownership and publication

The compiler writes its complete output to the reserved sibling `gx4btmp.tmp` file in the artifact directory. The service performs NativeElf validation and calculates artifact metadata from those temporary bytes. Until the finalization claim, `artifactValid` remains clear and the expected final path remains untouched.

After validation, publication retains an existing artifact at the reserved sibling `gx4bbak.bak`, renames the validated temporary file to the expected final path, and removes the backup. If the temporary-to-final rename fails, the service restores the backup and returns a publication failure. If backup cleanup fails after a successful rename, the new validated artifact remains successful and the fixed backup path is retried/recovered before a later Build; it cannot grow with every Build. If the underlying VFS also fails during rollback, the service reports that recovery failure and leaves the previous bytes in the backup path for recovery rather than writing an incomplete image to the final path.

The temporary and backup sidecars use FAT-compatible 8.3 names. They are reserved for the guest Build service in `build/bin/amd64`. Compiler object-cache files may be updated independently, but each cache object is written and validated using the compiler's existing cache publication contract; a cancelled Build never treats a cache object as the final application artifact.

Run implementation was not changed. Run continues to use the expected final path and its existing Build-result/hash/manifest/NativeElf checks. It cannot observe a partially written COMP4B artifact because publication happens only after validation and finalization claim.

## ABI and diagnostics

`gx_build_snapshot` size, version, and layout are unchanged. Existing state and compiler diagnostic values retain their numeric values and COMP4A classification behavior. `GX_BUILD_ERROR_BUILD_CANCELLED = 34` is append-only.

The NativeElf `gx_host_calls` table gains one append-only tail callback, `bare_metal_build_project_cancel`, at offset 456. Existing host-call offsets remain unchanged; the table size increases from 456 to 464 bytes. Runtime and hosted ABI layout assertions cover this addition. The callback is bounded to the existing job slot and is gated by the host-call table size like other optional calls. No new Windows runtime dependency was added.

There is no existing guest Developer Studio UI caller of this bare-metal Build callback in the inspected tree. COMP4B therefore exposes lifecycle/output data through the existing poll snapshot and adds the cancellation callback for consumers. Consumers should poll once per UI turn and render the latest output/state; no percent indicator is available or implied.

## Hosted qualification evidence

The new `run-compiler-build-service-cooperative-host-test.ps1` test links the production Build job manager, Build service, compiler driver, compiler front end, object/linker modules, and AMD64 backend. It uses the existing hosted VFS fake, and a small fake NativeElf validator/stack boundary. Compiler work is real production compiler execution over the hosted VFS; no compilation is mocked. The harness covers:

- queued cancellation before any stage executes;
- single-slot admission and a competing Build request;
- incremental stage/output progress and normal successful compilation/publication;
- cancellation injected during the monolithic compiler stage;
- cancellation during a syntax-failing compiler stage, retaining emitted diagnostic text while returning the cancellation classification;
- cancellation injected during temporary artifact validation;
- cancellation immediately before publication and rejection after finalization is claimed;
- failed syntax diagnostics, prior artifact preservation, and recovery on a later Build;
- forced publication rename failure, rollback of the prior artifact, and subsequent successful recovery;
- cleanup of temporary/backup files and VFS handles.

The production manager host test also covers repeated cancellation, ownership checks, generation reuse/stale handles, owner closure, shutdown, failure diagnostic retention, terminal state stability, one-slot queue bounds, and finalization arbitration. The COMP4A regression set passed all 12 suites: driver diagnostics, Build job manager, bootstrap, arrays, functions, globals, pointers, structs, struct arrays, object format, multifile linking, and compiler stack-frame bounds. The native ABI layout test passed with the appended callback. Service/manager and NativeElf loader syntax-only compile checks passed; the loader check showed only the existing unused-parameter warnings in `kernel_app.h`.

No QEMU or guest runtime test was run. These results qualify the hosted production code path and hosted fakes only. COMP4A historical guest evidence was not repeated and is not new COMP4B evidence.

## Remaining qualification and next milestone

Guest qualification remains pending for Build progress, cancellation, artifact rollback, and Build/Run compatibility on the actual VFS, scheduler, NativeElf application callback path, and target ABI. COMP4C should perform a fresh guest Build/Run regression and verify both supported VFS rename behavior and cancellation timing with real application polling, after the .NET Support track releases QEMU priority. No COMP4C work was started by this change.

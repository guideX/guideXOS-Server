# Developer Studio Phase 27W — Run Project

## Goal

Phase 27W adds the server-side Run Project boundary after the Phase 27V
build/dependency work. A supported Developer Studio NativeElf project is built
through `BareMetalBuildService`; the successful `BuildResult` is then validated,
temporarily registered, launched by the NativeElf loader, observed to exit, and
cleaned up.

This phase is deliberately a run/lifecycle phase. It adds no debugger behavior.

## Previous boundary

Before 27W, guideXOS Server could build and validate the project ELF and the
standalone Developer Studio already had a Run controller/callback shape, but
the bare-metal server path had no bounded development App Model registration.
There was therefore no server proof that a freshly built project artifact could
become a temporary application and be released again.

## Architecture

The existing `BareMetalBuildService` remains the only build entry point. The
new `NativeElfDevelopmentAppModel` is a deliberately small, in-memory bridge
for the bare-metal target. It owns one generation-bound temporary registration;
it does not persist, scan, install, or execute paths. `NativeElfRunService`
owns the run operation and calls the existing NativeElf validation and loader
path.

The hosted Server path remains the canonical multi-slot asynchronous
`DesktopService`/App Model implementation documented in
`docs/DEVELOPMENT_RUN.md`. Phase 27W extends the same build-result identity and
ownership rules to the bare-metal bootstrap service without creating a second
ELF loader.

## Build-to-run lifecycle

The supported sequence is:

1. Developer Studio requests a normal bare-metal build.
2. `BareMetalBuildService` reads the project and `app/app.json`, performs the
   existing incremental object/dependency build, links, validates NativeElf,
   and returns the exact artifact path, size, and SHA-256.
3. `NativeElfRunService::prepare` re-reads the project and manifest, checks the
   supported target, validates confinement, validates the exact artifact
   identity, and registers a `dev.guidexos.*` temporary application record.
4. `start` resolves that exact generation-bound record, revalidates the file,
   and invokes the production NativeElf loader/runtime. In a normal active
   NativeElf host this is the nested loader path; the kernel bootstrap proof
   uses the same loader through its top-level entry because it has no parent
   application.
5. The terminal path records the exit/log result, unregisters the temporary
   record, and permits release.

The service rejects a second owned operation while the first operation is
active. Pre-start close is cancellation. While the current bare-metal loader
is synchronously executing a target, request-close is not synthesized; the
target's normal runtime return is the close/completion boundary. The hosted
controller retains its existing event/poll-driven lifecycle for a live target.

## Development deployment ownership

The run operation owns its handle, generation, application identity, project
root, artifact-relative path, size, hash, runtime report, exit result, and
cleanup status. The temporary registration owns only bounded copied metadata;
no persistent package or App Model database state is created. Cleanup is
attempted on normal completion, validation/registration/launch failure, and
pre-start cancellation. Unregistering requires the exact handle, generation,
and application identity, preventing an old session from removing a newer one.

## Freshness and exact artifact rules

Run always starts with the ordinary Build Project operation. Existing Phase
27V object/dependency cache semantics decide which modules recompile. The
existing Phase 27P contract retains the last good ELF after a failed compile or
link for recovery, but a failed `BuildSnapshot` has no complete artifact
identity and cannot form a Run request. The Developer Studio controller skips
Run on build failure; the W service proof also rejects the failed snapshot
directly, so the retained ELF is never launched by that failed Run. Run accepts
only the successful build snapshot returned by that operation and rechecks
size, SHA-256, NativeElf validity, and entry point immediately before launch.

The W smoke proves cold build (`2 compiled / 0 reused`), warm build (`0 / 2`),
header/source invalidation, changed runtime output (`27W RUN 27` →
`27W RUN 28`), compiler failure blocking a Run request while the retained image
remains untouched, and link failure blocking a Run request on the same basis.

## Supported project and containment

The first target is intentionally narrow: AMD64,
`native-gui-application`, target profile
`guidexos.amd64.baremetal.bootstrap.native`, `NativeElf`, `gx_main`,
`guidexos-c-abi-v1`, and `native-elf` in `app/app.json`. The artifact must be
the project output under `build/bin/amd64/` and must be a validated static
ELF64 AMD64 image.

Application IDs must use the bounded `dev.guidexos.` development namespace.
Roots and artifact paths are bounded, project-owned, and reject absolute
substitution, `.`/`..` segments, duplicate separators, control characters,
malformed metadata, and identity mismatch. Installed kernel App IDs are
checked for collision. Run has no arbitrary executable-path API.

## Fixture and observable proof

`scripts/fixtures/phase27w` is a two-module project with a shared declaration.
The compiled `gx_main` emits `27W RUN 27` and returns `27`; after a shared
header/source edit it emits `27W RUN 28` and returns `28`. The proof also
exports the exact final guest-built ELF for host-side `readelf`/`objdump`
inspection.

The current bootstrap compiler supports the bounded `log`/return host-call
subset, not the full GUI/event API used by the hosted native GUI fixtures.
Consequently the W bare-metal proof demonstrates real NativeElf construction,
execution, observable output, exit, and teardown through serial/runtime state;
it does not claim a pixel-level GUI window proof. Extending the bootstrap
compiler with GUI/event calls is a later application-boundary phase.

## Repeated run and negative controls

The QEMU fixture proves `Run → close → cleanup → Run again` without rebooting
within a boot, rejects an already-running second prepare, rejects unsupported
project kind, missing artifacts, and escaping artifact paths, and verifies
cancellation before start leaves no active temporary registration. It also
deletes the artifact after registration to prove launch-time failure cleanup,
exercises compiler and link failure after a prior success, and restores the
project afterward.

## Validation

Host registration-model validation:

```text
powershell -ExecutionPolicy Bypass -File .\scripts\run-native-elf-development-app-model-host-test.ps1
```

Focused bare-metal validation, using a fresh disposable FAT-backed QEMU boot
per requested boot:

```text
powershell -ExecutionPolicy Bypass -File .\scripts\smoke-compiler-bootstrap.ps1 -Phase27WOnly -BootCount 3 -TimeoutSeconds 120
```

Required W evidence includes:

```text
DEVELOPER_STUDIO_PHASE27W_BEGIN
DEVELOPER_STUDIO_PHASE27W_BUILD_PASS
DEVELOPER_STUDIO_PHASE27W_DEPLOY_PASS
DEVELOPER_STUDIO_PHASE27W_LAUNCH_PASS
DEVELOPER_STUDIO_PHASE27W_RENDER_PASS
DEVELOPER_STUDIO_PHASE27W_CLOSE_PASS
DEVELOPER_STUDIO_PHASE27W_CLEANUP_PASS
DEVELOPER_STUDIO_PHASE27W_RERUN_PASS
DEVELOPER_STUDIO_PHASE27W_STALE_BLOCK_PASS
DEVELOPER_STUDIO_PHASE27W_NEGATIVE_PASS
DEVELOPER_STUDIO_PHASE27W_PASS
```

The terminal proof additionally records `phase27w_build_pass`,
`phase27w_deploy_pass`, `phase27w_launch_pass`, `phase27w_close_pass`,
`phase27w_cleanup_pass`, `phase27w_rerun_pass`,
`phase27w_changed_artifact_pass`, `phase27w_stale_block_pass`,
`phase27w_link_failure_stale_block_pass`, `phase27w_missing_artifact_rejected`,
`phase27w_launch_failure_cleanup_pass`, `phase27w_artifact`, and
`phase27w=PASS`.

## Limitations and status

Phase 27W's server build/run controller and bounded deployment boundary are
implemented and QEMU-proven. The current bare-metal bootstrap compiler/runtime
frontier prevents claiming the full visible native GUI lifecycle: no W fixture
window is rendered, and the bare-metal service uses the synchronous bootstrap
loader entry when invoked directly from the kernel smoke. The existing hosted
Developer Studio/App Model path remains the asynchronous live-application
route.

Therefore this phase is classified as **Outcome B — build/run controller
complete, application boundary blocks** until the bootstrap compiler exposes
the required GUI/event application calls and a live bare-metal target can stay
running for normal compositor close observation. The Developer Studio UI was
not physically mouse-driven in this validation; the existing Run command and
controller were not changed. No debugger work was added: breakpoints,
stepping, symbols, attach, inspection, and debugger UI remain deferred.

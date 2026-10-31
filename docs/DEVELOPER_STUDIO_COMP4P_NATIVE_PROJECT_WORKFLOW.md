# Developer Studio COMP4P — Native Project Workflow

## Disposition

**COMP4P_INTEGRATION_PARTIAL.** The target repository now contains a
compiler-compatible NativeElf Hello World project and a hosted integration
test that builds it through the production guideXOS compiler Build service.
The compiler emits and validates a NativeElf image containing the greeting,
and the supported native window calls are used by the sample source.

The New Project dialog, project/editor wiring, and visible application run
belong to the separate `D:\dev\guideXOS_Developer_Studio` repository. That
repository was inspected but not edited. Its current UI creates only the
existing Native GUI template and hard-codes `src/main.cpp` when opening a
created or opened project. It does not yet expose or reliably open this `.c`
sample. A hosted UI smoke exercised the existing editor and Build handler on
its isolated C++ fixture, but the hosted Build process failed and the expected
visible error text did not appear. No guest visual acceptance was performed.

## Repository preflight

- Repository: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO`
- Branch: `v0.5_DEVELOPER_STUDIO`
- Starting HEAD: `891e6aa9ec13460d3da7768c09253fffdca3b39c`
- Starting upstream divergence: 0 behind, 4 ahead of
  `origin/v0.5_DEVELOPER_STUDIO`
- Starting tracked worktree: clean
- Existing untracked artifacts: prior `out/` and `kernel/out/` validation
  directories were present and preserved
- No branch switch, merge, rebase, reset, stash, clean, history rewrite, push,
  QAC admission, or QEMU launch was performed
- No files in the Developer Studio Debugging or Tools worktrees were changed
- Ending implementation HEAD: `2d974107dad2bad421d4972201a3a0254e00cb22`
  (sample/test implementation commit;
  the final report-only commit is listed in the completion response)

The actual IDE implementation is in
`D:\dev\guideXOS_Developer_Studio` on branch `main`, HEAD
`058c83fe1ae270a103eadc7caca961328e8abe2a`. It had no tracked or untracked
changes at inspection. The server/compiler repository and IDE repository are
separate ownership boundaries.

## Existing IDE workflow audit

The inspected IDE repository contains the project model in
`src/developer_studio_projects.cpp`, workspace lifecycle in
`src/developer_studio_workspace.cpp`, Build controller in
`src/developer_studio_build.cpp`, Run request construction in
`src/developer_studio_run.cpp`, and GUI handlers in `src/main.cpp`.

| User action | Current state | Evidence and limitation |
|---|---|---|
| Open Developer Studio | Implemented and hosted smoke-tested | Phase 22 smoke started the hosted Server, launched the real app, and observed `initial_render=PASS`. |
| Select New Project | Implemented, model-tested separately | `commitNewProject` calls the existing workspace project service. COMP4P did not drive this dialog, and it does not select a compiler language/template. |
| Choose native application type | Partial | The UI hard-codes `ProjectKind::NativeGuiApplication`; the C-subset template is not offered. |
| Enter name and location | Implemented, model-tested separately | Project code bounds and validates names and paths, rejects a non-empty destination, and rolls back files/directories created by a failed transaction. COMP4P did not drive the New Project dialog. |
| Open generated source | Partial | The hosted smoke opened an existing `main.cpp` project. `openCreatedProject` and the open-project handler request `src/main.cpp` directly; a C-subset project needs the UI to use `src/main.c`. |
| Select Build | Implemented; hosted action attempted and failed | The real Ctrl+B path started on the hosted fixture, then reported `process_failed`, exit code 1, four errors, and zero warnings. The smoke could not complete its recovery Build. This is the hosted Clang backend, not the guest compiler; the trace does not establish the process failure's cause. |
| View diagnostics | Partial; visible error path failed this smoke | The Build Output pane rendered lifecycle/project/target lines, but the harness timed out waiting for `[Build] Error: Build Failed`; no source diagnostic text was observed in that pane. The production compiler service diagnostic snapshot passed in the COMP4P suite. |
| Select Run | Implemented, UI action unverified | `RunRequestFromBuild` requires the successful Build result's artifact identity. The bare-metal Run service revalidates the artifact before launch. |
| Observe application result | Partial | Phase 27X established the guest NativeElf window API. The COMP4P sample was compiled and ELF-validated, but its visible window was not run in a guest during this phase. |

These classifications distinguish IDE actions from direct compiler service
tests. The COMP4P hosted test calls the production Build service; it does not
call the Developer Studio New Project handler, and the Phase 22 UI smoke used
the hosted C++ toolchain fixture rather than the COMP4P guest compiler.

## Project template and generated files

The sample tree is
`sdk/samples/native-application-c-subset/`:

- `guidexos.project` — project ID, Native GUI kind, AMD64 bare-metal target,
  `gx_main`, source root/entry, registered source, ABI, and output name
- `app/app.json` — NativeElf manifest and `window`/`draw` permissions
- `src/main.c` — application entry and Hello World window lifecycle
- `include/guidexos_app.h` — four compiler-facing runtime declarations
- `README.md` — language/runtime boundary and build output

There is no `CMakeLists.txt` or hosted `build.ps1`: the in-kernel Build
service consumes the project metadata and manifest directly and invokes the
guideXOS compiler. It publishes to
`build/bin/amd64/hello-world.elf`. The project request uses
`guidexos.amd64.baremetal.bootstrap.native` and
`guidexos-native-baremetal-bootstrap-v1`.

This committed project tree is a source template, not a New Project dialog
option or a second project-generation framework. The existing IDE project
service supplies name/path validation and no-overwrite behavior for its
current template. Those protections have not yet been wired to this template
because that integration is owned by the IDE repository.

## Native language boundary

The template uses `.c` and the compiler's bounded C-like subset. This is not
general ISO C and does not imply full C++ support. The sample uses integer
locals/conditions, string literals, direct calls, and one quoted local header.
It does not use the standard C runtime, `printf`, STL, unsupported
preprocessing, or C++ language features. The COMP4O bytes utilities remain
optional and are not linked.

The entry contract is the existing NativeElf `gx_main` C ABI. The project is
AMD64-specific and uses the current compiler object ABI 17. C# remains a
separate future language target.

## Application output API

The compiler already supports four exact NativeElf window calls established
by Phase 27X: `gx_window_create`, `gx_window_set_text`, `gx_window_run`, and
`gx_window_destroy`. The compiler resolves these recognized declarations to
the versioned NativeElf application host-call table. The sample uses them to
create a small window and set its label to **Hello World**. It does not add a
GUI toolkit or host-side display call.

Phase 27X's guest proof exercised the same API family, but COMP4P did not
visually run the COMP4P sample. Visible Hello World acceptance remains
pending guest execution.

## Persistence and project ownership

The IDE project model already parses/serializes `guidexos.project`, validates
the application manifest and required files, and loads a project through its
existing workspace filesystem boundary. Workspace operations support project
open/close, source editing, Save All, and reopening. Existing project/workspace
tests belong to the IDE repository. The COMP4P compiler-service test stages
the sample files in a fake VFS, then builds from those persisted paths; this
does not prove the IDE New/Open dialog path.

The integration contract for the IDE owner is:

1. Add **Native Application — C Subset** as a `NativeGuiApplication` project
   template with the `.c` entry file and the committed project metadata/
   manifest/API contract above.
2. Keep bounded name, folder, and path validation; reject unsafe paths and
   existing non-empty destinations without overwriting. Track created files
   and preserve rollback behavior.
3. Open the project's declared entry source instead of assuming
   `src/main.cpp`; preserve the source set, target profile, manifest, and
   output identity after close/reopen.
4. When running inside guideXOS, select the bare-metal target and dispatch to
   the existing cooperative Build/Run ABI. Do not route this template through
   hosted Clang or present a hosted artifact as a guest-compiler result.
5. Carry Build stage, terminal state, error records, and source locations to
   the existing Build Output/Error List. Do not synthesize progress.
6. Keep Run bound to the current successful Build result's exact path, size,
   hash, and generation; a failed build must not authorize an older output.
7. Exercise the actual New Project, open-source, Build, and Run action
   handlers in an available hosted IDE harness. Include create/save/close/
   reopen/edit/rebuild and overwrite/invalid-name cases.

## Build and Run integration

The production bare-metal Build service validates project metadata, manifest,
registered sources, target, expected artifact path, and entry ABI before
compilation. It uses the compiler driver, AMD64 backend, object ABI 17,
linker, ELF writer, temporary artifact validation, and guarded publication.
The COMP4P test linked the production cooperative Build service, production
compiler, and production NativeElf validator. It did not use host Clang to
produce the sample ELF.

The existing service publishes the actual output path and successful artifact
identity in the Build snapshot. Failed attempts have no successful artifact
identity even when a prior ELF remains on disk. The existing Build/Run
contract revalidates the identity before launch. COMP4P's focused test checked
two successful generations, a failed edit between them, and preservation of
the previous ELF; the full COMP4B cooperative suite covers cancellation,
publication rollback, and lifecycle behavior.

The hosted `NativeBuildService` is a separate path: it launches a host
toolchain/build script. It is useful for hosted NativeElf project builds but
does not qualify a program as a product of the guest compiler.

## Execution location and command-line audit

The guideXOS bare-metal Build and NativeElf Run services are callable through
the guest application ABI. COMP4P exercised the Build service with the
production compiler and a hosted VFS harness; that is production-component
evidence, not proof that Developer Studio's GUI currently invokes those
services in a running guest. The available New/Open/editor/Build UI path was
exercised only by the separate IDE repository's hosted UI harness. No full
New Project-to-Run GUI flow inside guideXOS was demonstrated.

No end-user project-creation or project Build/Run command-line interface was
found in this repository. Its PowerShell entry points launch hosted regression
tests and compiler checks; they are qualification tools, not a competing
project workflow. Build snapshots and source-located diagnostics are exposed
through the existing service/API, but COMP4P did not find a standalone CLI
that presents those results for an ordinary project.

## Hosted qualification evidence

The dedicated runner is
`scripts/run-compiler-comp4p-native-project-host-test.ps1`; its outputs are
under `out/comp4p-validation/`. The suite loaded the committed sample files,
admitted a cooperative production Build job, compiled the `.c` source and
header with the guideXOS compiler, checked source-located lexical diagnostics,
validated the generated ELF with the production validator, checked the
greeting in the ELF, and confirmed that an edited successful Build has a new
identity while a failed attempt preserves the previous ELF.

The COMP4P compiler test did not exercise project name/path generation; those
operations belong to the separate IDE repository. The available IDE model
suite passed its project, Build, Run, and workspace file-flow tests (4/4) from
a build directory under `out/comp4p-validation/ide-model`; it did not exercise
the New Project GUI handler. The hosted Phase 22 UI smoke passed server launch,
initial render, project open, and Save actions, then failed at the hosted
Build/recovery step described above. It did not invoke the bare-metal Run
service for this sample, render its window, or verify a user-visible
application result. Its trace is
`out/comp4p-validation/hosted-ide-ui/developer-studio-phase22-workflow-trace.log`.
The existing COMP4A–COMP4O matrix passed 21 runners
covering 23 production suites with 0 warnings and 0 failures. The additional
COMP4P runner also passed, bringing the combined total to 22 runners and 24
production suites, still with 0 warnings and 0 failures. QAC source/helper
assertions passed 35/35.

### Required hosted coverage disposition

The statuses below are for COMP4P's C-subset sample. “Partial” means related
generic IDE/service evidence exists, but the required sample-to-user-action
path is not proven.

| # | Required coverage | Result | Evidence or remaining gap |
|---:|---|---|---|
| 1 | Native project template generation | Partial | Committed source template exists; the IDE New Project dialog does not offer it. |
| 2 | Valid and invalid project names | Partial | Generic IDE model tests passed; the COMP4P template generator/dialog was not exercised. |
| 3 | File-path and overwrite protection | Partial | Existing model has bounded validation/no-overwrite behavior; no C-subset generator path is wired to it. |
| 4 | Project configuration | Pass | Production Build service accepted the target, ABI, manifest, source registration, entry point, and expected artifact. |
| 5 | Source-file correctness | Pass | The committed `.c` source and quoted declaration header compiled with the production compiler. |
| 6 | Project reopening | Partial | Generic project/workspace tests passed; reopening this template through the IDE was not tested. |
| 7 | Build job admission | Pass | The cooperative production Build service admitted, polled, and released the sample build. |
| 8 | Compiler invocation | Pass | The guideXOS compiler pipeline compiled and linked the sample; host Clang did not create its ELF. |
| 9 | Compiler diagnostic propagation | Partial | Source-located lexical diagnostics reached the production Build snapshot; the hosted IDE smoke did not render its expected error text. |
| 10 | Successful ELF generation | Pass | The service published `build/bin/amd64/hello-world.elf`. |
| 11 | ELF validation | Pass | The production NativeElf validator accepted both successful generations. |
| 12 | Build identity tracking | Pass | The edited successful build had a new handle and SHA-256 identity. |
| 13 | Failed-Build artifact preservation | Pass | An injected lexical error failed without identity and preserved the previous ELF bytes. |
| 14 | Run eligibility validation | Partial | Generic IDE Run model tests passed; this sample was not passed from Build into the bare-metal Run service. |
| 15 | Repeated Build cycles | Pass | Two successful sample generations with a failed attempt between them completed as expected. |
| 16 | Project file modification and rebuild | Partial | A staged VFS source edit rebuilt successfully; IDE Save/reopen/edit was not exercised for this template. |
| 17 | Cancellation compatibility | Pass | The existing COMP4B cooperative Build cancellation regression passed in the matrix. |
| 18 | Resource and project-size limits | Partial | Existing services retain their bounds, but this sample's project/source-size boundary was not tested in COMP4P. |
| 19 | SDK integration where supported | Pass | The sample uses the four recognized NativeElf window calls; the optional COMP4O bytes library is not required. |
| 20 | Native application result verification | Pending | No guest Run or visible Hello World window was performed. |

## User walkthrough

The intended ordinary GUI workflow after the IDE integration contract is
implemented is:

1. Open Developer Studio in guideXOS and choose **File → New Project**.
2. Select **Native Application — C Subset**, then enter the name and project
   location.
3. The IDE creates the project and opens `src/main.c`.
4. Choose **Build** and review the project/attempt status and any source-located
   compiler messages in Build Output.
5. Choose **Run** after a successful build. The exact published ELF should
   open a window displaying **Hello World**; close the window to end the run.

This is the target user walkthrough, not a claim that the current New Project
dialog already offers the template. COMP4P's sample currently requires the
IDE-side template and entry-source integration before those GUI steps work as
written.

## Guest acceptance fixture and gates

`sdk/samples/native-application-c-subset/` is the future guest acceptance
fixture. After Compilation receives authorized guest/QEMU capacity, verify in
one controlled guest run:

- open/create the project through Developer Studio and edit `src/main.c`;
- invoke the authentic guest Build service and verify the published ELF,
  manifest path, ABI 17 object identity, and successful Build generation;
- Run that exact successful Build and observe the Hello World window;
- modify source and rebuild, then confirm a new identity and updated text;
- introduce a compiler error and confirm diagnostics appear and Run rejects
  the failed attempt while the previous ELF remains intact.

No QAC admission was requested and no QEMU process was started for COMP4P.
COMP4K remains `HOSTED_READY_GUEST_PENDING`; .NET Support retains QEMU
priority. Any visual acceptance must be part of a later authorized guest run.

## Recommended COMP4Q scope

COMP4Q should stay with the IDE owner and complete the small cross-repository
contract: selectable C-subset template, entry-source-driven project opening,
source persistence/reopening, bare-metal Build and Run dispatch, and
source-located Build Output. Add hosted tests that call the actual GUI action
handlers and the existing project model rather than mocking final statuses.
Then carry the committed sample as the guest fixture for authorized visual
acceptance. No new compiler, project framework, GUI toolkit, debugger feature,
or speculative SDK is required.

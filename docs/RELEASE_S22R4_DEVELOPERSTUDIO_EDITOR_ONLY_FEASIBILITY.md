# guideXOS Server — S22R4 DeveloperStudio Editor-Only Feasibility

**Date:** 2026-10-20
**Audit outcome:** **A — Feasibility audit accepted**
**v0.3 inclusion recommendation:** **NO-GO — defer DeveloperStudio**

S22R4 completed the requested architecture and compatibility audit. The modern DeveloperStudio can plausibly reach its main window on the v0.3 Native ELF shell without opening a workspace or starting build/debug services. That is not a useful editor release: the v0.3 runtime cannot open arbitrary user files or save them, and the modern UI has no new empty-document command. Disabling Build, Run, and Debug would leave a mostly empty shell.

This is a component recommendation only. It does not approve Server v0.3, qualify Native Settings, alter package inventory, or authorize an implementation phase. No compatibility profile, package, binary, ISO, or guest run was produced.

## 1. Scope, revisions, and stale-prompt gate

The Server checkout is D:/dev/guideXOSServer, branch main, starting at the expected S22R3 closeout HEAD 07bd81c7d5d47bade3bbd6d168ded517878784f2. It was two local commits ahead of origin/main. At the start, all 5,308 tracked paths were unchanged, none was staged, and 853 paths were pre-existing untracked. The complete starting porcelain inventory is preserved in the evidence directory. No existing S22R4 audit or later Server release decision was found; the current release record remains S22R3.

The modern source checkout is D:/dev/guideXOS_Developer_Studio, branch main, HEAD 9b66725990b604d426ef6d2be02e7097a0f9a46e, clean and one commit ahead of origin/main. That revision includes the latest debugger binding change and was the source inspected. Its source manifest is 2,202 bytes, SHA-256 5E66BF31978C1EAA78985B50D3EB71966D3AE7EA56BFD553340CE5798D5BEFF5. No binary built from this exact source HEAD was established.

The older modern-source pair recorded during S22R2 is not the current source identity: its 2,011-byte manifest hashes to AB1DB7C63A3DC49C06098B7A9341C33DBCF6FECBD8163460F62BCF907E7488E9 and its 1,104,652-byte ELF hashes to F07270EACBD62F9AB67F1F38856A326981280E43D0A4D44F8ED1D17C777F01A6. The Server Apps/DeveloperStudio and preserved candidate directories still contain that older pair. The inspected DeveloperStudio source manifest is different, and the available ELF predates the inspected source revision; neither pair has current v0.3 acceptance. The Phase 25 pair remains the Server build's exact pin.

The DeveloperStudio worktree was not edited, built, or tested. No DeveloperStudio .phase file was present at the checkout root, and no DeveloperStudio build process was observed. A QEMU process was present for D:/dev/guideXOSServer_DiskManagerImprovements and a g++ process was compiling a Navigator JavaScript test; neither process belonged to this DeveloperStudio checkout and neither was attached to or changed.

## 2. Modern application and package contract

The application is a statically linked Native ELF with App Model ID com.guidexos.developerstudio, AMD64 entry point gx_main, ABI guidexos-c-abi-v1, and runtime native-elf. The manifest at D:/dev/guideXOS_Developer_Studio/app/app.json declares minimum Server 0.5.0, read/write filesystem permissions, document activation, and nine C/C++/text file associations. CMake's Native ELF target compiles the shared model, UI, workspace, Build, Run, and debugger sources as freestanding code, links a static ELF with gx_main as the entry point, and emits it into the Server package tree. The configured target profile is guidexos.amd64.hosted.native.

The source layers a large UI shell over bounded text-buffer, document, project, search, syntax, completion, Build, Run, and debugger models. The shell and models are application code; the filesystem and process operations are supplied through Native ABI callbacks. This separation permits some startup degradation, but it does not create file capabilities that the runtime does not provide.

## 3. Startup trace and dependency map

Source review of src/main.cpp, src/developer_studio_workspace.cpp, and the Server's kernel/core/native_elf_baremetal.cpp gives this sequence:

1. The Native ELF loader reads the package manifest and executable from the mounted Apps tree, checks NativeElf/native-elf, AMD64, gx_main, and guidexos-c-abi-v1, maps the ELF, constructs the host table, and calls gx_main.
2. gx_main requires a valid context, get_api_version, log, and a window request callback. The v0.3 production host supplies these. It does not require a workspace, Build service, Run service, or debugger callback at this check.
3. Before making the window, DeveloperStudio probes for workspace stat/read callbacks only to decide whether to run an optional diagnostic sentinel lookup. When those callbacks are absent, it skips that lookup. It initializes its workspace controller with adapters that report unsupported operations if their host callbacks are null; it also initializes the Build, Run, and Debug controllers without starting them.
4. It requests the main window, then tries App Model document activation. The activation helper treats an absent callback as unavailable and returns to normal startup. It draws the first frame and enters the event loop.
5. The event loop polls input and controller state. Build and Run pollers return immediately when inactive. DebugControllerPoll also returns before calling the backend when the debugger is inactive.
6. On close, the app returns through the host exit callback; the Native ELF runtime owns the window and app-runtime cleanup.

**Startup conclusion:** source supports clean startup degradation around absent project and development services. A main window is source-level feasible without them. No launch was performed with the current artifact on the production guest, and the current manifest fails the v0.3 release minimum-version preflight.

The main UI does not start in a workspace or with a document. The explorer explicitly displays “No workspace open.” The first optional activation attempt happens after window creation. File, project, Build, Run, and Debug services are command-time dependencies, except document activation which is probed once after window creation.

## 4. Server v0.3 service and ABI compatibility matrix

The production AMD64 kernel compiles kernel/core/native_elf_baremetal.cpp through the core source list in kernel/Makefile. initialize_host_table zeroes the full current host-call structure, sets its size/version, and installs only the callbacks listed as present below. The table size therefore does not mean every callback is implemented.

| Service/API | DeveloperStudio use | v0.3 availability and contract | Editor-only requirement / correction |
| --- | --- | --- | --- |
| Native ELF identity, AMD64, gx_main, guidexos-c-abi-v1 | Loader to entry | Present; package parser checks identity and static ELF contract | Required; unchanged structurally |
| log, get_api_version | Startup and diagnostics | Present | Required; unchanged |
| request_window, request_window_ex | Main window | Present; compositor-owned surface | Required; unchanged |
| draw_text, draw_rect, present_frame | UI and editor rendering | Present | Required; unchanged |
| poll_event, wait_for_close, exit | Keyboard, pointer, close, application lifecycle | Present; production host routes keyboard and window events and owns teardown | Required; unchanged |
| get_ticks_ms | Input timing and UI behavior | Present | Required by some UI paths; unchanged |
| file_exists, file_read_all, file_read | Package resources | Present but package-relative and read-only | Insufficient for opening arbitrary source files |
| file_stat | File type, size, and path validation | Callback absent | Required to open standalone files and projects; runtime addition needed |
| file_read_workspace | Read arbitrary user/workspace path | Callback absent | Required to load a document; runtime addition needed |
| file_list | Explorer and workspace tree | Callback absent | Required even by the standalone-open route because its parent workspace is refreshed; unavailable |
| file_write_all | Save document and metadata | Callback absent | Required for safe persistence; runtime addition needed |
| file_create_directory, file_remove | Project creation and rollback | Callbacks absent | Required for project creation; unavailable |
| get_document_activation_path | Open-with/file-association activation | Callback absent; appended at offset 248 in the current SDK structure | Required for the manifest's activation route; runtime addition needed |
| build_project_start/poll/release | Compile/Build worker | Callbacks absent | Not needed for editor-only; must remain disabled |
| development_run_prepare/start/poll/request_close/release | Run a built project | Callbacks absent | Not needed for editor-only; must remain disabled |
| development_debug | Debugger command and state service | Callback absent | Not needed for editor-only; must remain disabled |
| filesystem.read/write manifest permissions | Authorization declaration | Present in the app manifest, but permissions do not implement missing host calls | Cannot substitute for a v0.3 filesystem API |
| minGuideXOSVersion | Runtime contract | Current source declares 0.5.0. The bare-metal loader does not enforce this field; release preflight does | A new v0.3 artifact needs a truthful 0.3.0 minimum after binary qualification |

The SDK documents workspace calls as hosted-development absolute-path services, not as a bare-metal VFS contract. The v0.3 file calls resolve only package-relative resources under Apps; there is no writable user-file callback. The editor's adapters do not fall back from file_read_workspace to package-relative file_read.

The ABI is structurally append-only: the existing base offsets remain in place, workspace calls follow get_ticks_ms, and later Build, Run, Debug, audio, and activation callbacks are appended. The production host leaves unsupported callbacks null; it does not repurpose an ordinal or pretend an unavailable operation succeeded. This is base ABI compatibility only, not application feature compatibility.

The existing Server tests/native_abi_layout_test.cpp still asserts sizeof(gx_host_calls) == 248. The current SDK appends get_document_activation_path at offset 248, making the table 256 bytes. A focused no-output syntax check confirmed the existing assertion fails with 256 == 248. This is a stale validation assertion, not evidence that the production host supplies the missing callback. It remains a Server ABI-test maintenance gate; S22R4 did not modify it.

## 5. Editor feature inventory

| Candidate feature | v0.3 classification | Finding |
| --- | --- | --- |
| Main window and rendered shell | Supported unchanged at the host-call level | Window and drawing callbacks exist; no production guest launch of this artifact was performed |
| Menus and toolbars | Supported unchanged as rendering | Many controls remain active and would be misleading until a profile gates them |
| In-memory text-buffer model | Conditionally supported | Existing buffer code supports bounded edits, but the release UI has no document to attach it to |
| New empty document | Unavailable | No blank-document command or pathless creation API was found. Ctrl+N opens New Project |
| Open supported file | Unavailable on v0.3 | The standalone document controller needs stat and read_workspace; those callbacks are absent |
| Save | Unavailable on v0.3 | Save calls file_write_all through the workspace adapter; the callback is absent |
| Save As | Unavailable | No Save As command or document-path reassignment workflow was found |
| Text insertion, selection, caret movement, keyboard navigation | Conditionally supported | Implemented for an active document; no supported v0.3 path creates or opens one |
| Mouse navigation and scrolling | Conditionally supported | UI handling is present and the host routes input; editing behavior has no v0.3 document to act on |
| Syntax highlighting | Conditionally supported | Bounded lexical syntax cache and rendering are implemented; depends on an active document |
| Undo/redo | Unavailable as a general editor feature | The code has completion and rename-specific undo, but no general text-buffer undo/redo stack |
| Multiple documents/tabs | Conditionally supported | Model supports up to eight open documents; loading the first document is blocked |
| Document activation | Unavailable on v0.3 | Manifest advertises it, but get_document_activation_path is not installed by the v0.3 host |
| Application close/reopen | Supported for app lifecycle only | Window close/relaunch is available; no session restore exists, so the app returns without a persisted document |

There is a projectless document path in the local controller, but it is not workspace-free: WorkspaceControllerOpenStandaloneDocument validates the absolute path, opens its parent directory as a projectless workspace, and then calls WorkspaceControllerOpenDocument. The latter requires workspace stat/read callbacks. That is a reuse of the existing workspace/document model, not a standalone empty-buffer model. The model tests use a hosted temporary filesystem adapter and do not prove v0.3 persistence.

The standalone open route needs all three file services before it can show a document: OpenWorkspace requires stat and list to open and refresh the parent directory; OpenDocument then requires stat and read. Save requires write. The File menu has New Project, Open Project, and Open Workspace, but no Open File or New Document.

The minimum useful editor profile fails its first document and persistence requirements. Although the base shell may launch, users cannot open an existing source file, create an empty document, save, or reopen edited content. It must not be advertised as a functional editor or as supporting file persistence.

## 6. Project-management feasibility

Project creation and opening are separate from editing and are not compatible with the v0.3 host:

- New Project writes a project manifest, template files, and directories, and relies on remove operations for rollback.
- Open Project validates metadata, paths, and manifests and reads project files.
- Open Workspace and explorer refresh require stat/list services; document opening requires stat/read.
- Project tree, source ownership, project reload, and persistent metadata all depend on workspace filesystem callbacks.
- Build configuration targets guidexos.amd64.hosted.native; there is no v0.3 compiler service or bare-metal project target.

Classification: **UI-only project controls on v0.3**. Do not preserve active Project, Workspace, Explorer, or project-build controls in a future editor profile unless their exact dependencies become available and are tested.

## 7. Build, Run, and Debug isolation

| Operation | Current entry points | Host dependency | v0.3 status and profile requirement |
| --- | --- | --- | --- |
| Compile / Build | Build menu, Build Project, Ctrl+Shift+B, project build path | build_project_start/poll/release | Missing; disable menu, toolbar, keyboard, and action dispatch |
| Rebuild / Clean | No distinct user-facing command found | Would use the same build path | Not a separate implemented operation; do not imply support |
| Run | Build menu, Run Project, F5 when no debugger session | Build plus development_run_* | Missing; disable all entry points and reject dispatch |
| Start Debugging | Debug menu, Ctrl+F5 | Build plus development_run_* and development_debug | Missing; disable before it can save, build, or start a session |
| Stop Debugging | Debug menu/panel and Shift+F5 route | development_debug and run close/release | Missing; command must not dispatch |
| Continue, pause, step over/into/out | Debug menu/panel; F5, F6, F10, F11, Shift+F11 | development_debug | Missing; disable menu, panel, shortcuts, and direct handlers |
| Breakpoints | Debug menu/panel and F9 | development_debug; project/document state | Missing; gate breakpoint toggles and state mutations |
| Debugger attach | No separate attach action found | None identified | Unimplemented in this source contract |
| Build output/status | Output panel and BuildController state | Build callbacks for new results | UI can render, but no v0.3 build results can be produced |
| Compiler selection | No selector found; target profile is fixed in source/project metadata | Hosted build target | Unavailable; no v0.3 compiler selection exists |

These commands are direct functions and event routes rather than one centralized command-ID registry. Hiding a menu alone is insufficient. A future profile must gate toolbar/menu states, keyboard shortcuts, mouse dispatch, panel controls, request functions, and controller starts. It must also prove no background worker or callback starts after a disabled action. Existing polling is guarded: Build and Run pollers require an active operation, and DebugControllerPoll requires an active session and backend callback. The current v0.3 host therefore does not get called by these polls during a normal idle launch.

## 8. Reversible profile and manifest design

If a future compatible Server exposes safe user-file open/save, use one shared DeveloperStudio source tree with a separate explicit compile-time editor-only release profile. A central feature policy should control both enabled UI state and action dispatch. Runtime callback presence should fail closed if a profile is launched on a host missing a required service. Avoid scattered Server-version checks.

The existing full DeveloperStudio profile and its 0.5.0 manifest remain intact. A reduced profile would emit a distinct ELF and manifest pair, declare only the features and permissions it actually provides, and receive separate exact-pair acceptance. Keep all ABI slots append-only and keep the existing minimum-version preflight. The current binary must never be relabeled with a lowered manifest.

This is a reversible design recommendation only; S22R4 did not add a profile or edit DeveloperStudio. Any future source work should use a separately authorized DeveloperStudio checkout in the owner's one-branch-per-checkout workflow, with the full development checkout left alone.

## 9. Validation and artifact status

Static evidence reviewed:

- Current source and manifest at DeveloperStudio HEAD 9b66725990b604d426ef6d2be02e7097a0f9a46e.
- Server production Native ELF loader/host table, SDK ABI definition, kernel source inclusion, release preflight, and App Model launch path.
- Existing DeveloperStudio model and app-activation test source, which exercises standalone file editing against a hosted mock filesystem.
- S22R3 report and its recorded 18-assertion release-integrity test result.

Newly run validation:

- No-output C++ syntax check of tests/native_abi_layout_test.cpp. It failed at the pre-existing 248-byte static assertion because the current host-call structure is 256 bytes after the activation callback append.

Not run: DeveloperStudio builds or unit suites, Server production builds, release-integrity harness, QEMU, or guest interaction. The active DeveloperStudio checkout and existing candidates were not used as build destinations. No runtime compatibility claim is based on hosted model tests or on the older ELF.

The Server's current minimum-version helper rejects a manifest requiring newer than target 0.3.0. build.ps1 applies this check together with the exact Phase 25 identity pin before cleanup, then receipt validation repeats it against the final package. Keep this fail-closed behavior unchanged.

## 10. Engineering effort and release risk

| Component | Effort | Reason |
| --- | --- | --- |
| Startup service isolation | Small | Startup already tolerates absent workspace callbacks and initializes controllers without starting them |
| Central editor-only feature policy and UI/action gating | Medium | Direct command routes span menus, shortcuts, toolbar, debug panels, and actions |
| New empty document support | Large | No current pathless document exists; adding one would introduce a new document lifecycle outside the current model |
| Safe file open/save on v0.3 | Large | Requires production host-path read/write, path authorization, activation or a file-open flow, integrity semantics, and guest qualification |
| Project management | Large | Needs the broader workspace filesystem and project lifecycle; outside an editor-only profile |
| Separate manifest/build/package profile | Medium | New exact ELF/manifest identity, profile-specific capability assertions, and integration with the pinned release pipeline |
| Focused action/capability tests | Medium | Must cover all UI and keyboard entry points, null service callbacks, no side effects, dirty state, and persistence failures |
| Production guest qualification | Medium–Large | Requires an exact approved binary, writable isolated media, open/edit/save/reopen/close interaction, and package-integrity proof |
| Ongoing dual-profile maintenance | Medium, recurring | Shared source avoids forks, but both artifact contracts and acceptance sets must be maintained on each release |

The material risks are file/data integrity and runtime authorization: v0.3 does not provide a user-file write API, and the existing writer is direct truncate/write/flush with no atomic replacement guarantee. Manifest permissions cannot grant a missing service. UI-only hiding would leave unsupported direct command routes. The ABI's stable base does not make the app feature-compatible. A profile change also carries regression risk for the active full DeveloperStudio, so it needs a separate checkout and exact-profile tests.

Kernel stability risk is low if no kernel services are added; adding filesystem APIs makes it materially higher. v0.3 schedule impact is high relative to the benefit because a useful profile needs persistent files and the current server does not provide them. The owner prioritizes stability over an empty demonstration shell.

## 11. Decision and recommended next phase

**Recommendation: NO-GO for including modern DeveloperStudio as a useful v0.3 editor-only application.** The main window is likely separable from Build/Run/Debug, but the product cannot satisfy open, save, and reopen requirements on the actual v0.3 host. The smallest viable product is not reachable through the current UI or ABI, and fixing this would require runtime filesystem and App Model work rather than only hiding controls.

There is no DeveloperStudio compatibility implementation scope for S22R5. The fallback is to keep v0.3 package qualification moving without DeveloperStudio only after a separate Server release-owner decision authorizes an explicit package-inventory deferral. Validate the revised required-app contract and exact release preflight before producing any ISO. Until that decision and remaining component gates pass, do not publish or call the current candidate accepted.

If a later Server runtime adds supported, authorized persistent file access, a separate profile phase can re-evaluate the smaller shared-source feature-gating plan. That phase must not lower the existing version gate or reuse an unapproved ELF.

## 12. Native Settings and whole-release gates still open

Native Settings remains **unqualified for v0.3**. The repaired default-to-Dinos persistence behavior passed the recorded 1,426/1,426 native runtime checks, but those are not exact-package guest proof. The remaining package gates recorded in the current qualification report are:

1. Resolve and accept the exact DeveloperStudio package policy before any new production package; S22R3 did not approve the current source pair.
2. Run the canonical isolated AMD64 package build with every required stage and receipt passing; do not treat a zero wrapper exit as sufficient if a child stage reports an error.
3. Verify the exact packaged manifest, ELF identities, and App Model bytes.
4. Boot that exact ISO on isolated writable media and test default Blue Flower state, selecting and applying Dinos, visible/readback state, persisted config, reopen/cold-boot state, and legacy Display Options compatibility.
5. Resolve the release record's explicit kernel undefined-symbol policy gate and any other whole-release checks before publication.

S22R4 changed none of these classifications. The audit is accepted independently of the NO-GO inclusion result. The detailed source references, exact file identities, starting status inventory, and validation output are indexed under docs/evidence/release-s22r4-developerstudio-editor-only-20261020/.

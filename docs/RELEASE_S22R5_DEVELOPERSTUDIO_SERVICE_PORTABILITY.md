# Server S22R5 — DeveloperStudio Service Portability Audit

**Date:** 2026-10-20  
**Audit outcome:** Outcome A — audit complete  
**Recommendation:** GO for a separately built, editor-only v0.3 profile using a v0.3-native compatibility adapter. The current v0.3 runtime/package remains NO-GO as-is.

## Executive decision

The v0.5 workspace callbacks are real freestanding kernel code, but copying them into v0.3 is not the right port. They rely on v0.5 context checks, runtime state, and ABI slots. They also do not enforce manifest filesystem permissions or restrict paths to a workspace root, so their current authorization is not sufficient for the requested safe editor service.

v0.3 already has production VFS operations for stat, read, directory enumeration, write, create, and remove. Its FAT write path has better error reporting, rollback on new-file creation failures, and explicit volume flushing than the v0.5 path inspected. A bounded service adapter and a small standalone-document UI can therefore support persistent editing without importing v0.5’s App Model, process manager, compiler, Run service, or debugger.

Recommended design:

1. Preserve the v0.3 host-table prefix, including current audio and document-activation slots.
2. Append an explicitly versioned bare-metal workspace extension after the measured 256-byte v0.3 prefix.
3. Authorize access in the kernel using trusted runtime identity/capability and a designated root. Manifest strings alone must not grant authority.
4. Validate caller memory and bound paths, buffers, file sizes, and enumeration work.
5. Build a separate editor-only profile with New Document, Open File, Save As, and command-level Build/Run/Debug gating.
6. Qualify exact binary/manifest pairing and the persistent guest workflow before any release decision.

The workflow **New → Edit → Save → Close → Reopen → Read identical contents** is feasible without the complete v0.5 project architecture. Existing text buffers and save/dirty handling are reusable, but there is no untitled-document registration path or independent New/Open File command. This is bounded work, but more than wiring callbacks.

**3–5 phase answer:** Five focused phases are credible as an aggressive best case, conditional on authorization/ABI design and existing FAT/VFS behavior. Three phases are not credible once production guest acceptance and packaging are included. Most likely: six phases.

## Scope and evidence labels

This was a source/repository audit. No production code, manifest, package, branch, or reference checkout was changed. No tests, build, ISO generation, or QEMU run was performed. The active QAC/QEMU workload belonged to another checkout and was left untouched.

- **Source evidence:** directly inspected source, tests, metadata, or Git history.
- **Existing validation:** a pre-existing report/test/evidence artifact; not rerun here.
- **Inference:** portability/effort conclusion derived from source.
- **Unverified:** requires implementation or a dedicated experiment.

## Checkout identity and preservation

| Checkout | Starting branch and HEAD | Starting status | Audit disposition |
|---|---|---|---|
| v0.3 target, D:\dev\guideXOSServer | main, 0965387b970254035ffce81f1e58aa6893c57936 | 0 staged, 0 tracked unstaged, 853 pre-existing untracked paths | Only audit report/evidence added; existing untracked files preserved. |
| v0.5 reference, D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO | v0.5_DEVELOPER_STUDIO, b3753c5db1d9bac9fcb0d6a608c45b00078bf39b | Tracked, staged, and untracked clean | Read-only; unchanged. |
| Modern DeveloperStudio, D:\dev\guideXOS_Developer_Studio | main, 9b66725990b604d426ef6d2be02e7097a0f9a46e | Tracked, staged, and untracked clean | Read-only; unchanged. |

Expected S22R4 v0.3 HEAD 2fde8142a183ea7a4c1ba39bc764c604c0484fcc is the parent of the actual starting HEAD. The one intervening commit, 0965387b (“Integrate Server x86 development launcher with QAC”), changes docs, QAC launcher scripts, and tests, not production kernel services.

The paired v0.5 rehearsal fb71e36c77866cdabf20873e6ad3de0ddc3bbe02 is an ancestor of current HEAD, which is 97 commits later. Later history adds Native ELF runtime, compiler/build and file callbacks, Run, debugger/lifecycle, and Phase 29/30/QAC acceptance. This is a substantially advanced reference, not the historical rehearsal checkout. DeveloperStudio package last changed at ec7a48a33e3a073a0037bcf146dd61a2415bbd47.

Modern source is at the expected 9b66725990b604d426ef6d2be02e7097a0f9a46e. Its app/app.json is byte-identical to the v0.5 manifest (SHA-256 5E66BF31978C1EAA78985B50D3EB71966D3AE7EA56BFD553340CE5798D5BEFF5). This does not prove that the v0.5 ELF was built from this exact source revision; no immutable source-to-binary identity was found.

Initial process inspection found QAC CLI PID 20380, coordinator PID 408, and QEMU PID 712. Their command line/evidence identified a separate D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT acceptance run with outputs under E:\guideXOS_C181R1_evidence\guest-runs\focused-r1-reset-repro10-qac-sequence\...; it was not touched.

## Direct source comparison

Roots: v0.3 is D:\dev\guideXOSServer; v0.5 is D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO.

| Component | v0.3 | v0.5 | Portability |
|---|---|---|---|
| Runtime/context | kernel/core/native_elf_baremetal.cpp: Runtime 321–356; runtime_from 1068–1070 returns context->userData. | kernel/core/native_elf/native_elf_loader.cpp: app_context_valid 425–435 validates active context, table, and userData. | Add v0.3-native exact active context/runtime checks and trusted service authority; do not transplant helper assumptions. |
| Package file callbacks | host_file_exists/read/read_all at native_elf_baremetal.cpp:1231–1289 use package_path at 575. | Workspace operations are distinct VFS callbacks. | Package-relative reads cannot provide arbitrary user-file access. |
| Stat | SDK fields exist, but initialize_host_table at 1820–1840 does not register workspace callbacks. VFS stat exists at vfs.cpp:1395. | host_bare_file_stat at native_elf_loader.cpp:451–464 calls vfs::stat. | Existing v0.3 primitive; new authorized adapter required. |
| Read | VFS read exists at vfs.cpp:1495; no app workspace callback. | host_bare_file_read_workspace at :466–478 validates output, copies path, calls vfs::read_file. | Small-to-moderate v0.3 service addition after root and caller-range checks. |
| List | VFS opendir/readdir/closedir exist; no app callback. | host_bare_file_list at :480–511 caps returned entries to 128 and closes iterator, but keeps traversing after output capacity. | Reuse VFS; cap total traversal too. |
| Write | vfs::write_file at vfs.cpp:1506 reaches rich status-based FAT writes. No app callback. | host_bare_file_write_all at :513–525 calls VFS after input-range check. | Reimplement wrapper on v0.3 status API; report exact bytes and errors. |
| Create/remove | v0.3 VFS has mkdir/rmdir/unlink/status paths. | callbacks at :527–550 call VFS. | Lower layer exists; app-boundary authorization/error contract missing. |
| Authorization | Manifest id exists; no workspace root or permission authorization in the loader path. | Active app context is checked, but not filesystem.read/write permissions, trusted Studio identity, or a workspace root. | Neither callback set proves safe authorization. Kernel policy required. |
| Caller memory | v0.3 package read passes app buffer to VFS without equivalent visible range guard in that callback. | app_pointer_range at :405–410 and app_string at :413–423 validate guest ranges/bounds. | Reuse concept, implement against v0.3 image/stack. |
| VFS/FAT | v0.3 VFS has stat/read/write/list/create/remove; FAT status, rollback and flush paths. | v0.5 has direct VFS helpers and older bool-style FAT paths. | Use v0.3 VFS; no VFS backport needed. |
| Lifecycle | Single loader/runtime structure; no v0.5 Run process model. | Scheduler/trampoline, nested runtime images, run-service state, cleanup and debugger hooks. | Synchronous per-call file service can be independent; Run/Debug cannot. |
| Activation / ABI | SDK activation pointer at offset 248; callback not installed in current v0.3 table. | Activation at offset 448 in the later table. | Preserve v0.3 prefix; keep activation separate. |
| Build/Run/Debug | No equivalent v0.5 NativeElfBuildService/RunService/debug callback architecture. | Custom compiler, Run process lifecycle, debugger support. | Broad, separate backport; exclude from editor profile. |

### v0.5 production service inventory

The functions are in freestanding production code, not only SDK declarations or hosted mocks. Callback values use the v0.5 bare-metal table in sdk/include/guidexos/abi.h, whose layout is incompatible with v0.3.

| Service and source | Request/result | Bounds and cleanup | Dependency/gap |
|---|---|---|---|
| Stat: host_bare_file_stat, native_elf_loader.cpp:451 | Path pointer and file-info output; bounded path copy, VFS stat, mapped status/metadata. | Path capped at VFS_MAX_PATH; output range checked; no retained handle. | Context → pointer/path validation → VFS stat. No root authorization. |
| Read: host_bare_file_read_workspace, :466 | Path, output buffer, requested bytes, returned bytes; read from offset zero. | Guest output range checked; bounded path and buffer; synchronous, no retained handle. | VFS read. No root policy; client must handle stat/read size differences. |
| List: host_bare_file_list, :480 | Path, entries, capacity, count/truncated. | Capacity max 128; output validated; iterator closed. It does not stop traversal at capacity. | VFS directory iterator; add total-entry budget. |
| Write: host_bare_file_write_all, :513 | Path, input buffer, length, written count. | Input validated; no retained handle. Negative result maps to GX_ERROR_FAILED; short positive write still maps GX_OK. Client checks exact count. | VFS write; preserve detailed v0.3 errors and reject short success. |
| Create directory: host_bare_file_create_directory, :527 | Path to VFS mkdir, mapped result. | Bounded path, no retained handle. | Must be constrained to authorized root. |
| Remove: host_bare_file_remove, :535 | Stat then rmdir or unlink. | Bounded path, no retained handle. | Destructive; same root check and explicit exposure required; not needed for editor minimum. |
| Activation | Separate callback, not file I/O. | No file handle. | Keep separate from workspace service. |

The v0.5 app-pointer helper permits addresses only in the guest image or stack; app_string bounds path copying. These checks bind calls to the active runtime but do not grant filesystem capability. The modern client can prefer bare-metal callbacks and otherwise use generic hosted callbacks whose paths are host paths and whose root policy belongs to the caller. Do not silently reinterpret that fallback as a v0.3 VFS contract.

## Dependency graph and reuse

Production v0.5 read path:

~~~text
Native ELF host dispatch
  -> app_context_valid (live context/table/userData)
  -> app_pointer_range (guest image/stack)
  -> app_string (bounded path copy)
  -> VFS normalization and mount lookup
  -> stat/read or directory iterator
  -> status mapping
  -> app-owned output
~~~

v0.3 adapter path:

~~~text
new v0.3 callback
  -> active Runtime/context validation
  -> trusted app capability and restricted-root check
  -> app image/stack range validation
  -> bounded normalized root-relative path
  -> existing VFS stat/read/list/write/mkdir/remove
  -> FAT status and cleanup
  -> app-owned output and stable service status
~~~

- **Present/compatible:** v0.3 stat, read, list, write, create/remove, VFS handles, FAT layer.
- **Bounded adapter:** error mapping, workspace-relative paths, output structures and service registration.
- **Missing but independently portable:** active app/range checks at this boundary, authorization/root policy, app-visible service callbacks, bounded enumeration.
- **Missing and tied to broader v0.5 infrastructure:** compiler service, Run/process service, debugger lifecycle/traps.
- **Unverified:** chosen v0.3 root/mount, short reads in all media states, media removal during write, power-loss behavior.

**Can v0.5 file services be adapted without major App Model/process/VFS backport?** Yes, their semantics can be reproduced over v0.3’s VFS. The exact v0.5 source is not ABI-compatible and its authorization is insufficient. Reuse its validation patterns selectively, not its whole context/ABI implementation.

## Read and write safety

v0.3 path normalization is in kernel/core/vfs.cpp:638; stat/read are at 1395/1495; directory iteration is also present. Package callbacks use package_path and cannot read arbitrary user files. A workspace service must use paths relative to one designated authorized root, reject absolute/traversal paths after normalization, and prevent escape through VFS aliases/mount behavior. Bind read/write rights to a kernel-trusted capability or immutable trusted runtime identity. Manifest strings may declare intent but cannot be the sole authorization.

For each call, validate the live runtime and the complete caller memory range, cap path and byte counts, copy path to kernel-owned bounded storage, and return stable status plus exact byte count. Bound returned directory entries and total enumeration work. Keep calls synchronous and close VFS iterators before return; no retained file handles need app-close cleanup.

v0.3 FAT overwrite at fs_fat.cpp:1900–1950 reports detailed errors and flushes volume I/O, but modifies content in place before final success. A partial I/O error can damage old content. New-file create at 2019–2098 rolls back allocated clusters for data/metadata write errors and flushes after publication; final flush failure can occur after the entry is visible. It is not journaled or crash-atomic.

v0.5 VFS write is at vfs.cpp:1335. Old FAT overwrite at fs_fat.cpp:1446–1474 requires the current allocation to fit and writes in place without the explicit v0.3 flush; create at 1477–1552 lacks the same rollback. Directly porting v0.5 storage code would lose v0.3’s richer failures.

For an editor, synchronous writes may clear dirty state only after complete success. Modern save logic already requires written == buffer length; unsuccessful writes leave the document dirty and prevent close. The service should reject/report short writes. Save As requires the UI to bind a path to an untitled buffer, not a retained FS handle.

Neither inspected implementation proves crash-atomic replace, old-content preservation after media failure, concurrent write isolation, or power-loss recovery. If required, investigate temp-file/replace and flush semantics separately; do not claim those properties from current FAT operations.

**Read-only conclusion:** lower VFS APIs are already present; app-visible access is a small-to-moderate adapter, not a major architecture change. It is not already provided by package reads.

## Standalone editor workflow

Modern source is D:\dev\guideXOS_Developer_Studio, principally src/main.cpp and src/developer_studio_models.cpp.

| Step | Existing code/state | Required work and acceptance |
|---|---|---|
| Launch | v0.3 loads native apps; modern manifest requires 0.5.0. | Separate v0.3 binary/manifest; verify initialization and close. |
| Empty editor / New | Ctrl+N at main.cpp:17418 opens New Project. No standalone untitled document command. WorkspaceModelAddDocument at developer_studio_models.cpp:578–617 creates a path-based doc; buffer initialization follows around 664. | Add pathless document registration and New Document command; test ownership/dirty state. |
| Edit | Existing text editor, multi-document model and dirty tracking. | Reuse with editor-size bounds. |
| Choose path / Save As | Save flow is tied to a path/workspace; no standalone Save As assigns a path to untitled doc. | Add path selection under authorized root; reject traversal and oversized names. |
| Save | WorkspaceControllerSaveDocument at main.cpp:1180 requires exact written count; failure preserves dirty and blocks close. | Use adapter; test short write, disk full, unavailable medium, byte match. |
| Close | Existing dirty-state and close behavior. | Preserve dirty state on write error; test cancel/retry. Synchronous file service leaves no handle open. |
| Relaunch | Existing app lifecycle. | Launch exact package after cold guest restart. |
| Open file | WorkspaceControllerOpenStandaloneDocument at main.cpp:1021 normalizes/traversal-checks, opens containing directory as a projectless workspace and refreshes stat/list, then stat/read at 1063–1066. | Could accept small projectless root or decouple open from workspace refresh. Fix missing verification that read bytes equal earlier stat size. |
| Verify/edit/save again | Existing buffer and save model; hosted controller tests cover some model behavior. | Compare exact bytes including empty, multiline, UTF-8, max size; modify/save/reopen and test failure preservation in guest. |

Document text and dirty state belong to the editor model; the FS service is synchronous and returns status/bytes. The complete project persistence system is unnecessary. **New → Edit → Save → Close → Reopen → identical contents** is achievable, but changing Ctrl+N alone is insufficient.

## Projects, Build, Run, Debug

Projects reuse stat/list/read/write/mkdir/remove but add metadata format, templates, partial-setup rollback, project tree refresh, source add/remove, open-document references, persistence/reload, malformed metadata behavior and broader close/dirty handling. The workspace model can be reused, but project management should follow editor acceptance as a separate scope.

v0.3 has no matching production Native ELF build service in kernel/core/compiler, no v0.5 NativeElfRunService, and no debugger trap/breakpoint lifecycle. Existing language/compiler facilities do not prove unrestricted C++ support. v0.5’s compiler service is freestanding and custom; its large Run service depends on scheduler/trampoline, nested runtime images and process cleanup. Debug adds traps, breakpoints, stepping, stop/continue and teardown. Hosted tests do not establish guest acceptance.

| Feature | Incremental dependency |
|---|---|
| Compile/build | Compiler contract, diagnostics, output/ELF, memory accounting, cancellation and cleanup; separate substantial work. |
| Run | Scheduler, nested runtime, guest memory and process lifetime; broad runtime backport. |
| Debug | Breakpoint/trap, stepping/state transfer and cleanup; larger than Run alone. |

Existing v0.5 QAC/Phase28O evidence covers specific acceptance paths, not all file-operation failures/security and not v0.3.

## ABI and package contract

v0.3 SDK is sdk/include/guidexos/abi.h; production table initialization is native_elf_baremetal.cpp:1820–1840. play_pcm is at offset 240 and document activation at 248. Natural pointer alignment makes gx_host_calls 256 bytes; production uses sizeof(gx_host_calls). tests/native_abi_layout_test.cpp:81–82 expects size 248, so that assertion is stale by eight bytes.

v0.5 ABI declares GX_API_VERSION 0 and guidexos-c-abi-v1, with a 456-byte host table in tests/native_abi_layout_test.cpp:93. Bare-metal build slots are 240/248/256; file stat/read/list/write/create-directory/remove at 264/272/280/288/296/304; activation at 448. This replaces v0.3’s audio slot and shifts activation. Shared name/version do not establish layout compatibility. No negotiated tail-extension rule was found. Append to the v0.3 256-byte prefix; do not copy the v0.5 header.

Modern DeveloperStudio imports the Server SDK through external GUIDEXOS_SERVER_ROOT in CMake; its repository is not an independent authoritative ABI copy. It prefers v0.5 bare-metal file callbacks, with hosted fallback of different path semantics. Build a separate profile against explicit v0.3 SDK contract.

The 2202-byte v0.5 manifest matches modern app/app.json, requires Server 0.5.0, declares filesystem read/write, document activation, file associations and AMD64/ARM64 binaries. Packaged AMD64 ELF is 1,128,572 bytes, SHA-256 1387CB09F2026252E4A277E96256C2BB59D06AE70ACFAA56A84EFEFF2F9CF845. Manifest equality does not prove exact source build. v0.5 kernel/core/include/kernel/version.h says KERNEL_VERSION_STRING "0.1.0" while the app manifest requires 0.5.0; treat this as a source-level product-version mismatch, not proof of runtime identity.

Any v0.3 profile needs its own binary, truthful minimum, capabilities, trusted identity, compatible ABI and verified manifest/binary pair. Preserve fail-closed version/integrity checks; do not lower the existing binary minimum.

## Strategy comparison and recommendation

| Strategy | Assessment |
|---|---|
| A — directly port v0.5 callbacks | Not recommended. Context and ABI differ; root/permission enforcement is absent; some errors are collapsed. Reuse validation patterns and semantics selectively. |
| B — v0.3-native adapter | Preferred service design. Reuses v0.3 VFS/FAT, avoids broad App Model backport, adds authorization, memory/path checks, bounded work, append-only ABI and exact results. This is a real kernel feature. |
| C — reduced editor profile | Preferred product scope paired with B. Add standalone UI and centralized command gating; keep full DeveloperStudio artifact/source untouched and package separately. |

This hybrid preserves the v0.3 ABI prefix, minimizes privileged service scope, supports persistent editing, and is testable in QEMU. It still carries moderate release risk because loader/VFS changes can affect existing apps and persistent storage.

## Phases and estimates

A phase is a focused task with acceptance criteria. Estimates include guest qualification and package work.

| Phase | Objective; likely files | Reuse and additions | Main risk; acceptance | Difficulty |
|---|---|---|---|---|
| 1. Contract/authorization | sdk/include/guidexos/abi.h, native_elf_baremetal.cpp, runtime metadata/path policy | Reuse v0.5 pointer/path patterns; define appended ABI, trusted identity/root, errors/caps. | Manifest cannot grant identity; test layout, pointer/traversal/unauthorized/caps; freestanding AMD64 compile/link. | High |
| 2. VFS adapter/failures | v0.3 native loader, vfs.cpp, fs_fat.cpp | Reuse stat/read/list/write/mkdir/remove and status paths. | Partial overwrite, short writes, media/disk-full and listing bounds; test VFS errors/iterator cleanup and existing Settings/legacy flows. | High |
| 3. Document model/UI | Modern main.cpp, developer_studio_models.cpp | Reuse text buffer, dirty/save controller. Add untitled/New/Open File/Save As. | Path ownership and projectless refresh; test failures, size, binary and read/stat mismatch. | Medium |
| 4. Reduced profile/gating | Modern command dispatch and separate build/manifest | Reuse editor panels/startup/close. Centralize all menu/shortcut/panel/background dispatch gates. | Hidden UI is insufficient; exercise every command route and package identity/version. | Medium-high |
| 5. Exact package/guest | Release/QAC scripts/evidence | Reuse QAC workflow; create exact pair. | Source/manifest/binary mismatch; QEMU cold New/Edit/Save/Close/relaunch/Open/byte-compare/modify; failure and regression matrix. | High |

| Scope | Best | Most likely | Worst reasonable | Acceptance burden |
|---|---:|---:|---:|---|
| Useful editor | 4 phases | 6 | 10 | Root/ABI, VFS failures, UI/gating, separate package, QEMU persistence and regressions. Five is an aggressive feasible best case. |
| Editor + projects | 8 | 13 | 20 | All editor work plus templates, metadata, tree, create/remove, reload/recovery. |
| Full DeveloperStudio | 20 | 35 | 60+ | Above plus compiler, Run lifecycle, debugger/traps/stepping/cleanup; each guest-qualified. |

Five phases are realistic only with strict scope and no major authorization/storage surprise. Three are not: service/security and ABI, document UI, command gating, integration regression and exact guest/package acceptance are distinct gates. Six is most likely because authorization/ABI and storage failure behavior may split, and guest qualification may need a correction cycle.

Estimate exceeds 10 editor phases if trusted identity cannot be established, VFS path aliases defeat root restriction, memory validation is unsupported, crash-atomic writes are required, target media exposes FAT failures, full project support becomes mandatory, provenance fails, or release packaging adds constraints.

## Regression and release gates

Future implementation needs coverage for Native Settings persistence and package/version launch, Legacy Control Panel, File Explorer, package-relative resource reads, app lifecycle and cleanup, VFS normalization and all file operations, FAT overwrite/create rollback/flush/disk-full/media errors, pointer boundaries and malformed paths, maximum path/buffer/list sizes, unauthorized app/root, desktop persistence, compositor/window/input, memory allocation, ABI and old-app loading, freestanding AMD64 compile/link, and exact package integrity.

Keep kernel code free of STL, exceptions, RTTI, hosted APIs, unbounded buffers/work, and uncontrolled allocations. Use bounded kernel-native storage and deterministic errors.

Decision gates:

1. Stop unless trusted identity/root and append-only ABI are defined.
2. Require service tests for bounds, authorization, path handling and errors.
3. Reassess after exact guest New/Edit/Save/Close/Reopen byte match.
4. Decide separately whether to add projects.
5. Require a new decision for compiler/Run/debug.
6. Do not advance release acceptance until exact artifacts and regression suites pass.

S22R4 marked current v0.3 DeveloperStudio NO-GO as-is. Prior Settings reports say Native Settings lacks exact-package and fresh guest ISO qualification. S22R5 changes neither status and does not publish or approve Server v0.3.

## Final recommendation

**GO — bounded portability is feasible through a v0.3-native adapter and reduced editor profile.** This applies only to a new separately built editor-only edition after the gates above. It is not approval for the current v0.5 app, direct ABI transplant, or a v0.3 release.

Recommended next Codex task: Phase 1 only—define trusted v0.3 runtime identity/root policy, append-only ABI contract, and bounded service tests. Do not add Studio UI before authorization and service-contract tests pass. Keep v0.5 and modern DeveloperStudio checkouts unchanged.

# S22R6 Developer Studio Editor Adapter — Architectural Reassessment

Date: 2026-10-20

## Outcome

**Outcome C — architectural reassessment.** S22R6 did not implement a file-service adapter or Developer Studio profile. The current Native ELF execution model does not provide an enforceable application identity or privilege boundary. Adding workspace callbacks on top of the existing callback context would create the appearance of restricted access without preventing a native application from impersonating another package or bypassing the callbacks.

The S22R5 v0.3 VFS portability findings remain valid. Its recommendation to proceed with an adapter must be held until the runtime can enforce caller identity and filesystem authority. No UI workflow, package, guest operation, or release qualification is claimed here.

## Stale-prompt and repository audit

The repository was inspected before changes. The expected historical S22R5 checkpoint `41ed479f5f4db414595262e4d0bcca500452e2bb` is an ancestor of the current branch; HEAD was not reset or switched. No S22R6 implementation or accepted-outcome marker existed.

| Checkout | Starting identity | Starting state |
|---|---|---|
| Server v0.3: `D:\dev\guideXOSServer` | `main`, `62f0c9efc71215dc2069f3b3e3fbb3c9a83bf843`; `origin/main` is `0965387b970254035ffce81f1e58aa6893c57936` | Tracked and staged clean; 853 pre-existing untracked paths; 0 behind, 4 ahead |
| Server v0.5 reference: `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO` | `v0.5_DEVELOPER_STUDIO`, `b3753c5db1d9bac9fcb0d6a608c45b00078bf39b` | Clean |
| Modern Developer Studio reference | S22R5 evidence checkout `D:\dev\guideXOS_Developer_Studio`, `main`, `9b66725990b604d426ef6d2be02e7097a0f9a46e` | Clean |

The task text names `D:\dev\guideX_Developer_Studio`, which is absent. S22R5's evidence identifies the existing `D:\dev\guideXOS_Developer_Studio` checkout at the expected HEAD; that checkout was inspected read-only. The v0.5 and modern checkouts remain unchanged.

The v0.3 checkout has four commits after the S22R5 evidence's starting HEAD. Those commits are the S22R5 report and QAC/startup fixes. They do not change the audited Native ELF ABI, runtime, or VFS sources. The recorded SHA-256 values still match for `native_elf_baremetal.cpp`, its header, `sdk/include/guidexos/abi.h`, `tests/native_abi_layout_test.cpp`, `vfs.cpp`, and `fs_fat.cpp`. The modern Studio `main.cpp` and manifest and the v0.5 ABI header also match the S22R5 evidence hashes.

References: [S22R5 portability audit](RELEASE_S22R5_DEVELOPERSTUDIO_SERVICE_PORTABILITY.md) and [curated S22R5 evidence](evidence/release-s22r5-developerstudio-portability-20261020/README.md).

## v0.3 ABI findings

`gx_host_calls` in `sdk/include/guidexos/abi.h` has natural amd64 pointer alignment (8 bytes), no packing override, and a **256-byte** current layout. The last callback, `get_document_activation_path`, begins at byte 248 and occupies the final 8 bytes. The complete field offsets are:

| Offset | Field |
|---:|---|
| 0, 4 | `size`, `version` |
| 8–112 | `log`, `get_api_version`, `request_window`, `draw_text`, `draw_rect`, `wait_for_close`, `poll_event`, `exit`, `file_read_all`, `file_exists`, `request_window_ex`, `file_read`, `present_frame`, `get_ticks_ms` (8-byte slots) |
| 120–160 | `file_stat`, `file_read_workspace`, `file_list`, `file_write_all`, `file_create_directory`, `file_remove` |
| 168–232 | `build_project_start`, `build_project_poll`, `build_project_release`, `development_run_prepare`, `development_run_start`, `development_run_poll`, `development_run_request_close`, `development_run_release`, `development_debug` |
| 240, 248 | `play_pcm`, `get_document_activation_path` |

The previous assertion of `sizeof(gx_host_calls) == 248` was stale: it counted the start of the final callback, not its end. The test now pins `get_document_activation_path == 248`, `alignof(gx_host_calls) == 8`, the first two fields, and the full size of 256. No production ABI field or callback signature was changed.

The v0.5 table is **456 bytes** and is incompatible. It places bare-metal build callbacks at 240/248/256, bare-metal file callbacks beginning at 264, and document activation at 448. At offset 240 v0.3 has `play_pcm`; at 248 v0.3 has document activation. Both revisions still use `GX_API_VERSION == 0` and `GX_ABI_NAME == "guidexos-c-abi-v1"`, which does not distinguish the layouts. The v0.5 header/table was not copied or used.

An append-only v0.3 service tail is mechanically possible while preserving the 256-byte prefix: retain a 256-byte legacy table for existing manifests and expose a separately named v0.3 editor ABI only when an explicit size/version/capability descriptor is present. A distinct ABI identity is required; the shared v1 name/version is not sufficient. This extension was **not** implemented because the caller trust boundary fails first.

## Trust boundary and architectural blocker

The v0.3 runtime has a kernel-owned `Runtime` record and `s_activeRuntime`, but this is not an isolated process identity:

- Package IDs and display names are parsed from `/Apps/<directory>/app.json` by `parse_package`; manifests and executables have no signature or trusted registry check. A manifest's ID is self-declared package metadata.
- `gx_app_context.userData` points to the runtime record. `runtime_from()` currently obtains the runtime by reading that field; callbacks do not establish a non-forgeable caller principal or validate the context against a kernel-owned process capability/generation.
- `kernel/arch/amd64/native_elf_call.asm` switches to an app-owned stack and calls the ELF entry directly. It does not enter CPL3 or switch CR3/address spaces. The app executes with the kernel's privileges and mapped address space.
- Therefore a hostile Native ELF app can bypass an adapter callback, access kernel memory, or forge runtime data. Checking `package->id`, the app name, a caller-supplied PID, or `userData` cannot enforce a security boundary in this execution model.

This is the blocking contract incompatibility. A callback that compares `context` with `s_activeRuntime->context` would improve accidental misuse and stale-call handling, but it would not stop another kernel-mode Native ELF from bypassing the callback or changing the data being checked. The adapter must not claim restricted-workspace security on that basis.

Before revisiting the adapter, the runtime needs an isolated application domain (for example, user-mode execution with a validated syscall/host-call bridge), plus a trusted package identity source such as verified signatures or a kernel-controlled installation registry. The runtime must bind a non-forgeable capability and workspace root to that identity and invalidate it on teardown. This is a bounded prerequisite, but it is a substantial runtime/lifecycle design change.

## VFS and file-service contract status

The audited v0.3 VFS primitives remain present: `stat`, `read`, `opendir`/`readdir`, `write`, `create_file`, `unlink`, and directory operations. Path normalization collapses `.`/`..`, mounts may include aliases, and the VFS has a 256-byte path and 128-byte filename capacity. The existing Native ELF package resource callbacks accept package-relative reads only; they do not provide an editor workspace. The file stat/list/write/create/remove and build/run/debug slots are zero-initialized in the v0.3 host table and are not installed by `initialize_host_table`.

No editor service operation is exposed by S22R6. In particular, there is no authorized root selection, root-to-mount binding, containment check, file-content/entry cap, caller-buffer validation, editor error mapping, or capability negotiation. Deletion is not needed for the first New/Edit/Save/Reopen workflow and must stay out of the minimal service until a separate requirement exists.

The existing FAT write paths have detailed lower-level status and new-file creation rollback. Overwrite updates a file in place and is not crash-atomic; no safe temporary-file replacement/recovery protocol has been established. A later service must preserve short/partial-write and rollback distinctions and report success only after the complete write succeeds. No adapter save semantics are claimed here.

Runtime state is stack-local to synchronous `run_package()`; image and stack storage are released and `s_activeRuntime` is cleared on return. That lifecycle helps clean synchronous resources, but does not create a trustworthy principal or generation-checked authority for future asynchronous operations.

## Developer Studio compatibility and dependency inventory

The modern manifest still requires Server `0.5.0`, advertises AMD64 and ARM64, and uses the shared v1 ABI name. The v0.3 Native ELF implementation is currently compiled for AMD64; the non-AMD64 path has no equivalent app runtime. Modern CMake imports the selected Server SDK through `GUIDEXOS_SERVER_ROOT` and writes the Native ELF output under `Apps/DeveloperStudio/bin/<architecture>`; there is no separate v0.3 editor output identity/path in the modern checkout.

| Dependency | S22R6 classification |
|---|---|
| Native ELF window, drawing, events, package-relative resource reads | Available in v0.3, but not sufficient for editing user documents |
| Trusted process identity, workspace authorization, editor stat/list/read/create/save service | Architectural blocker; not supplied |
| v0.5 Native ELF workspace callbacks and 456-byte host table | Intentionally excluded as ABI-incompatible |
| Project metadata/workspace provider, compiler/build service | Intentionally disabled for an editor-only edition; no v0.3-compatible profile was produced |
| Run/process service and debugger lifecycle/traps | Intentionally disabled; unavailable in the v0.3 runtime |
| New Document, Save As, separate package/output path, UI command gating | Deferred; modern Developer Studio was not modified |

Build, Run, and Debug remain available in the normal modern Developer Studio source/configuration. No v0.3 editor-only binary/profile was produced, and no command-gating claim is made. Current dispatch/controller paths include `BuildControllerStart`, `RunControllerPrepare`/`RunControllerStart`, and `DebugControllerStart` in modern `src/main.cpp`, as well as menu/panel and keyboard routes. A future profile must gate those central routes and all nested/internal service calls, then audit automation or scripting entry points. Merely omitting non-null callbacks from the v0.3 table is not a safe user-facing rejection contract.

## Validation and release impact

- Hosted adapter tests: **0**; production adapter logic was not implemented.
- ABI layout regression: **1 focused test passed** after correcting the stale size assertion. Command: `g++ -std=c++17 -Isdk/include tests/native_abi_layout_test.cpp -o build/s22r6_native_abi_layout_test.exe`; result: `Native ABI layout test PASS`.
- Extension-size/capability negotiation and old-client runtime behavior: not implemented or tested.
- Freestanding kernel build and Native ELF application build/link: not run; no kernel/runtime or application source was changed. No adapter or package qualification is claimed.
- Package/ELF inspection: no S22R6 package was generated.
- QEMU/guest acceptance: deferred. No QEMU process was launched; exclusive qualified QAC reservation and competing-runner coordination were not established for this task.
- Existing v0.3 release candidate, release markers, channel selection, readiness, and ISO were not modified. No release promotion or publication occurred.

## Files changed and commit

- `tests/native_abi_layout_test.cpp` — corrected the stale 248-byte assertion and pinned the actual final callback offset, table alignment, header fields, and 256-byte size.
- `docs/RELEASE_S22R6_DEVELOPERSTUDIO_EDITOR_ADAPTER.md` — recorded the audit, blocker, validation limits, and recommendation.

The 853 pre-existing untracked v0.3 paths were preserved. The focused test executable at `build/s22r6_native_abi_layout_test.exe` was generated for validation and remains as one additional untracked path; the environment rejected the targeted cleanup command. After the local commit, tracked files are clean and the worktree has 854 untracked paths (the 853 original paths plus that generated executable) on `main`. Push was not attempted.

## Recommendation

Do not build or release the v0.3 editor adapter on the current in-kernel Native ELF trust model. The next authorized design phase should define and qualify isolated Native ELF execution, verified application identity, and generation-bound workspace capabilities before adding file operations. After that gate, revisit a v0.3-specific append-only service descriptor and separate modern Studio build/package profile. This report does not start S22R7, Phase 30C, COMP2, or release publication.

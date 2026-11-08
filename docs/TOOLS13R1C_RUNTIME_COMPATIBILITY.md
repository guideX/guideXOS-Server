# TOOLS13R1C Runtime Compatibility

**Outcome: C — SERVER_RUNTIME_GAP_IDENTIFIED**

The accepted TOOLS12 AMD64 ELF passes the Server candidate's production NativeElf structural validator and package audit. Its `guidexos-c-abi-v1` table layout matches the candidate SDK. However, the application's normal startup window path does not create the GUI object required by the Server's draw and present callbacks. The resulting pairing is not ready for guest acceptance.

The version mismatch is a separate unresolved policy question. The evidence does not authorize changing either version string.

## Repository and artifact inventory

| Repository | Branch / HEAD | State observed |
|---|---|---|
| Developer Studio source, `D:\dev\guideXOS_DeveloperStudio_Tools_Source` | `DEVELOPER_STUDIO_TOOLS`, `48f2da543517bd7238378c5a18769be148b10863` | Clean tracked and untracked state. |
| Server Tools integration, `D:\dev\guideXOS_DeveloperStudio_Tools` | `DEVELOPER_STUDIO_TOOLS`, `1cddd5fe9dd44627b58b7ae30de8ba2bd1dd7d34` | Had one pre-existing local commit ahead of its remote. This work adds only this report and the isolated hosted-check source/runner. No push was made. |
| Server candidate, `D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO` | branch `v0.5_DEVELOPER_STUDIO`, `801b4763aa09ddd6cab06bf726610063ca0389de` | No tracked changes. Pre-existing untracked `kernel/out/comp*`, `out/comp*`, `native-c-hello-*`, `z36`, and `z48` trees were left untouched. `v0.5_DEVELOPER_STUDIO` is the development branch identity; the only release tag in this checkout is `v0.2`. |

The accepted artifact is in the source checkout's ignored TOOLS12 output, not in the checked-in package binaries in either Server tree:

| Item | Bytes | SHA256 |
|---|---:|---|
| Accepted AMD64 `developerstudio.elf` | 1,262,228 | `9CBC32163417A553B38A1F01EF69CDBAF49B99C58B9D63D0F06E2E9A31C1AC16` |
| TOOLS12 package `app.json` | — | `47A9C855AE32A372CC7674350A9E7255BB58927FF943798CC929F6EDFCBCF1BE` |
| Paired TOOLS12 ARM64 ELF in the dual-architecture package | 1,448,964 | `9CEB966F4AD74EE70E138F7A2BA1797FCE9B50A007F3E57E74BD3C5EAD365AB7` |

The package audited was `D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio`. Its AMD64 manifest entry is `gx_main`, ABI `guidexos-c-abi-v1`, runtime `native-elf`; its minimum is `0.5.0`. The audit passed with exactly `app.json` and the two architecture ELFs. The different 1,128,572-byte ELF in the Server checkout was not used or substituted.

The TOOLS12 build script copies the committed app manifest and requires an explicit Server SDK include root. The historical invocation and SDK-header hash used for this exact accepted ELF were not retained, so build-time header provenance is not independently attested. No rebuild or substitution was performed.

## Version contract and enforcement

The source trace found these distinct declarations:

| Declaration | Evidence | Meaning established by source |
|---|---|---|
| Server `0.1.0` | `kernel/core/include/kernel/version.h` defines `KERNEL_VERSION_STRING` as `0.1.0`. | Kernel version only; no separate authoritative Server product or NativeElf runtime semantic version was found. |
| Native C ABI | `sdk/include/guidexos/abi.h`: `GX_ABI_NAME` is `guidexos-c-abi-v1`; `GX_API_VERSION` is `0`. | ABI identity and API-table version, not a Server product version. |
| Developer Studio `0.1.0` | `app/app.json` and the TOOLS12 package manifest. | Application version. |
| `minGuideXOSVersion: 0.5.0` | `app/app.json` and the TOOLS12 package manifest. | Application metadata; no mapping to the kernel version or ABI version is documented. |
| `v0.5_DEVELOPER_STUDIO` | Server candidate branch name. | Repository development identity, not an authoritative runtime version declaration. |

`AppManifestLoader` copies `minGuideXOSVersion` into the manifest model. `AppManifestValidator::Validate` does not require, parse, or compare it. The app registry and launch resolver select by app kind, architecture, and entry path; the NativeElf loader validates ELF structure and invokes the entrypoint. No production loader, package audit, or installation comparison against the minimum was found. The Server package audit passed this package while its minimum remained `0.5.0`, which confirms that the audit does not enforce a platform-version comparison.

**Actual behavior:** the minimum is parsed metadata and is not enforced in the inspected production load/launch path. This absence does not certify compatibility.

**Version-policy result:** insufficient evidence to choose Case A or Case B. The Server version owner must identify the authoritative platform/runtime version field and document whether it maps to `KERNEL_VERSION_STRING`, another Server release value, or a separate compatibility scheme. The Developer Studio owner can then confirm the declared minimum against that rule. Until then, keep `0.1.0` and `0.5.0` unchanged. No compatibility comparison should be bypassed or invented.

## ELF and ABI checks

The accepted artifact is ELF64, little-endian, `ET_EXEC`, AMD64, with entry `0x500D36F0`. It has five program headers: `PT_PHDR`, three `PT_LOAD` segments, and `PT_GNU_STACK`. It has no section metadata or relocations. Its load segments are:

| File offset | Virtual address | File size | Memory size | Flags |
|---:|---:|---:|---:|---|
| `0x0` | `0x50000000` | `0x2CAA0` | `0x2CAA0` | R |
| `0x2CAA0` | `0x5002DAA0` | `0x1076EB` | `0x1076EB` | R-X |
| `0x134190` | `0x50136190` | `0x104` | `0xE22110` | R-W |

The Server loader uses fixed image base `0x50000000`, a 512 MiB NativeElf window, a 2 MiB ELF-file buffer, at most four load segments, and a 512 KiB application stack. The production load path raises the validator file bound from the compiler-bootstrap validator's 96 KiB default to the runtime's 2 MiB limit. The accepted ELF maps to 16,093,184 bytes (`0xF59000`), within the runtime's 16 MiB nested-image snapshot bound. The entry, segment count, memory extent, and permissions fit the candidate loader contract. No structural incompatibility was found.

The ABI header defines a 464-byte `gx_host_calls` table: 32-bit `size` and `version` followed by 57 pointer slots. AMD64 `GX_CALL` is Microsoft x64 (`ms_abi`) in the SDK; the candidate runtime uses the same SDK declarations and has compile-time offset/size assertions. The app build enables `GXOS_DEVELOPER_STUDIO_BARE_METAL`, so it selects the bare-metal workspace, build, run, and debug callbacks. The accepted artifact's exact historical SDK header remains unverified as noted above.

## Runtime dependency matrix

“Partially verified” means the candidate has a real implementation path and its contract was inspected, but the callback was not executed end-to-end in this non-guest phase.

| Developer Studio dependency | Candidate implementation and evidence | Status |
|---|---|---|
| AMD64 ELF loading, fixed base, entry, segments, relocation model | `kernel/core/native_elf/native_elf_loader.cpp` invokes `validate_native_elf` with the runtime limits; loader copies/zero-fills PT_LOAD ranges and calls the declared entry. Exact accepted bytes pass the same production validator. | **Verified compatible** for structural loading. |
| Host table size, field ordering, function-pointer signatures, calling convention | `sdk/include/guidexos/abi.h`, `kernel/core/native_elf/native_elf_runtime.h` static assertions, and `tests/native_abi_layout_test.cpp`. Candidate layout test passes. | **Verified compatible** for the inspected v1 layout; historical build-header provenance is unverified. |
| Stack, image memory, allocation, cleanup | 512 KiB aligned stack and pointer-range checks exist; loader clears the image/stack and tears down after return. Accepted image fits the 16 MiB nested snapshot bound. | **Partially verified**; entry invocation and teardown were not run. No host allocator slot is required; Developer Studio uses its freestanding allocator. |
| Startup, logging, API version, termination | `initialize_app_context` sets table size/version and wires log, version, and exit. Context is authenticated before callbacks; exit returns the app result. | **Partially verified**; not invoked with the accepted image. |
| Window creation, drawing, presentation | `host_bare_request_window` writes handle `1` and reports success when the framebuffer exists. `request_window_ex` delegates to it. But `native_window_valid` requires a live `s_guiApplication`, window, and matching `s_guiWindowId`; draw and present callbacks reject calls without them. Only `host_native_window_create` creates that object. Developer Studio startup calls `request_window_ex` and does not call `native_window_create`. | **Incompatible — confirmed Server runtime gap.** The normal app path can report window creation success, then fail every initial draw/present callback. |
| Keyboard/mouse event delivery | `host_bare_poll_event` uses the real input subsystem and emits key/mouse events with window `1`; it returns timeout when no input is ready. It does not emit paint/close events. | **Partially verified**; input path inspected, not exercised. Its handle is not backed by the GUI instance required by drawing. |
| Workspace stat/read/list/write/create/remove | Bare-metal callbacks validate app pointers and call the VFS (`stat`, `read_file`, `opendir/readdir`, `write_file`, `mkdir`, `unlink/rmdir`). | **Partially verified**; actual callback/VFS fixture operations were not run. |
| Build | Bare-metal start/poll/release/cancel callbacks forward to `BareMetalBuildService` with an authenticated runtime identity and copied bounded request strings. | **Partially verified**; no callback request was executed. |
| Run | Bare-metal prepare/start/poll/close/cancel/release callbacks forward to `NativeElfRunService`; run snapshots are copied with caller-size checks. | **Partially verified**; no guest execution or callback request was run. |
| Debug, call stack, variables, expressions | Bare-metal callbacks forward to `NativeElfRunService` debug methods and use bounded, loader-owned snapshots. | **Partially verified**; no callback request was executed. No Debugging code was changed. |
| Document activation | The callback is present but returns `GX_ERROR_UNSUPPORTED` with an empty path. SDK documents this as optional; Developer Studio maps unsupported to “Unavailable.” | **Verified compatible** as optional absence; document activation is not provided by this guest route. |

The GUI row is a specific incompatibility, not a conclusion based on names: the startup and callback bodies form a deterministic failed sequence. `reset_gui_runtime` leaves the GUI pointer null on the first run; `request_window` does not allocate it; the app then calls draw functions that require it. `drawShell` ignores those callback return values, so startup markers alone could falsely suggest a usable window.

### ABI callback inventory and requiredness

The accepted build enables `GXOS_DEVELOPER_STUDIO_BARE_METAL`. The candidate zero-initializes the full 57-slot table and advertises `size = 464`, `version = 0`; the following inventory accounts for every slot in `gx_host_calls`. “Required” describes the application's active path or required UI behavior; extension callbacks remain optional when the application checks the table size and pointer before use.

| ABI slots | Use / requiredness in this build | Candidate wiring and result |
|---|---|---|
| `log`, `get_api_version` | Required at `gx_main` entry. | Wired; implementation inspected. |
| `request_window`, `request_window_ex` | One request path is required; `_ex` is preferred when present and falls back to `request_window`. | Both wired. Both take the same broken bare-metal path: return handle `1` without creating the GUI object required for rendering. **Incompatible.** |
| `draw_text`, `draw_rect` | Required for the app's visible shell and panels. | Wired, but both reject the returned handle until a real native GUI window exists. **Incompatible.** |
| `poll_event`, `wait_for_close` | Optional as a pair in the ABI; the app uses polling when available and close-wait as fallback. | Both wired. Polling translates key/mouse input; drawing/window identity remains broken. **Partially verified.** |
| `exit` | Optional; app returns `GX_OK` if absent. | Wired; app result is returned. Teardown was not dynamically exercised. **Partially verified.** |
| `present_frame`, `get_ticks_ms` | Optional extensions. The current Developer Studio startup does not call `present_frame`; any caller must still provide a valid window. | Both wired. `present_frame` shares the invalid-window guard and is unusable after the current request path. `get_ticks_ms` uses the runtime clock. |
| `file_read_all`, `file_exists`, `file_read`; hosted `file_stat`, `file_read_workspace`, `file_list`, `file_write_all`, `file_create_directory`, `file_remove` | Hosted absolute-path surface; not selected by this bare-metal build. | Null by zero-initialization. Not required for this package's selected VFS route. |
| Hosted `build_project_start`, `build_project_poll`, `build_project_release` | Hosted build surface; not selected by this bare-metal build. | Null by zero-initialization. The separate bare-metal build slots are wired. |
| Hosted `development_run_prepare`, `development_run_start`, `development_run_poll`, `development_run_request_close`, `development_run_release` | Hosted Run surface; not selected by this bare-metal build. | Null by zero-initialization. The separate bare-metal Run slots are wired. |
| Hosted `development_debug`, `development_debug_call_stack`, `development_debug_inspect_variables`, `development_debug_evaluate_expression` | Hosted Debug surface; not selected by this bare-metal build. | Null by zero-initialization. The separate bare-metal Debug slots are wired. |
| `bare_metal_file_stat`, `bare_metal_file_read_workspace`, `bare_metal_file_list`, `bare_metal_file_write_all`, `bare_metal_file_create_directory`, `bare_metal_file_remove` | Active VFS workspace operations; each is size/pointer checked by the app. | All wired to the Server VFS. Callback behavior against a disposable workspace was not executed. **Partially verified.** |
| `bare_metal_build_project_start`, `bare_metal_build_project_poll`, `bare_metal_build_project_release`, `bare_metal_build_project_cancel` | Start/poll/release support the app's Build feature; cancellation is optional/additive. | All wired to `BareMetalBuildService`. No request or cancellation was executed. **Partially verified.** |
| `bare_metal_development_run_prepare`, `bare_metal_development_run_start`, `bare_metal_development_run_poll`, `bare_metal_development_run_request_close`, `bare_metal_development_run_release`, `bare_metal_development_run_cancel` | Start/poll/close/release are gated as a feature set; cancellation is additive. | All wired to `NativeElfRunService`. No callback was executed. **Partially verified.** |
| `native_window_create`, `native_window_set_text`, `native_window_destroy`, `native_window_run` | Additive native GUI surface. The app's startup calls `request_window_ex`, not `native_window_create`; it conditionally calls destroy during cleanup if that slot exists. | All wired. `native_window_create` is the only inspected path that creates `NativeElfGuiApplication`; the app does not call it before drawing. This is the concrete window lifecycle mismatch. |
| `bare_metal_development_debug`, `bare_metal_development_debug_call_stack`, `bare_metal_development_debug_inspect_variables`, `bare_metal_development_debug_evaluate_expression` | Bare-metal Debug UI checks table size and all four callbacks before enabling those operations. | All wired to `NativeElfRunService`. No operation was executed. **Partially verified.** |
| `get_document_activation_path` | Optional append-only activation callback; app also checks table size and handles `GX_ERROR_UNSUPPORTED`. | Wired, returns unsupported with no path in this route. The app degrades to unavailable. **Verified compatible as optional absence.** |

This inventory distinguishes implementation presence from runtime proof. It also shows why `native_window_create` being wired does not satisfy the app's active startup contract: the application calls the separate legacy request callbacks and never enters the native create path.

## Hosted evidence

No guest image was built and no QEMU/QAC process was started.

1. The Server Tools integration runner compiled the candidate's existing `native_abi_layout_test.cpp`; result: `Native ABI layout test PASS`.
2. It compiled the candidate's production `native_elf_validator.cpp` against the exact accepted ELF using runtime limits; result: `PASS; bytes=1262228 mapped=16093184 loads=3 entry=0x500D36F0`.
3. The source repository's package audit against the exact dual-architecture TOOLS12 package returned `package_content_audit=PASS`, preserving and reporting the accepted AMD64 hash.
4. These tests verify ABI layout, package identity, and ELF structure. They do not execute `gx_main` or the privileged Server callbacks. The GUI gap was established by tracing the actual app startup calls against the actual callback implementations.

Reproduce the hosted checks from the integration checkout:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\run-tools13r1c-runtime-compatibility-test.ps1 `
  -ServerRoot D:\dev\guideXOSServerV0.5_DEVELOPER_STUDIO `
  -AcceptedElf D:\dev\guideXOS_DeveloperStudio_Tools_Source\out\TOOLS12\amd64-acceptance-stage\Apps\DeveloperStudio\bin\amd64\developerstudio.elf
```

The runner checks the artifact SHA before compiling and writes executables only under a fresh `%TEMP%\TOOLS13R1C\<id>` directory. It does not write to the candidate Server checkout.

## Required Server handoff and version decision

The smallest corrective phase is Server-owned: make `request_window` and `request_window_ex` create or acquire the same real GUI window used by `draw_text`, `draw_rect`, `present_frame`, and `poll_event`; return its actual handle; and ensure teardown closes that window. Keep the Developer Studio application and its accepted ELF unchanged. Add a focused test that exercises the real callback sequence through request, first draw/present, input delivery, and cleanup. Do not modify Compilation or Debugging, and keep Replace Apply disabled.

Separately, the Server version owner must publish the authoritative product/runtime version field and its relation, if any, to the application minimum. No version/header/manifest change is justified before that decision.

## Guest readiness and QAC gate

**Not ready.** The confirmed GUI gap blocks meaningful guest acceptance. The current phase did not construct an image, reserve QAC, or launch QEMU.

After the Server fix and version-owner decision, use this isolated procedure:

1. Create a detached worktree from the approved Server fix commit, based on candidate baseline `801b4763aa09ddd6cab06bf726610063ca0389de`. Require a clean worktree. Leave the original candidate's pre-existing untracked outputs untouched; do not copy them into the build.
2. In that worktree, run `powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -Arch amd64` without `-Clean` and without `-RunQemu`. This produces the bootloader/kernel/ESP inputs in the disposable worktree only.
3. Copy the complete accepted TOOLS12 package directory `...\out\TOOLS12\dual-architecture-package\Apps\DeveloperStudio` into the disposable `ESP\Apps\DeveloperStudio`. Recheck the app manifest hash and AMD64 ELF size/SHA above; do not invoke the Developer Studio build script.
4. Place a disposable workspace fixture under `ESP\TOOLS13R1C\workspace` (guest path `/TOOLS13R1C/workspace`), outside the application package. Record its source/hash and include a small C file plus a read-only text file. Do not enable or exercise Replace Apply.
5. Audit the staged package and produce a sorted SHA256 inventory of `BOOTX64.EFI`, `kernel.elf`, `ramdisk.img`, the TOOLS13R1C workspace fixture, and every file under `Apps\DeveloperStudio`. Verify the staged kernel hash matches the just-built kernel and the staged AMD64 ELF hash remains exactly `9CBC3216...C1AC16`. Reject unexpected package files.
6. Use a new TOOLS13R1C-focused QAC acceptance runner that stages this exact hash and requires visible first-frame, keyboard/mouse, and clean-close evidence. The existing `Phase29YUseStagedDeveloperStudioPackage` path is pinned to SHA256 `7605FE1B...744A0DE`, so it is not a valid runner for this accepted TOOLS12 artifact without an owned, reviewed staging update.
7. Request the managed QAC reservation only after the Server correction and version decision are approved and `.NET Support` releases the priority baton. A reservation is an independent gate; an idle coordinator is not authorization. Do not use a direct QEMU invocation.

## Final determination

There is no ELF format, fixed-base, segment, stack-size, or ABI-layout mismatch in the checks completed here. The current Server candidate nevertheless has a concrete UI startup incompatibility: it acknowledges the requested window without establishing the GUI object its own draw callbacks require. Therefore it cannot be certified as a usable TOOLS12 Developer Studio runtime pairing in this phase. The version comparison also remains undefined and unenforced. The next action is the focused Server GUI callback correction, followed by the authoritative Server version-policy decision and then a newly scoped guest acceptance phase.

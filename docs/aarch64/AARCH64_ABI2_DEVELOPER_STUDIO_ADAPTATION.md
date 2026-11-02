# AARCH64 ABI2 Developer Studio Adaptation Audit

**Outcome:** `MANDATORY_HOST_SERVICE_MISSING`
**Qualification date:** 2026-11-01
**Repository:** `D:\dev\guideXOSServer_AARCH64`
**Branch / starting HEAD:** `AARCH64_SUPPORT` / `eecc865ea16bc358cab762ae0fd786bfb7f82964`
**Upstream:** `origin/AARCH64_SUPPORT`; local branch was three commits ahead
**Developer Studio source:** `D:\dev\guideXOS_Developer_Studio` at `058c83fe1ae270a103eadc7caca961328e8abe2a`
**v0.5 SDK source:** `D:\dev\guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION`; the checked ABI and related headers match the locked blobs from `f63a7710a30b49f785aac134abf06a3a6c5b5aca`.

## Decision

The AARCH64 Server host-call layout is not the only incompatibility. The Phase-12 ARM64 runtime constructs the correct 400-byte AARCH64 table and publishes the Phase-11 file/build and Phase-12 Run services, but it leaves `draw_text` and `draw_rect` null. Those callbacks are the pinned Developer Studio's immediate-mode rendering path. Its draw wrappers silently do nothing when the callback is null, and the consumer does not call the available widget callbacks. There is no verified equivalent service in the current runtime that preserves this rendering behavior.

The missing renderer is mandatory for a usable IDE window. Adding a false callback, reading a v0.5 callback slot, or claiming the existing widget API is an equivalent renderer would be unsafe. Work therefore stopped before making a compatibility overlay, modifying either pinned source checkout, or generating an IDE package. The minimum viable next change is a coordinated AARCH64 GUI implementation that publishes the existing SDK's `draw_text` and `draw_rect` slots with correct AAPCS64 signatures and backs them with the app window renderer, or a separately scoped Developer Studio renderer rewrite using supported widgets.

The pinned source also targets newer SDK types and debugger APIs. A direct AARCH64 syntax probe reported 406 errors across missing debug types/constants, newer Run states, error constants, and incompatible host-call members. This requires a broader consumer adaptation after the renderer dependency is resolved.

## Preflight and safety record

The Server checkout was on the requested `AARCH64_SUPPORT` branch and expected starting HEAD. The tracked worktree was clean. The following 18 untracked qualification directories already existed and were preserved:

`out/aarch64-gicv3-gic3-2-final/`, `out/aarch64-gicv3-gic3-2/`, `out/aarch64-gicv3-host-phase12-alignment/`, `out/aarch64-gicv3-host/`, `out/aarch64-gicv3-qualify-gic3-2/`, `out/aarch64-gicv3/`, `out/aarch64-phase12-alignment-build/`, `out/aarch64-phase2-gic3-2-final/`, `out/phase12-abi-isolation/`, `out/phase12-integration-repro/`, `out/phase12-integration-repro2/`, `out/phase12-sdk-alignment-20261101/`, and `out/phase12-sdk-alignment-regression/` through `out/phase12-sdk-alignment-regression6/`.

The pinned Developer Studio checkout was clean on `main` at the locked revision. The v0.5 SDK checkout was not switched or edited; its current branch is `codex/qac-phase4r1-admission`, and its relevant header blobs match the lock. The unrelated Hypervisor checkout was not accessed or modified.

The existing wrapper still stops before starting a build or writing a package. A fresh run produced `PHASE12_SDK_CONTRACT_BLOCKED` for the activation callback/layout conflict. Its early guard remains intact.

## Dependency provenance and layout

The AARCH64 SDK baseline is Server commit `1d596d4eaeb194bddeaae53f032b14989deac5df`. Its `sdk/include/guidexos/abi.h` is Git blob `97f49d2492e9c3c670b170f20358cb77f002c9d2` (SHA-256 `10bf1243bf56bdf14170b24d90bc3b949f7a2ba75b6ce4b563d70e976a30f5d2`). The file remains identical at the requested starting HEAD.

The v0.5 contract is from Server commit `f63a7710a30b49f785aac134abf06a3a6c5b5aca`. Its `abi.h` is blob `4513bb24930cf7a4069f5f82e51897d07bd52f4a` (SHA-256 `7fa89a1d996defa69fdeb0f03ea83b02276ce303ef3288f55e34f253756a08d7`). The activation callback was introduced at `3b47738fbd4616990c05c11d6e1f88fe08d58b4c`.

Both definitions use natural 8-byte pointer alignment on the tested 64-bit targets. `GX_CALL` is `__attribute__((ms_abi))` on x86-64 and the default AAPCS64 convention on AArch64. Cross-target layout probes against the genuine source-controlled headers passed.

| Contract | Size / alignment | Conflict points |
| --- | ---: | --- |
| AARCH64 Server | 400 bytes / 8 bytes | `guiVersion` at 240; `window_destroy` at 248; `bare_metal_build_project_start` at 288 |
| Server v0.5 | 464 bytes / 8 bytes | `bare_metal_build_project_start` at 240; `native_window_destroy` at 368; `get_document_activation_path` at 448; `bare_metal_build_project_cancel` at 456 |

At offset 240, AARCH64 stores a 32-bit `guiVersion` value while v0.5 reads a function pointer for Build start. The AARCH64 Build callback is at 288. The layouts must not be reinterpreted or shared.

The AARCH64 profile's required assertions are already present in `tests/phase12_aarch64_sdk_layout_test.cpp`: pointer size 8, natural alignment 8, `guiVersion` offset 240, Build start offset 288, final Run release offset 392, and table size 400. The independent v0.5 probe asserts Build start 240, activation 448, Build cancel 456, and size 464. The ABI1 alignment script compiled and ran both genuine contract probes and the v0.5 activation test.

## Complete Developer Studio host-call dependency audit

This audit includes production source under `src/` and excludes fixture apps. All function-pointer signatures below are declared with `GX_CALL`. “A offset” means the AARCH64 byte offset; “v0.5 offset” means the locked 464-byte contract offset. A dash means the field is absent.

| Member | A offset | v0.5 offset | Signature / data | AARCH64 Phase-12 runtime | Class and action |
| --- | ---: | ---: | --- | --- | --- |
| `size` | 0 | 0 | `uint32_t` | Set to `sizeof(host)` = 400 | A; use as table extent |
| `version` | 4 | 4 | `uint32_t` | Set to `GX_API_VERSION` | A; direct |
| `log` | 8 | 8 | `gx_result(ctx, const char*)` | Populated | A; direct |
| `get_api_version` | 16 | 16 | `uint32_t(ctx)` | Populated | A; direct |
| `request_window` | 24 | 24 | `gx_result(ctx, title, int, int, gx_handle*)` | Populated | A; direct |
| `draw_text` | 32 | 32 | `gx_result(ctx, window, int, int, text)` | **Null** | **D; mandatory renderer dependency** |
| `draw_rect` | 40 | 40 | `gx_result(ctx, window, x, y, width, height, color)` | **Null** | **D; used by the immediate-mode UI renderer** |
| `wait_for_close` | 48 | 48 | `gx_result(ctx, window, timeoutMs)` | Null; `poll_event` is populated | C; use event polling |
| `poll_event` | 56 | 56 | `gx_result(ctx, gx_event*, timeoutMs)` | Populated | A; direct |
| `exit` | 64 | 64 | `gx_result(ctx, gx_result)` | Populated | A; direct |
| `request_window_ex` | 88 | 88 | `gx_result(ctx, title, int, int, flags, gx_handle*)` | Populated | A; direct |
| `file_stat` | 120 | 120 | `gx_result(ctx, path, gx_file_info*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `file_read_workspace` | 128 | 128 | `gx_result(ctx, path, buffer, capacity, bytesRead*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `file_list` | 136 | 136 | `gx_result(ctx, path, entries, capacity, count*, truncated*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `file_write_all` | 144 | 144 | `gx_result(ctx, path, data, bytes, bytesWritten*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `file_create_directory` | 152 | 152 | `gx_result(ctx, path)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `file_remove` | 160 | 160 | `gx_result(ctx, path)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `build_project_start` | 168 | 168 | `gx_result(ctx, request*, handle*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `build_project_poll` | 176 | 176 | `gx_result(ctx, handle, snapshot*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `build_project_release` | 184 | 184 | `gx_result(ctx, handle)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_run_prepare` | 192 | 192 | `gx_result(ctx, request*, handle*, snapshot*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_run_start` | 200 | 200 | `gx_result(ctx, handle)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_run_poll` | 208 | 208 | `gx_result(ctx, handle, snapshot*)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_run_request_close` | 216 | 216 | `gx_result(ctx, handle)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_run_release` | 224 | 224 | `gx_result(ctx, handle)` | Null; bare-metal counterpart is populated | B; select bare-metal service |
| `development_debug` | 232 | 232 | `gx_result(ctx, request*, snapshot*)` | Not populated | C; debugging unavailable in this runtime |
| `guiVersion` (not a callback) | 240 | — | `uint32_t` | Set to `GX_GUI_ABI_VERSION` | A; confirms the incompatible v0.5 interpretation |
| `window_destroy` | 248 | — | `gx_result(ctx, window)` | Populated | A; generic AARCH64 window service |
| `bare_metal_build_project_start` | 288 | 240 | `gx_result(ctx, request*, handle*)` | Populated by Phase 11 | A; direct field access under AARCH64 layout |
| `bare_metal_build_project_poll` | 296 | 248 | `gx_result(ctx, handle, snapshot*)` | Populated by Phase 11 | A; direct |
| `bare_metal_build_project_release` | 304 | 256 | `gx_result(ctx, handle)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_stat` | 312 | 264 | `gx_result(ctx, path, gx_file_info*)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_read_workspace` | 320 | 272 | `gx_result(ctx, path, buffer, capacity, bytesRead*)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_list` | 328 | 280 | `gx_result(ctx, path, entries, capacity, count*, truncated*)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_write_all` | 336 | 288 | `gx_result(ctx, path, data, bytes, bytesWritten*)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_create_directory` | 344 | 296 | `gx_result(ctx, path)` | Populated by Phase 11 | A; direct |
| `bare_metal_file_remove` | 352 | 304 | `gx_result(ctx, path)` | Populated by Phase 11 | A; direct |
| `bare_metal_development_run_prepare` | 360 | 312 | `gx_result(ctx, request*, handle*, snapshot*)` | Populated by Phase 12 | A; direct |
| `bare_metal_development_run_start` | 368 | 320 | `gx_result(ctx, handle)` | Populated by Phase 12 | A; direct |
| `bare_metal_development_run_poll` | 376 | 328 | `gx_result(ctx, handle, snapshot*)` | Populated by Phase 12 | A; direct |
| `bare_metal_development_run_request_close` | 384 | 336 | `gx_result(ctx, handle)` | Populated by Phase 12 | A; direct |
| `bare_metal_development_run_release` | 392 | 344 | `gx_result(ctx, handle)` | Populated by Phase 12 | A; direct |
| `native_window_destroy` | — | 368 | `gx_result(ctx, window)` | Absent; `window_destroy` is the AARCH64 generic-window equivalent | B; adapt the relaunch cleanup call |
| `bare_metal_development_debug` | — | 392 | `gx_result(ctx, request*, snapshot*)` | Absent | C; explicitly disable/report debugger |
| `development_debug_call_stack` | — | 400 | `gx_result(ctx, request*, callStack*)` | Absent | C; debugger inspection unavailable |
| `bare_metal_development_debug_call_stack` | — | 408 | `gx_result(ctx, request*, callStack*)` | Absent | C; debugger inspection unavailable |
| `development_debug_inspect_variables` | — | 416 | `gx_result(ctx, request*, variables*)` | Absent | C; debugger inspection unavailable |
| `bare_metal_development_debug_inspect_variables` | — | 424 | `gx_result(ctx, request*, variables*)` | Absent | C; debugger inspection unavailable |
| `development_debug_evaluate_expression` | — | 432 | `gx_result(ctx, request*, expression*)` | Absent | C; debugger inspection unavailable |
| `bare_metal_development_debug_evaluate_expression` | — | 440 | `gx_result(ctx, request*, expression*)` | Absent | C; debugger inspection unavailable |
| `get_document_activation_path` | — | 448 | `gx_result(ctx, char*, capacity, requiredBytes*)` | Absent | C; report activation unavailable; keep File/Open and workspace document operations |

The SDK fields for GUI widgets exist at AARCH64 offsets 256, 264, and 272, but the pinned Studio's production renderer does not call them. The Phase-12 runtime publishes `widget_create`, `widget_set_text`, and `widget_set_value` at those positions. They do not provide a verified replacement for the consumer's immediate-mode text and colored rectangle operations.

The source also uses hosted file, Build, and Run slots as fallbacks. The AARCH64 profile must prefer the populated bare-metal callbacks; it must not infer them from the v0.5 offsets. The AARCH64 runtime does not publish the hosted callbacks.

## Runtime publication verification

The AARCH64 Phase-12 path calls `phase9_initialize_host` in `kernel/core/native_elf_baremetal_arm64.inc`. It clears the entire table, writes `size = sizeof(host)`, sets API and GUI versions, then assigns log, API version, window creation/destruction, widget, event poll/wait, and exit callbacks. Under `GXOS_AARCH64_PHASE10` it assigns the six bare-metal file services and three bare-metal Build services. Under `GXOS_AARCH64_PHASE12` it assigns the five bare-metal Run services.

It does not assign `draw_text` or `draw_rect`. Because the table is zero-initialized, both are null despite the fields existing in the 400-byte header. The Developer Studio helpers at `src/main.cpp` lines 9219–9224 guard those calls with a pointer check and silently skip rendering when null. The source contains no use of the runtime's widget callbacks for the IDE's own renderer.

The runtime stores the host table inside its Phase-9 runtime object and points `gx_app_context.host` at that member while the app is active. The object remains in scope through the synchronous app call and is destroyed on return; no persistent host pointer is published. Callback declarations and assignments compile under the AARCH64 foundation build, but this does not execute the guest callbacks. No guest runtime behavior is claimed.

## Additional SDK source incompatibilities

The AARCH64 SDK headers are the definitions at the pinned AARCH64 baseline, not the v0.5 headers:

| Header | AARCH64 blob | v0.5 blob |
| --- | --- | --- |
| `abi.h` | `97f49d2492e9c3c670b170f20358cb77f002c9d2` | `4513bb24930cf7a4069f5f82e51897d07bd52f4a` |
| `types.h` | `cc1676a82b532422c82c16212e4792e39fdf8de5` | `b6e74827c79d9f969b19625094bbfad890228476` |
| `development_run.h` | `61fec321775a102f4b16583d6fe1d8c3436d2499` | `c5521391b3837100ba6d1a2554945d55ddda508a` |
| `development_debug.h` | `efebc8a577d610f2db3dc4d6ec979c33b0edc9ba` | `d466a4326c50a53430cebbd5600888c4efc82211` |

A direct compile against the real AARCH64 include root also reports missing newer Developer Studio dependencies, including `gx_development_debug_call_stack`, `gx_development_debug_variables`, `gx_development_debug_expression`, breakpoint and output-record fields, `GX_DEVELOPMENT_RUN_CLOSING/CANCELLED/PAUSED/STEPPING`, and `GX_ERROR_NOT_FOUND/NOT_MOUNTED`. The AARCH64 Debug header exposes a smaller request/snapshot contract. These errors are not safely repaired by substituting the v0.5 headers because the host-call table would be misread.

## Qualification evidence

| Gate | Result |
| --- | --- |
| AARCH64 / v0.5 genuine SDK layout and offset probes | PASS; 400 and 464 bytes, pointer size/alignment 8; critical offsets verified |
| v0.5 document activation ABI test | PASS; 9/9 |
| AARCH64 negative control for the v0.5 activation consumer | PASS; incompatible AARCH64 header rejected |
| Direct pinned Developer Studio AARCH64 syntax probe | FAIL; 406 compiler errors across SDK type/API mismatches; no object or package emitted |
| Phase-12 ARM64 Server foundation | PASS; UEFI loader, 51,708,120-byte ARM64 kernel, AARCH64 and AMD64 proof payloads, fresh FAT32 ramdisk |
| Phase-10 hosted application loading controls | PASS; ARM64 resolver selected `EM_AARCH64`, AMD64 resolver selected `EM_X86_64`; malformed, traversal, missing, wrong-machine, oversized, duplicate, and legacy controls passed |
| App Model activation checks | PASS; 145/145 |
| App Model lifecycle cycles | PASS; 100/100 |
| Developer Studio document activation checks | PASS; 9/9 against v0.5 |
| Studio run-controller test from pinned build script | BLOCKED; build script's link command omits definitions for `BuildRequestFromProject` and `BuildRequestSourceMembershipHash` |
| GICv3 hosted regression | PASS; 62 assertions |
| Phase-12 Developer Studio ARM64 component | NOT BUILT; existing early ABI1 guard stopped before compilation |
| Full Phase-12 Developer Studio package | NOT GENERATED |
| QEMU, QAC guest slot, or physical hardware | NOT RUN |

The 145/145 App Model and 100/100 lifecycle counts were reproduced by the pinned Developer Studio build script using an isolated temporary output root. The script then passed project parser/generator, document activation (9/9), startup lifecycle, and the 12 focused sentinel cases, before stopping at the unrelated run-controller test link failure. No Developer Studio source or test was changed to bypass it.

### Exact qualification commands

From `D:\dev\guideXOSServer_AARCH64`:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-phase12-sdk-alignment.ps1 -OutputDirectory .\out\phase12-sdk-alignment-abi2-audit-20261101
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase10.ps1 -Phase12Proof -OutputDirectory .\out\aarch64-phase12-abi2-audit-20261101
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\test-aarch64-gicv3-host.ps1 -OutputDirectory .\out\aarch64-gicv3-abi2-audit-20261101
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-aarch64-phase12.ps1 -OutputDirectory .\out\phase12-abi2-package-blocked-20261101
```

From the pinned Developer Studio checkout (v0.5 regression, outputs isolated under the system temporary directory):

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build.ps1 -TargetArchitecture amd64 -ServerRoot C:\Users\guideX\AppData\Local\Temp\guideXOS-ABI2-v05-build -SdkInclude D:\dev\guideXOSServerV0.5_PHASE4R1_QAC_ADMISSION\sdk\include
```

Direct syntax-only AARCH64 source probe (executed from the pinned Developer Studio root; it failed as expected and wrote no object):

```powershell
& 'C:\Program Files\LLVM\bin\clang++.exe' --target=aarch64-none-elf -std=c++11 -ffreestanding -fno-exceptions -fno-rtti -fno-stack-protector -fno-unwind-tables -fno-asynchronous-unwind-tables -ferror-limit=0 -DGXOS_DEVELOPER_STUDIO_BARE_METAL -DGXOS_DEVELOPER_STUDIO_AARCH64 -DGXOS_PHASE12_SMOKE -I 'D:\dev\guideXOSServer_AARCH64\sdk\include' -I src -fsyntax-only src\main.cpp
```

The fresh foundation artifacts are in `out/aarch64-phase12-abi2-audit-20261101/`. Their identities were:

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `BOOTAA64.EFI` | 11,264 | `c6799231c67c594eb752183d611deaad67cd5ce9ef81c0c69e850e9cda7ba4ca` |
| `kernel.elf` | 51,708,120 | `7f09796060a7ac80c587fecf25cf77b6296b686eb5bda3981a7c70a05cd10fb6` |
| `phase10-multiarch-proof-arm64.elf` | 2,416 | `f7d1db8eda22c67e7cb9ed5224a30594a946978456712736e5dec03dc9c77ba0` |
| `phase10-multiarch-proof-amd64.elf` | 2,408 | `22634854394e7844ee749ccb1bbadfee7b046edd52601d689e9437ce2aadfe5e` |
| `ramdisk.img` | 67,108,864 | `dff052b7c7027f01f3ca28d17a18f6bbd983b1db61c85aea8779b2f08e2e4fd1` |

These are Server foundation artifacts and do not contain an adapted Developer Studio package.

## Remaining work and next phase

1. Implement and hosted-test AARCH64 Phase-9 rendering callbacks for the existing SDK slots at offsets 32 and 40, using the kernel's actual app window surface. If the chosen design is a widget-based Developer Studio renderer instead, scope and test that as a separate adaptation.
2. Re-audit the 400-byte table and callback signatures after the renderer work. Do not add fields or interpret v0.5 offsets.
3. Create an explicit Developer Studio AARCH64 SDK profile and source overlay, then adapt unsupported document activation and debugger paths without fabricating callbacks.
4. Resolve the source/API differences in Build, Run, Debug, and error types using real AARCH64 definitions and compile-time assertions.
5. Fix or separately account for the pinned build script's existing run-controller link failure.
6. Run the full hosted and ARM64 build gates and generate the package only after all mandatory services and tests pass.
7. Keep guest execution queued for a later authorized QAC phase; no QEMU or physical hardware was used here.

During this audit, a separate local commit `85f6179e90887460e23b1874d0eddcc1ed735782` advanced the same branch from the requested starting HEAD. It adds the SDK include-provenance check to the existing alignment script and a follow-up note to the ABI1 report. It was preserved and is not part of this ABI2 documentation change. The ABI1 alignment script run recorded above includes the AARCH64 include-dependency check.

The following additional untracked outputs appeared after initial preflight and were preserved: `out/aarch64-gicv3-host-verification-20261101/`, `out/aarch64-phase12-verification-20261101/`, `out/phase12-sdk-alignment-verification-20261101/`, and `out/phase12-sdk-alignment-verification2-20261101/`. This audit added `out/phase12-sdk-alignment-abi2-audit-20261101/`, `out/aarch64-phase12-abi2-audit-20261101/`, and `out/aarch64-gicv3-abi2-audit-20261101/`. The blocked wrapper output path was not created.
## Changes and commit

No SDK, runtime, Developer Studio source, build script, or test changes were made. This report and a status note in `AARCH64_PHASE12_DEVELOPER_STUDIO_IDE.md` are the only intended tracked changes for this audit. No Developer Studio overlay patch was created because the mandatory renderer dependency remains unresolved.

## RENDER1 follow-up (2026-11-02)

The ABI2 outcome above is the historical result of the 2026-11-01 audit. The
renderer dependency has since been implemented and qualified in
[`AARCH64_RENDER1_HOST_GUI_FOUNDATION.md`](AARCH64_RENDER1_HOST_GUI_FOUNDATION.md):
the AARCH64 runtime now publishes the existing `draw_text` and `draw_rect`
slots only for a valid live drawing context, and the callbacks render to
generation-checked per-window surfaces. Hosted pixel tests, a 100-cycle
lifecycle test, and the freestanding ARM64 foundation build pass. This resolves
the two missing GUI drawing services only. It does not resolve the other
Developer Studio SDK incompatibilities listed above, and it does not unblock or
generate the Phase-12 Developer Studio package; the `PHASE12_SDK_CONTRACT_BLOCKED`
guard remains active.

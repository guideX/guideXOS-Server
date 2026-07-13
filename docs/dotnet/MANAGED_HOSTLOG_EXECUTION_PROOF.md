# NativeAOT managed-code execution proof

## 1. Objective

Validate the narrow proof path where guideXOS Server accepts a staged NativeAOT-managed ELF payload and executes the generated `ManagedMain` entry through the existing native ELF/App Model infrastructure.

## 2. Scope and explicit non-goals

This milestone is limited to:

- Static NativeAOT artifact generation.
- ABI and calling-convention compatibility review.
- Opt-in staging of the final ELF plus minimal manifest metadata.
- Proof harness preparation for launch and repeat-launch testing.

This milestone does not prove:

- Full .NET runtime initialization.
- Garbage collection.
- Managed allocation beyond the generated proof methodâ€™s stack use.
- Exceptions, threads, thread pool, reflection, `System.IO`, `System.Console`, or general CoreLib compatibility.
- General portable `.dll` execution.

## 3. Prior artifact baseline

The prior successful proof artifacts showed:

- Final ELF shape: `ELF64`, little-endian, AMD64, `ET_EXEC`.
- Entry point: `0x10001900`.
- Six `PT_LOAD` segments.
- No `PT_INTERP`.
- No dynamic section.
- No relocations.
- No section-header table.

The current clean rebuild reproduced the same layout and entry address, with the proof envelope now recording the rebuilt ELF hash:

- `0F68F13374C30E84308454908344EA1AE5A70136A9F22D91A12FFA7396F9291C`

## 4. Existing Server launch path used

The narrow working path is already present in Server:

- `desktop_service.cpp`
  - `DesktopService::InspectNativeAppPipeline`
  - `DesktopService::NativeAppPipelineSmokeTest`
  - `launchNativeElfProcess`
- `native_elf_executor.cpp`
  - `NativeElfExecutor::CanExecute`
  - `NativeElfExecutor::Execute`
- `native_elf_image_loader.cpp`
  - `NativeElfImageLoader::LoadImage`
- `native_elf_launch_pipeline.cpp`
  - `NativeElfLaunchPipeline::PrepareLaunch`
- `native_elf_trampoline_win64.cpp`
  - `CallNativeElfWin64Entry`

The proof is discovered through an opt-in registry source root:

- `app_registry.cpp`
  - `AppRegistry::DefaultSources()`
  - `experimentalSourcesFromEnvironment()`

The smoke path chosen is `nativeapp.smoketest`, which routes through `InspectNativeAppPipeline` and only executes when the experimental executor gate passes.

## 5. Managed and native ABI comparison

Managed proof source:

- `samples/managed/HostLogProof/Program.cs`
  - `Program.ManagedMain(NativeGxAppContext* ctx)`
  - `Program.Main()` is empty.
- `samples/managed/HostLogProof/NativeAbi.cs`
  - `GxAbi.ApiVersion = 0`
  - `GxAbi.ErrorInvalidArgument = -2`
  - `GxAbi.ErrorUnsupported = -3`
  - `NativeHostCallTable`
  - `NativeGxAppContext`

Server-side ABI source:

- `native_app_runtime.h`
  - `kGuideXOSNativeApiVersion = 0u`
  - `kGuideXOSNativeAbiName = "guidexos-c-abi-v1"`
  - `NativeHostCallTable`
  - `NativeGxAppContext`
- `sdk/include/guidexos/abi.h`
  - `GX_API_VERSION = 0u`
  - `GX_ABI_NAME = "guidexos-c-abi-v1"`
  - `GX_CALL` expands to `__attribute__((ms_abi))` on x86_64.

The proof and Server ABI contracts align on:

- AMD64.
- 32-bit `size` and `apiVersion` fields.
- Pointer-sized `host` and `userData`.
- Host `log` callback signature `log(ctx, message)`.

## 6. Calling-convention findings

Findings from `out/dotnet/managed-hostlog/artifacts/HostLogProof.native.objdump.txt` and the server ABI headers:

- `ManagedMain` receives its first argument in `rcx`, which is Microsoft x64 ABI behavior.
- The generated function preserves nonvolatile registers and allocates Windows shadow space.
- The NativeAOT body loads the context from `rbx`, then reads the host table and indirects through the `log` callback.
- The host callback table in `sdk/include/guidexos/abi.h` uses `GX_CALL`, which maps to Microsoft x64 on AMD64.
- `CallNativeElfWin64Entry` in `native_elf_trampoline_win64.cpp` explicitly moves the context into `rcx` and reserves 40 bytes of shadow space before calling the entry.
- The return value is carried back in `eax`/`rax`, matching `gx_result`.
- No adapter was required beyond the existing Windows AMD64 trampoline.

Field layout alignment:

- `NativeGxAppContext.size` at offset `0`.
- `NativeGxAppContext.apiVersion` at offset `4`.
- `NativeGxAppContext.host` at offset `8`.
- `NativeGxAppContext.userData` at offset `16`.
- `NativeHostCallTable.size` at offset `0`.
- `NativeHostCallTable.version` at offset `4`.
- `NativeHostCallTable.log` at offset `8`.

## 7. Adapter design

No new adapter was added.

Reason:

- The managed entry is already Microsoft x64 compatible.
- Serverâ€™s existing `CallNativeElfWin64Entry` trampoline already supplies the required calling convention and shadow space.
- The proof entry and host table share the same ABI contract.

## 8. Artifact staging

New opt-in staging path:

- `scripts/dotnet/stage-managed-hostlog-proof.ps1`

Staging details:

- Build is rerun from a clean output root.
- Existing artifact assertions are rerun against the regenerated proof outputs.
- The final ELF is copied into an isolated stage tree.
- Minimal manifest metadata is written beside the staged app.
- The app registry only sees the staged source when `GXOS_NATIVE_ELF_STAGE_ROOT` is set.

Staged tree:

- `out/dotnet/managed-hostlog/stage-managed-hostlog-proof/apps/ManagedHostLogProof/app.json`
- `out/dotnet/managed-hostlog/stage-managed-hostlog-proof/apps/ManagedHostLogProof/bin/amd64/HostLogProof.elf`
- `out/dotnet/managed-hostlog/stage-managed-hostlog-proof/proof/proof-envelope.json`

The stage envelope records the current rebuilt ELF hash:

- `0F68F13374C30E84308454908344EA1AE5A70136A9F22D91A12FFA7396F9291C`

## 9. Context and host-table construction

Server runtime construction path:

- `native_app_runtime.cpp`
  - `NativeAppRuntime::Prepare`
  - `NativeAppRuntime::BeginHostCallDispatch`
  - `NativeAppRuntime::EndHostCallDispatch`
  - `hostLog`

Construction details:

- `NativeAppRuntime::Prepare` fills `NativeGxAppContext` with:
  - `size = sizeof(NativeGxAppContext)`
  - `apiVersion = kGuideXOSNativeApiVersion`
  - `host = &runtimeContext.hostCalls`
  - `userData = nullptr`
- `NativeAppRuntime::Prepare` populates the host table, including `hostLog`.
- `runtimeContextFor()` validates:
  - non-null context,
  - non-null host table,
  - minimum context size,
  - active runtime table match.
- `hostLog`:
  - increments the host log call count,
  - records the last message,
  - writes the message to Server logging,
  - returns `GX_OK`.

## 10. Execution trace

Blocked before live execution.

Validation attempted:

- `build-native-experimental.bat`
- `build.bat`

Both fail in this workspace with the same missing dependency:

- `png_loader.cpp:61:10: fatal error: third_party/stb/stb_image.h: No such file or directory`

Because the Server executable could not be built, the smoke harness could not launch `nativeapp.inspect` or `nativeapp.smoketest` against a running Server process.

## 11. Managed-message integrity evidence

Static evidence from the managed source and generated NativeAOT object shows the message is produced by the managed method itself:

- `Program.ManagedMain` builds `ReadOnlySpan<byte>` from the UTF-8 literal `Hello from managed guideXOS code`.
- The message is copied into stack memory with `stackalloc`.
- The method NUL-terminates the stack buffer and passes the pointer to `ctx->host->log`.
- The object disassembly shows the buffer copy and indirect `call *%rsi` through the host log pointer.

This proves the message origin is managed-generated data, not launcher-side hardcoding.

## 12. Return-value evidence

Static return path:

- Managed code returns the integer produced by `ctx->host->log(...)`.
- The server host log callback returns `GX_OK` on success.
- The NativeAOT function returns via `eax`, and the Server executor captures that as the native exit code.

Because Server could not be launched in this workspace, this remained a static proof rather than a live execution trace.

## 13. Failure-case results

Not executed live, because the Server binary could not be built.

The smoke harness is prepared to test:

- Unstaged app lookup failure.
- Repeat launch in the same Server process.
- Process-table cleanup after each run.
- Host-log count and return-code assertions.

## 14. Repeat-launch and cleanup results

Not executed live, because the Server binary could not be built.

The smoke harness is prepared to prove repeat launch by:

- Running `nativeapp.smoketest` twice in one Server session.
- Verifying `runtimeId` increases.
- Verifying the process table ends with exited records, not a live running slot.

## 15. Hosted-mode results

Blocked.

Hosted Server build could not complete because of the missing `third_party/stb/stb_image.h` dependency. No hosted execution trace was possible.

## 16. QEMU/bare-metal results

Blocked.

The normal build is unavailable in this workspace for the same missing dependency, so there is no fresh experimental image to boot under QEMU or bare metal.

## 17. Files changed

Source and script changes:

- `app_registry.cpp`
- `scripts/dotnet/stage-managed-hostlog-proof.ps1`
- `scripts/smoke-dotnet-managed-hostlog-execution.ps1`

Documentation added:

- `docs/dotnet/MANAGED_HOSTLOG_EXECUTION_PROOF.md`

Generated outputs remained under `out/` and are not tracked.

## 18. Remaining limitations

- Server build is blocked by the missing `third_party/stb/stb_image.h`.
- The workspace may also need the previously noted `third_party/mbedtls` sources if later code paths are exercised.
- No live execution has been demonstrated yet.
- No repeat-launch or cleanup trace has been captured yet.
- No QEMU/bare-metal validation was possible.

## 19. Exact next experiment

Restore the missing canonical third-party dependencies needed for the stock Server build, then rerun:

1. `build-native-experimental.bat`
2. `scripts/smoke-dotnet-managed-hostlog-execution.ps1`

If the Server build succeeds, the next expected live trace is:

- `nativeapp.capabilities`
- `nativeapp.smoketest com.guidexos.experimental.nativeaot.hostlogproof`
- `nativeapp.smoketest com.guidexos.experimental.nativeaot.hostlogproof`
- `nativeapp.processes`

## 20. What this milestone proves

This milestone proves that:

- The managed `ManagedMain` body is real NativeAOT-generated AMD64 code.
- The direct proof path is ABI-compatible with Serverâ€™s native context and host table.
- The proof ELF is a fixed-address `ET_EXEC` image at `0x10001900`.
- The staged artifact can be isolated outside the default app inventory.
- Server-side runtime preparation and host-call wiring are already aligned for the proof.

## 21. What this milestone does not prove

This milestone does not prove:

- Live Server execution of the managed payload.
- GC initialization or managed heap behavior.
- Static constructor behavior.
- Exception handling.
- Threading or thread pool support.
- General .NET application compatibility.
- Windows import runtime initialization.
- QEMU or bare-metal runtime success.


## 22. Execution attempt update

### 22.1 Dependency source and restoration method

The missing third-party dependencies were restored from canonical sibling trees, not from current package-manager releases:

- `third_party/stb/stb_image.h` was copied from `D:\dev\guideXOSServerV0.2\third_party\stb\stb_image.h`.
- `third_party/mbedtls/**` was copied from `D:\dev\guideXOSServer\third_party\mbedtls` at commit `545d1b77a29ac33b219a6681489d5e63b63c3b3a`.

The restoration method was direct vendor-tree copying because this workspace has no root `.gitmodules` entry for those dependencies and the exact branch revision is not using submodule checkout for them.

### 22.2 Exact dependency revisions

- `stb_image.h` identifies `stb_image v2.30` and the source hash was `1F8C1B6B408F26E3B20CBFBBD4758AFB3DC9B837FF1E17C258928F406148A87C`.
- `mbedtls` reports `4.1.0` in `include/mbedtls/build_info.h`.
- The sibling tree was described as `v4.1.0-84-g545d1b77a2-dirty`, which is the closest recorded revision metadata available in this workspace.

### 22.3 Registry-hook review and simplification

`app_registry.cpp` was reviewed but not changed in this pass.

The opt-in hook remains narrow and inert by default:

- It only activates when `GXOS_NATIVE_ELF_STAGE_ROOT` is present.
- It trims whitespace and accepts semicolon-delimited roots.
- It returns no extra sources when the environment variable is absent.
- It does not alter the default built-in application inventory.
- It stays confined to the experimental staged-source discovery path.

No further simplification was necessary.

### 22.4 Hosted Server build results

Both server builds were rerun earlier in the pass and completed successfully:

- `build-native-experimental.bat` passed.
- `build.bat` passed.

### 22.5 Managed artifact rebuild and staging results

The proof artifact was rebuilt and staged successfully after the converter fix.

Key evidence from the latest successful run:

- `build-managed-hostlog-proof.ps1 -Clean` passed.
- `stage-managed-hostlog-proof.ps1` passed.
- Source and staged ELF hashes matched: `CDE78AD872795394D212E3AE0FCC9863B643C5D0B9414DAC4302D3A1A3AE0BF5`.
- `readelf` reports `ELF64`, `ET_EXEC`, AMD64, entry `0x10001900`, and `Number of program headers: 7`.
- The first `LOAD` segment now starts at `0x10000000`, which restores the preferred-base mapping envelope required by the hosted loader.

### 22.6 Default-isolation result

Default isolation held.

- With no stage root set, `nativeapp.inspect com.guidexos.experimental.nativeaot.hostlogproof` reported `Result: app not found`.
- With `GXOS_NATIVE_ELF_STAGE_ROOT` pointed at a non-existent directory, the same result occurred.
- The normal inventory remained unchanged apart from the expected omission of the staged proof app when the stage root was absent or invalid.

### 22.7 First launch trace

The first hosted launch succeeded in the same Server process.

Observed evidence:

- `NativeElfLaunchPipeline::PrepareLaunch` accepted the artifact.
- `NativeElfImageLoader::LoadImage` loaded the ELF.
- The image loader reported `EntryVA: 0x10001900` and `preferredBase: 0x10000000`.
- `NativeElfExecutor::CanExecute` returned true.
- `NativeAppRuntime` entered host-call dispatch for runtimeId `1`.
- The host callback logged `Hello from managed guideXOS code`.
- The executor diagnostics reported `Preferred-base mapping: success` and `Native ELF gx_main returned 0`.

### 22.8 First return result

The first run returned `0`.

### 22.9 Second launch trace

The second hosted launch also succeeded in the same Server process.

Observed evidence:

- `runtimeId` advanced to `2`.
- The loader again reported `EntryVA: 0x10001900` and `preferredBase: 0x10000000`.
- The host callback again logged `Hello from managed guideXOS code`.
- The executor diagnostics again reported `Preferred-base mapping: success` and `Native ELF gx_main returned 0`.

### 22.10 Second return result

The second run returned `0`.

### 22.11 Rejection-test results

Safe rejection cases supported by the current harness were exercised:

- Stage root absent: PASS. The preflight run reported `Result: app not found`.
- Stage root invalid: PASS. A deliberately invalid stage root also reported `Result: app not found`.
- Proof artifact absent: PASS. A disposable temp copy with `HostLogProof.elf` removed reported `validationSuccess: false`, `imageLoaderSuccess: false`, `executionSuccess: false`, and `Result: inspection complete; no ELF code executed`.
- Metadata absent or invalid: PASS. A disposable temp copy with `app.json` removed reported `Result: app not found`, and a malformed JSON manifest produced a manifest parse failure before lookup.
- Artifact hash mismatch: BLOCKED. The current harness does not expose a safe hash-injection path for this branch without altering the loader or staging contract.
- Unsupported API version: BLOCKED. This would require rebuilding broader Server code to inject a mismatched API version.
- Null logging callback: BLOCKED. The existing proof harness does not safely expose that injection point.

### 22.12 Managed-message integrity evidence

The message originated in managed code and was delivered through the host callback:

- The server log shows `NativeAppHost ... log: Hello from managed guideXOS code`.
- The executor diagnostics record `Host log call count: 1` and `Last host log message: Hello from managed guideXOS code` for each launch.
- The message is the exact UTF-8 text built by `Program.ManagedMain`.

### 22.13 Any crash or failure diagnosis

The earlier hosted failure was a preferred-base mapping problem, not a managed ABI problem.

Root cause and fix:

- The regenerated ELF had started its first `PT_LOAD` at `0x10001000`, which prevented exact preferred-base mapping.
- The PE-to-ELF converter was restored to emit an initial reserved `PT_LOAD` page at `0x10000000`.
- After that change, hosted `VirtualAlloc` mapping succeeded at the preferred base and the managed payload executed.

A secondary harness issue also surfaced during validation:

- The smoke script originally anchored regexes against CRLF text and expected camelCase host-log labels that the executor did not emit.
- The harness was adjusted to normalize carriage returns and to match the actual host-log wording.

### 22.14 QEMU status

Not attempted.

Hosted execution is now the proven target, but QEMU/bare-metal was not exercised in this pass.

### 22.15 Updated decision outcome

Outcome A.

The actual NativeAOT-generated `ManagedMain` executed, the host callback emitted the expected message, the real return value reached Server, and both launches succeeded in one Server process.

### 22.16 What is now proven

- The canonical staged proof path can be restored from the recorded vendor sources.
- The hosted Server build remains healthy.
- The staged NativeAOT ELF loads at the expected fixed base.
- The managed entry executes through the existing native ELF trampoline.
- The host log callback receives the managed UTF-8 message.
- The integer return code is captured correctly.
- Repeat launch works in the same Server process.

### 22.17 What remains unproven

- A general .NET runtime layer.
- Managed allocation beyond the stack-only proof.
- GC integration.
- Threading, exceptions, reflection, and general CoreLib support.
- QEMU/bare-metal execution for this proof.

### 22.18 Exact next experiment

Build a minimal guideXOS NativeAOT runtime/platform layer that supports one managed allocation while retaining the proven entry ABI, host callback, and opt-in packaging path.

### 22.19 Current status correction

Sections 22.15–22.18 preserve the historical successful-run report and its original Outcome A label. They are not a current reproducibility claim. The clean-build regression and exact reverse-P/Invoke fault are documented in [NATIVEAOT_REVERSE_PINVOKE_ENTRY_DIAGNOSIS.md](NATIVEAOT_REVERSE_PINVOKE_ENTRY_DIAGNOSIS.md); the current final status is Outcome C, and allocation is not authorized.

The historical run demonstrates generated managed-body instructions and the guideXOS host ABI. It does not, by itself, demonstrate a runtime-correct reverse-P/Invoke transition, thread attachment, GC safety, or allocation safety.


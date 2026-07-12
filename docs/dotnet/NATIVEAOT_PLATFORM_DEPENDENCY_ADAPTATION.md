# NativeAOT Platform Dependency Adaptation

## 1. Executive Summary

This pass narrowed the managed proof to a direct NativeAOT entry that is genuinely managed C# at the machine-code level, but the stock Windows-targeted NativeAOT runtime pack still drags in a broad PE import surface.

The important split is now clear:

- `ManagedMain` is the real entry used by the proof build.
- The final ELF is a fixed-address, static `ELF64` image with no `PT_INTERP`, no dynamic section, and no relocations.
- The Windows imports live in the intermediate PE/runtime pack and are mostly startup, error, or feature baggage rather than the direct `ManagedMain` path.
- The old guideXOS C# runtime can inform a narrow host ABI and a few reusable runtime ideas, but it cannot be transplanted as a kernel-facing runtime layer.

Selected strategy: **Strategy B** for the platform/toolchain direction, with a narrow A-style host ABI reuse layer. The outcome remains **B** because the platform layer is identified but still incomplete: the proof is ready as a loader candidate, yet the wider freestanding packaging story still depends on the Windows NativeAOT pack and PE-to-ELF staging.

Evidence files:

- [Managed proof doc baseline](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/docs/dotnet/MANAGED_HOSTLOG_ARTIFACT_PROOF.md)
- [Reuse audit](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/docs/dotnet/GUIDEXOS_CSHARP_NATIVEAOT_REUSE_AUDIT.md)
- [Import inventory dump](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.pe.objdump.txt)
- [Native object disassembly](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.native.objdump.txt)
- [Final ELF readelf](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.readelf.txt)

## 2. Previous Outcome B Baseline

The previous pass already established that:

- NativeAOT produced real managed native code.
- The managed body calls the host log table indirectly.
- The output ELF is `ELF64`, little-endian, AMD64, and `ET_EXEC`.
- The final artifact is structurally static, with six `PT_LOAD` segments and no ELF dynamic dependencies.
- The PE-to-ELF converter is still a staging copier, not a semantic linker.

This pass did not overturn that baseline. It refined the evidence chain so we can say why the PE imports exist and which parts are actually on the direct proof path.

## 3. Complete PE Import Inventory

Source: [HostLogProof.pe.objdump.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.pe.objdump.txt)

### ADVAPI32.dll

| DLL | Imported symbol | Ordinal/name | Referencing code/data | Likely purpose | Reachability status |
| --- | --- | --- | --- | --- | --- |
| ADVAPI32.dll | `RegisterEventSourceW` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Advapi32__RegisterEventSource` | Event-log registration | U |
| ADVAPI32.dll | `ReportEventW` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Advapi32__ReportEvent` | Event-log emission | U |
| ADVAPI32.dll | `DeregisterEventSource` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Advapi32__DeregisterEventSource` | Event-log cleanup | U |

### bcrypt.dll

| DLL | Imported symbol | Ordinal/name | Referencing code/data | Likely purpose | Reachability status |
| --- | --- | --- | --- | --- | --- |
| bcrypt.dll | `BCryptGenRandom` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop__GetRandomBytes`, `S_P_CoreLib_System_Random_XoshiroImpl___ctor` | Runtime random seeding | U |

### KERNEL32.dll

| DLL | Imported symbol | Ordinal/name | Referencing code/data | Likely purpose | Reachability status |
| --- | --- | --- | --- | --- | --- |
| KERNEL32.dll | `CloseHandle` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Kernel32__CloseHandle` | Thread-handle cleanup | P |
| KERNEL32.dll | `CreateEventExW` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_EventWaitHandle__CreateEventCore` | Event creation | P |
| KERNEL32.dll | `DuplicateHandle` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Kernel32__DuplicateHandle` | Thread-handle duplication | P |
| KERNEL32.dll | `FormatMessageW` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Kernel32__GetMessage_0` | Win32 message formatting | P |
| KERNEL32.dll | `GetConsoleOutputCP` | name | `HostLogProof.obj` / console helpers | Console encoding setup | U |
| KERNEL32.dll | `GetCurrentProcess` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__GetOSHandleForCurrentThread` | Current-process pseudo-handle | P |
| KERNEL32.dll | `GetCurrentProcessorNumberEx` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__InitializeCurrentThread` | Processor-aware thread init | P |
| KERNEL32.dll | `GetCurrentThread` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__GetOSHandleForCurrentThread` | Current-thread pseudo-handle | P |
| KERNEL32.dll | `GetEnvironmentVariableW` | name | `HostLogProof.obj` / `S_P_CoreLib_System_AppContextConfigHelper__GetInt16Config_0` | AppContext/config lookup | U |
| KERNEL32.dll | `GetLastError` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Kernel32__GetMessage_0` | Win32 error plumbing | P |
| KERNEL32.dll | `GetModuleFileNameW` | name | `HostLogProof.obj` / path/config helpers | Module-path lookup | U |
| KERNEL32.dll | `GetStdHandle` | name | `HostLogProof.obj` / console helpers | Console handle lookup | U |
| KERNEL32.dll | `GetThreadPriority` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__InitializeCurrentThread` | Thread priority query | P |
| KERNEL32.dll | `GetTickCount64` | name | `Runtime.WorkstationGC:PalRedhawkCommon.cpp.obj` / `PalGetTickCount64` | Tick counter | P |
| KERNEL32.dll | `IsDebuggerPresent` | name | `HostLogProof.obj` / debug helpers | Debugger check | U |
| KERNEL32.dll | `LocalFree` | name | `HostLogProof.obj` / `S_P_CoreLib_Interop_Kernel32__GetMessage_0` | Error-string cleanup | P |
| KERNEL32.dll | `MultiByteToWideChar` | name | `HostLogProof.obj` / message helpers | String conversion | U |
| KERNEL32.dll | `QueryPerformanceCounter` | name | `Runtime.WorkstationGC:gcenv.windows.cpp.obj` / `GCToOSInterface` | High-resolution timing | P |
| KERNEL32.dll | `QueryPerformanceFrequency` | name | `Runtime.WorkstationGC:gcenv.windows.cpp.obj` / `GCToOSInterface` | High-resolution timing | P |
| KERNEL32.dll | `RaiseFailFastException` | name | `Runtime.WorkstationGC:EHHelpers.cpp.obj` / `RhpFallbackFailFast`, `S_P_CoreLib_System_Runtime_EH__FailFastViaClasslib` | Fail-fast path | P |
| KERNEL32.dll | `SetEvent` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_EventWaitHandle__Set` | Event signaling | P |
| KERNEL32.dll | `SetLastError` | name | `HostLogProof.obj` / interop helpers | Error plumbing | P |
| KERNEL32.dll | `Sleep` | name | `Runtime.WorkstationGC:PalRedhawkMinWin.cpp.obj` / `PalSleep`, `S_P_CoreLib_System_Threading_Thread__Sleep` | Sleep/yield | P |
| KERNEL32.dll | `VirtualAlloc` | name | `Runtime.WorkstationGC:PalRedhawkMinWin.cpp.obj` / `PalVirtualAlloc` | GC/runtime allocation | P |
| KERNEL32.dll | `VirtualFree` | name | `Runtime.WorkstationGC:PalRedhawkMinWin.cpp.obj` / `PalVirtualFree`, `S_P_CoreLib_Internal_Runtime_FrozenObjectHeapManager__ClrVirtualFree` | GC/runtime release | P |
| KERNEL32.dll | `WaitForMultipleObjectsEx` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_WaitHandle__WaitForMultipleObjectsIgnoringSyncContext` | Wait-handle support | P |
| KERNEL32.dll | `WideCharToMultiByte` | name | `HostLogProof.obj` / message helpers | String conversion | U |
| KERNEL32.dll | `WriteFile` | name | `HostLogProof.obj` / `S_P_CoreLib_Internal_Console__WriteCore` | Console/file output | U |
| KERNEL32.dll | `RtlCaptureContext` | name | `HostLogProof.obj` / stack-trace and EH helpers | Stack capture | P |
| KERNEL32.dll | `FlsGetValue` | name | `HostLogProof.obj` / thread-statics helpers | Fiber-local storage | P |
| KERNEL32.dll | `FlsSetValue` | name | `HostLogProof.obj` / thread-statics helpers | Fiber-local storage | P |
| KERNEL32.dll | `SwitchToThread` | name | `Runtime.WorkstationGC:PalRedhawkMinWin.cpp.obj` / `PalSwitchToThread` | Cooperative yield | P |
| KERNEL32.dll | `GetCurrentThreadId` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_ManagedThreadId__GetCurrentThreadId` | Thread identity | P |
| KERNEL32.dll | `VirtualQuery` | name | `HostLogProof.obj` / GC / memory helpers | Memory probing | P |
| KERNEL32.dll | `EnterCriticalSection` | name | `HostLogProof.obj` / runtime lock helpers | Locking | P |
| KERNEL32.dll | `LeaveCriticalSection` | name | `HostLogProof.obj` / runtime lock helpers | Locking | P |

### ole32.dll

| DLL | Imported symbol | Ordinal/name | Referencing code/data | Likely purpose | Reachability status |
| --- | --- | --- | --- | --- | --- |
| ole32.dll | `CoGetApartmentType` | name | `HostLogProof.obj` / thread apartment helpers | COM apartment query | P |
| ole32.dll | `CoInitializeEx` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__InitializeCom` | COM apartment init | P |
| ole32.dll | `CoUninitialize` | name | `HostLogProof.obj` / `S_P_CoreLib_System_Threading_Thread__UninitializeCom` | COM apartment teardown | P |
| ole32.dll | `CoWaitForMultipleHandles` | name | `HostLogProof.obj` / COM wait helpers | COM wait integration | P |

### api-ms-win-crt-heap-l1-1-0.dll

| DLL | Imported symbol | Ordinal/name | Referencing code/data | Likely purpose | Reachability status |
| --- | --- | --- | --- | --- | --- |
| api-ms-win-crt-heap-l1-1-0.dll | `free` | name | `Runtime.WorkstationGC.lib` / CRT heap support | Heap release | P |
| api-ms-win-crt-heap-l1-1-0.dll | `_callnewh` | name | `LIBCMT.lib(tlssup.obj)` / CRT heap support | New-handler hook | U |
| api-ms-win-crt-heap-l1-1-0.dll | `malloc` | name | `Runtime.WorkstationGC.lib` / CRT heap support | Heap allocation | P |

## 4. Contributor/Object Analysis

The imports are not "from the managed app" in a simple sense. They come from a mix of the ILCompiler-generated object and the stock runtime libraries.

| Contributor object/library | Imports or helpers introduced | Notes |
| --- | --- | --- |
| `HostLogProof.obj` | `ManagedMain`, `MainMethodWrapper`, `StartupCodeMain`, thread helpers, `GetMessage_0`, `FailFastViaClasslib`, `GetRandomBytes`, `Thread__InitializeCurrentThread`, `Thread__GetOSHandleForCurrentThread`, `EventWaitHandle__CreateEventCore`, `WaitHandle__WaitForMultipleObjectsIgnoringSyncContext` | This is the main NativeAOT object that still contains both the selected entry and the stock startup/runtime scaffolding. |
| `Runtime.WorkstationGC:PalRedhawkMinWin.cpp.obj` | `VirtualAlloc`, `VirtualFree`, `Sleep`, `SwitchToThread` | GC/runtime PAL for Windows. |
| `Runtime.WorkstationGC:PalRedhawkCommon.cpp.obj` | `GetTickCount64` | Common PAL timing helper. |
| `Runtime.WorkstationGC:gcenv.windows.cpp.obj` | `QueryPerformanceCounter`, `QueryPerformanceFrequency` | GC timing implementation. |
| `Runtime.WorkstationGC:EHHelpers.cpp.obj` | `RhpFallbackFailFast` and the `RaiseFailFastException` path | Fail-fast implementation, not normal control flow. |
| `Runtime.WorkstationGC:thread.cpp.obj` | Thread P/Invoke return-address helpers and thread bookkeeping | Supports thread identity and OS handle management. |
| `LIBCMT.lib(tlssup.obj)` and the CRT heap import library | `malloc`, `free`, `_callnewh` | CRT heap plumbing retained by the Windows runtime pack. |
| `HostLogProof.obj` event/logging helpers | `RegisterEventSourceW`, `ReportEventW`, `DeregisterEventSource` | Appears to be telemetry/event-source baggage, not part of the proof path. |

## 5. Reachability Classification

The actual direct proof path is:

`ManagedMain` -> host table null checks -> UTF-8 stack buffer -> indirect `ctx->host->log` call -> integer return.

No Windows import thunk is on that direct path.

Classification used here:

- `R`: confirmed reachable on the selected `ManagedMain` path.
- `P`: probably reachable through the stock NativeAOT startup/runtime or fail-fast path that is present in the module.
- `U`: present in the PE but not on the selected proof path and not needed for this candidate.

In this proof:

- `R` is effectively empty for Windows imports.
- `P` covers the runtime/startup/GC/COM/heap helpers that the stock NativeAOT pack still roots.
- `U` covers event-log, random, console, and string-conversion imports that are present in the PE but not needed for the direct proof path.

## 6. guideXOS C# Replacement Mapping

Only the imports with a plausible runtime need are mapped here. The old guideXOS C# tree is useful, but only in a narrow form.

| Windows import | Purpose | guideXOS C# equivalent | Reusable? | Required adaptation |
| --- | --- | --- | --- | --- |
| `GetCurrentThreadId` | thread identity | `D:\dev\guideXOS\Corlib\Internal\Runtime\CompilerHelpers\StartupCodeHelpers.cs:8-9` (`__imp_GetCurrentThreadId` returns 0) | Partially | Keep the tiny runtime stub shape, but do not assume Win32 thread IDs. |
| `GetThreadPriority` | thread startup bookkeeping | `D:\dev\guideXOS\Kernel\Misc\Threading.cs` and `SynchronizedMethodHelpers.cs` show custom thread/sync mechanics, but no priority API | No | Replace with a host/runtime-neutral thread state query or drop the dependency. |
| `DuplicateHandle` / `GetCurrentProcess` / `GetCurrentThread` / `CloseHandle` | thread-handle management | No direct equivalent in the reusable legacy runtime | No | Replace with opaque host-managed handles or remove the need for OS handles. |
| `CreateEventExW` / `SetEvent` / `WaitForMultipleObjectsEx` | event and wait-handle primitives | `SynchronizedMethodHelpers.cs` and `Kernel\Misc\Threading.cs` are the closest semantic shape | Partially | Route waits through a narrow host ABI; do not emulate Win32 handles in Server. |
| `CoInitializeEx` / `CoUninitialize` / `CoWaitForMultipleHandles` | COM apartment setup | No direct equivalent | No | Avoid COM entirely in the proof path. |
| `GetLastError` / `LocalFree` / `GetTickCount64` | minimal Win32 shim support | `D:\dev\guideXOS\guideXOS\Kernel\Win32Shim.cs:22,24,27,77,95,110` | Yes, for the tiny subset | Keep the isolated shim idea, but only for the small set that is actually needed. |
| `VirtualAlloc` / `VirtualFree` | runtime allocation / release | `StartupCodeHelpers.RhpNewFast` / `RhpNewArray` and `System.Object.Dispose()` (`malloc` / `free`) | Partially | Adapt to a host allocator or arena; do not expose kernel assumptions. |
| `QueryPerformanceCounter` / `QueryPerformanceFrequency` / `Sleep` / `SwitchToThread` / `GetTickCount64` | time and scheduling | `Win32Shim.cs` only covers `GetTickCount64`; no direct high-resolution timer abstraction is present | Partially | Add a host-timer abstraction if a runtime service truly needs it. |
| `RaiseFailFastException` | fatal error path | `StartupCodeHelpers.__fail_fast` spins forever; `System.RuntimeExceptionHelpers` is only partially implemented | Partially | Fail fast clearly, do not fake success. |
| `BCryptGenRandom` | random seeding | No direct OS RNG equivalent; the tree only shows `System.Random_XoshiroImpl` / `ThreadSafeRandom` as managed PRNG machinery | No | Use a host RNG hook only if a reachable path proves it is required. |
| `RegisterEventSourceW` / `ReportEventW` / `DeregisterEventSource` | event log plumbing | No practical equivalent in the legacy C# runtime | No | Drop the telemetry path for this proof. |
| `malloc` / `free` / `_callnewh` | heap plumbing | `StartupCodeHelpers.RhpNewFast`, `RhpNewArray`, and `Object.Dispose()` | Partially | Keep the allocator isolated; do not pretend it is a full CRT. |

## 7. Runtime-Library Strategy Selected

Selected strategy: **Strategy B**, with a narrow A-style host ABI boundary.

Why:

- The proof can already express the host contract as a tiny unmanaged context table.
- The old guideXOS C# runtime is useful for a few isolated ideas, but not for transplanting kernel-side threading, COM, or handle management into Server.
- The stock Windows NativeAOT runtime pack is still the source of the PE import baggage.
- A freestanding/runtime-neutral build configuration or custom runtime pack is still the right long-term fix if the goal is to remove the Windows import directory from the intermediate build entirely.

## 8. Runtime Support Files Reused or Created

Used in this pass:

- [samples/managed/HostLogProof/Program.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/Program.cs)
- [samples/managed/HostLogProof/NativeAbi.cs](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/NativeAbi.cs)
- [samples/managed/HostLogProof/runtime_support.c](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/samples/managed/HostLogProof/runtime_support.c)
- [scripts/dotnet/build-managed-hostlog-proof.ps1](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/scripts/dotnet/build-managed-hostlog-proof.ps1)

Created evidence artifacts:

- `HostLogProof.native.objdump.txt`
- `HostLogProof.native.reloc.txt`
- `HostLogProof.ilc.rsp`
- `HostLogProof.link.rsp`

What changed in the sample:

- the host context is validated before dereferencing the host table
- the runtime proof stays on a fixed UTF-8 byte buffer
- the managed entry is still the real NativeAOT output
- the empty `Main()` remains only because the C# compiler requires a static entry stub for the project type

## 9. Managed-Feature Minimization

The proof intentionally avoids:

- managed `System.String` crossing the ABI boundary
- managed heap allocation in the entry path
- dynamic loading
- reflection
- threading APIs on the direct path
- file system or GUI APIs

It keeps only:

- context pointer acceptance
- host table access
- UTF-8 logging
- integer return
- explicit invalid-argument / unsupported failures

## 10. Packaging Changes

The proof build script now validates both the intermediate PE and the final ELF.

It asserts:

- the PE import table stays within the known Windows-runtime allowlist for this proof
- the final ELF has no `PT_INTERP`
- the final ELF has no dynamic section
- the final ELF has no relocations
- the final ELF has no Linux dynamic dependencies such as `libc`, `libpthread`, `libdl`, or `libm`
- the final ELF has no writable-and-executable segment
- the native object still contains the managed `ManagedMain` body and its indirect host-log call

The entrypoint remains fixed-address at `0x10000000`, with `ManagedMain` at `0x10001900` in the current build.

## 11. Final Artifact Inspection

Final ELF properties:

- `ELF64`
- little-endian
- AMD64
- `ET_EXEC`
- entry point `0x10001900`
- six `PT_LOAD` segments
- no section headers
- no dynamic section
- no relocations
- no interpreter
- one `RW` segment with BSS represented in `MemSiz > FileSiz`

Managed-body proof:

- [HostLogProof.native.objdump.txt](D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT/out/dotnet/managed-hostlog/artifacts/HostLogProof.native.objdump.txt) shows `HostLogProof_HostLogProof_Program__ManagedMain`
- the function reads the context, checks the host table, constructs the UTF-8 stack buffer, and ends with `call *%rsi`
- the map file places `ManagedMain` at `0x10001900`

## 12. Remaining Imports or Unresolved Symbols

There are no remaining unresolved symbols in the final ELF.

What remains is the intermediate PE import surface:

- `ADVAPI32.dll`
- `bcrypt.dll`
- `KERNEL32.dll`
- `ole32.dll`
- `api-ms-win-crt-heap-l1-1-0.dll`

Those imports are not on the selected direct `ManagedMain` path. They are retained by the stock Windows NativeAOT runtime pack and the standard startup/error helpers that the module still contains.

## 13. Runtime Startup Assumptions

The proof assumes:

- the host supplies a valid `NativeGxAppContext*`
- `ctx->size` is large enough for the selected ABI shape
- `ctx->apiVersion` matches `GxAbi.ApiVersion`
- `ctx->host` and `ctx->host->log` are valid for the duration of the call
- the host may read the UTF-8 buffer only during the call
- the direct entry is `ManagedMain`, not the empty `Main()`
- the fixed load address remains acceptable for the experimental loader path

## 14. Loader Compatibility Status

Current status:

- The ELF shape is loader-friendly for a focused experiment.
- The loader path still needs to respect the fixed base and the static `ET_EXEC` layout.
- The packaging pipeline is not yet a production-grade direct ELF link path.
- The current proof is credible as an isolated loader candidate, but not yet as the final application format.

## 15. Risks and Unsupported Behavior

Risks:

- the intermediate PE still carries Windows imports
- the Windows-targeted NativeAOT pack still roots startup and fail-fast baggage
- COM and thread-handle helpers are present but not part of the direct proof path
- the current PE-to-ELF tool is a copier, not a semantic linker
- the empty `Main()` is compiler glue, not a second runtime entry

Unsupported behavior:

- no Win32 compatibility layer in Server
- no kernel changes
- no GUI or filesystem porting in this pass
- no promise that the legacy guideXOS kernel runtime can be transplanted as-is

## 16. Exact Next-Step Recommendation

Next experiment:

1. Keep the direct `ManagedMain` shape.
2. Isolate the smallest possible freestanding NativeAOT runtime configuration or target pack so the intermediate PE stops importing Windows DLLs.
3. If that is not achievable with the current toolchain, route only the needed host ABI through a tiny custom runtime library and keep the rest of the stock runtime out of the active path.
4. Do not touch the Server loader yet.

## 17. Files a Loader Experiment Would Touch

Likely loader-only targets:

- `native_elf_image_loader.cpp`
- `native_elf_executor.cpp`
- `native_elf_launch_pipeline.cpp`
- `native_app_runtime.cpp`
- `native_app_runtime.h`
- `elf_validator.cpp`

Read-only proof inputs for that experiment:

- `out/dotnet/managed-hostlog/artifacts/HostLogProof.elf`
- `out/dotnet/managed-hostlog/artifacts/HostLogProof.elf.readelf.txt`
- `out/dotnet/managed-hostlog/artifacts/HostLogProof.native.objdump.txt`
- `out/dotnet/managed-hostlog/artifacts/HostLogProof.map`

## 18. Files a Loader Experiment Must Not Touch

Do not touch:

- kernel code
- virtual-memory implementation
- process manager
- scheduler
- compositor
- VFS
- App Model resolver
- host-call table
- default application inventory
- default build scripts
- boot image contents
- normal Server feature flags

Outcome: **B - Minimal platform layer identified but incomplete.**

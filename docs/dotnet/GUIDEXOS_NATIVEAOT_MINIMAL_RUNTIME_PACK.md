# guideXOS Minimal AMD64 NativeAOT Runtime Pack

## 1. Executive summary

This experiment restores the existing non-allocating `HostLogProof` through its
real NativeAOT reverse-P/Invoke entry. The selected construction is an
application-scoped Approach B pack: it uses the exact 9.0.0 NativeAOT package
inputs, replaces the active Windows FLS and reverse-transition symbols with a
small guideXOS platform object, and leaves the normal Server and default
application inventory unchanged.

The clean custom pack builds reproducibly. The real managed method logs
`Hello from managed guideXOS code` exactly once per launch and returns `0`.
Two launches in one Server process and two separately launched Server
processes pass. This is a bounded proof pack, not a general .NET runtime. The
non-allocating milestone remains the baseline; the next opt-in capability is
documented in [`MANAGED_SINGLE_ALLOCATION_PROOF.md`](MANAGED_SINGLE_ALLOCATION_PROOF.md)
and is limited to one managed allocation with collection disabled.

**Decision: Outcome A — Minimal guideXOS runtime pack executes managed entry
correctly.** The result is limited to the non-allocating proof envelope stated
here.

## 2. Exact runtime and toolchain identity

The identity is locked in
[`tools/dotnet/runtime-pack/runtime-pack.lock.json`](../../tools/dotnet/runtime-pack/runtime-pack.lock.json).

| Input | Value |
| --- | --- |
| SDK | .NET SDK `10.0.301`, commit `96856fd726` |
| MSBuild | `18.6.4` |
| Host | .NET `10.0.9`, commit `901ca94124` |
| Target | `net9.0`, `win-x64`, AMD64 |
| ILCompiler | `Microsoft.DotNet.ILCompiler` `9.0.0` |
| Runtime pack | `runtime.win-x64.microsoft.dotnet.ilcompiler` `9.0.0` |
| Package source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Package root | `C:\Users\guideX\.nuget\packages\runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0` |
| `bootstrapper.obj` | SHA-256 `E97995D4179E4B493232CE386DC8D2780E53E5EAF8724E663E598EA096FD7685` |
| Stock `Runtime.WorkstationGC.lib` | SHA-256 `0E6A134AD4150CD604317A47860DAE82EB30AAE4D9CDB14144E06454E7BB1948` |
| `System.Private.CoreLib.dll` | SHA-256 `BAD35E3A8C882A49EEC31AC2A3E544DFBA1042C9990913B2842BBEE0E121BF0D` |
| CoreLib product version | `9.0.0+9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

The lock also records the hashes of the other native runtime libraries and the
compiler/runtime assemblies used to build the proof. The package nuspec proves
the runtime source commit. No guideXOS fork commit is claimed.

The old guideXOS C# tree uses a `7.0.0-alpha.1.22074.1`-era runtime package,
so it is not the same runtime generation as this `9.0.0` proof.

## 3. Previous FLS failure

The stock clean-build path was:

```text
ManagedMain at 0x10001900
  -> RhpReversePInvoke
  -> RhpReversePInvokeAttachOrTrapThread2
  -> ThreadStore::AttachCurrentThread
  -> __imp_FlsGetValue
```

The import slot at `0x10052108` contained `0x8DC44`; execution attempted that
address and faulted with `0xC0000005`. The existing Server bootstrap supplied a
Windows TLS index and zeroed block, but not the NativeAOT FLS binding,
`g_flsIndex`, runtime/module state, thread-store attachment, or transition
state. The previous successful artifact was not a preserved, reproducible
runtime-correct proof.

## 4. Required runtime capability matrix

| Capability | Current proof | Later allocation/runtime | Status |
| --- | --- | --- | --- |
| Runtime instance creation | No general instance; bounded per-image state cell | General runtime instance and lifetime | M for bounded proof; L later |
| Module registration | No additional module registration reached by this proof | Type-manager/module registration | M only insofar as generated image is link-complete; L general |
| Frozen/module tables | Not traversed | Required by broader generated code | N |
| Static constructor tables | No user static constructor reached | Cctor scheduling and ordering | N |
| GC initialization | No allocation or collection; no general GC startup | Workstation GC initialization and invariants | N now; L later |
| Thread-store creation | Replaced by one per-native-thread runtime cell | Full ThreadStore | M bounded; L later |
| Current-thread lookup/attachment | TLS-backed current block and runtime cell | General thread object and mode transitions | M bounded; L later |
| Thread state transitions | Reverse frame link only | Cooperative/preemptive GC transitions | M for this entry; L later |
| FLS get/set | Eight per-thread cells at block offset `0x80`, index `0` | Dynamic slots, destructors, callbacks | M bounded; L later |
| FLS destructor behavior | None needed or claimed | Cleanup callbacks | N |
| Reverse-P/Invoke entry | Implemented for the generated AMD64 frame | Full helper/exception semantics | M bounded; L broader |
| Reverse-P/Invoke return | Restores the previous frame link | Full detach/transition semantics | M bounded; L broader |
| Nonvolatile registers/stack | Existing AMD64 trampoline and generated ABI preserved | Per-architecture stubs and unwind | M |
| Fail-fast | Local `__fastfail(7)` policy | Rich host diagnostics and policy | M bounded; L later |
| Thread detachment | No detached user thread; frame is restored on return | Thread-store detach and shutdown | N now; L later |
| Runtime reuse | Startup state is idempotent and per-thread cells are reused | General runtime lifetime | M bounded; L later |
| Module unregistration | Not needed for process-lifetime proof | General unload | N |

The “M” entries are the operations actually required by the tested path, not a
claim that the stock NativeAOT runtime has been generally ported.

## 5. Stock Windows runtime dependencies

The stock runtime object set still contains broad Windows support. The active
stock transition depended on Windows FLS and thread-store attachment. The
custom link removes those active FLS imports and binds the transition symbols
to the guideXOS object.

The custom PE still has an import directory inherited from unreachable or
unproven stock support objects:

| DLL | Remaining imported names |
| --- | --- |
| `ADVAPI32.dll` | `RegisterEventSourceW`, `ReportEventW`, `DeregisterEventSource` |
| `bcrypt.dll` | `BCryptGenRandom` |
| `KERNEL32.dll` | `CloseHandle`, `CreateEventExW`, `DuplicateHandle`, `FormatMessageW`, `GetConsoleOutputCP`, `GetCurrentProcess`, `GetCurrentProcessorNumberEx`, `GetCurrentThread`, `GetEnvironmentVariableW`, `GetLastError`, `GetModuleFileNameW`, `GetStdHandle`, `GetThreadPriority`, `GetTickCount64`, `IsDebuggerPresent`, `LocalFree`, `MultiByteToWideChar`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `RaiseFailFastException`, `SetEvent`, `SetLastError`, `Sleep`, `VirtualAlloc`, `VirtualFree`, `WaitForMultipleObjectsEx`, `WideCharToMultiByte`, `WriteFile`, `GetModuleHandleW`, `GetProcAddress`, `RtlCaptureContext`, `RtlRestoreContext`, `VerSetConditionMask`, `SwitchToThread`, `GetCurrentThreadId`, `SuspendThread`, `ResumeThread`, `GetThreadContext`, `SetThreadContext`, `GetModuleHandleExW`, `LoadLibraryExW`, `VerifyVersionInfoW`, `InitializeContext`, `GetEnabledXStateFeatures`, `SetXStateFeaturesMask`, `VirtualQuery`, `EnterCriticalSection`, `LeaveCriticalSection` |
| `ole32.dll` | `CoGetApartmentType`, `CoInitializeEx`, `CoUninitialize`, `CoWaitForMultipleHandles` |
| `api-ms-win-crt-heap-l1-1-0.dll` | `free`, `_callnewh`, `malloc` |

There are no `FlsGetValue` or `FlsSetValue` imports in the custom image.
Static checks and the live proof
also show that the active transition does not enter a Windows FLS thunk. A
future full pack must remove or replace the remaining active-support surface,
rather than treating this proof-specific import elimination as a general PAL.

## 6. guideXOS C# reuse findings

The old sources were inspected from the locations identified by the reuse
audit. They were not copied into the Server or runtime-pack tree.

| NativeAOT requirement | Stock Windows implementation | guideXOS C# implementation | Reuse class | Adaptation |
| --- | --- | --- | --- | --- |
| Startup/module helpers | NativeAOT `StartupCodeHelpers` and generated startup data | `Corlib/Internal/Runtime/CompilerHelpers/StartupCodeHelpers.cs` | C | Use as design reference only; current native ABI differs |
| Type-manager/module tables | Native runtime/type-loader data | Old module/type initialization helpers | C | Re-derive from current generated image |
| Thread startup | Native `ThreadStore` and runtime thread data | Custom kernel scheduler/thread startup | D | Do not copy kernel-coupled scheduler |
| FLS/TLS | Windows FLS plus NativeAOT TLS template | Custom kernel thread structures | D | Replace at the native platform boundary |
| Reverse-P/Invoke | Native AMD64 helpers and transition frames | Old helpers are no-op stubs | D | Use current generated ABI and AMD64 transition |
| GC initialization | Workstation GC and Windows PAL services | Custom allocator/kernel support | D | Defer allocation and port PAL later |
| Fail-fast | Native fail-fast/exception policy | Simplified infinite-loop behavior | C | Keep a bounded explicit fail-fast hook |
| Native allocation | Windows virtual/CRT allocation paths | Kernel/native allocation helpers | D | Not in this pass |
| Static constructors | NativeAOT cctor runner | Simplified non-thread-safe cctor runner | C | Do not use for this proof |

Useful old material remains reference for naming and architectural intent. It is
not ABI-compatible evidence for this runtime generation.

## 7. Runtime-pack construction approach

Approach B was selected: build the smallest application-specific native object
set reachable by this proof while retaining ABI-matched, locked stock support
objects where they are not on the active transition.

The build extracts `thread.cpp.obj` and `EHHelpers.cpp.obj` from the locked
stock `Runtime.WorkstationGC.lib`, renames the replaced exports to
`guideXosStock...`, and rebuilds an adapted archive. The guideXOS platform
object then supplies the public reverse-P/Invoke and FLS symbols. Archive member
timestamps and compiler reproducibility options are normalized; two clean pack
builds produced identical object and adapted-library hashes.

Approach A is deferred until a matching, reproducibly checked-out runtime
source tree is available and a real PAL/GC port can be made. Approach C is
deferred/rejected as an implementation source because the old C# support is
kernel-coupled and from a different NativeAOT generation.

## 8. Directory and build layout

Tracked source and lock inputs are under:

```text
tools/dotnet/runtime-pack/
  README.md
  runtime-pack.lock.json
  build-runtime-pack.ps1
  src/platform/guidexos_nativeaot_platform.cpp
```

The wrapper is
[`scripts/dotnet/build-guidexos-nativeaot-runtime-pack.ps1`](../../scripts/dotnet/build-guidexos-nativeaot-runtime-pack.ps1).
Generated SDK copies, native libraries, objects and manifests are under
`out/dotnet/runtime-pack/` and are ignored by Git. The managed proof opts in
with `-UseGuideXosRuntimePack`; stock analysis remains the default.

The optional `-ExternalRuntimeRoot` parameter verifies the requested runtime
checkout revision and does not modify it. The current pack does not require
that checkout to build because the package identity and binary hashes are
locked locally.

## 9. FLS/TLS design

The existing Server loader continues to allocate the NativeAOT TLS template
block and write `_tls_index` for the mapped image. The platform object reads the
AMD64 GS TLS vector, indexes the image's `_tls_index`, and uses the block as the
runtime-owned per-native-thread storage.

The proof envelope reserves:

```text
runtime cell:              block + 0x30
initialized flag:          cell + 0x38
transition-frame link:     cell + 0x40
guideXOS FLS cells:        block + 0x80, eight pointer cells
minimum block size:        0x110 bytes
guideXOS FLS index:        0
```

`FlsGetValue` and `FlsSetValue` operate only on the current thread's cells.
Before the pack startup state is set, get returns null and set returns false;
after startup, invalid indexes fail without touching memory. No Server C++
object layout crosses this boundary, no COM is used, and no single global
runtime pointer is used as a thread substitute.

The implementation is intentionally a fixed, bounded namespace. It does not
claim dynamic `FlsAlloc`, destructors, or arbitrary NativeAOT FLS consumers.

## 10. Thread-store design

This proof does not construct the stock `ThreadStore` or a managed `Thread`
object. It provides the minimum current-thread state required by the generated
reverse transition: a TLS-backed runtime cell, initialized flag, FLS cell, and
transition-frame link. Initialization is idempotent for repeated entry on the
same native thread. A different native thread obtains a different TLS block and
therefore cannot inherit the first thread's runtime value.

The Server process remains responsible for the native host thread and its TLS
block lifetime. No user-created managed thread, thread pool, task, detach path,
or scheduler is supported.

## 11. Module and runtime startup

`initializeRuntimeState` sets the deterministic FLS index once, binds the
current TLS block's runtime cell to itself, sets the initialized flag, and
seeds the local FLS cell. It is called by the custom reverse-P/Invoke entry
before the managed body.

The generated image contains NativeAOT startup/module symbols, but this proof
does not reach a user static constructor, frozen-object lookup, allocation,
collection, reflection, or a general module-registration operation. Therefore
the pack reports bounded proof startup complete, not full NativeAOT runtime
startup. Full module/type-manager/GC initialization remains a later capability
when the allocation experiment begins.

## 12. Reverse-P/Invoke transition

The custom AMD64 entry receives the generated transition frame, validates the
frame and current TLS block, initializes the bounded runtime state, stores the
previous frame link in frame slot `0`, stores the runtime-cell pointer in frame
slot `1`, and publishes the frame at runtime-cell offset `0x40`.

The return helper validates the frame and cell and restores the previous frame
link. The existing Server Win64 trampoline remains responsible for the native
call boundary and register/stack ABI. The platform object supplies the
AMD64-specific low-level implementation; ARM64 will require its own equivalent
assembly/ABI layer.

## 13. Platform services implemented

Implemented for this proof:

- per-native-thread FLS get/set with deterministic index `0`;
- current-thread runtime-cell binding;
- idempotent runtime startup state;
- reverse-P/Invoke frame entry and return;
- explicit fail-fast for malformed runtime state;
- no Windows FLS import thunk on the active link path.

The Server executor retains only generic entry, mapping, TLS-bootstrap, callback,
return-code and result diagnostics. The former vectored fault-capture block and
`GX_NATIVE_ELF_FAULT_DIAGNOSTICS` proof switch were removed from generic
executor code. Symbolization, import inspection and proof assertions remain in
the dotnet/tools and smoke-script layer.

## 14. Unsupported platform services

This pack does not implement managed allocation, GC collection, managed
exceptions, managed threads, thread pool, tasks, console, filesystem,
networking, reflection, globalization, GUI, COM, dynamic FLS allocation,
FLS-destructor callbacks, general PAL synchronization, runtime unload, or a
general Windows compatibility layer. Remaining stock support objects and
imports must not be used as evidence that those services work.

## 15. Import and dependency results

Static custom-build checks pass:

- `FlsGetValue` and `FlsSetValue` are not in the PE import table;
- the map contains the guideXOS platform object and custom reverse symbols;
- the generated ELF remains fixed-base and dependency-free as required by the
  existing staging envelope;
- no native support object emits the success message.

The image is structurally staged through the existing PE-to-ELF conversion.
The remaining import directory is listed in section 5 and is not claimed to be
a complete Windows-free runtime. No active test path entered a Windows FLS
thunk, and no access violation was suppressed.

## 16. Standalone harness results

There is no separate fake native harness. The generated image is a fixed-base,
sectionless ET_EXEC with the guideXOS host-context ABI; the existing
experimental Server loader is the narrowest real-method harness available.
The requested FLS-before-initialization and second-native-thread isolation
checks are therefore statically validated/design-covered and marked BLOCKED as
standalone tests, while same-thread repeat and fresh-process behavior are
covered by the real Server execution below.

No native fake method was substituted.

## 17. Server execution results

The experimental Server build passed. The custom live smoke passed with the
clean custom image:

```text
preferred-base mapping: PASS (0x10000000)
existing trampoline: PASS
runtime-pack startup: PASS
reverse-P/Invoke to actual managed body: PASS
host log callback count per launch: 1
message: Hello from managed guideXOS code
managed/native return code: 0
fault/access-violation suppression: none
native fallback success message: none
```

The host callback output is the message emitted by the managed method through
the existing host ABI.

## 18. Repeat-launch results

One Server process ran the proof twice. Both launches returned `0`, each
executed one host callback with the exact managed message, and the runtime IDs
were distinct and increasing. The runtime startup state was reused
idempotently; the transition frame was restored on each return.

Result: **same-process repeat PASS**.

## 19. Cross-process results

A second separately launched Server process repeated the same two-launch test.
Both launches mapped at the preferred base, reached the managed body, logged
once, and returned `0`.

Result: **cross-process repeat PASS**.

## 20. Architecture boundaries

The current boundary is:

```text
Managed HostLogProof
  -> locked NativeAOT compiler/runtime-pack inputs
  -> guideXOS bounded runtime-neutral policy
  -> AMD64 GS-TLS and reverse-transition object
  -> guideXOS host context/log ABI
  -> Server native ELF loader
```

Runtime-neutral policy includes the bounded runtime-state lifetime, FLS
contract, host callback boundary, and fail-fast policy. AMD64-specific code
includes GS TLS-vector access, the NativeAOT block offsets validated for this
image, Win64 calling convention, frame layout, and future unwind details.
ARM64 or another architecture must provide its own TLS access and transition
implementation behind the same narrow policy; the AMD64 object must not be
treated as portable source.

## 21. Files changed

Tracked source and build changes:

- `native_elf_executor.cpp` — removed temporary vectored fault diagnostics;
- `samples/managed/HostLogProof/HostLogProof.csproj` — opt-in runtime-pack
  object input;
- `scripts/dotnet/build-managed-hostlog-proof.ps1` — pack opt-in, identity and
  static validation;
- `scripts/dotnet/managed-hostlog-artifact-assertions.ps1` — stock/custom
  artifact assertions;
- `scripts/dotnet/stage-managed-hostlog-proof.ps1` — pack metadata/hash
  validation and proof envelope;
- `scripts/smoke-dotnet-managed-hostlog-execution.ps1` — custom-mode execution
  and envelope checks;
- `.gitignore` — ignored generated runtime-pack output;
- `tools/dotnet/runtime-pack/` — lock, build recipe, platform source and
  README;
- `scripts/dotnet/build-guidexos-nativeaot-runtime-pack.ps1`;
- `scripts/smoke-dotnet-runtime-pack-static.ps1`;
- `scripts/smoke-dotnet-runtime-pack-hostlog.ps1`;
- `scripts/smoke-dotnet-runtime-pack-state.ps1`;
- this document.

`native_app_runtime.cpp`, `native_elf_trampoline_win64.cpp`, and
`native_elf_trampoline_win64.h` were reviewed and were not changed in this
pass. No generated runtime library, object, DLL, executable, archive, or ELF
file is tracked.

## 22. Remaining risks

- The pack is not a complete NativeAOT PAL/GC port; the first allocation is an
  explicit new experiment, not an implied capability of this result.
- The remaining import directory needs source-level removal or replacement as
  the runtime surface expands.
- The exact NativeAOT source commit is proven by package metadata, but a
  matching source checkout was not available locally for a complete Approach A
  rebuild.
- NativeAOT TLS offsets and transition assumptions remain tied to this locked
  compiler/runtime generation and generated image.
- Standalone FLS-before-init and second-thread harness coverage remains blocked
  by the fixed-base loader contract.
- No managed exception, static-constructor, GC, or runtime-shutdown semantics
  are established by this proof.

## 23. Exact next experiment

Execute one runtime-correct managed method that allocates one small byte array,
fills it, logs its contents through the proven host ABI, and returns
successfully twice. Add only the allocation and the minimum runtime/GC/PAL
capabilities proven reachable by that method; do not broaden the Server
application model or import a general Win32 compatibility layer.

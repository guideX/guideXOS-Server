# NativeAOT Workstation GC Platform Object Replacement

Status: current 2026-07-22 validation. No RhInitialize, GC heap construction,
finalizer/helper startup, managed allocation through the collector, or
collection was executed. Decision: Outcome B. The Windows gcenv object was
replaced and exact collector symbol binding succeeds, but a separate
NativeAOT PAL/runtime object family still contributes mandatory Windows import
candidates. The HostLog proof also remains reproducibly unresolved.

## 1. Objective

Replace the locked .NET 9 Workstation GC Windows environment object with a
guideXOS object providing the exact GCToOSInterface, GCEvent,
CLRCriticalSection, and g_SystemInfo symbols. The object routes to the
validated guideXOS VM, event, mutex, native-thread, FLS, timing, and processor
adapters. It does not rename collector call sites or provide fake VirtualAlloc,
VirtualFree, VirtualQuery, or VirtualProtect exports.

The required path is:

~~~text
Runtime.WorkstationGC.lib collector objects
  -> exact GCToOSInterface/GCEvent/CLRCriticalSection symbols
  -> guidexos_gcenv.obj
  -> guideXOS NativeAOT adapters
  -> generic guideXOS VM/event/thread/mutex/FLS services
~~~

## 2. Stock library identity

| Field | Value |
| --- | --- |
| Stock path | C:\Users\guideX\.nuget\packages\runtime.win-x64.microsoft.dotnet.ilcompiler\9.0.0\sdk\Runtime.WorkstationGC.lib |
| SHA-256 | 0E6A134AD4150CD604317A47860DAE82EB30AAE4D9CDB14144E06454E7BB1948 |
| Length | 5,802,942 bytes |
| Format | COFF archive, !<arch> newline |
| Archive tool | MSVC lib.exe |
| Source | NativeAOT/runtime commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3 |
| Pack/configuration | ILCompiler 9.0.0, AMD64, win-x64, Workstation, one heap, server/background/concurrent disabled |
| Interfaces | GC 5.3, EE 2 |

The stock copy and complete member list are preserved under
[out/dotnet/gc-platform-object-replacement](../../out/dotnet/gc-platform-object-replacement/).
The reconstruction script validates the locked hash before and after the
build; the stock package file was unchanged.

## 3. Windows platform object inventory

The removed member is:

~~~text
nativeaot\Runtime\Full\CMakeFiles\Runtime.WorkstationGC.dir\__\__\__\gc\windows\gcenv.windows.cpp.obj
~~~

The complete dumpbin symbol inventory is in
[gcenv.windows.cpp.obj.inventory.json](../../out/dotnet/gc-platform-object-replacement/symbols/gcenv.windows.cpp.obj.inventory.json):
421 defined records and 62 undefined records, including COFF/debug records.
The raw object, headers, directives, symbols, imports, and all extracted
members are in the same output tree. The stock archive has 64 members; the
full list is [stock-members.txt](../../out/dotnet/gc-platform-object-replacement/stock/stock-members.txt).

## 4. Symbols supplied by the Windows object

The exact decorated names are in
[symbol-binding-report.json](../../out/dotnet/gc-platform-object-replacement/symbols/symbol-binding-report.json).
The 53 required definitions and their replacement status are:

| Family | Definitions | Startup status | Existing guideXOS adapter | Replacement |
| --- | --- | --- | --- | --- |
| System information | g_SystemInfo | Required | processor/page/granularity service | Replaced |
| Critical sections | CLRCriticalSection::Initialize, Destroy, Enter, Leave | Required | critical-section adapter | Replaced |
| Events | GCEvent constructor, CloseEvent, Set, Reset, Wait, CreateAutoEventNoThrow, CreateManualEventNoThrow, CreateOSAutoEventNoThrow, CreateOSManualEventNoThrow | Required | event adapter | Replaced |
| VM | Initialize, Shutdown, VirtualReserve, VirtualRelease, VirtualReserveAndCommitLargePages, VirtualCommit, VirtualDecommit, VirtualReset | Required for selected paths | GC-owned VM reservation registry | Replaced |
| VM status/capability | SupportsWriteWatch, ResetWriteWatch, GetWriteWatch, GetVirtualMemoryLimit, GetVirtualMemoryMaxAddress, GetPhysicalMemoryLimit, GetMemoryStatus | Selected configuration | GC-owned accounting/VM adapter | Replaced |
| Thread/yield | Sleep, YieldThread, GetCurrentProcessorNumber, CanGetCurrentProcessorNumber, SetCurrentThreadIdealAffinity, GetCurrentThreadIdealProc, GetCurrentThreadIdForLogging, GetCurrentProcessId, SetThreadAffinity, BoostThreadPriority | One CPU; unsupported priority is honest | scheduler/native-thread services | Replaced |
| Processor/affinity | GetCacheSizePerLogicalCpu, SetGCThreadsAffinitySet, GetTotalProcessorCount, CanEnableGCNumaAware, GetNumaInfo, CanEnableGCCPUGroups, GetProcessorForHeap, GetCPUGroupInfo, ParseGCHeapAffinitizeRangesEntry | One heap/one CPU; NUMA/CPU groups disabled | processor service | Replaced |
| Timing/diagnostics | FlushProcessWriteBuffers, DebugBreak, QueryPerformanceCounter, QueryPerformanceFrequency, GetLowPrecisionTimeStamp | Timing required; unexpected debug break aborts | timing/fence service | Replaced |

The source implementation is an integration object, not a second VM or
synchronization implementation:
[guidexos_gcenv.cpp](../../tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv.cpp).

## 5. Selected replacement strategy

Strategy A was selected: replace the complete Windows gcenv object. That
member owns an inseparable family of exact collector platform definitions.
Splitting only VM methods would leave event, lock, timing, processor, and
diagnostic ownership ambiguous. No collector algorithm or call site changed.

Optional paths are source-faithful. Large pages, NUMA, CPU groups, write-watch,
and unsupported reset/discard paths are not enabled by the locked configuration.
Unexpected entries fail fast instead of returning fake success.

## 6. Toolchain and ABI

The matching MSVC x64 toolchain was used:

| Setting | Value |
| --- | --- |
| Compiler | MSVC 19.51.36248 for x64, toolset 14.51.36231 |
| Target | AMD64 COFF/MSVC NativeAOT object ABI |
| C++ | /std:c++17 |
| Runtime | /MT, MT_StaticRelease, /Zl |
| Exceptions/RTTI | /EHs-c-, /GR- |
| Security/optimization | /GS-, /O2, /Oi, /Brepro |
| Defines | FEATURE_NATIVEAOT, NATIVEAOT, TARGET_AMD64, HOST_AMD64, HOST_64BIT, _WIN64, GXOS_BARE_METAL, GXOS_TRUE_VIRTUAL_MEMORY |
| Packing/calling convention | Stock x64 MSVC defaults; no packing override |

Stock /FAILIFMISMATCH directives require the static CRT and MSVC-compatible
object ABI. The replacement uses the same mode. A MinGW ELF conversion is
retained only as an ABI-boundary artifact, not as an executable substitute.

## 7. GuideXOS platform object

Authored source is isolated under
[tools/dotnet/runtime-pack/src/gcenv](../../tools/dotnet/runtime-pack/src/gcenv/).
VM methods use the bounded collector-owned raw-address registry, preserving
true unbacked reservation, partial commit, zero-on-commit,
decommit/recommit, protection, release, generation, and rollback. Events,
critical sections, timing, processor, FLS, and native-thread paths use the
existing adapters.

The exact inactive probe is
[guidexos_gcenv_exact_symbol_probe.cpp](../../tools/dotnet/runtime-pack/src/gcenv/guidexos_gcenv_exact_symbol_probe.cpp).
It calls the actual decorated methods linked from the adapted archive and does
not call RhInitialize or construct the collector.

## 8. Archive reconstruction

[build-guidexos-workstationgc.ps1](../../tools/dotnet/runtime-pack/build-guidexos-workstationgc.ps1)
validates the stock hash, extracts every member, dumps symbols/imports,
compiles the replacement objects, removes only the exact Windows gcenv member,
appends the replacement objects, normalizes archive timestamps, and emits
member, symbol-binding, import, configuration, identity, and stock-unchanged
evidence.

Retained stock members keep their original order. Replacement objects append in
a fixed order. The strict binding report found no duplicate strong exact
definitions, so archive selection does not have an uncontrolled winner.

## 9. Symbol parity

| Field | Value |
| --- | --- |
| Adapted path | out/dotnet/gc-platform-object-replacement/rebuilt/Runtime.WorkstationGC.lib |
| SHA-256 | BA847F225439A8D693CD975CCAACDD01264BDA88BE4F2BBF18D5A0E4DB0F1F52 |
| Length | 5,936,896 bytes |
| Members | 75 |
| Replacement object | guidexos_gcenv.obj |
| Replacement object SHA-256 | 1F255DFCF8CBEB0A93289F0E160C04DA5410EB41F9B06113186BF9D3438F0CCB |

The machine report says:

~~~text
requiredSymbolCount=53
missingDefinitions=0
duplicateStrongDefinitions=0
windowsGcenvMemberPresent=false
all required definitions: replaced by guidexos_gcenv.obj
~~~

The Windows member is absent from
[adapted-members.txt](../../out/dotnet/gc-platform-object-replacement/rebuilt/adapted-members.txt).

## 10. Duplicate and missing symbol checks

Every exact symbol formerly supplied by the Windows object was checked against
the replacement object and complete rebuilt symbol set. Missing definitions:
zero. Duplicate strong definitions: zero. No replacement symbol was silently
discarded. The original stock archive remains separate and unchanged.

## 11. Windows import elimination

The removed gcenv object no longer contributes GC-owned imports for VM,
synchronization, timing, processor, or event methods. No fake Win32 VM exports
were added.

The adapted archive still has 19 raw Windows import candidates in other stock
objects:

| Import candidate(s) | Contributing member |
| --- | --- |
| VirtualQuery, GetTickCount64 | PalRedhawkCommon.cpp.obj |
| VirtualAlloc, VirtualFree, VirtualProtect, CreateEventW, CloseHandle, CreateThread, FlsAlloc, FlsGetValue, FlsSetValue, GetCurrentThreadId, GetLastError, GetCurrentProcess, SwitchToThread | PalRedhawkMinWin.cpp.obj |
| GetLastError | thread.cpp.obj |
| QueryPerformanceCounter, QueryPerformanceFrequency, SleepEx | time.c.obj |

The complete report is
[remaining-windows-platform-symbols.json](../../out/dotnet/gc-platform-object-replacement/imports/remaining-windows-platform-symbols.json).
These imports are not labeled dead; their contributing members remain the
precisely identified PAL/runtime blocker. The removed gcenv imports were
eliminated by object replacement, not compatibility shims.

## 12. Exact-symbol hosted probe

[build-guidexos-gcenv-exact-symbol-probe.ps1](../../tools/dotnet/runtime-pack/build-guidexos-gcenv-exact-symbol-probe.ps1)
builds a hosted inactive probe against the adapted archive. Its map resolves
the actual exact GCToOSInterface, GCEvent, CLRCriticalSection, and g_SystemInfo
providers to Runtime.WorkstationGC:guidexos_gcenv.obj.

Result: **PASS**. VM reserve/commit/write/decommit/recommit-zero/protection/
release, event lifecycle, critical-section lifecycle, FLS mapped semantics,
helper-thread lifecycle, timing, processor count, and shutdown cleanup passed.
See [probe-output.txt](../../out/dotnet/gc-platform-object-replacement/rebuilt/exact-symbol-probe/probe-output.txt).
Hosted execution imports from generic hosted support are not the final
bare-metal import-elimination proof.

## 13. Exact-symbol QEMU probe

[probe-guidexos-workstationgc-qemu.ps1](../../tools/dotnet/runtime-pack/probe-guidexos-workstationgc-qemu.ps1)
extracts the actual replacement object and records its MSVC symbols. The
adapted archive cannot execute in the current MinGW QEMU target because MSVC
COFF/Win64 C++/CRT/TLS objects are not interchangeable with MinGW ELF/SysV
AMD64 objects. The converted object is only inspection evidence.

Result: **BLOCKED for exact Workstation archive execution**. Static archive
binding, Windows-member absence, and the generic true-VM QEMU foundation pass.
Exact VM/event/critical/FLS/thread/timing execution and leak checks are blocked
by this ABI boundary. See
[qemu-probe-result.json](../../out/dotnet/gc-platform-object-replacement/rebuilt/qemu-exact-symbol-probe/qemu-probe-result.json).

## 14. Adapted library identity

The versioned identity is in
[adapted-identity.json](../../out/dotnet/gc-platform-object-replacement/rebuilt/adapted-identity.json)
and in the lock file's adaptedWorkstationGc section. The adapted archive was
not copied over the NuGet package cache.

## 15. Reproducibility

Two clean builds produced the same adapted archive hash, replacement object
hash, and normalized member-manifest hash. See
[reproducibility-report.json](../../out/dotnet/gc-platform-object-replacement-reproducibility/reproducibility-report.json).
Both archive hashes are
BA847F225439A8D693CD975CCAACDD01264BDA88BE4F2BBF18D5A0E4DB0F1F52.

## 16. HostLog regression status

The clean nonallocating stock HostLog proof **reproduced but is unresolved**:
exit 0xC0000005 before the host callback log line. Current diagnostics record
faultAddress/rip 0x8dc44, FLS slot 7, stack bounds 0x551b400000 through
0x551b600000, and the last callback as entering host call dispatch.

Map disassembly identifies ManagedMain at 0x10001900 and an indirect call at
0x100019cf through ctx->host->log; the callback did not complete. The fault
target is outside the proof image and mapped server image, so the source-level
faulting instruction is not established. ThreadStore attachment was not
reached or is unknown. This proof used UseGuideXosRuntimePack=False, so the
adapted GC archive was not linked and no exact GC platform method was entered.
The current proof hashes are:

~~~text
HostLogProof.exe  24B8F7D827627A40BDE2F0910AB6D7409686BEDD402B2B0F27992410391DF285
HostLogProof.elf  F19BD9D8E9DA9AE3B041F0F14911BB155D7178EDCCA542BE5D91F81B896B5DA7
HostLogProof.map  E94F39120E23AC11144365A5A890B5F3C619BD1A5B9ABF24662B8459A0196832
~~~

This is not classified as the historical FlsGetValue path because current RIP
and binding evidence do not demonstrate that path.

## 17. Repeated-allocation regression status

The corrected clean guideXOS repeated-allocation invocation is **PASS** for the
64 KiB and 4 KiB bounded no-collection proofs. The 64 KiB proof performs 234
objects of size 280 and controlled OOM; the 4 KiB proof performs 14 objects
and controlled OOM. Collection count, heap expansion, pointer mutation,
integrity, monotonicity, and overlap checks pass.

The earlier PE-import assertion was not reproduced after the clean stage was
corrected. The observed import difference was the intentional resolver-only
FreeLibrary and SetThreadErrorMode addition. The repeated proof has no live
FLS or CRT imports and does not enter those resolver paths. The assertion is
stale for that intentional import delta, not evidence of a new reachable
GC/platform import, and was not broadly relaxed.

## 18. Updated readiness result

| Gate | Result |
| --- | --- |
| Stock identity; Windows member identified/removed | PASS |
| GuideXOS object; exact parity/binding | PASS, 53/53 |
| Duplicate/missing definitions | PASS, 0/0 |
| Windows VM/synchronization/FLS/thread elimination | PASS for removed gcenv; FAIL for separate PAL/runtime objects |
| Exact hosted probe | PASS |
| Exact QEMU probe | BLOCKED by MSVC-to-MinGW ABI boundary |
| HostLog baseline | Reproduced but unresolved |
| Repeated-allocation baseline | PASS after clean rebuild/stage |

The GC startup gate remains **not ready**. No RhInitialize call is authorized.

## 19. Remaining blocker

Outcome B: the primary Windows GC environment object replacement works, but the
mandatory NativeAOT PAL/runtime platform family remains bound in
PalRedhawkCommon.cpp.obj, PalRedhawkMinWin.cpp.obj, thread.cpp.obj, and
time.c.obj. This is the exact remaining symbol family, not a request for fake
Win32 compatibility exports.

The HostLog access violation is an independent unresolved managed-proof
regression, and the exact Workstation archive cannot yet execute in the current
QEMU ABI target. Both remain readiness blockers.

## 20. Exact next experiment

Rebuild the identified NativeAOT PAL/runtime object subset from the same locked
source and matching MSVC ABI, replacing its Windows platform dependencies with
guideXOS PAL adapters. Repeat the archive/member/symbol/import audit and add an
exact-symbol probe in a target with the same ABI, or produce a separately
proven ABI-compatible QEMU build. Keep RhInitialize, heap construction,
finalizer/helper startup, managed collector allocation, and collection
disabled. Diagnose the HostLog indirect-call fault separately before
reconsidering the startup gate.


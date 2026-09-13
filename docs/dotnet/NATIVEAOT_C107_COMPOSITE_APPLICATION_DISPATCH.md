# C107 — Composite Managed NativeAOT Application Dispatch

Date: 2026-09-12
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`
Starting HEAD: `8dfc14ff97c9e19663b3c754ad74a90764cf884` — `Select composite NativeAOT application architecture`
Locked NativeAOT identity: `9.0.0`, AMD64, Workstation GC

## Outcome

**Outcome B — the authentic composite dispatch proof passes; ThreadStatic persistence is deferred to C108.**

The production guideXOS path executes the following logical sequence from one resident NativeAOT ELF:

```text
A PASS → invalid rejected → B PASS → A PASS
```

The required core sequence `A PASS → B PASS → A PASS` therefore passes, with the invalid-selector negative test interposed. The executable is mapped once, the same native entrypoint is re-entered three times, the managed dispatcher selects the logical application, and guideXOS returns to its main loop. No NativeAOT multi-module runtime support was added.

The result is classified as Outcome B only because the existing resident bridge reinstalls the current thread's TLS on each resident entry. The C107 test proves that App A and App B have separate `[ThreadStatic]` fields and that each field is independently readable/writable during its own invocation, but it does not claim persistence of a thread-static value across resident redispatch. That lifecycle question is explicitly deferred to C108 and does not block the core A/B/A proof.

## C106 architecture inheritance

C106 selected Outcome B: multiple guideXOS C# applications are logical managed application entrypoints compiled into one resident composite NativeAOT image. The runtime continues to own exactly one `RuntimeInstance`, scalar `CoffNativeCodeManager`, managed-code range, GC, ThreadStore, TLS/PAL domain, and module domain. C107 implements that selection without changing the locked runtime source or weakening the independent-image `Busy` guard.

The lifecycle is cooperative and resident:

```text
ordinary guideXOS boot
  → production application discovery
  → one mapped composite ELF
  → one native-visible NativeAOT entrypoint
  → managed dispatcher(appId)
  → logical App A or App B
  → managed return status
  → guideXOS launcher/kernel
```

Logical application return is not runtime shutdown, module unload, GC restart, or image unmapping. Logical static state remains resident until explicitly reset by application code or until the runtime context ends.

## Composite image and project layout

The composite is the existing `HostLogProof` NativeAOT project built in a new `C107Composite` mode:

* Project: `samples/managed/HostLogProof/HostLogProof.csproj`
* Managed source: `samples/managed/HostLogProof/Program.cs`
* ABI definitions: `samples/managed/HostLogProof/NativeAbi.cs`
* Build mode: `HostLogProofMode=C107Composite`
* Native-visible entrypoint: `GuideXosNativeAotApplicationEntry`
* ELF artifact: `out/dotnet/c011ec107-composite-application-dispatch/build/composite/artifacts/HostLogProof.elf`
* Production staged path: `/system/wall/C107.ELF`

The project contains both logical application bodies and one managed dispatcher. It is one `dotnet publish`/NativeAOT image, not a pair of native images and not a dynamically loaded module set.

## Selector ABI and dispatcher

The selector representation is a bounded `uint32_t` logical application ID:

| ID | Meaning |
| --- | --- |
| `0` | Invalid/reserved selector |
| `1` | App A |
| `2` | App B |

The existing `NativeGxAppContext` ABI carries `userData` as `void*`. The native bridge encodes the `uint32_t` selector as a pointer-sized scalar in that field. `launchLogical(path, logicalAppId, report)` calls the same mapped image entrypoint for every selector. The managed `DispatchC107Composite` method casts `context->userData` back to `uint32`, logs the selector, and uses a managed switch to call `RunC107AppA` or `RunC107AppB`.

The native side does not jump to app-specific managed addresses. It calls only the existing `GuideXosNativeAotApplicationEntry(void*)` address returned for the resident image. The dispatcher itself is managed code inside the composite image.

Return semantics are deterministic:

* App A success: managed `0`, native `LaunchStatus::Success`.
* App B success: managed `0`, native `LaunchStatus::Success`.
* Invalid selector: managed `-4` (`ErrorInvalidApplicationId`), native `LaunchStatus::InvalidApplicationId` (`-13`).
* Other managed failure: managed failure, native `LaunchStatus::ManagedFailed`.

## Logical application behavior

### App A

`RunC107AppA` has its own method body, marker set, static invocation counter, and `[ThreadStatic]` field. Each invocation allocates `new byte[8]`, fills it with `1..8`, and verifies the sum `36`. It logs `C107-APP-A-ENTRY`, a state line, and `C107-APP-A-PASS` before returning `0`.

### App B

`RunC107AppB` has a distinct method body, marker set, static invocation counter, and separate `[ThreadStatic]` field. Each invocation allocates `new byte[5]`, fills it with `2,5,8,11,14`, and verifies the weighted result `150`. It logs `C107-APP-B-ENTRY`, a state line, and `C107-APP-B-PASS` before returning `0`.

### Static lifecycle

The accepted serial evidence contains these logical static values in every fresh boot:

```text
App A first:  C107-APP-A-STATE count=1 ... allocation=PASS calculation=36
App B:        C107-APP-B-STATE count=1 ... allocation=PASS calculation=150
App A second: C107-APP-A-STATE count=2 ... allocation=PASS calculation=36
```

The A counter is therefore `0 → 1 → 2`, while the independent B counter is `0 → 1`. This proves genuine redispatch, persistent logical static state, and distinct per-app state inside one resident image.

### Thread-static result

App A and App B use different `[ThreadStatic]` fields. Their own invocation logs show independent values of `threadBefore=0` and `threadAfter=1`; no field is shared between A and B. The bridge's production resident-entry path reinstalls the current thread's TLS, so the observed thread-static value is not persistent across redispatch: App A's second invocation also begins at `threadBefore=0`. C107 records this as a bounded functional/addressability result and defers persistent thread-static lifecycle semantics to C108. No GC forensic phase or C011EC12 work was reopened.

## Allocation and return evidence

All three logical application calls that should succeed allocate managed arrays and validate their contents:

* App A first allocation: `byte[8]`, sum `36`, `allocation=PASS`.
* App B allocation: `byte[5]`, weighted result `150`, `allocation=PASS`.
* App A second allocation: `byte[8]`, sum `36`, `allocation=PASS`.

The invalid selector returns `-4` without entering either logical application body. It does not crash, corrupt the resident state, or prevent the following App B and App A dispatches.

## Runtime singularity evidence

Each accepted fresh boot reports:

| Observation | Count/result |
| --- | --- |
| Composite image mapping | `1` — `[C102-LOADER] mapped ELF64 AMD64` |
| Resident image reentry | `3` — `[C103-LOADER] reusing resident` |
| PAL/VM/GC startup seam | `1` |
| NativeAOT startup stages | stages `1..7`, each exactly once |
| Managed logical dispatches | `4` — A, invalid, B, A |
| Successful logical launches | `3` |
| Invalid logical launch | `1` |
| Return to kernel main loop | observed |

The existing once-only bridge state remains authoritative: `g_startupState` prevents a second `RhInitialize`/PAL startup, `g_guideXosNativeAotCodeManagerRegistered` prevents a second code-manager registration, and `g_guideXosNativeAotModulesInitialized` prevents a second module initialization. The serial evidence shows one startup sequence and no startup re-run on resident calls. The ThreadStore is not recreated by the resident path; no runtime shutdown or restart is attempted.

The resulting C107 counts are:

* Runtime initialization: `1`.
* PAL initialization: `1` observable startup seam.
* GC initialization: `1` within the single PAL/VM/GC startup seam.
* Code-manager registration: `1`, protected by the existing once-only bridge flag.
* Module initialization: `1`, protected by the existing once-only bridge flag.
* ThreadStore setup: `1` resident runtime setup; subsequent calls use resident reentry.

These are observational/source-authenticated counts, not new runtime decision logic or replacement counters.

## Mapping and physical-frame accounting

The composite executable range was stable in all three fresh boots:

```text
ELF type: ET_EXEC
Base:     0x0000000100000000
Span:     0x000000000012E000
Range:    [0x0000000100000000, 0x000000010012E000)
Entry:    0x000000010004CF00
Segments: 7
```

The first report in each boot records `mappingsPersistent=1`, `postVmRegion=0x260`, `postPageTable=0x7`, and `ownerResidual=0`. The three later calls report the same base, span, entry, VM region, page-table count, and zero residual ownership. There is no second executable mapping and no per-logical-app image remap.

The first managed allocation changes the tracked frame totals; subsequent logical dispatches do not create another executable mapping or alter the resident mapping accounting. Representative first-boot accounting is:

```text
before first A:  totalTracked=0x306C4 free=0x2C82E allocated=0x3E96
after first A:   totalTracked=0x306C4 free=0x2C5C7 allocated=0x40FD
                 vmRegion=0x260 pageTable=0x7 ownerResidual=0
later calls:     same post-allocation values; ownerResidual=0
```

The frame delta is consistent with legitimate runtime/managed allocation and VM setup. It is not a second NativeAOT executable image.

## Independent-image safety boundary

The existing independent-ELF `Busy` guard remains intact. C107 calls one path, `/system/wall/C107.ELF`, for all four logical launches and never registers a second image. No `CoffNativeCodeManager` registry, second `RuntimeInstance`, module unregister, shutdown/restart protocol, or multi-module runtime support was added.

The prior C104 regression evidence remains the independent-image guard reference: `App A PASS → App B busy → App A PASS`. It was not disturbed or replaced by C107. A separate C104 boot was not rerun during this phase because the guard source and existing regression path were unchanged, while the C107 runner used a single composite ELF.

## QEMU validation

The production runner performed three fresh QEMU boots. Each boot passed the semantic checker, singularity checks, allocation/state checks, fault checks, and main-loop check:

| Boot | Result | Mapping | Resident reentries | Startup stages | Sequence |
| --- | --- | ---: | ---: | --- | --- |
| 1 | PASS | 1 | 3 | `1,1,1,1,1,1,1` | A PASS → invalid rejected → B PASS → A PASS |
| 2 | PASS | 1 | 3 | `1,1,1,1,1,1,1` | A PASS → invalid rejected → B PASS → A PASS |
| 3 | PASS | 1 | 3 | `1,1,1,1,1,1,1` | A PASS → invalid rejected → B PASS → A PASS |

Every accepted boot returned to:

```text
[KERNEL] Entering main loop (waiting for input)...
```

An earlier development boot correctly exposed the TLS reinstall behavior before the optional thread-static assertion was relaxed. Two later checker-only issues were corrected: Windows serial-handle hashing and CRLF/zero-padded diagnostic matching. Those preliminary attempts are not counted as accepted reproductions; the three listed boots are the acceptance reproductions.

## Build and test commands

Managed composite build and production staging:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/run-c107-composite-application-dispatch.ps1 `
  -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT `
  -PythonExe C:\Users\guideX\AppData\Local\Programs\Python\Python312\python.exe `
  -FreshBootCount 3 -TimeoutSeconds 120
```

The final acceptance rerun reused the already built managed/kernel artifacts with `-SkipManagedBuild -SkipKernelBuild`; the initial complete run built the C107 NativeAOT image, runtime pack, wallpaper ramdisk, and kernel. The runner validates the ELF, stages `/wall/C107.ELF`, boots QEMU, and writes a manifest.

Focused evidence is in:

* `out/dotnet/c011ec107-composite-application-dispatch/c107.manifest.json`
* `out/dotnet/c011ec107-composite-application-dispatch/inputs.json`
* `out/dotnet/c011ec107-composite-application-dispatch/boot-01/serial.log`
* `out/dotnet/c011ec107-composite-application-dispatch/boot-02/serial.log`
* `out/dotnet/c011ec107-composite-application-dispatch/boot-03/serial.log`
* `out/dotnet/c011ec107-composite-application-dispatch/build/composite/artifacts/HostLogProof.elf`
* `out/dotnet/c011ec107-composite-application-dispatch/build/composite/artifacts/HostLogProof.map`
* `out/dotnet/c011ec107-composite-application-dispatch/build/composite/artifacts/HostLogProof.elf.readelf.txt`

## Files changed

Production/functional changes:

* `kernel/core/include/kernel/nativeaot_application.h` — logical app ID/status fields and `launchLogical` API.
* `kernel/core/nativeaot_application.cpp` — selector propagation through the existing context, C107 marker observation, invalid return mapping, and resident/first-load report integration. The independent-image guard is unchanged.
* `kernel/core/main.cpp` — production C107 launch sequence and report output.
* `samples/managed/HostLogProof/HostLogProof.csproj` — `C107Composite` NativeAOT project mode.
* `samples/managed/HostLogProof/NativeAbi.cs` — stable IDs and `-4` invalid-selector return.
* `samples/managed/HostLogProof/Program.cs` — managed dispatcher, distinct logical apps, managed allocations, statics, markers, and bounded thread-static observation.
* `scripts/dotnet/build-managed-hostlog-proof.ps1` — accepts `C107Composite`.
* `scripts/generate-wallpaper-pack.ps1` — stages the one composite ELF as `C107.ELF`.
* `scripts/dotnet/run-c107-composite-application-dispatch.ps1` — build/stage/fresh-QEMU validator and evidence manifest.

Documentation:

* `docs/dotnet/NATIVEAOT_C107_COMPOSITE_APPLICATION_DISPATCH.md` — this report.

NativeAOT runtime-source changes: **none**.
GC behavior/policy/algorithm changes: **none**.
NativeAOT shutdown, unload, module unregister, EH/unwind, and code-manager dispatch changes: **none**.

## Final result and next phase

C107 proves the selected C106 architecture in the production launch path. Multiple logical managed applications can execute and return from one resident composite NativeAOT image while the guideXOS host remains alive and usable. Logical application statics persist across dispatch. Managed arrays allocate successfully for A, B, and the second A call. Logical return is an integer status to the dispatcher and then the kernel; it does not unload the application.

The exact next phase is **C108 — resolve and document the production TLS/[ThreadStatic] lifecycle boundary**. C108 should focus narrowly on whether resident redispatch should preserve, reset, or explicitly reinitialize thread-static values under the current TLS reinstall contract. It must preserve the one-runtime/one-module model and must not reopen C011EC12, GC forensic phases, NativeAOT multi-module support, or runtime shutdown/unload.

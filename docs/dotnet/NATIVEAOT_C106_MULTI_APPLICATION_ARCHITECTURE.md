# C106 — NativeAOT Multi-Application Architecture

Date: 2026-09-12  
Branch: `v1.1_DOTNET_SUPPORT`  
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`  
Locked NativeAOT identity: `9.0.0`, AMD64, Workstation GC, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

## Decision

**Outcome B — composite managed NativeAOT image selected.**

On guideXOS Server, “multiple C# applications” should mean multiple logical managed application entrypoints compiled into one NativeAOT image and selected by one managed dispatcher. It should not mean two independently produced ET_EXEC images resident in the current direct-ELF runtime domain.

This is compatible with the locked runtime because the runtime sees one image, one managed-code envelope, one authentic `CoffNativeCodeManager`, one `RuntimeInstance`, one `InitializeModules` call, one GC, one ThreadStore, and one TLS/PAL domain. The application dispatcher is ordinary managed code inside that image. Each logical application returns to the dispatcher; the dispatcher returns to the guideXOS launcher/kernel. This is a logical lifecycle boundary, not runtime shutdown or image unload.

The selection is architectural and source-supported. C106 did not build or execute a new composite image, so it does not claim `App B PASS`, composite-image GC, composite-image EH, or independent application isolation.

## Preflight and repository state

The required starting state was present and authoritative:

| Item | Starting value |
| --- | --- |
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| HEAD | `e88561aa5149ecc622e6a6570aad3bb6862cfba0` — `Trace NativeAOT resident module limitation` |
| Upstream | `origin/v1.1_DOTNET_SUPPORT` |
| Divergence | ahead `0`, behind `0` |
| Worktree | clean; `git status --short` empty |
| Diff stat | empty |
| Diff summary | empty |

No advanced HEAD, dirty-worktree work, reset, restore, clean, branch switch, history rewrite, push, canonical artifact replacement, production runtime edit, build, or QEMU run was performed during C106.

## Completed provenance

### C103 — resident lifecycle

C103, `docs/dotnet/NATIVEAOT_C103_PRODUCTION_APPLICATION_LIFECYCLE_RELAUNCH.md`, established the supported lifecycle as resident reuse. One image is mapped and registered once; the authentic managed entrypoint can be called again after return from managed code without repeating PAL hooks, GC startup, module initialization, code-manager registration, TLS setup, or ThreadStore creation. The valid two-launch boot reached managed entry and PASS twice, returned `0` twice, and returned to the ordinary kernel main loop. The negative sequence valid → missing → valid preserved the resident state.

C103 also established that the locked runtime has no supported complete shutdown/unload protocol: no usable runtime shutdown boundary, GC restart boundary, code-manager deregistration, module/type-manager unload, or image-unload protocol. A valid registered image therefore remains mapped.

### C104 — distinct images

C104, `docs/dotnet/NATIVEAOT_C104_MULTIPLE_PRODUCTION_APPLICATIONS.md`, produced two fixed-base artifacts:

* App A: `[0x100000000, 0x10012E000)`, entry `0x10004C5C0`.
* App B: `[0x102000000, 0x10212E000)`, entry `0x10204C600`.

The ranges do not overlap. The accepted behavior was `App A PASS → App B busy → App A PASS`. B was rejected before mapping, runtime registration, module initialization, managed entry, allocation, or GC participation. The evidence therefore proves that B was not rejected for a virtual-memory collision and does not prove B managed execution.

### C105 — singleton registration

C105, `docs/dotnet/NATIVEAOT_C105_MULTI_MODULE_RUNTIME_REGISTRATION.md`, authenticated the deeper reason for the Busy boundary. The loader has one `ResidentApplication`, while the runtime has one `RuntimeInstance`, one scalar code-manager slot, and one managed-code start/range pair. The runtime bridge has once-only `g_guideXosNativeAotCodeManagerRegistered` and `g_guideXosNativeAotModulesInitialized` state. Removing the Busy guard would not create a supported second-module path.

## Locked source audit

The primary source evidence is the ignored locked checkout at:

`out/dotnet/runtime-pack-c68/nativeaot-fp-repair-source/`

Its identity is locked by `tools/dotnet/runtime-pack/runtime-pack.lock.json:14-20,96-127`.

### RuntimeInstance and code-manager ownership

`RuntimeInstance.h:42-46` stores a single `ICodeManager* m_CodeManager`, a single managed-code start address, and a single managed-code range. `RuntimeInstance.cpp:96-108` implements managed-address recognition as one range test and returns that one manager. `RuntimeInstance.cpp:209-216` asserts that the manager slot is null before storing the manager and range. The registration function is therefore add-once, not a registry.

`RuntimeInstance.cpp:264-293` does maintain add-only TypeManager and OS-module lists. Those lists do not replace code-manager dispatch: `FindMethodStartAddress`, classlib-function lookup, unboxing-stub lookup, stack walking, EH, and GC-info decoding first need the correct code manager from the managed address. `RuntimeInstance.cpp:305-327` creates one ThreadStore for the instance and asserts `g_pTheRuntimeInstance == NULL` with the explicit message `multi-instances are not supported`.

### CoffNativeCodeManager contract

`CoffNativeCodeManager.h:34-106` exposes the per-image operations required by the runtime: `FindMethodInfo`, frame-pointer and safe-point queries, GC-reference enumeration, unwind, EH enumeration, OS-module lookup, classlib-function lookup, associated-data lookup, and method-start lookup. `CoffNativeCodeManager.cpp:186-197` owns image base, managed range, runtime-function table, and classlib-function table. `:254-295` validates a control PC against that image range and decodes its runtime-function table. `:434-499` enumerates GC references; `:651-842` performs unwind and reverse-P/Invoke transition handling; `:1060-1130` enumerates EH clauses; and `:1158-1182` performs classlib/associated-data lookup.

This makes the distinction precise: multiple manager objects could each contain valid image-local metadata, but the current `RuntimeInstance` has no supported address-to-manager registry that would select the correct object before these operations run.

### Module/type metadata and statics

`TypeManager.h` and `TypeManager.cpp:4-73` describe a module-local TypeManager containing the ReadyToRun header, GC-static section, thread-static section, classlib-function table, and its count. `RuntimeInstance::RegisterTypeManager` is add-only, but the current direct bridge supplies one generated module array and one classlib-function table to `InitializeModules`.

The bridge in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp:32284-32457` obtains the linker-produced `__modules_a`/`__modules_z` and `__managedcode_a`/`__managedcode_z` bookends, calls authentic `RhRegisterOSModule`, sets the code-manager flag, then calls authentic `InitializeModules`. It guards the whole sequence with once-only module and code-manager flags. That is correct for one composite image and not a complete per-module protocol for separately loaded images.

### GC, roots, ThreadStore, TLS, and PAL

The Workstation GC source has process/runtime-wide ownership: `gccommon.cpp:16-47` defines `g_theGCHeap`, `g_theGCHandleManager`, and global heap address state; `gc.h:319-323` describes the GC heap and handle manager as single instances. `gcenv.ee.cpp:94-145` implements `GcScanRoots` by iterating the shared ThreadStore. For each thread it scans inline thread-static roots, thread-static storage, and the managed stack. This is a shared root topology, not a separately selectable root domain per ELF image.

The NativeAOT ThreadStore is owned by RuntimeInstance (`RuntimeInstance.cpp:311-319`), while `threadstore.cpp:23-26,54-76,92-149` supplies the shared ThreadStore access and attach/detach operations. `startup.cpp:361-386` runs `PalInit`, registers process-exit cleanup, invokes `InitDLL`, and thereby reaches RuntimeInstance/GC startup. The shutdown code at `startup.cpp:309-357` is process/thread shutdown handling, not a supported reusable runtime teardown protocol.

The guideXOS bridge provides one PAL callback domain, one local-storage/FLS namespace, and one TLS installation path. C103 proved that reinstalling the same TLS base for resident reentry is safe; it did not prove per-image TLS domains. The bridge's `fillPalTable`, `fillGcTable`, `installTls`, and once-only flags are in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`; FLS/thread attachment is adapted by the adjacent `guidexos_nativeaot_fls_adapter.*` and `guidexos_nativeaot_threadstore_adapter.*` files.

## Architecture A — isolated runtime/address-space contexts

Separate page tables or virtual address spaces alone are insufficient. The locked runtime has C++ globals, a single `g_pTheRuntimeInstance`, a single code-manager/range slot, a single GC heap/handle manager, shared ThreadStore assumptions, PAL process initialization, and TLS/FLS state. If two contexts executed the same linked runtime globals, they would still overwrite or observe one another's state even if their managed images occupied disjoint virtual ranges.

A genuinely isolated context would have to isolate, at minimum:

* the RuntimeInstance object and global runtime-instance pointer;
* the GC heap, handle manager, card/heap address state, GC locks, allocation state, and all GC-to-EE callbacks;
* PAL initialization, callback tables, FLS/local-storage namespace, process-exit state, and VM callback ownership;
* ThreadStore, Thread records, suspend/resume state, thread attach/detach, TLS blocks, and thread-static storage/root ownership;
* the code-manager registry, managed-code ranges, method/EH/unwind/GC-info lookup, classlib tables, and unboxing-stub ranges;
* TypeManager/module lists, module initialization state, static bases, GC static roots, thread-static roots, and type metadata;
* all image/code/data/metadata lifetimes and any callbacks, handles, worker threads, and host resources that can retain image addresses; and
* kernel-visible process resources: address-space/page-table ownership, scheduling identity, handles, I/O, and failure/termination semantics.

That is a process-like isolation model, even if the kernel implements it without a conventional hosted process. The current source is not a supported multi-instance runtime design, so Architecture A is rejected for C106. It remains a possible long-term architecture only after a deliberate process/context boundary is designed and proven.

## Architecture B — one composite NativeAOT image

The locked toolchain has the right shape for a composite image. `HostLogProof.csproj:1-18` is a single `net9.0`, `win-x64`, `PublishAot` project with one native `EntryPointSymbol`; `:29-45` already changes the managed payload by build mode while preserving one image. The build script invokes one `dotnet publish` (`scripts/dotnet/build-managed-hostlog-proof.ps1:367-404`), and the project links one fixed-base image with one coherent managed-code range and one map (`HostLogProof.csproj:393-410`). The converter stages one ELF and one manifest entry (`scripts/dotnet/stage-managed-hostlog-proof.ps1:231,330-366`).

The supported composite shape is therefore:

```text
one NativeAOT image
  └── one authentic guideXOS entry
        └── managed dispatcher(appId, arguments)
              ├── logical App A entry method → return status
              └── logical App B entry method → return status
        └── return to guideXOS launcher/kernel
```

The image has one valid `CoffNativeCodeManager` and one coherent managed-code range covering both logical applications. All method metadata, GC info, EH tables, and unwind data are generated for that one image and are selected by the existing scalar manager. `InitializeModules` and module initializers run once for the composite image. App-specific static fields can be distinct fields in the same image, but they are not unloadable domains; values persist until explicitly reset or until the runtime context ends. Thread statics are per shared managed thread, not per application. GC static and thread-static roots are enumerated by the shared runtime, which is exactly the required model for one image.

The logical application lifecycle must be explicit:

* the dispatcher owns application selection and argument translation;
* each app returns an integer status instead of terminating the runtime;
* app cleanup must release host resources and stop/join app-created workers before return;
* app state that must be fresh must be reset by app code or by a per-invocation context object;
* no logical app may assume its code, statics, TLS, handles, or callbacks will be unloaded after return; and
* after dispatch returns, the managed image and runtime remain resident and the guideXOS launcher regains control.

This is the only evaluated option that satisfies the current one-manager, one-runtime contracts without a runtime fork. It is selected as the near-term supported architecture, with the lifecycle restrictions above made part of the application contract.

### What C106 does not claim for B

C106 did not add a dispatcher, modify the C# project, build a new composite artifact, or run QEMU. The existing C104 A/B boot remains evidence about two independent ELF images and the Busy guard; it is not evidence about a composite image. A follow-up phase must prove the composite dispatcher and its static/thread-static/lifecycle behavior before calling that implementation production-ready.

## Architecture C — genuine NativeAOT multi-module extension

Architecture C is technically describable but not bounded enough for the current phase. A loader-side vector would be insufficient. A defensible extension would require all of the following contracts to be designed, implemented, and tested together:

1. Replace scalar code-manager storage with an owned, synchronized multi-manager registry.
2. Register non-overlapping managed ranges and implement address → manager lookup for every runtime caller.
3. Route `FindMethodInfo`, method-start, GC-info, safe-point, GC-reference, associated-data, unboxing-stub, classlib-function, and method-range queries through the selected manager.
4. Route EH enumeration, hardware-fault remapping, reverse-P/Invoke transition lookup, and native unwind through the selected image metadata.
5. Define TypeManager/OS-module registration, duplicate registration, partial-failure rollback, and module identity rules.
6. Define per-module `InitializeModules` ordering and completion state while preserving runtime-global initialization ordering.
7. Define ownership of module static bases, GC static sections, class initialization, type metadata, and static GC-root enumeration.
8. Define thread-static storage/index ownership, inlined thread-static roots, thread-static GC roots, TLS/FLS namespace behavior, and ThreadStore interaction.
9. Define PAL ownership and lifetime for callbacks, local storage, worker/event slots, VM callbacks, and process-exit behavior.
10. Define image/code/metadata lifetime, no-use-after-unmap guarantees, duplicate registration prevention, application-entrypoint ownership, and the absence or design of unload semantics.
11. Rebuild and revalidate the runtime pack, linker bookends, generated module tables, PE→ELF conversion, code ranges, EH/unwind metadata, GC roots, ThreadStore, TLS, and all failure paths.

The current runtime explicitly fails the first two prerequisites: `RuntimeInstance` stores one manager/range pair and `RegisterCodeManager` asserts the slot is unused. The current bridge also makes module registration and initialization once-only. Architecture C is rejected as a C106 selection because it is a core NativeAOT runtime extension, not a bounded guideXOS loader repair.

## Architecture D — one resident module

Architecture D remains the current direct-ELF fallback and remains fully supported for the current runtime composition: one resident NativeAOT module/runtime domain, with repeatable entry into that same image. It is not a defect that C104 returns Busy for a different independently built image. The guard protects code-manager, module, statics, roots, TLS, and runtime lifetimes that have no supported second-module protocol.

D is rejected as the selected *multi-application meaning* only because B provides a safer way to package multiple logical applications without changing the runtime. D remains the supported fallback until the composite-image implementation is proven. It is also the correct behavior for attempts to load a second independent ELF module.

## Contract matrix for the selected architecture

| Contract | Composite image result |
| --- | --- |
| Managed-code address lookup | One coherent generated range and one authentic manager |
| Code-manager ownership | One resident `CoffNativeCodeManager` for the image |
| `FindMethodInfo` | Existing scalar dispatch selects the one image manager |
| EH and unwind | One image's generated `.pdata`/`.xdata`/EH metadata; no cross-image lookup |
| Module/type metadata | One generated module table and one `InitializeModules` lifecycle |
| Static bases and GC static roots | One image/domain; logical apps own explicit reset semantics |
| Thread statics and thread-static GC roots | Shared ThreadStore/TLS domain; not per-app isolation |
| GC | One shared Workstation GC and root universe |
| ThreadStore | One runtime-owned ThreadStore; app workers must be quiescent at return |
| TLS/PAL | One shared per-thread runtime cell and PAL/FLS namespace |
| VM ownership | One runtime/bridge callback domain and one resident image lifetime |
| Initialization | Runtime, code manager, and module initialization once per image |
| Managed entry | One native entry plus managed dispatcher to logical app methods |
| Managed return | Logical app returns to dispatcher; dispatcher returns status to kernel; image remains resident |

## Selection and rejected alternatives

Architecture B is selected because it preserves every currently authenticated runtime contract while giving guideXOS a coherent definition of multiple applications. A is rejected because it requires process-like isolation of all runtime, GC, PAL, ThreadStore, TLS, module, and kernel resource state. C is rejected because the locked runtime lacks the required multi-manager dispatch and unload/ownership contracts; a loader vector cannot supply them. D is retained as the current independent-ELF safety boundary and fallback, but it is not the desired packaging model for multiple logical C# applications. Outcome H is not applicable: the source and existing C103–C105 evidence are sufficient to select an architecture without speculative implementation.

## Required final report

1. **Outcome:** B — one composite managed NativeAOT image.
2. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
3. **Branch:** `v1.1_DOTNET_SUPPORT`.
4. **Starting HEAD/subject:** `e88561aa5149ecc622e6a6570aad3bb6862cfba0` — `Trace NativeAOT resident module limitation`.
5. **Ending HEAD/subject:** the C106 documentation commit; recorded in the final closeout response and repository log.
6. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
7. **Starting divergence:** ahead `0`, behind `0`.
8. **Ending divergence:** ahead `1`, behind `0` after the local documentation commit.
9. **Starting worktree:** clean; status, diff stat, and diff summary empty.
10. **Ending worktree:** clean after the C106 documentation commit.
11. **C103 provenance:** resident image/runtime registration once; authentic managed entry repeatable; valid two-launch and negative valid/missing/valid boots passed; no supported full shutdown/unload.
12. **C104 provenance:** fixed-base non-overlapping A/B artifacts; accepted `A PASS → B busy → A PASS`; B stopped before map/registration/managed execution.
13. **C105 provenance:** one `RuntimeInstance`, one scalar manager/range, once-only guideXOS registration/module flags; Busy is a safety boundary, not a VM collision.
14. **Relevant RuntimeInstance findings:** scalar `m_CodeManager`, scalar managed range, add-once registration assertion, scalar address lookup, one `g_pTheRuntimeInstance`, one RuntimeInstance-owned ThreadStore, and explicit `multi-instances are not supported` assertion.
15. **Architecture A findings:** separate address spaces alone do not isolate runtime globals; a valid design is process-like and must isolate runtime, GC, PAL, VM, ThreadStore, TLS/FLS, metadata, roots, and kernel resources.
16. **Architecture B findings:** one NativeAOT project/image can contain multiple logical managed app methods behind one managed dispatcher and one native entry; it preserves one runtime/module domain.
17. **Architecture C findings:** a real extension requires registry/lookup, EH/unwind, classlib, TypeManager/module, statics, thread statics, roots, ThreadStore, TLS, PAL, VM, lifetime, duplicate-registration, and entrypoint contracts; it is not bounded to a loader change.
18. **Selected architecture:** B, composite managed image with cooperative logical lifecycle.
19. **Rejected alternatives:** A requires process-like isolation; C is a core runtime extension; D remains the independent-ELF safety boundary but is not the multi-app packaging model.
20. **Meaning of multiple C# applications:** logical application entry methods compiled into one NativeAOT image, selected by a managed dispatcher, with explicit per-app state/reset and return-status semantics.
21. **Contract preservation:** yes for the selected composite shape, provided the image remains resident and all logical apps obey dispatcher/lifecycle rules; no claim is made that this was executed in C106.
22. **Defect or limitation:** architectural limitation, not a defect. The Busy behavior is correct for two independent modules under the current runtime.
23. **Production repair justified:** no multi-module runtime repair or Busy-guard removal is justified. A focused composite-image implementation is justified as the next phase.
24. **Production code changes:** none.
25. **Builds performed:** none during C106; publish/link/converter behavior was audited from source.
26. **QEMU runs performed:** none during C106; C103/C104/C105 evidence was not rerun.
27. **Evidence created:** this report only; no new generated or ignored C106 evidence directory.
28. **Files changed:** `docs/dotnet/NATIVEAOT_C106_MULTI_APPLICATION_ARCHITECTURE.md`.
29. **Validation performed:** source audit, preflight checks, `git diff --check`, full diff inspection, intended-file check, and clean-worktree verification.
30. **Commit SHA/subject:** recorded in the final closeout response after the documentation-only commit.
31. **Push status:** not pushed.
32. **Exact recommended next phase:** C107 — implement and validate a composite-image managed dispatcher: compile logical App A and App B into one image, dispatch by an explicit app identifier, prove `A PASS → B PASS → A PASS`, verify one code-manager/module initialization, and prove return-to-launcher/static/thread-static lifecycle semantics without adding multi-module runtime support.

## Final conclusion

guideXOS Server should represent multiple NativeAOT C# applications as logical entrypoints inside one resident composite NativeAOT image, because that is the safest model compatible with the current one-`RuntimeInstance`, one-code-manager, one-GC, one-ThreadStore, one-TLS/PAL runtime and it avoids pretending that disjoint virtual addresses provide module isolation; the smallest justified follow-up is C107, a narrowly scoped composite-image dispatcher implementation and proof of `A → B → A` logical dispatch and return semantics, with no multi-module runtime or unload work.

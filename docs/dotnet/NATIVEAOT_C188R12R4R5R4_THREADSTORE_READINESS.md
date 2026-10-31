# NativeAOT ThreadStore Attachment and Managed-Entry Readiness

- **Phase:** C188R12R4R5R4
- **Outcome:** `THREADSTORE_DEFECT_PROVEN`
- **Branch:** `v1.1_DOTNET_SUPPORT`
- **Starting HEAD:** `7a76bb77eb6595a14b76e664b3ae8b83b9f6b4f4`
- **Candidate source HEAD:** `5038777b9f6b1b489ceb9de3e26cf6299b46956f`
- **Pinned NativeAOT source:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

## Result

The pinned runtime creates and publishes a `ThreadStore` during `RhInitialize`'s runtime-instance initialization. It does not require the entry thread to be attached at that point. The supported reverse-P/Invoke path can attach the current thread later, after successful initialization and module registration.

The audit established two concrete defects in the experimental handoff path. The earlier R3 override wrote private values into the stock reverse-P/Invoke frame and bypassed the stock attachment helper. Separately, the freestanding PAL's `PalGetMaximumStackBounds` returned failure, while stock `Thread::Construct` requires valid stack bounds and fail-fasts if that query fails. Both defects are reached only after `RhInitialize` succeeds, so neither explains the earlier guest's `RhInitialize == false`. The R4 profile now delegates to the stock reverse-P/Invoke entry/return implementation and supplies stack bounds through the guideXOS PAL callback.

This is a source, hosted-regression, and build qualification. No guest was booted and no guest ThreadStore, managed entry, or GC behavior was executed. The earlier failed initialization remains unattributed; the previous guest result is not relabeled as success.

## Source-defined lifecycle

The source audit used the pinned checkout under `out/dotnet/gc-feasibility-baseline/nativeaot-runtime` and the exact source hashes recorded in the candidate manifest.

| Stage | Contract |
|---|---|
| A. Adapter startup | The host must establish the NativeAOT TLS envelope before runtime calls that read TLS. The adapter records its entry and raw state without assuming that a `Thread` is already attached. |
| B. `RhInitialize` | `startup.cpp` initializes the PAL and runtime DLL state, then calls `RuntimeInstance::Initialize`. `RuntimeInstance.cpp` creates a `RuntimeInstance`, calls `ThreadStore::Create`, stores the store on the instance, and publishes the runtime singleton. `RhInitialize` returns its actual Boolean result. Thread attachment is not part of this initialization sequence. |
| C. Module registration | The adapter registers the NativeAOT module and code manager only after `RhInitialize` succeeds, then runs module initialization. A failed initialization keeps the managed-entry gate closed. |
| D. Attachment readiness | Before a reverse-P/Invoke transition, the current thread may be absent from ThreadStore. A non-null RuntimeInstance and ThreadStore establish their availability, not attachment. The runtime's current-thread TLS accessor and the ThreadStore attached-thread list are separate state. |
| E. Managed transition | The supported `RhpReversePInvoke` path uses `ThreadStore::AttachCurrentThread` when the current thread has not already been initialized. The attach path is idempotent for an already-attached thread; otherwise it calls `PalAttachThread`, constructs the runtime `Thread`, and registers it under the ThreadStore lock with `TSF_Attached`. The stock reverse-P/Invoke frame and return routine preserve NativeAOT ABI state. |
| F. `ManagedMain` | Entry is eligible only after successful initialization, module registration and initialization, and successful supported reverse-P/Invoke attachment. The candidate does not force this call. |

Relevant pinned implementation units are `coreclr/nativeaot/Runtime/startup.cpp`, `RuntimeInstance.cpp`, `threadstore.cpp`, `thread.cpp`, `thread.h`, `threadstore.h`, and `threadstore.inl`. `RuntimeInstance::Initialize` creates the store; `ThreadStore::AttachCurrentThread` and `Thread::Construct` describe the later attach boundary. `Thread::Construct` obtains the OS thread id and maximum stack bounds before the thread can be registered.

### Defects and correction

1. **Reverse-P/Invoke frame contract.** The R3 diagnostic override put telemetry in `frame[0]` and `frame[1]`; those are stock frame fields. It also bypassed the stock helper that performs lazy attachment. The R4 runtime-pack build renames the pinned stock exports to `guideXosStockRhpReversePInvoke`, `guideXosStockRhpReversePInvokeReturn`, and the stock attach helper symbol, then delegates entry and return to the stock implementation. Diagnostic nesting state uses a separate fixed-capacity, allocation-free LIFO (capacity 8), outside the stock frame. Regression coverage checks preservation of the frame, nested calls, and overflow rejection.

2. **Freestanding stack bounds.** The pinned Windows `PalGetMaximumStackBounds` implementation in `Runtime/windows/PalRedhawkCommon.cpp` relies on `VirtualQuery`. The guideXOS shim returned zero, so a later `Thread::Construct` could not obtain stack bounds and would fail-fast. The R4 runtime archive omits that stock common-PAL object and supplies `guidexos_nativeaot_pal_common_replacement.threadstore.obj`. Its replacement calls `guidexos_nativeaot_pal_stack_bounds`, validates and records low/high/current/status values, and returns failure if the kernel callback does not provide valid bounds. This fixes the attachment prerequisite without changing allocation, collection, or the runtime's lazy-attachment architecture.

These are post-initialization defects. They do not explain or repair the prior `RhInitialize == false`; that failure still needs an initialization trace from a future guest.

## Diagnostics and hosted coverage

The bounded marker family is `C188R12R4R5R4_THREAD`. Instrumented boundaries include adapter entry, raw `RhInitialize` return and kernel mapping, runtime and ThreadStore pointers, current-thread TLS/state, module registration and initialization, transition entry, PAL attach, thread id, stack bounds, attach completion/incompletion, managed dispatch/entry, and transition return. Raw values are captured separately from readiness classification. The current-thread accessor is explicitly not treated as proof of list attachment. Diagnostic fields and markers are confined to the isolated profile.

The focused run at `out/dotnet/c188r12r4r5r4-threadstore-readiness/hosted-20261031T174732492Z` passed. It compiled the allocation-free transition telemetry helper with `-Wall -Wextra -Werror` (`framePreserved=2`, `nested=1`, `overflowRejected=1`) and exercised eight synthetic serial/analyzer fixtures: no markers, `RhInitialize` false, inconsistent Boolean mapping, incomplete module registration, null runtime/store/TLS state, incomplete attachment, PAL stack-query failure, and complete supported handoff. These fixtures validate the gates and parser; they simulate runtime states and do not execute actual NativeAOT ThreadStore attachment.

Other hosted qualifications passed:

- R1 initialization attribution regression; R3 return-contract regression; and GC ownership hosted proof.
- C188 factory predicates (`9` cases) and host qualification (`14` factory, `9` predicate, and `9` link-boundary cases), R12R3 lifetime predicates (`13`), R11 lifecycle (`7`), R11 output bounds (`6`), R11R4 QAC ownership (`15`), and R10/R11/R11R2 runner predicate suites (`13`, `30`, and `24`). R11R4R2 runner predicates exited successfully; QAC stream capture passed `2` cases; C181 QMP event capture passed.
- Managed UI regressions passed 40 suites / 969 cases. Direct C167/C168 qualification under Windows PowerShell 5.1 passed: C167 20 cases (60 stress cycles), C168 31 cases (250 page cycles, 250 selection changes, 100 apply/reset cycles, 250 popup cycles), and zero direct slot references. The aggregate wrapper's C167/C168 step twice failed with a blank nested `dotnet.exe` `NativeCommandError`; the direct qualification passed independently, so aggregate-wrapper success is not claimed.

`smoke-native-stack-bounds.ps1` passed its hosted stack-bound checks, bare-metal stack-bound compile, 14 adapter probes, and inactive ThreadStore probe. Its final generic kernel build failed on unrelated pre-existing NativeAOT integration references, including `nativeaot` undeclared in `kernel/core/desktop.cpp` and missing `NativeAotManagedSurface` members / `ManagedReturnTarget` in `nativeaot_application.cpp`. The R4 specialized diagnostic kernel and separate proof-disabled production kernel both built successfully. The generic smoke script therefore remains a failed aggregate even though the targeted checks passed.

The future R4R1 runner path was checked statically only. It verifies candidate identities, performs exclusive QAC preflight, uses a one-boot `CreateNew` guard with automatic retry disabled, preserves raw serial separately from parsed evidence, tracks QAC ownership before/during/after, and waits for the terminal GC proof result after managed entry. It was not invoked in this phase.

## Builds and candidate

The isolated diagnostic NativeAOT candidate and its specialized diagnostic kernel built successfully. The separate proof-disabled production NativeAOT and production kernel builds also passed; both production logs had zero warnings and zero errors. Production qualification records `productionNativeAotPersistentCompositeLifecycle=true`, with diagnostic instrumentation and proof profiles excluded.

The diagnostic managed/runtime build completed with 66 warnings and zero errors (56 `LNK4099`; remaining macro, linker, analyzer/compiler, and toolchain warnings). The specialized diagnostic kernel completed with 219 warnings and zero errors. The candidate PE import directory has RVA and size zero, so it has no Windows loader import table. The map attributes `PalGetMaximumStackBounds` to the replacement object and excludes the stock `PalRedhawkCommon.cpp.obj` member.

- **Candidate root:** `out/dotnet/c188r12r4r5r4-threadstore-readiness/candidate-20261031T174601065Z`
- **Candidate result:** `PASS_BUILD_READY_FOR_THREADSTORE_READINESS_GUEST`
- **Manifest SHA-256:** `36657A671D9088E51005D87F3D72033E88C65E234F932AFA32014F5858B9442E`

| Artifact | SHA-256 |
|---|---|
| Managed PE | `3B38117F2A7729135857063E51DA1B590B36BD7F409216A2856AD52E2F13187A` |
| Managed map | `CD56254E4A7645D3F7E597D66E5CB40E2144C1A995D0ECE5E88391635036A070` |
| Managed ELF | `3B79938BEAC61F828D012BBAE9017934FCD0A536FCC464A7B5B5D39B03633175` |
| Diagnostic kernel ELF | `223EB621BDBD3CACA97F2A511F1090D46F8146C6FA98619CD9C801FFF8124B05` |
| Runtime-pack platform object | `5CC3406DB4008806D7A7930D7FCEDF4F12559A75A1B9349684541C5A6EC0CAEC` |
| Runtime-pack library | `EA0C6D64397BE22B06DF19784019F79891C4CB6EFADCF4CB7CDD4B0C3EE9D34F` |
| Runtime-pack manifest | `71C2ACF9355432D590505BC6F4554602724F26760DA4EC2C0C78A28C4D0363DA` |
| ThreadStore PAL common replacement object | `C2D84686F3683C2888680952DF34DB04E258596358D0BD2B853C9F6D365B9213` |
| Startup adapter object | `3270FEC7B671186D163D6699546EE2994D055B9F505C3F1030859AA6E126CDD4` |
| Production NativeAOT ELF | `2FAD05E138D3A807E271A10087B46C2C951E78CCEA183A0E70D96C368F2B5B55` |
| Production kernel ELF | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |
| Staged bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Staged ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The candidate manifest contains hashes for the remaining sources, logs, maps, runtime-pack contents, staged files, and qualification manifests. It records the pinned source-file hashes, the implementation source HEAD above, `qemuLaunched=false`, `qacReserved=false`, `managedMainEnteredByCandidate=false`, and `actualNativeAotThreadStoreExecuted=false`.

## Preserved state and next phase

The workspace `ESP/ramdisk.img` hash remains `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`; the candidate's staged copy has the same hash. Existing generated/untracked host outputs were preserved. The implementation was committed as `5038777b9f6b1b489ceb9de3e26cf6299b46956f`; this closeout report is committed separately. No push was performed.

No QEMU was launched and QAC was not reserved. The smallest justified next phase is **C188R12R4R5R4R1**, using the staged candidate and its fresh one-boot guard. That guest should capture the actual `RhInitialize` result and first failing initialization boundary; only on success should it observe module registration, stock reverse-P/Invoke attachment and stack-bound results, and managed entry. Run the isolated GC ownership proof only after those gates pass. This phase's hosted results do not establish guest ThreadStore attachment or GC correctness.

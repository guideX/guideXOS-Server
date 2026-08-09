# NativeAOT thread-static runtime support

Date: 2026-08-09. Final classification: **Outcome A**.

This pass answered the missing NativeAOT `[ThreadStatic]` contract exposed by
the prior non-null-root attempt. It did not scan roots, invoke `GcScanRoots`,
promote, mark, restart the EE, or resume managed execution from a GC boundary.

## Checkpoint and locked identity

Starting branch: `v1.1_DOTNET_SUPPORT`.

Starting HEAD: `febf2c71ceb37a77a02c3d6c87f5434e25e2f40d`.

The starting worktree was clean. No commit, amend, reset, or history rewrite
was performed. The ending worktree is intentionally dirty with the runtime,
proof, script, documentation, and generated evidence changes listed below.

Locked NativeAOT identity is unchanged: NativeAOT 9.0.0, AMD64, Workstation GC,
interfaces `5.3 / 2`, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## Inherited fault: exact instruction and address calculation

The inherited three-boot Outcome E fault was RIP `0x1008E2BE`, CR2
`0xFFFB5FF9`. The immutable source artifact is
`out/dotnet/gc-first-non-null-root-callback-boundary/build/artifact/NativeAotGcSingleThreadSuspendEe.exe`.
The complete captured analysis is
`out/dotnet/thread-static-runtime-support/run-20260809-154140040/inherited-fault-analysis.md`.

The exact instruction is:

```text
RIP 0x1008E2BE: 48 8B 52 10    mov rdx,QWORD PTR [rdx+0x10]
```

It is four bytes long and reads one 8-byte machine word. At the fault:

```text
RDX = 0x00000000FFFB5FE9
CR2 = 0x00000000FFFB5FF9
EA  = 0x00000000FFFB5FE9 + 0x10 = 0x00000000FFFB5FF9
```

Relevant registers were RAX `0`, RBX `0x392CBE0`, RCX `0`, RSI `0`, RDI `0`,
RSP `0x4E68B30`, RBP `0x4E68C10`, and GS base `0x392CCE0`. The immediately
preceding helper was `RhpGetModuleSection` at `0x1000A4F0`; its section length
made the generated code select the dynamic-type path.

The faulting generated method was
`S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow`,
source `System.Private.CoreLib/src/Internal/Runtime/ThreadStatics.cs:36-51`.
The RIP is not a TLS/FLS base calculation, module TLS offset, thread-static
index, or `Thread*`-relative field. The load first fetched the cell
`__GCSTATICS@S_P_CoreLib_Internal_Runtime_Augments_RuntimeAugments@@` at
`0x1011C260`; that cell still contained the invalid dehydrated placeholder
`0xFFFB5FE9`. CR2 was therefore the direct effective address of an
uninitialized NativeAOT GC-static base, not a corrupted TLS index.

## Locked NativeAOT architecture

The exact source trace is:

| Layer | Locked source and contract |
|---|---|
| Managed declaration/access | `System.Private.CoreLib/src/Internal/Runtime/ThreadStatics.cs:28-51,85-145`; negative type-static index uses the inlined base, nonnegative index uses `RhGetThreadStaticStorage` and a per-module array. |
| Slow helper | `GetInlinedThreadStaticBaseSlow` obtains `MethodTable.Of<object>()->TypeManager`, calls `AllocateThreadStaticStorageForType(typeManager,0)`, calls `RhRegisterInlinedThreadStaticRoot`, then publishes the base. |
| Storage allocation | `ThreadStatics.cs:121-145` calls `RhGetModuleSection` for `ThreadStaticRegion`; dynamic types use `RuntimeAugments.TypeLoaderCallbacks.GetThreadStaticGCDescForDynamicType`, then `RhNewObject` allocates the descriptor-defined storage object. |
| Runtime imports | `System.Private.CoreLib/src/System/Runtime/RuntimeImports.cs:559-564`: `RhGetThreadStaticStorage` and `RhRegisterInlinedThreadStaticRoot`. |
| Runtime thread | `Runtime/thread.cpp:1251-1281`: `Thread::GetThreadStaticStorage` returns `&m_pThreadLocalStatics`; the inline root is registered on the current `Thread` with its `TypeManager`. |
| Thread layout | `Runtime/thread.h:76-84,311-314`: `RuntimeThreadLocals::m_pThreadLocalStatics`, `m_pInlinedThreadLocalStatics`, and `InlinedThreadStaticRoot`. |
| Module startup | `Common/src/Internal/Runtime/CompilerHelpers/StartupCodeHelpers.cs:30-55,74-115,122-152,188-245`: `InitializeModules` rehydrates metadata, creates TypeManagers, initializes GC static bases, publishes module tables, and runs eager constructors. |
| Augments callback | `System.Private.CoreLib/src/Internal/Runtime/Augments/RuntimeAugments.cs:293-300,633-664`: thread-static lookup and TypeLoader callback publication. |
| Native section lookup | `Runtime/MiscHelpers.cpp:413` implements the `RhpGetModuleSection` path used by the generated helper. |

The direct ELF launcher was calling `ManagedMain` after `RhInitialize` and TLS
installation, but it bypassed the generated bootstrapper's
`InitializeModules(osModule,__modules_a,count,classlibTable,16)` call. Thus
ordinary NativeAOT thread-static access reached the real lazy path before
`RuntimeAugments` GC statics and TypeLoader callbacks were published.

The missing contract was therefore **generated NativeAOT module startup**, not
OS TLS, FLS, a fabricated root slot, or a thread registration redesign.

## Generated primitive and reference paths

The proof fields are in
`samples/managed/HostLogProof/Program.cs:9-20,116-193`:

```csharp
[ThreadStatic] private static int s_threadStaticInt;
[ThreadStatic] private static byte[]? s_threadStaticRef;
```

The generated map for the combined image is
`out/dotnet/thread-static-runtime-support/build/artifact/NativeAotThreadStatic.map`:

* `HostLogProof_HostLogProof_Program__ManagedMain`:
  `0x10063A60`;
* `S_P_CoreLib_Internal_Runtime_ThreadStatics__GetInlinedThreadStaticBaseSlow`:
  `0x1008AEF0`;
* `?__THREADSTATICINDEX@HostLogProof_HostLogProof_Program@@`:
  `0x100C1CA8`;
* `RhpGetModuleSection`: `0x10007C10`;
* `RhGetThreadStaticStorage`: `0x10007720`;
* `RhRegisterInlinedThreadStaticRoot`: `0x10007750`;
* `RhpAssignRef`: `0x10008910`;
* `RhpCheckedAssignRef`: `0x10008980`.

The primitive generated sequence obtains the same thread-static base and reads
and writes the `int` at base offset `0x8`. The reference sequence uses the
same base and stores the `byte[]` at base offset `0x10`. Thus primitive and
reference fields use the same storage lookup and lazy initialization path;
the reference adds ordinary NativeAOT reference assignment/barrier semantics.

The generated combined disassembly proves the shape: `ManagedMain` calls the
record hook, reads/writes `[base+0x8]`, allocates the 16-byte array, writes the
reference through `RhpAssignRef`, reads `[base+0x10]`, and compares exact object
identity. The dynamic slow helper calls `RhpGetModuleSection`, the dynamic
TypeLoader helper when needed, `RhNewObject`, `RhRegisterInlinedThreadStaticRoot`,
and `RhpCheckedAssignRef` to publish the base.

## TLS/FLS and runtime-thread relationship

NativeAOT uses its own `Thread` storage abstraction reached through the
current-thread runtime state; it does not use an OS TLS index as the thread
static storage itself. guideXOS supplies the required ABI TLS vector at
GS:`0x58`, with assumed module TLS index `0`. The vector points to the
`0x110`-byte runtime block, whose FLS cell `0` contains the same NativeAOT
`Thread*` used by `ThreadStore`.

Combined proof observations were stable across all fresh boots:

* runtime `Thread*`: `0x000000000392AC00`;
* native thread ID: `0x00000000100CDF78`;
* TLS block: `0x000000000392ABD0`;
* FLS/runtime identity: `0x000000000392AC00`;
* `Thread::GetThreadStaticStorage()` return: address of the real
  `m_pThreadLocalStatics` field, `0x000000000392AC90`;
* `InlinedThreadStaticRoot`: `0x000000000392ABE0`;
* storage object: `0x0000000100A01F38`, size `0x90` bytes in the combined
  descriptor-defined object;
* owning TypeManager descriptor: `0x00000000102197F0`;
* registered managed threads: exactly `1`;
* duplicate storage blocks: `0`.

The primitive-only and reference-only images have their own descriptor-defined
storage sizes (`0x88` and `0x88` respectively); the combined image is `0x90`.
No static global proof buffer or hard-coded field address services access.

## Correction

`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp` now
implements the real generated startup call. It resolves the actual generated
`InitializeModules`, `__modules_a`, `__modules_z`, `PalGetModuleHandleFromPointer`,
and the locked 16-entry classlib table, then calls the generated entry exactly
once after `RhInitialize` and before managed entry. This is a production runtime
correction shared by ordinary NativeAOT applications; it is not gated to the
proof field. The proof-gated additions are diagnostics, marker exports, the
managed workload, and the QEMU harness only.

The direct launcher also publishes the already-constructed current `Thread`
into the real ThreadStore before the proof records ownership. It does not
construct another managed thread or alter general thread registration.

## Standalone proof

Markers are `7A510001` primitive start, `7A510002` primitive success,
`7A510003` reference start, and `7A510004` reference success. The proof does
not call `GC.Collect`, `GcScanRoots`, promotion, or marking.

Primitive-only image, run
`out/dotnet/thread-static-runtime-support/run-20260809-155819459/`:

* initial read: `0x00000000`;
* assignment/readback: `0x13572468` / `0x13572468`;
* result: 3/3 PASS, marker `7A510002`;
* storage initialization requests/entries/completions: `1/1/1`;
* storage allocation observation: `1`; repeated lookups: `1`.

Reference-only image, run
`out/dotnet/thread-static-runtime-support/run-20260809-160346858/`:

* initial read: null;
* assigned object: `0x0000000100A01FC0`;
* readback: `0x0000000100A01FC0`;
* identity match: `1`; object contents valid: `1`;
* result: 3/3 PASS, marker `7A510004`;
* storage initialization requests/entries/completions: `1/1/1`;
* storage allocation observation: `1`; repeated lookups: `0`.

Combined image, final run
`out/dotnet/thread-static-runtime-support/run-20260809-160559276/`:

* primitive initial/assigned/readback: `0 / 0x13572468 / 0x13572468`;
* reference assigned/readback: `0x0000000100A01FC8 / 0x0000000100A01FC8`;
* identity match: `1`; object contents valid: `1`;
* result: 3/3 PASS, marker `7A510004`;
* storage initialization requests/entries/completions: `1/1/1`;
* storage allocation observation: `1`; repeated lookups: `3`;
* write-barrier diagnostic callbacks: `0` (no wrapper was added). The generated
  map and disassembly identify `RhpAssignRef` at `0x10008910`; the reference
  assignment reached it and completed. No new write-barrier infrastructure was
  required.

The managed reference was a deliberately small `byte[16]`, filled with
`0xA0..0xAF`; readback validated every byte and used `GC.KeepAlive`.

## QEMU evidence and no-GC result

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

Final combined proof kernel SHA-256:
`F145126AFAF72050F9C14379AA26034850303455C6DABD5A09AFB710DFF9B94F`.

| Run | Result | Serial SHA-256 |
|---|---|---|
| first-run | PASS | `8324A24F0503CBEEE7559C13DB73F82EEA5EAC667F7943D049DAAF959E7860AA` |
| repeat-1 | PASS | `17B59653CA606DCCC1149B58ABC2B1F42E82A640B7423753C8C3605F2A170D9C` |
| repeat-2 | PASS | `5607ED1AE413AD971DCC30668BFFFA75AE94B933068C2A278AC79212C90C1792` |

All three combined boots recorded one managed thread, the same FLS/runtime
identity, one storage publication, no duplicate storage, zero faults, and the
same deterministic success fields. Standalone proof counters were:

```text
collection requests = 0
collection entries  = 0
suspension requests = 0
```

The reference proof's one managed allocation was the deliberate `byte[16]`
proof object. The thread-static storage allocation itself was observed once
through the real `RhNewObject` path; no proof-specific allocator was added.

## Regression results and retained limitations

* `C011EC05`: PASS, 3/3 fresh QEMU boots in
  `out/dotnet/gc-first-root-candidate-load/run-20260809-161259572/`. One real
  candidate slot load, raw null, zero callbacks/promotion/marking/mutation/
  restart/resume. The changed startup allocation count is `0x25`; the report
  retains that as production startup cost. `C011EC06` was not attempted.
* FLS/local-storage validation: PASS, including the NativeAOT FLS adapter.
* Hosted native-thread validation: PASS. QEMU native-thread validation remains
  non-clean/retained: unrelated scheduler stack/ABI and wait/timer assertions
  ended in `ALL_FAIL`; no thread-static claim depends on it.
* First-allocation QEMU: blocked at harness preflight because the existing
  script did not find its expected compile-definition text; no new allocation
  claim is made.
* First-refill QEMU: non-clean. The real bounded proof entered the loop but
  failed the established context geometry/counter contract after the shared
  startup allocation change; no thread-static claim depends on it.
* Multiple-refill/first-segment QEMU: non-clean at the established exact
  counter assertion; retained as a GC regression limitation.
* Hosted single-allocation static proof: PASS.
* Repeated-allocation static proofs: PASS for both 4 KiB (`14` allocations)
  and 64 KiB (`234` allocations). The first 4 KiB `-SkipBuild` invocation was
  correctly rejected for using a 64 KiB artifact and is not counted as a pass.
* Runtime-pack state: PASS. Runtime-pack static identity check remains blocked
  by the pre-existing observed identity mismatch; it is not counted as pass.
* Script parsing, JSON manifest parsing, ELF/map/readelf validation, serial
  field validation, exact ordinary-kernel hash validation, and
  `git diff --check`: PASS.
* Broad native-stack wrapper and malformed-code checks remain explicitly
  non-clean/blocked per their historical classification.

No GC root support is claimed by this report. Correct thread-static
initialization does naturally populate the real `InlinedThreadStaticRoot` and
`Thread::GetThreadStaticStorage` state later consumed by GC enumeration; that
relationship is observed here, but callback/promotion semantics remain the
next milestone.

## Ordinary runtime impact and restoration

The correction is shared production runtime behavior. It changes the ordinary
NativeAOT startup image because it now performs the required module publication
and eager initialization. The previous ordinary kernel/ESP hash was
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`. After a
clean exact build, the ordinary kernel/ESP payload was restored with the new
production hash:

```text
kernel.elf = 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ESP/kernel.elf = 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
```

The source change responsible for this ordinary-image change is the
production `InitializeModules` call in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.
It is correct for ordinary NativeAOT applications because it restores the
locked bootstrap contract that publishes module metadata, GC static bases,
TypeManagers, and runtime callbacks before managed entry.

## Files and next milestone

Production/runtime changes:

* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp` —
  real `InitializeModules` startup contract, current-thread publication, and
  proof-gated identity diagnostics;
* `tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_managed_host_shims.cpp` —
  only linkability shims for unused old proof imports.

Proof and harness changes:

* `samples/managed/HostLogProof/Program.cs` and `.csproj` — separate primitive,
  reference, and combined workloads;
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_thread_static_diagnostics.h`;
* `kernel/core/nativeaot_pal_qemu_test.cpp`, its header, `kernel/core/main.cpp`,
  and `kernel/Makefile` — bounded diagnostics, markers, and selector;
* `scripts/smoke-nativeaot-thread-static-runtime-support-qemu.ps1`;
* the narrowly updated historical candidate-load validator only to accept the
  final marker ordering and legitimate startup-count delta.

Evidence manifest:
`out/dotnet/thread-static-runtime-support/run-20260809-160559276/manifest.json`.

Recommended next smallest milestone: return to the previously blocked
`C011EC06` experiment and reach the first genuine non-null GC root callback
boundary, carrying forward this real thread-static storage state. Do not claim
root support until that separate boundary is proven.

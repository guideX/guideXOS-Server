# NativeAOT R21 — Real Settings Factory Integration

## Result

**Outcome D — `REAL_SETTINGS_BOUNDARY_NARROWED`.** R21 builds the existing selector-5 Settings factory into a fresh NativeAOT candidate using the R20 unwind repair and a real Workstation GC profile without the C188 image arena. The one authorized QAC boot initialized the native PAL/VM/GC seam, completed the C188R11 host initialization markers, and stopped at `C102-STARTUP stage=C188B002`. It produced no managed-entry, Notes-ready, selector-5, Settings factory, allocation, registration, or return evidence. The cause and Settings factory result are therefore not established.

This is not evidence that the historical Settings allocation failure still occurs, nor that it is fixed. The R21 candidate changed the allocator topology, but the guest did not reach a managed allocation.

## Git and preservation

- Starting branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `fa10bf447b419971557b5a7bc0e320a4a6cc1d01`
- R21 source implementation commit: `e3b997236e39c034580d46429f087efb2bc41298`
- The report is committed as a documentation-only closeout directly above the implementation commit. The final repository HEAD is the report commit; its exact object ID is in the task completion record.
- No push was made.

The three pre-existing modified root objects remain modified and were not staged or consumed as unverified build inputs:

| File | Preserved SHA-256 |
| --- | --- |
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

## Existing Settings path and contract

The R21 candidate keeps the existing C160 composite App Model and its selector table. `GuideXosApplicationRegistry` registers selector 5, `Managed Settings Center`, with a managed `Func<GuideXosApplication>` that calls `C188R12R4FactoryDiagnostics.CreateSettingsCenter()`. That method first touches the existing Settings type-initialization contract, then returns `new ManagedSettingsCenter()` as the factory result. It accepts no arguments and has no native-facing factory ABI; the delegate invocation and returned reference are managed-to-managed.

The external application boundary remains the existing AMD64 NativeAOT startup path:

1. The native exported `GuideXosNativeAotApplicationEntry(void*)` uses the C `__cdecl` ABI and receives the established startup context.
2. It installs the PAL hooks, PAL hook table, and GC platform hooks, calls `RhInitialize(0)`, installs current-thread TLS, then calls the exported `ManagedMain(void*)` entry.
3. The managed entry validates the existing `NativeGxAppContext`, uses its selector and generation fields, and dispatches through the App Model. It returns the existing integer `GxAbi` result to native code.
4. On a selector-5 launch, `GuideXosManagedApplicationLifetime.TryStart` invokes the registered factory, rejects a null result, and stores the object in its static lifetime pool with selector and generation. The App Model then calls the application's existing `Launch(host)` method.
5. `ManagedSettingsCenter.Launch` builds the existing control composition and asks the host to create a surface with `TryCreateSurface`. That surface request is the established native/window integration point. It is separate from factory construction and was not reached in R21.

The real constructor initializes the Settings model, control groups, section arrays, callbacks, and existing layout state. The broader existing feature path also depends on the managed UI controls and host services. R21 does not add a replacement application, factory API, registration table, or native raw-object pointer.

The static App Model lifetime pool is the intended strong managed owner after factory return. R21 adds a bounded diagnostic check: it temporarily creates a `WeakReference<GuideXosApplication>`, clears the local factory result, requests one blocking compacting collection, then looks up the same selector/generation in the pool and compares reference identity. The check emits `C188R21-ROOT` only if a collection occurred and the same resident object remains reachable. It does not store a raw managed address or install a permanent diagnostic root. This managed check is compiled only into the R21 profile and is not evidence from the native guest because selector 5 was not reached.

## Historical failure and stack budget

The earlier selector-5 stall was progressively localized in the R12R3/R12R4 reports. The preserved guest evidence showed:

- A 136-byte Settings object request succeeded via the stock object path while 104 bytes remained in the image-backed 64 KiB arena; the object was returned outside that arena.
- The next observed array request was 88 bytes, while the current thread allocation context had 64 bytes available. The R4 bounded wrapper fail-fast path ran before the stock array slow path. The exact requesting array/type was not identified.
- The factory did not reach the base constructor marker, lifetime registration, `ManagedSettingsCenter.Launch`, or a factory return.

The older “5,624 bytes remaining” observation was heap-arena capacity measured before the Settings invocation, not a stack measurement. The associated reports explicitly distinguish it from stack space and from the 64 bytes of local allocation-context capacity at the failing array request. The 88-byte request therefore established a bounded allocation-context failure in that candidate, not exhaustion of all managed memory.

R21 changes that prerequisite: its build profile uses the NativeAOT Workstation GC object/array allocation path and sets `managedHeapBytes=0` with `imageBackedArenaPresent=false`. Thus the old R4 precheck is absent from this profile; the R21 boot did not reach the managed code needed to confirm stock allocation behavior for Settings.

R20 separately proved that a previous GC-root scan stall came from an `RtlVirtualUnwind` import bound to a no-op (`xor eax,eax; ret`). The R20 repair delegates to the shared Windows AMD64 unwind adapter and correctly updates caller state. The historical selector-5 88/64 allocation failure was established before that root-scan diagnosis and was not attributed to unwind. R21 does not conflate those two findings.

The current kernel bootstrap stack is declared as 16 KiB (`boot_stack_bottom` through `boot_stack_top` in the AMD64 boot assembly); the kernel registers those exact bounds for the bootstrap thread, and PAL exposes them through `PalGetMaximumStackBounds`. R21 adds an out-of-line stack marker at the real Settings factory entry and computes the distance to the live PAL low bound. That marker did not run, so the actual remaining bytes at factory entry are **unknown**. The configured 16 KiB span is not a substitute for a live remaining-stack reading, and the old 5,624-byte heap figure must not be reused as one.

## R20 foundation and R21 source changes

The candidate retains the R20 shared `guideXosRtlVirtualUnwind` implementation. Its runtime manifest is checked for the implementation identity, source/object hashes, and linked-by-managed-proof bit; the NativeAOT link response must contain that exact object. The preserved R20 hosted regression replays the captured frame through the production-shared primitive and Windows ABI adapter, recovering caller RIP `0x100001A8E`, advancing SP by `0xA0`, and restoring the saved RBP pointer at `SP+0x90` (31 assertions passed).

R21 source changes are limited to the existing integration and candidate tooling:

- `C188R12R4FactoryDiagnostics` adds R21 entry/body/result/exit markers and the PAL-based live stack probe while retaining the selector-5 real factory.
- `GuideXosApplication` adds the temporary weak-reference/compacting-GC ownership check against the existing static lifetime pool.
- The project and build gates add a separately guarded R21 profile that selects C160 composite App Model + R11 state provenance + R12R3 lifetime tracing + persistent runtime initialization + R20 Workstation GC allocation. It rejects the image-backed C188 arena and keeps the standalone R5 GC proof entry point out of the R21 image.
- The candidate builder isolates outputs under E:, validates source branch/HEAD and input hashes, checks the runtime-pack/unwind provenance and linked symbols, stages the existing ramdisk, runs the hosted R20 import regression, and writes a 659-file integrity inventory.
- Runner, staging, kernel-build, and Settings-handoff predicates now admit the E: candidate and the R21 root/collection evidence shape without changing the production App Model contract.

## Hosted qualification

Focused hosted/source checks passed:

| Check | Result |
| --- | --- |
| R21 Settings factory source contract | 9 assertions passed |
| R12R4 factory host aggregate | 14 factory + 9 predicate + 9 link-boundary cases passed |
| R12R3 lifetime predicates | 13 passed |
| R11 runtime lifecycle / runner / output bounds | 7 / 30 / 6 passed |
| R5 GC ownership proof | Passed |
| R4 ThreadStore readiness | 7 mode-model + 6 telemetry assertions passed |
| R2R2R4 safe handoff | Passed |
| R4 QAC admission | 35 predicates passed |
| R20 iterator/unwind regression used by the candidate builder | 31 assertions passed; zero native boots |
| `git diff --check` | Passed (Git emitted only the existing LF-to-CRLF working-copy notices) |

These checks prove the source/build contracts and hosted regressions only. They do not prove native Settings construction, `Launch`, surface creation, rendering, input handling, persistence, or ownership after a live native collection.

## Candidate provenance

Candidate root:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r21-settings-factory\candidate-20261114T145926714Z-81611eeea5914d6ca9578c24e80ab729
```

The candidate manifest records starting/source HEAD `fa10bf447b419971557b5a7bc0e320a4a6cc1d01` on `v1.1_DOTNET_SUPPORT` plus the R21 working-tree changes that were later committed as `e3b997236e39c034580d46429f087efb2bc41298`. This distinction matters: the build was made before the source commit, from that exact checked-out source plus its R21 changes.

The manifest result is `PASS_BUILD_READY_FOR_QAC_PREFLIGHT`, profile `C160Composite+C185+C188R11+C188R12R3+R20WorkstationGC+R21SettingsFactory`. It binds selector 5 to `ManagedSettingsCenter` and records constructor symbol `ManagedSettingsCenter___ctor_0`. The map contains `GuideXosNativeAotApplicationEntry` and `PalGetMaximumStackBounds`; the NativeAOT ELF contains the R21 factory, stack, root, and “Managed Settings Center” markers. No unverified root object inputs were consumed.

| Artifact | SHA-256 |
| --- | --- |
| Candidate manifest | `7F257F9FDD61AED9189BC8090517B93BDA14798ECDFD89473B2E6D27E0BBBBD4` |
| Candidate integrity inventory (659 files) | `6E3D370E6DD40F1CC2D041D065919680BAFD3A77D547DA0631A047F8DA26C8EF` |
| Managed PE | `D95FA2549BA54DD981FEC01C53FB942400A370DF61239F4BB7090C3A958B2544` |
| Managed ELF | `C0B63BC1026B8AD1548D9AD2E2164496EDFA1CE1F38EFF28D9F3F5DDF75B31A7` |
| Managed map | `7863A9AAE490F795ED5F08DF4A1438CC4758C073F6E311498E3F7B9C8324BAD2` |
| Kernel ELF | `568B2C084387E45FE3718D422570EE761798542E461674AE8CE14A6B7804E864` |
| Staged ramdisk | `4A24EEB3CFC7FB56630A0A95177A84290222A9581412665E92CBFEA36E1044A1` |
| Runtime-pack manifest | `7FC199257F89A0B2C0D5A854C9E0DE4035C6118EF849C0A9DE1E1C7C7F68AF02` |
| R20 unwind primitive source | `9F94B5388535F2224D94318AC30D69260944ADB942C5F77FF5C90F0CDA9A3D3E` |
| Candidate R20 unwind primitive object | `87FDD16EA3A7B0CA58EFC191882804C620CA1E9D6968A94ED890EF329E6FA5FC` |

The QAC boot copied the staged candidate kernel and ramdisk into its separate run directory. Candidate artifacts and previous qualification outputs were preserved.

## One-boot QAC result

The no-boot preflight found QAC online with zero reservations/sessions and zero QEMU processes. One exclusive reservation was granted and one QEMU process (PID 16600) was launched. QAC emitted a transient “process census is incomplete” degraded-reservation warning; the follow-up ownership snapshot showed one owned process, zero foreign processes, and one reservation/session. There was no retry.

Run evidence directory:

```text
E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r21-settings-factory\qac-settings-factory-20261114T150300000Z-4d5f2073743448048f76bd0d111a4b7c
```

The serial trace reached image mapping, reported the PAL/VM/GC startup seam, ran startup stages 1–7, installed the TLS bridge, and emitted C188R11 `initialize-ready` / `C102-STARTUP stage=C188B002`. The startup wrapper's ready marker follows successful `RhInitialize(0)` and TLS installation. The trace ended before any managed-entry or scenario-dispatch line; it did not establish why execution stopped there. The runner reported `guest-runner-exited-before-Notes-readiness` after its readiness timeout. No Notes readiness, Settings click, guest input event, Settings factory entry, or Settings marker was observed. `clickAttempted=false`.

| Milestone | R21 evidence |
| --- | --- |
| M1 — NativeAOT runtime initialized | **Yes, startup boundary only:** `RhInitialize` returned through startup stages and TLS installation completed. No managed application entry followed in the captured trace. |
| M2 — Actual Settings factory entry | No |
| M3 — Managed factory initialization began | No |
| M4 — Required Settings allocations succeeded | No evidence |
| M5 — Expected Settings factory result constructed | No |
| M6 — Native/App Model integration | No Settings integration evidence; selector-5 factory and `TryCreateSurface` were not reached |
| M7 — Actual factory return | No |
| M8 — Runtime thread/transition state valid | Partial: startup TLS bridge and host-ready markers were emitted; no managed return/transition completion was observed |
| M9 — Managed Settings ownership valid | No native evidence; the hosted source check is not a guest ownership result |
| M10 — QEMU terminated and QAC released | Yes: QEMU exit code 0; QAC logged reservation release and a fresh status/list check showed no process or reservation |

The generic dispatch evidence file reports `qacReleaseVerified=false` because the runner exited before its normal post-readiness evidence collection. QAC's own stderr log records exit code 0 and release for reservation `c4b10c16e0894c13a29b63b17055da24`; the external post-run status/list confirmed the empty lane. The release conclusion is based on those direct QAC records, not the incomplete runner field.

## Gaps and next phase

- First establish why execution stops after `C188B002` and before the first managed-entry marker. Preserve the existing one-boot guard; make any future attempt a fresh candidate/guard only after the boundary is understood and preflight again passes.
- Once managed entry is reached, verify Notes readiness, perform the single planned Settings launch through the real App Model path, and capture the R21 stack reading at the actual factory entry.
- Observe the real constructor's allocations and initialization, selector/generation registration, `C188R21-ROOT` after compacting GC, the actual factory/lifetime return, and whether `ManagedSettingsCenter.Launch` requests and receives its surface.
- If launch proceeds, separately qualify rendering, interaction, callbacks, teardown, and persistence. None is implied by factory construction.
- The R21 candidate is an instrumented NativeAOT integration build. A proof-disabled production compatibility build and guest run were not performed in this phase.

The nearest next phase is a startup-boundary diagnosis using the preserved R21 serial/QAC evidence, followed by a fresh single-boot Settings attempt only after managed entry is shown to be reachable. Do not alter the historical allocation diagnosis or claim a Settings result until new guest evidence reaches that code.

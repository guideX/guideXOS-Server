# C188R10 managed state ownership and callback reentry

**Date:** 2026-10-25 · **Branch:** `v1.1_DOTNET_SUPPORT` · **Starting HEAD:** `d4f8e0c13604852b855c064f3e70389473b67848` · **Outcome:** **D — blocked at QAC admission**

## Result

C188R10 added proof-only observations around the actual managed and native selector-4 callback path, a hosted test using the real static registry and C181 five-slot pool, and an isolated diagnostic NativeAOT/kernel candidate. The hosted test demonstrates that, in one hosted managed runtime, the real selector-4 launch and input dispatch route share the same registry and pool owners; repeated input, stale-generation rejection, close, and reopen behave as designed. It does not reproduce or explain the C188R9 guest discrepancy.

Source and binary inspection found one C188 NativeAOT composition with one exported `GuideXosNativeAotApplicationEntry`; both launch and input call the same resident `g_application.entryPoint`. The startup wrapper invokes `RhInitialize(0)` only on its uninitialized path. Once ready, it installs TLS and re-enters `ManagedMain`. The registry table is a private `static readonly` descriptor array, and the C181 pool is one static pool object with five mutable instance slots. No input-path registration, registry reset, or C181 clear is present in the source route. Those findings rule out an ordinary source-level reset in the inspected path, but they do not prove what the running NativeAOT guest observes.

The required fresh pre-boot QAC census found a live, owner-verified acceptance reservation from another managed session. No R10 QAC reservation was requested, no QEMU was launched by this phase, and no guest input or screenshot exists. Per the phase boundary, the reservation was left untouched and no retry or queue was made. **The first inconsistent runtime boundary therefore remains unobserved.** No product routing or lifetime correction was justified. Outcome D is due to QAC admission, not a failed build or hosted regression.

## 1. C188R9 discrepancy carried forward

C188R9 observed Notes selector 4 launch with five descriptors, a live `ManagedNotes` instance at generation 1, and successful managed/native return. On later input dispatch, the diagnostics reported no descriptors and no live selector-4/generation-1 instance; `TryFind(4)` returned false and `Dispatch` returned `GxAbi.ErrorInvalidApplicationId` (`-4`) before the ordinary pool lookup and `ManagedNotes.HandleInput`.

Those two snapshots prove different observations at launch and input. They do not identify the transition between them. The R9 report remains [NATIVEAOT_C188R9_REGISTRY_INPUT_REENTRY.md](NATIVEAOT_C188R9_REGISTRY_INPUT_REENTRY.md).

## 2. State ownership model

| State | Definition and initialization | Owner and intended lifetime |
|---|---|---|
| Application descriptors | `GuideXosApplicationRegistry.s_entries` in [`GuideXosApplication.cs`](../../samples/managed/HostLogProof/GuideXos/GuideXosApplication.cs); for the C160/C188 managed-app-return composition, the static field initializer creates selectors 4–8 and their factories. | Type-owned `static readonly` array reference in the single NativeAOT image. The array is read by `TryFind`; there is no registration/removal API. Intended to remain for the managed image lifetime. |
| Resident slot pool | `GuideXosApplicationRegistry.s_lifetime` is one static `GuideXosManagedApplicationLifetime` instance. The implementation is in [`GuideXosManagedApplicationC150Tests.cs`](../../samples/managed/HostLogProof/GuideXos/GuideXosManagedApplicationC150Tests.cs). | Type-owned static reference; the object owns a five-entry array. Intended to retain logical app instances across synchronous native callback reentry until valid close/replacement or failed-launch cleanup. |
| C181 slots and generation | Pool instance fields `_entries` and `_generation`; each slot stores selector, generation, and application reference. `TryStart` allocates/replaces a slot and assigns a generation; `TryGet` validates selector and generation; `Clear` calls `OnTearingDown` and clears only a matching slot. | Instance-owned mutable storage inside the one static pool. Capacity remains five. The pool is not reconstructed by input dispatch in source. |
| R10 initialization marker | `s_c188r10RegistryInitializationGeneration` is initialized to `1` by the registry type initializer. | Type-owned diagnostic sentinel for this static owner, not a process-global epoch. It helps label one owner's initialized state; it cannot by itself detect a full static-segment reload that resets the sentinel. Owner tokens and the managed invocation counter provide separate observations. |
| Native application state | `g_application` and `g_startupState` are native globals; `g_application.entryPoint` is populated from the loaded application report. `g_startupState` is in the NativeAOT startup object. | Kernel-owned resident application state. The entrypoint remains associated with the loaded application image while its lifecycle state is `Resident`. |
| Native callback context | `invokeManagedWithHostMetadata` constructs `NativeGxAppContext` and `NativeAotStartupContext` on the native stack. `managedApplicationUserData` packs selector in the low 32 bits and the managed surface generation in the high 32 bits. | Native-owned synchronous call data. Both stack contexts remain live for the duration of the entrypoint call; the context is not retained by the managed registry. |
| Callback registration | `NativeAotManagedSurface` event callbacks call `invokeManagedInput`; the dispatcher calls the same `invokeManagedWithHostMetadata` used for launch. | Native-owned surface/callback state. It carries selector and generation into the shared invocation helper; no app-specific managed callback address is registered. |

The R10 owner identifiers are bounded hashes of the addresses of the registry and pool static reference slots. They expose no raw pointer. The initialization marker is intentionally not described as proof that a runtime could never be reinitialized; the owner and invocation observations are needed to test that claim in a guest.

## 3. Launch and input call graph

### Launch

1. Native application loading resolves `GuideXosNativeAotApplicationEntry` and stores its entry address in the resident `g_application` record.
2. Native launch dispatch calls `invokeManagedWithHostMetadata(selector=4, ...)`.
3. That helper builds the host table and stack contexts, sets the active managed context, and calls `g_application.entryPoint(&startup)`.
4. On the first call, [`c102_startup.c`](../../samples/managed/HostLogProof/c102_startup.c) installs PAL/GC hooks, calls `RhInitialize(0)`, installs TLS, marks startup ready, and calls `ManagedMain`. Subsequent calls take the ready path: install TLS and call `ManagedMain` without calling `RhInitialize` again.
5. The C188 `ManagedMain` branch in [`Program.cs`](../../samples/managed/HostLogProof/Program.cs) routes the selector-4 production path through `DispatchC188ExternalAppIdentity`, then `DispatchC107Composite`, then `GuideXosApplicationRegistry.Dispatch`.
6. `TryFind(4)` resolves the descriptor. A non-input launch calls `s_lifetime.TryStart`, which creates/replaces the managed app instance and returns its generation. Notes launch then runs synchronously and the registry records launch-return state.

### Input reentry

1. The existing native `NativeAotManagedSurface` pointer/key callbacks call `invokeManagedInput(selector, flags)`.
2. `invokeManagedInput` calls the same `invokeManagedWithHostMetadata` helper. It creates a fresh stack context with the same selector/generation transport and invokes the same `g_application.entryPoint`.
3. Because the startup wrapper is already ready, it installs TLS and re-enters `ManagedMain`; it does not rebuild or reload the NativeAOT image in this source path.
4. `ManagedMain` uses the same C188 selector route and static `GuideXosApplicationRegistry`. `Dispatch` calls `TryFind`, then, if found, `s_lifetime.TryGet(selector, generation)`, and only then invokes `ManagedNotes.HandleInput`.
5. Native code restores the prior active context, selector, and invocation surface after the synchronous call.

The R10 NativeAOT candidate retained `ManagedMain`, registry dispatch/lookup, C181 `TryGet`, and the exported application entrypoint in the same NativeAOT object/map. The link response links one `HostLogProof.exe` PE image and passes that image to the fixed-base PE-to-ELF converter. The source and artifact structure therefore show one managed composition and one entrypoint address used by launch and input. They do not prove a guest did not remap/reinitialize it at runtime.

## 4. Initialization, cleanup, and integrity audit

| Operation | Source path and precondition | Launch/input effect |
|---|---|---|
| Registry initialization | Static `s_entries` and `s_lifetime` initializers run through the registry type initializer. `TryFind` only reads the array. | No source call to register, remove, or replace descriptors occurs in `Dispatch` or `ManagedMain`. |
| Runtime startup | `g_startupState` transitions uninitialized → initializing → ready, or terminal failed. `RhInitialize(0)` is called only before ready. | Input uses the ready branch and `installTls`; no source call to repeat PAL/GC setup or `RhInitialize` is present. |
| Instance start/replacement | `Dispatch` reaches `s_lifetime.TryStart` only when `!IsInput && !IsAction`. A replacement tears down the prior matching selector as part of `TryStart`. | Not run for ordinary input reentry. The launch is the only normal creation path. |
| Valid close | `ClearResident` delegates to generation-checked `s_lifetime.Clear`. It is used by the lifetime-close action and successful app-specific close branches (File Explorer, Calculator, Task Manager). | Requires a matching selector and generation. No close action is implied by `ManagedMain` return. |
| Failed launch cleanup | `if (isNewLaunch && result != Success) ClearResident(...)`. | Applies only to a failed new launch. The observed R9 Notes launch returned success. |
| Notes session cleanup | The C155 clear action clears the separate Notes return-session object. | It does not remove the static descriptors or clear the C181 pool. |
| Teardown | `GuideXosManagedApplicationLifetime.Clear` and replacement call the app's `OnTearingDown`; the R10 wrapper counts calls/attempts for diagnosis. | No ordinary input branch invokes it. R10 does not suppress valid cleanup. |

The C188R10 instrumentation is compile-time gated. Registry snapshots use stack-allocated buffers, skip pointer-motion/wheel events, and stop after 48 records. Managed/native boundary logs are limited to 24 selector-4 launch or non-motion input invocations each (entry and return records), also skipping motion and wheel input. No locks, heap allocations, ABI changes, registry mutation, or registration operations were added to the input path. The diagnostic does not infer memory corruption from the R9 symptom.

## 5. NativeAOT image and static-data evidence

The R10 diagnostic candidate uses the existing `HostLogProof.exe` PE output and the repository's SHA-256-locked fixed-base converter [`pe_to_elf_v2_fixed_base.py`](../../tools/dotnet/pe_to_elf_v2_fixed_base.py). The map places the exported `GuideXosNativeAotApplicationEntry`, `ManagedMain`, `GuideXosApplicationRegistry.Dispatch`/`TryFind`, and C181 `TryGet` in one linked image. The registry static-base and writable-data symbols, including the runtime GC-static base, are in the same map; native `g_startupState` is also in the mapped writable data region.

For the final bounded diagnostic build, ELF validation reports **ELF64 x86-64, seven `PT_LOAD` segments, and zero relocations**. The writable load segment begins at `0x1001ec000`, with file size `0x1200` and memory size `0x4be80`; this includes the mapped writable/static-data region and its zero-filled memory tail. There is no dynamic section or relocation table. This confirms that the converter emits writable image storage for the NativeAOT static roots and startup state. C# static references still refer to managed objects allocated/initialized by NativeAOT; the map does not mean the whole managed array or pool is inline in the ELF `.data` bytes.

Binary layout supports the intended persistent owner within one loaded image. It cannot substitute for launch/input runtime snapshots, and no guest evidence was collected in this phase.

## 6. Hosted and build qualification

Actual executed counts:

| Qualification | Result |
|---|---|
| C188R10 real-registry/C181 reentry | **14 cases PASS**; same hosted owner token across launch, three input reentries, close/reopen, and stale-generation rejection. |
| C188R9 registry/C181 suite | **14 cases PASS**. |
| C188 external identity/lifecycle | **25 cases PASS**; identity stress 1,000, resizes 500, invalid geometry 250, stale attempts 250, lifecycles 100, hot-path allocations 0. |
| C129/C136/C138 input and Notes edit contract | **136 cases PASS**: 31 + 36 + 62 + 7. |
| Managed UI aggregate | **38 suites / 930 cases PASS**; inventory validator also rejected all 6 deliberately incorrect manifests. |
| Prior qualification | C182 **15**, C185 **28**, C186 **15**, C187 **22** cases PASS. |
| C188 checkpoint-sequence validator | **20 tests PASS**. |
| C188R10 runner predicates | **13 fixtures PASS**, including LF/CRLF, decimal/padded generations, wrong generation/selector, input marker formats, and launch-return calls after earlier selector probes. |

ManagedUiContracts and CounterApp were restored and built independently under the isolated C188 hosted artifact path. The diagnostic NativeAOT build and PE-to-ELF conversion passed; proof-disabled NativeAOT passed; diagnostic and proof-disabled freestanding kernel builds passed. The production qualification manifest reports selectors 4–8 preserved, no CounterApp descriptor, and no C188 selector route. A byte scan of the proof-disabled production kernel found no C188 R10 marker, CounterApp identity, or development identity-registry marker.

The managed UI compilation emitted an existing `CS0162` unreachable-code warning at `Program.cs:2662`; all corresponding suites passed. No warning or test failure establishes a C188R10 state defect.

### Final candidate hashes

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF (`diag-002`) | `C772C48205CADC8C857B07E143C8FD641DC06B7E0A1FC41A970B203D7635DB13` |
| Diagnostic kernel ELF (`diag-002`) | `9C8C346BA912D5FCBEAB2645108E96F6CA630B3F6E6E20D4B7386419695597BD` |
| Proof-disabled production NativeAOT ELF (`production-nativeaot-002`) | `E0FD3BD1AE01DCF8FBE09014126EE2B7C1E5AB167D3DC145F8C30CA88C5AA4EC` |
| Proof-disabled production kernel ELF (`production-002`) | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| Staged diagnostic RAM disk (`runs/diag-002`) | `8E622629071FD9E311F6E0F79B466B9C91746ADD40741FA55AF853CA9F86129F` |
| Boot EFI | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Protected source `ESP/ramdisk.img` before and after staging | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The staging tool wrote the candidate app only to `/APPS/GXOSAPP.ELF` in an isolated RAM disk and reported 185 unrelated files preserved. The original `ESP/ramdisk.img` was not used as a staging target.

## 7. QAC gate and guest evidence

The initial preflight census was empty. A fresh census before the first planned R10 boot (`diag-001`) found QAC online but occupied:

- Existing managed acceptance reservation: `6f4cfa8818854aa9a8af4a20f4a7d33c`.
- QAC owner: `DeveloperStudio`, owner verified; managed session was running.
- Existing QEMU PID: `30980`; QAC owner process PID: `29208`.
- QAC reported one QEMU process, one reservation, and one managed session.

No R10 reservation was acquired. The C188R10 runner was not launched, and the existing reservation/process was not cancelled, interrupted, or polled again. After bounding the diagnostic markers, the `diag-002` candidate was rebuilt and staged without another QAC census or boot attempt. Therefore this phase has **no R10 QEMU PID or command line, guest serial, ordered checkpoints, QMP event log, Notes screenshot, pointer/key delivery result, editor-focus result, document mutation result, redraw result, or R10 reservation-release record**. The owned-session release check is not applicable because no R10 session existed. The runner and isolated `diag-002` media are staged for a future authorized continuation, but the next action is not automatic.

The original R9 and R8 runtime findings remain historical evidence; they are not evidence from this R10 candidate. In particular, C188R10 cannot claim Notes input acceptance or identify the first lost-state boundary.

## 8. Preservation and worktree

The fresh baseline was captured on `v1.1_DOTNET_SUPPORT` at the expected starting HEAD: **13 protected roots, 8,497 files, and 136 pre-existing status paths**. The final pre-commit comparison passed with **13/13 roots unchanged, 8,497/8,497 protected files unchanged, 136/136 pre-existing status paths and hashes preserved, zero differences, and 28 new status paths for this phase**. The protected RAM disk hash matched AABE…FD8 before and after all isolated staging. The comparison is recorded in the ignored phase output at `out/dotnet/c188r10-nativeaot-state-reentry/preflight/final-comparison.json`; it preserves the C186R1 workflow and carries forward the historical uncertainty involving **212 C185 generated files**.

Pre-existing generated/untracked directories and guest evidence were preserved. R10 build, hosted, and staged-media output remains under `out/dotnet/c188r10-nativeaot-state-reentry/` (the first hosted probe and baseline capture under `out/dotnet/c188r10-managed-state-reentry/` were also retained). No branch switch, reset, clean, stash, merge, rebase, cherry-pick, history rewrite, or push was performed. The unrelated modified RAM disk remains untouched.

## 9. Outcome and remaining gates

**Outcome D — Blocked.** Source and binary inspection plus hosted qualification show the intended static ownership model and a working same-process hosted reentry route. They do not explain why the C188R9 NativeAOT guest observed an empty registry/pool. The first inconsistent state transition is still between the successful launch and the first managed input observation. No state-loss category can be selected from A–F without that missing runtime evidence; memory corruption, reinitialization, a second runtime owner, and an observation mismatch remain unproven.

No product correction was made. C188 acceptance remains open, including real Notes input/edit/redraw; proof-disabled production Notes readiness/input; dedicated CounterApp selector-9 acceptance; coexistence, resize, lifecycle, and identity scenarios; and the original six focused/production C188 scenarios. The six-scenario campaign and C189 were not started.

The immediate next step is to resume only the bounded C188R10 diagnostic guest after QAC has an empty exclusive acceptance census. Compare native invocation IDs, managed entry counts, registry/pool owner tokens, descriptor count, C181 occupancy, generation validity, and teardown count from launch-return through the first pointer/key callbacks. Do not change registry or pool architecture unless those observations prove a specific source defect. If a later C188R11 phase is opened, keep it to one evidence-driven boundary and one bounded guest; C188R10's QAC gate must be closed first.

## C188R10R1 guest continuation — 2026-10-25

**Outcome B — boundary narrowed.** One safely admitted C188R10 `diag-002` guest reproduced the launch/input state difference. At Notes launch return, the registry had five descriptors and C181 held selector 4 at generation 1. At the first observed pointer callback, the descriptor count and resident mask were both zero. The registry and pool owner-slot tokens and initialization sentinel were unchanged, and teardown count remained zero. `TryFind(4)` returned false and dispatch returned `-4`. The operation that changed state was not observed, so this is not a root-cause finding and does not establish a NativeAOT runtime defect.

### Admission and candidate identity

The fresh QAC census at `2026-10-25T17:48:44Z` found the coordinator online with no QEMU processes, reservations, managed sessions, queued requests, or foreign QEMU process. The existing bounded runner then received exclusive acceptance reservation `abd258873474412d91552a5e69ce78b3` for `guideXOSServer`; owner `LEON2_RDT\guideX` was verified (owner PID 17904). QEMU child PID 30496 ran `C:\Program Files\qemu\qemu-system-x86_64.exe`. Admission took 0.9 seconds. The run used `run-c188-qac-guest.ps1`, scenario `registry-input-reentry`, with a 60-second admission bound, 180-second boot bound, and 240-second runtime bound.

The `diag-002` candidate was reused unchanged. Every saved pre-boot hash matched, and the copy staged at `runs/diag-003/esp/` matched those hashes before and after the guest. The NativeAOT build manifest, kernel qualification, and ramdisk staging manifest all report PASS. No source or candidate was rebuilt.

| Candidate artifact | Bytes | SHA-256 |
|---|---:|---|
| Diagnostic NativeAOT PE | 2,083,328 | `E25E66960C69643BBEDA65D0DD38A01AC0AD537D113A777419CE0B60274FC742` |
| Diagnostic NativeAOT ELF | 2,109,440 | `C772C48205CADC8C857B07E143C8FD641DC06B7E0A1FC41A970B203D7635DB13` |
| NativeAOT link map | 2,809,875 | `57F2BC89D71660F7B85CAFAE39A931194375BF8E625E69EE952236E85305190A` |
| Diagnostic kernel ELF | 2,366,992 | `9C8C346BA912D5FCBEAB2645108E96F6CA630B3F6E6E20D4B7386419695597BD` |
| Staged guest RAM disk | 67,108,864 | `8E622629071FD9E311F6E0F79B466B9C91746ADD40741FA55AF853CA9F86129F` |
| Bootloader | 52,224 | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| R10 targeted runner | 10,374 | `B8B3B449E29D2F56B90A9DB72FC5B3832F401798F979F511FD90250043A68B3C` |
| Bounded QAC guest runner | 28,428 | `53D34D8A8AB94DB957A7C3B80A6475071E47437A43B4AAAAD07935836A51465E` |
| R10 predicate helper | 1,850 | `42B66CC7F810888D6C1449E971CBEE734B67E968A0F80FA0816CEAD4A5A9B334` |
| QMP control helper | 10,415 | `72731B3F892B0778589FAC9F02AF6309940F4F886235AC132C0C579A68613EE2` |
| NativeAOT build manifest | 1,593 | `7F3AB5E7CD6B59D90E08E61B4F915D5BED5B8E871E3DCBEA461112247D844F00` |
| Kernel qualification manifest | 2,072 | `60FEE81686BB59F799C9B7AC46E408E68E23DB437C416660A74AB8091C2F4A4D` |
| Ramdisk staging manifest | 871 | `3E6F076B7D0C4B1368D6940FF0CBC3DDF311AA642DBB04A33FB4D8A5916F1334` |
| Saved pre-boot hash manifest | 584 | `A9A62C24F978C27897BECC6FFD2EA4248341576ADF3101E75B4F6E1D842C9F5F` |

The retained ELF contains the `C188R10 state stage=` and `C188R10-MANAGED stage=` diagnostics; the kernel contains `C188R10-NATIVE`. The R10 predicate source supports LF/CRLF and decimal/zero-padded generations and checks native launch-return and optional managed checkpoints. The runner predicates accept LF/CRLF and decimal/zero-padded generation formats, check the native launch-return marker, and expose checkpoint results for native invocation, managed entry, registry snapshot, input entry, handler, and return. The historical report records 13 predicate fixtures passing; their source and fixtures were inspected but not rerun for R1. The 14-case hosted reentry result was reused from its saved passing log. The 930-case managed UI aggregate was not rerun.

### Runtime state and first inconsistent boundary

| Snapshot | Registry token | Init | Descriptors | Pool token | Resident mask | Selector/generation | Valid | Teardown count | Result |
|---|---:|---:|---:|---:|---:|---|---:|---:|---|
| Notes launch return | `1780116663` | 1 | 5 (`4–8`) | `3728137811` | `1` | `4 / 1` | 1 | 0 | `rc=0` |
| First pointer callback, dispatch | `1780116663` | 1 | 0 | `3728137811` | 0 | `4 / 1` | 0 | 0 | lookup `-4` |
| Pointer release callback, dispatch | `1780116663` | 1 | 0 | `3728137811` | 0 | `4 / 1` | 0 | 0 | lookup `-4` |

The launch trace shows `stage=start` with mask 1 and a valid generation, then `stage=launch-return` and `stage=return` with five descriptors, mask 1, and `rc=0`. At input, both pointer down and release entered the managed callback with selector 4 and generation 1. The first input `dispatch` snapshot already had `n=0`, mask 0, and invalid generation; `descriptor-lookup` returned `-4` before C181 lookup or `ManagedNotes.HandleInput`. Both native input calls returned `FFFFFFFC` (`-4`).

The registry token and pool token are 32-bit hashes of the addresses of their static reference slots. Their repeated values are consistent with the same slot addresses, but hash collisions mean they do not prove address equality; neither token exposes the referenced array or C181 pool instance. The `init=1` value is a fixed diagnostic sentinel, not a monotonic initialization epoch, so it cannot detect a complete reinitialization that recreates the same values. Teardown count stayed zero, and no close marker was observed, but the counter covers the instrumented clear path and cannot rule out an uninstrumented replacement or state reset. These observations do not distinguish in-place mutation, reference replacement, reinitialization at the same mapping, memory corruption, or an observation-context defect.

`C188R10-NATIVE` logged the two input callbacks with the same input image token `A6619991`, selector 4, and generation 1. No native launch-boundary record appeared, so the runtime cannot compare that token or a native launch-return code against the launch. Managed launch entry/exit records show `rc=0`, Notes surface creation succeeded, and the guest reached the main loop. Source and link-map evidence still show one exported NativeAOT composition, but runtime continuity across launch and input is not established by the missing launch marker.

The narrowest established interval is after the valid launch-return snapshot and before the first managed input-dispatch snapshot. No intervening checkpoint identifies the operation that made the descriptors and resident slot unavailable.

### Input, window, QMP, and QAC release

The before-input screenshot showed Notes window `000003E8` at approximately screen bounds `(340,220)`–`(940,580)`. Screen point `(379,359)` was inside its editable text region, corresponding to client point `(39,115)`. One pointer click was sent. Since the first callback had already rejected selector 4, no keyboard character was sent. No Notes handler, pointer-focus, or edit-success marker was logged; the after-pointer screenshot shows unchanged document text. The window remained visible in the after-pointer screenshot; this is a separate visibility observation and does not imply registry state survived.

QMP reported `running=true` before and after the click. The command/reply log records the screen captures, one pointer click (down/up), and the owned `quit`; no unsolicited `SHUTDOWN` event was retained. QEMU exited with code 0. QAC verified release of reservation `abd258873474412d91552a5e69ce78b3`, and its post-run census showed zero QEMU processes, reservations, managed sessions, queued requests, and blocked requests. The runner acceptance manifest is `FAIL` because the expected successful managed input path did not occur; QAC admission, QEMU termination, and reservation release succeeded.

### Evidence and preservation

The full run is retained under `out/dotnet/c188r10-nativeaot-state-reentry/runs/diag-003/`. `runtime-results.json` records the QAC identity, state snapshots, input outcome, media hashes, and evidence-file hashes; its SHA-256 is `D62CEF2A48F59314FE8B0580B2F7C10E29583FF368B2D281D2D10A4741B0F764`.

| Runtime evidence | SHA-256 |
|---|---|
| QAC reservation/child log | `C62FA9999D2EE3298457BC71104A45D926CEE793907D0F87E05D05681192A7EB` |
| Acceptance manifest | `96BF2166B5777EB81087C5C390723EC0515EB67865B9D93A035A54AEA1DA657E` |
| Raw serial | `C379DC090E06E2852CB808CAD5CA1DBEADC1BF99E0F199835B54BB7DA3D99C15` |
| QMP log | `139737A0D8229A702F49E1EF274004C759E1A441036E706F8CB7C962E2A6F410` |
| QMP control log | `9D603429C63185FABE76C77FD3BA999D3BA34BB60EDDEB39F0DEF043F88A296E` |
| Before-input screenshot | `95BCAC045DBD3713D3721D12FE7917B913D3FB262CD55F0AA47E3742B8111FA3` |
| After-pointer screenshot | `A79CBCB59AE889F1DF107B11C1EBFDD2482EBD6C42A8C92E297A25504615F8EA` |

The fresh pre-run baseline contained 13 protected roots, 8,497 files, and 136 existing status paths. The post-run comparison passed with 13/13 roots, 8,497/8,497 protected files, 136/136 prior status paths and hashes preserved, zero differences, zero new status paths, and 136 current status paths. The original `ESP/ramdisk.img` remained at SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. `preflight-final-comparison.json` (SHA-256 `E4D474B8B015E0F7E00BB8E859CB80F1948BBFE1A6448A372FB5E9E0EF586048`) retains the root membership, relative paths, sizes, hashes, and historical uncertainty for 212 C185 generated files. Guest media and framebuffer images remain uncommitted under the ignored run directory.

No product behavior was changed, no production candidate was booted, and the six-scenario C188 campaign and C189 were not started. C188 acceptance remains open; Notes input acceptance was not demonstrated.

### Recommended next action

If C188R11 is opened, scope it to locating the interval between launch return and first input dispatch. Add a monotonic initialization/reset epoch and diagnostic identity for the actual registry-array and C181-pool references, and resolve why the native launch-boundary marker is absent. Keep one bounded guest and stop before keyboard input when descriptor lookup rejects. Do not implement a registry, C181, or NativeAOT runtime correction until evidence identifies the operation responsible.

# C188R9 managed application registry and input reentry

**Date:** 2026-10-25 · **Branch:** `v1.1_DOTNET_SUPPORT` · **Starting HEAD:** `60ca4ea3e3aa67454a58e7af9e4b31f6045ef283` · **Outcome:** **B — registry boundary narrowed; Notes input remains unqualified**

## Result

C188R9 established the immediate reason for the C188R8 rejection. During the Notes launch dispatch, the static registry contained five descriptors, selector 4 resolved successfully, and the C181 lifetime pool contained the Notes instance at generation 1. On the first later input callback, the same source dispatch observed an empty descriptor table and no live selector-4/generation-1 entry. `TryFind(4)` returned `false`; `Dispatch` translated that result to `GxAbi.ErrorInvalidApplicationId`, which is `-4`.

The guest received a real click in the Notes client at screen `(379,359)` and a key-char `a`. Both reached managed dispatch with selector 4 and generation 1. Dispatch returned at the descriptor check, before its normal lifetime lookup and before `ManagedNotes.HandleInput`. No C117 pointer-focus or document-edit marker appeared. The after-input framebuffer had no visible Notes window, and QMP still reported the guest running. No managed close or save marker was recorded.

The source contains one static registry definition for the active C160/C188 profile and no registration-removal path. Native launch and native input both enter `ManagedMain`, route selectors 4–8 through `DispatchC107Composite`, then call `GuideXosApplicationRegistry.Dispatch`. The evidence therefore narrows the fault to the registry/lifetime state visible during managed callback reentry. It does not establish whether NativeAOT state was reinitialized, a different managed static state was used, or memory was overwritten. No product behavior correction is justified yet. The proof-only diagnostics, focused hosted coverage, QAC records, and this report are committed locally; C188 acceptance and proof-disabled production Notes input remain open.

## 1. C188R8 findings carried forward

C188R8 had already established the input-device and target-window path: selector-4 Notes launched, created window `000003E8`, returned successfully, and reached the normal input loop. The compositor hit that client window, and native pointer and keyboard callbacks delivered selector 4/generation 1 to managed dispatch. R8 stopped at `TryFind(4)` returning false, before C181 validation or Notes input handling. The R8 report remains [NATIVEAOT_C188R8_NOTES_INPUT_ROUTING.md](NATIVEAOT_C188R8_NOTES_INPUT_ROUTING.md).

## 2. Registry implementation and `-4` contract

In the C160 composite, `GuideXosApplicationRegistry` is a `public static unsafe` class. Its private `static readonly` `s_entries` array is initialized with the compile-time descriptors for selectors 4–8: Notes, Settings, Calculator, Task Manager, and File Explorer. The descriptor contains selector/name/application factory information. `RegistrationCount` is the array length; this profile reports 5. There is no separate registration API or independent descriptor-capacity setting. The table is intended to be immutable after type initialization.

`TryFind(uint, out GuideXosApplicationDescriptor)` scans `s_entries`, returns `true` with the descriptor on a match, and otherwise returns `false` with `default`. It has no lookup side effects. It does not return an integer. `GxAbi.ErrorInvalidApplicationId` is explicitly `-4` in `samples/managed/HostLogProof/NativeAbi.cs`; `Dispatch` returns this status when the boolean lookup is false. Thus the guest's `result=-4` is a dispatch error code meaning “descriptor not found,” not a table index, generation error, or `TryFind` return value.

There is no public “registry not initialized” state or explicit registration operation to call before lookup. The static field initializer supplies the descriptors before the first member access. The hosted reproducer tests initial access and persistence through launch/close/reopen; a pre-type-initialization state is not externally reachable through this API.

## 3. Launch and input dispatch comparison

| Launch | Input reentry |
|---|---|
| Native application model selects `com.guidexos.apps.managed.notes`, selector 4, image `/system/apps/GXOSAPP.ELF`. | `NativeAotManagedSurface` forwards pointer/key-char callbacks through `invokeManagedInput`. |
| `ManagedMain` calls `DispatchC188ExternalAppIdentity`; selectors 4–8 route to `DispatchC107Composite`. | Native input constructs the managed context with selector 4 in the low selector field and generation 1 in the application-lifetime context. The same `ManagedMain` and selector-routing path call `DispatchC107Composite`. |
| `GuideXosApplicationRegistry.Dispatch` creates `GuideXosHost`, then calls `TryFind(4)`. | `GuideXosApplicationRegistry.Dispatch` creates a host, extracts selector 4, then calls `TryFind(4)` again. |
| A descriptor is passed to `s_lifetime.TryStart`; a `ManagedNotes` instance is created at generation 1. Notes launch creates its native surface/window. | The diagnostic lookup returns false before Dispatch's normal `s_lifetime.TryGet` call and before `ManagedNotes.HandleInput`. The diagnostic's separate observation of the pool also finds no selector-4/generation-1 entry. |

The successful first launch proves that `TryFind(4)` succeeded on that launch call: the source returns immediately on a false lookup, while the guest then records `C150-APP-INSTANCE id=4 generation=1`, Notes surface creation, and a successful Notes/native launch return. This is control-flow evidence, not an assumption based only on the visible window. The compact R9 launch snapshot independently records table count 5, slots `4,5,6,7,8`, lookup result 0, and a live pool entry after `TryStart`.

## 4. Registry, lifetime, and native identity ownership

| State | Owner and meaning | C188R9 evidence |
|---|---|---|
| Selector-to-application registration | `GuideXosApplicationRegistry.s_entries`, static immutable descriptor table | Launch snapshot: count 5, selectors `4,5,6,7,8`; input snapshot: count 0 and no slots. |
| Running managed instance | Static `s_lifetime`, implemented by the C181 pool with five bounded slots | Launch `instance-start`: selector 4/generation 1 is live. Input diagnostic probe: `live=0`. |
| Managed generation | C181 generation attached to the live instance and propagated to callbacks | Launch and input both report generation 1. Dispatch never reaches its ordinary `TryGet` on the failing input path. |
| Native app identity | `com.guidexos.apps.managed.notes`, selected by native selector 4 | Native launch and C150 records confirm the Notes identity. |
| Native surface/window | Notes surface owns window `000003E8` | C150 surface creation and C172 client hit confirm window `000003E8`. This is neither selector 4 nor generation 1. |

The selector, app ID, generation, and window ID are distinct identifiers. The callback carries the expected selector and generation, and the hit belongs to the Notes window. The evidence does not indicate an identifier mix-up. The registry describes application factories; the C181 pool separately owns live instances. The R9 trace's `live=0` is a direct diagnostic probe of the pool; Dispatch itself returns on missing descriptor before performing its normal pool lookup.

## 5. Exact lookup failure and first invalid observation

The first boot's input trace records three managed input callbacks. At dispatch entry the R9 snapshot reports `count=0 slots= live=0`; at descriptor lookup it again reports `count=0 slots= live=0 result=-4`. R8 then records `stage=selector-rejected ... result=-4`, followed by `C112-UNKNOWN-SELECTOR`. The selector is 4, generation is 1, and the pointer/key event fields are valid. Because the table has no entries, the rejected condition is specifically `TryFind(4) == false`.

The earliest invalid state directly observed is the first managed input-dispatch entry, before `TryFind`. The last compact snapshot of valid state is the launch's `instance-start` record, where the five-descriptor table is present and C181 reports generation 1 live. The Notes launch then returns success. The `launch-return` diagnostic marker itself was not observed, so the precise point between `instance-start` and the first input callback remains unknown.

## 6. Focused hosted reproducer

Added `GuideXosApplicationRegistryC188R9Tests` and a dedicated host project. They use the actual registry and actual C181 lifetime pool, not a substitute registry. The 14 passing cases cover:

- The five production descriptors, Notes selector 4, factory/name identity, and missing selectors 0 and 9.
- Stable repeated lookup and registry count.
- Notes lookup, instance start, the same descriptor and instance after launch, stale-generation rejection, close rejection, and reopen with a larger generation.
- Independent Notes and Settings instances, cross-selector generation rejection, and all production selectors remaining resolvable.
- Five concurrent production descriptors filling the real C181 pool and rejection of a sixth unique descriptor while preserving the five live records.

The focused suite passes: `C188R9-REGISTRY cases=14 firstFailedCase=0 result=PASS`. The C160 static table and C181 pool behave consistently in the hosted process. This does not reproduce the guest's empty state at callback reentry; the guest-only state loss remains the unresolved boundary.

## 7. Bounded proof-only diagnostics

The R9 trace is gated by `HOSTLOGPROOF_C188R9_REGISTRY_INPUT_REENTRY` and the corresponding build property. It reports only selector 4, skips pointer-move and wheel input, is capped at 18 records, uses stack-allocated storage, exposes no pointer, and samples both the static descriptor table and C181 lifetime pool. The compact line stays within `GuideXosHost.TryLog`'s 127-byte payload limit. The native log prefix adds its own display label after that payload limit.

The confirmation candidate captured `dispatch-entry`, `descriptor-lookup`, and `instance-start` snapshots with the populated table. It did not emit the expected `launch-return` snapshot even though the Notes and native launch return markers followed. That missing stage is included as a diagnostic limitation; it is not interpreted as proof of a state change at that exact line.

## 8. Root-cause decision and correction policy

The immediate failure is explained: input reentry observes an empty registry, so `TryFind(4)` is false and dispatch returns `-4`. The source contains no descriptor-table mutation or normal teardown that would produce this state, and the input is routed through the same source registry type. Both the descriptor table and the C181 pool are absent in the input-time diagnostic view. This is a meaningful state boundary, but it does not distinguish among NativeAOT/module static-state reinitialization, a distinct runtime/static-data instance on reentry, or memory corruption. No evidence justifies assuming a NativeAOT static-initialization defect.

No product dispatch or lifetime behavior was changed. The change adds bounded diagnostics and hosted coverage only. In particular, it does not bypass `TryFind`, route selector 4 directly to Notes, alter generations, add mutable registration, add a second global table, or change the native ABI.

## 9. Hosted regression qualification

Current recorded results:

- C188R9 real registry/C181 reproducer: 14 cases, pass.
- C188 identity/lifecycle suite: 25 cases, pass; 1,000 identity operations, 500 valid resizes, 250 invalid geometry requests, 250 stale-generation attempts, 100 launch/close lifecycles, and zero resize hot-path allocations.
- C182 15, C185 28, C186 15, C187 22: pass.
- Managed UI baseline: 38 suites / 930 cases, pass.
- R8 focused input coverage: C129 31, C136 36, C138 62, and Notes editor 7; pass. The R8 runner-marker contract remains 3 passing cases and checkpoint validator remains 20 passing cases.

The prior suites were run in the isolated R9 output root before the final diagnostic-format shortening; the final focused R9 suite was rerun after the test expansion. The production rebuild also compiled the final shared source with the R9 diagnostic conditional disabled. No resize geometry or hot-path production behavior changed.

## 10. NativeAOT, kernel, and production qualification

The final diagnostic NativeAOT candidate is ELF64 x86-64 with seven load segments and zero relocations. Its manifest records the R9 trace gate, development CounterApp selector 9, production selectors 4–8, and retained static CounterApp factory/dispatch symbols. The candidate kernel is the freestanding C188 kernel built with `-std=c++14`, `-ffreestanding`, `-fno-exceptions`, `-fno-rtti`, `-nostdlib`, `-nostdinc++`, and `-fno-builtin`.

Proof-disabled production NativeAOT and kernel builds were rerun after the final C# diagnostic change. Production selector descriptors 4–8 remain; the CounterApp descriptor and C188 development selector route are absent. The C188R9 diagnostic marker is excluded. ABI v4 remains 128 bytes with callbacks at offsets 104, 112, and 120. C181 remains five slots. No kernel source or native ABI assignment changed.

## 11. Targeted QAC guest 1 — input diagnostic

Fresh pre-run QAC census: coordinator online, zero QEMU processes, reservations, managed sessions, and queued requests. QAC granted exclusive reservation `16e35c18ca524a7fa8caf2395a565b3f` to verified owner `LEON2_RDT\guideX` (runner PID 18680); QEMU child PID 20868 exited with code 0. The runner's automation initially rejected the JSON width because it was deserialized as `Int64`, so the click/key sequence was sent with the existing QMP control tool while the owned guest remained live. QMP reported `running=true` before the owned quit.

The QMP sequence moved to `(379,359)`, clicked primary, and sent `a` down/up. C172 records a client hit on window `000003E8`, translated to local `(39,115)`. The R9/R8 traces then show selector 4/generation 1, empty descriptor count, `-4`, and selector rejection. C136 reports pointer down/up failure and C129 reports key-char failure. No `lifetime-found`, `notes-handler-enter`, C117 focus, or C117 edit marker appears. The before-input image shows the Notes window. The after-input image shows no visible Notes window; the serial has no normal managed close/lifetime-close marker. No successful character insertion, document mutation, redraw, or save is evidenced.

The original runner acceptance manifest is preserved as `FAIL`: its R9/R8/C117 regexes did not include the native serial prefixes. The runner was then corrected to parse the actual prefixed markers and numeric JSON dimensions. This does not change the raw run result. QAC logged the owned QEMU exit and reservation release; its post-run census showed zero QEMU processes, reservations, managed sessions, and queued work.

## 12. Optional confirmation guest 2 — launch-state snapshot only

One additional QAC boot was used after shortening the diagnostic payload. QAC granted reservation `5643967fe3b541e7a2576205733de069` to verified owner `LEON2_RDT\guideX` (runner PID 33244); QEMU child PID 28092 exited with code 0. It used the final compact diagnostic ELF, the same candidate kernel, and an isolated staged ramdisk. The serial confirms selector-4 Notes startup, generation 1, window `000003E8`, and the normal input loop.

The launch snapshots show count 5 with selectors `4,5,6,7,8`, successful lookup result 0, and `live=1` after instance start. The controller's startup guard then rejected the serial's CRLF and eight-digit zero-padded `generation=00000001` format, so it sent no input. The runner/controller errors and raw serial are retained; this is not a second input confirmation. The controller predicates have been corrected and checked offline against the captured startup serial. No third boot was started. A fresh post-run QAC census again showed no QEMU process or active reservation/session.

## 13. QAC artifacts and hashes

All raw files are under `out/dotnet/c188r9-registry-input-reentry/runs/`. The exact QEMU argument arrays are in each run's `qac-run-arguments.json`.

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT ELF used for guest 1 (`build/diag-002`) | `96E7C57A326DE1F7EA68BE7FD88BDAEDCF26F5DFDDAAE73811BBD968679A7C5C` |
| Guest 1 diagnostic PE | `51EB679E7E7F7E83C09CEAFC1906E341E57A3D58EB73FD5577BF5450BF92E4E7` |
| Guest 1 NativeAOT map | `CBA13142EDD974C705057172F2BA77C395ECFCC381EF53361D91EF335124E5CD` |
| Compact diagnostic NativeAOT ELF used for guest 2 (`build/diag-003`) | `DDCDD322027D706009718F15A6403F1D3E54269E7FF67F8C183EC4A858031C0D` |
| Final diagnostic PE | `E21822283C138D29769C590EE9CAB64D102055E0AF0CAC54D14972D1BF4F7CF7` |
| Final diagnostic NativeAOT map | `8A5B619267B3DB440EA19A37D4DC30931A655C5209982A08AEED77EDFEB45BC8` |
| Independent ManagedUiContracts assembly | `668D9DE2623B5A327104BC6E292719F941ABC8982FFB91EAEB5372E141EAC6B2` |
| Independent CounterApp assembly | `21386721CCE8570F398EC70F0DB60367A4FF8ABC1FCAA3A0928100B9092B3A49` |
| Candidate kernel ELF | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Guest 1 staged ramdisk | `4A040260AB4E3C39E2E60F96DE9538590DEC58CDD527C0F1504D978A0F54A458` |
| Guest 2 staged ramdisk | `430789154B598C27AD5383C177B21F195F8011845C55A3ADCD0DEE8C2A43C10A` |
| UEFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Guest 1 raw serial | `3CA8A656286B81DAF6CA427E229EF81AF2F500D138B1C9C8ECEF3BEA4864A2BC` |
| Guest 1 QMP log | `0D17F58845B241FE36413C8F978716FE445609B87BFA3580DCA091D9E6EED43F` |
| Guest 1 QMP input/capture log | `E20910FC11580900AD6DDC9C26A3CF62F0D41F295A82EF74B906B8A7B5C279C8` |
| Guest 1 before-input PNG | `2780912AEEF6EB8D8F88AC8D465B3D4E7973AF80BD33A7CAF81FA947D287A1BB` |
| Guest 1 after-input PNG | `3C8B621BE5F747531A585DFF8F98F8B80CF15509CA3B1EA5B8EE151AC878F005` |
| Guest 2 raw serial | `7FDAD8A50A789390231F9179A438F3A9261568A6E4C7EA47E56BBE3D5E612756` |
| Production NativeAOT ELF | `0FE47F32E2A36A0C184120F74E2973183700C1AAB7B8B3C334C78D9CE287C93A` |
| Production NativeAOT map | `1924D1DC69DBE73B1A38589D78E9A72FCE26EA99D2155C724237FB0FC04E4D76` |
| Production kernel ELF | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |

The staged ramdisk hashes differ because each isolated copy contains its own diagnostic ELF; the original modified `ESP/ramdisk.img` was never staged in place.

## 14. Preservation and worktree comparison

The fresh baseline was captured before edits at `out/dotnet/c188r9-registry-input-reentry/preflight/baseline.json`. It measured 13 protected roots, 8,497 protected files, and 136 pre-existing worktree status paths. The final comparison passed with all 13 roots and 8,497 files unchanged, all 136 pre-existing status paths preserved with matching flags and file hashes, and zero new status paths. The original `ESP/ramdisk.img` SHA-256 was `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`; the final comparison confirms that exact hash. The machine-readable result is `out/dotnet/c188r9-registry-input-reentry/preflight/final-comparison.json`.

The historical uncertainty around 212 older C185 generated files remains unchanged and is not presented as a new deletion or alteration. Existing R8 evidence and earlier NativeAOT artifacts were not overwritten. Only new R9 artifacts were written under the dedicated R9 output root.

## 15. Remaining C188 gates

Notes input acceptance is not achieved: there was no handler call, editor focus, insertion, document-state change, or successful redraw. Proof-disabled production Notes input remains a separate gate. The six C188 acceptance scenarios were not run. C189 was not started. No branch change, worktree, stash, reset, clean, merge, rebase, cherry-pick, history rewrite, or push was performed.

## 16. Recommended C188R10 direction

Use the established launch/input snapshots to investigate why the managed descriptor table and C181 pool are empty to `Dispatch` on the callback reentry path. Focus on module/static-data ownership and initialization across NativeAOT `ManagedMain` reentry, and on whether callback execution observes the same module state as launch. Add a bounded non-pointer state token and capture the state immediately after Notes launch returns and on the first input entry. Keep the native ABI, selector mapping, five-slot limit, and generation checks intact. First create a hosted reproducer only if it can exercise the real ownership boundary; otherwise preserve the guest as the repro. Make a product fix only after locating a concrete state transition, then require one corrected targeted guest showing handler invocation, focused editor, character insertion, document change, redraw, and continued responsiveness.

This is a recommendation only. C188R10 and C189 have not been started.

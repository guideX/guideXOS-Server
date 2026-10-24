# C188R5 — Notes proof-test invocation separation

## Result

**Outcome C — The unintended C133/C134 host-test invocation is corrected, but Notes startup still stalls later in the launch path.** The selector-4 diagnostic guest reached C120 setup, created the Notes surface, and returned from its first `RenderMain`. It did not return from `ManagedNotes.Launch`, reach the kernel input loop, display the Notes window in the framebuffer, or accept guest input. No proof-disabled production guest was run.

C188R5 remained on `v1.1_DOTNET_SUPPORT`, starting at `3cabd98615e5e35e8b3156f0688f6ffc992e56d2`. It did not start C188R6, C189, or the six-scenario acceptance campaign.

## C188R4R1 finding and context audit

C188R4R1 entered real selector-4 `ManagedNotes.Launch`, validated C147/C148 state, and stopped at checkpoint `C188R4-380 BEFORE C133-PROOF-TEST-CALL`. Its evidence did not distinguish the preceding synchronous log from `GuideXosComboBoxC134HostTests.Run`; neither is individually identified as the blocking call.

The source audit confirmed that `_c133ProofContext` is application setup state. C151 is merged into it, C152 initialization sets `_c152Enabled`, and `_c151ProofContext` includes that enabled state. Those flags also feed C120/C147 related context setup, including the C147-to-C120 propagation. They are not authorization to run C133/C134 host suites.

| Launch context or merged state | C133 setup state | C133 host suite | C134 host suite |
|---|---:|---:|---:|
| Ordinary / empty launch context | May be enabled by shared C151/C152 state | No | No |
| `c147-native-notes` | Enabled for Notes and C120 consumer setup | No | No |
| `c151-notes`, `c152-notes`, or their merged state | Enabled for shared Notes setup | No | No |
| C133 control, Notes, disabled, API, or Combo API context | Context-specific | Only the existing C133 API path where applicable | No |
| `c133-host`, `c133-combo-host-tests` | Explicit host qualification | Yes | Yes, the existing C133 host companion path |
| `c134` ordinary control context | Context-specific | No | No |
| `c134-host-tests` | Explicit host qualification | No | Yes |
| C135/C136/C137/C139 contexts | May merge setup state | No | No |
| Any authorized context after the suite already ran | Unchanged | No duplicate run | No duplicate run |

The audited intent is to keep proof-context initialization and control registration intact while authorizing the C134 host suite only for exact C133/C134 host-test launch contexts. The logger/helper ambiguity remains unresolved internally; the correction bypasses the branch during ordinary Notes startup rather than claiming either call independently blocks.

## Source correction and focused regression

Added `GuideXosNotesProofTestInvocation` with exact-context predicates. `ManagedNotes` still computes and merges `_c133ProofContext`, initializes C152 state, propagates C147-to-C120 state, and initializes the Notes control host. Its C134 companion suite now uses `ShouldRunC134HostTests(launchContext, alreadyRun)`. The C133 host classifier also uses the shared exact-context helper. No managed instance fields or input/resize hot-path allocations were added.

The focused host mode passed:

- 23 invocation-context cases, including C147/C151/C152 and C137/C139 merged contexts.
- 8 C152 document-state cases covering untitled edits, Save, Save As, failed-save preservation, valid hydration, and invalid-path rejection.
- C133 host suite: 24/24.
- C134 host suite: 30/30.

The normal managed UI aggregate wrapper passed **38 suites / 930 cases**. The C188 host suite passed **25 cases**, including 1,000 identity operations, 500 valid resizes, 250 invalid geometries, 250 stale-generation attempts, 100 create/close lifecycles, and zero resize hot-path allocated bytes. Prior hosted suites passed C182 15, C185 28, C186 15, and C187 22. The checkpoint validator passed **10 tests**, including the R5/R4 skipped-branch sequence.

The 8-case C152 state test is an additional pure hosted state test. It does not claim the host-backed Save As dialog suite ran in this guest; the normal C166 ordinary composition omits those qualification suites. The partial diagnostic guest reached normal C152 initialization and rendering, but did not reach the input loop.

## Isolated builds and staged diagnostic media

All R5 builds and guest media are under `out/dotnet/c188r5-notes-proof-invocation/`.

| Artifact | Result | SHA-256 |
|---|---|---|
| Diagnostic NativeAOT ELF with C188R3/R4 checkpoints | PASS, ELF64 x86-64, 7 load segments, 0 relocations; CounterApp and ManagedUiContracts compiled independently | `98A7DFEA4582A328A36A5792574B842E766F173A6584FF41F308DE5223EF0F13` |
| Diagnostic freestanding kernel | PASS | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Proof-disabled production NativeAOT ELF | PASS; production selectors 4–8 retained, no CounterApp descriptor or C188 route | `A74F8F9186DBDA2AB7C293149F0F3D2BA09254E7CAB9D8FA8EED784254D83279` |
| Proof-disabled production kernel | PASS; freestanding flags and development-descriptor exclusion verified | `22BC4AFE228263E4187B2AA57EAF21D1CEA46D3992A139F81EC185B1C22154AF` |
| Staged diagnostic ramdisk | PASS; 185 unrelated files preserved | `588E8A5C10EFAB8CF51E8F3D166195E27B977056FFF9090B1A9BA262C2CCEBBA` |
| Diagnostic ESP bootloader | SHA-256 | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The source change did not alter kernel code, the native ABI, ABI v4 / 128-byte table, callback offsets 104/112/120, five residency slots, selector mappings, or C174/C180/C181 behavior. Proof-only R3/R4 checkpoints are present in the diagnostic composite and absent from the proof-disabled production composite.

## Targeted QAC diagnostic guest

The one R5 diagnostic reservation was `55b1920095e14871813022cb38e84381`, owned by the runner PID recorded in `runs/diag-001/active.json`, with guest PID `22548`. QAC verified the reservation and guest release; its post-run census had zero QEMU processes and reservations.

The serial stream proves:

1. Selector 4 launched `com.guidexos.apps.managed.notes`.
2. C147/C148 context checks passed.
3. `C188R4-379` reported `C133-PROOF-TEST=SKIPPED`.
4. C120 control-host setup ran.
5. `C150-SURFACE` created the Notes surface/window.
6. `C188R3-160/161` showed first `RenderMain` entry and successful return; `C188R3-163` completed post-render settings.

The last checkpoint was `C188R3-163 NOTES-POST-RENDER-SETTINGS-RETURN`. The validator's first missing checkpoint was **Notes launch result**: `C188R3-011 APPLICATION-LAUNCH-RETURN` was not observed, and the kernel did not log input-manager initialization or its main input loop. The final QMP screenshot (`86F347EDE6519F3DD1DCC99A221BC2F618CFEA59FE8B61658959A8036B87EF70`) shows the desktop shell rather than the Notes window. No Notes pointer or keyboard input was sent because the guest had not entered its input loop. Therefore the visual-window and real-input requirements are not met.

The generated manifest's `result` field says `GUEST-REACHED-LOOP-BUT-NOTES-INPUT-PROBE-FAILED`, but its own `outcome=startup-timeout-or-guest-exit` and `bootLoopReached=false` fields show that the loop was not reached. That result-priority bug was corrected in the runner after this captured run; the historical manifest and serial evidence were left intact.

The diagnostic guest progressed well beyond the R4R1 boundary, demonstrating that the unintended C133/C134 host-test invocation was removed. A later uninstrumented stall remains after the first Notes render and before launch return. The exact later blocking operation is not established. The broad C137 proof context can also be enabled by C151 state merging, but this run did not capture enough instrumentation to claim that its host-test helper is the blocker.

Because the diagnostic did not reach the input loop with a visible Notes window, the conditional proof-disabled production guest was not launched. A separate QAC reservation appeared after the R5 guest had released its own lease; it was left untouched. The next saved census is empty. Production runtime startup and input readiness remain unverified.

## Preservation and remaining gates

The fresh preflight recorded 13 protected roots, 8,497 files, 136 pre-existing status paths, and the pre-existing `ESP/ramdisk.img` SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. Post-run comparison found zero path, size, or hash differences across all 13 roots and zero differences in the full historical C188R3/R4 evidence inventories. The ramdisk hash remained unchanged. The prior C185 uncertainty concerning 212 generated files remains unresolved and preserved.

C188's six identity/lifecycle/coexistence acceptance scenarios remain pending; this was one diagnostic attempt only. Proof-disabled production runtime, a visible Notes-window screenshot, actual Notes input, and full C188 acceptance remain gates.

### Recommended next scope

For a future C188R6, keep the R5 invocation predicate. Add a small bounded checkpoint pair around the remaining `ManagedNotes.Launch` work after `C188R3-163`, with particular attention to the C137 helper selected by merged C151 state, then use one fresh diagnostic guest only after QAC is free. Do not infer the exact blocker from the current boundary, and do not resume C188 acceptance until the Notes launch reaches the input loop with real rendering and input.

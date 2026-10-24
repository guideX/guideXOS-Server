# guideXOS Server .NET Support — C188R3 Notes Startup Boundary

**Outcome B — Boundary narrowed.** The diagnostic guest entered the real selector-4 `ManagedNotes.Launch`, passed the recorded C147/C148 context checks, and completed the startup state reset. It then stopped producing serial checkpoints before control initialization began. QMP still reported QEMU running at the bounded timeout, and QAC released the sole diagnostic reservation. The failing operation inside the remaining source interval is not established; no product correction is justified.

## Scope and historical evidence

The run started from `ee2f372ce3fe333744cc0b8c84fd55d9f8b76cfb` on branch `v1.1_DOTNET_SUPPORT`. The six historical C188 scenarios remain failed or incomplete as recorded in [the C188R2 diagnosis](NATIVEAOT_C188R2_STARTUP_READINESS_DIAGNOSIS.md): focused runs stopped during selector-4 Notes startup after C148; production runs stopped after `C150-APP-INSTANCE`; none reached the main input loop. C188R2's single production response probe confirmed `running=true` and guest PS/2 interrupt activity without a redraw or launch return.

C187's accepted guest replaced the selector-4 Notes factory with CounterApp, so that boot does not qualify the real Notes path. C181R1 records an earlier accepted real Notes baseline, but its source and artifacts predate the current candidate. These older results provide context and do not identify the current failure.

The C188R3 development candidate kept production selectors 4–8 and the development-only CounterApp selector 9. The actual diagnostic launch was selector 4 Notes. The standard production NativeAOT build contains selectors 4–8, has no CounterApp descriptor or C188 route, and was used only for build-time diagnostic-exclusion checks; it was not booted in this phase. ABI v4 / 128 bytes, callback offsets 104/112/120, five managed application slots, and existing resize/lifecycle contracts were retained.

## Source call graph

The relevant path is:

1. The kernel's managed-launch path enters the NativeAOT entry in `samples/managed/HostLogProof/c102_startup.c`, then the selector dispatcher in `GuideXosApplication`.
2. After the C150 lifetime manager creates or retrieves the selector-4 instance, `C150-APP-INSTANCE` is emitted. The diagnostic `C188R3-010` marker is immediately before `application.Launch(host)`; `C188R3-011` and `-012/-013` report its return and result.
3. The Notes instance dispatches to `ManagedNotes.Launch` in `samples/managed/HostLogProof/Applications/ManagedNotes.cs`. C188R3 records its entry (`-100`), context validation (`-101`), settings/context readiness (`-102`), and launch state reset (`-103`).
4. Later checkpoints bracket control-host creation, bounded list setup, Notes document loading, `host.TryCreateSurface`, C175 layout initialization, the first `RenderMain`, and the final Notes launch result (`-110` through `-171`).
5. If managed launch returns, the native caller emits `[C102-LAUNCH-RETURN]`; kernel startup then emits the input-manager marker and, after initialization, `[KERNEL] Entering main loop (waiting for input)...` from `kernel/core/main.cpp`.

The rendering checkpoints (`-200` through `-299`) bracket `RenderMain`, its buffer setup, background and text drawing stages, Notes controls, buttons, and the method return. They use the existing host logging service; they add no native ABI or callbacks into managed UI code.

## Instrumentation and qualification

All new source checkpoints are under the proof-only `HOSTLOGPROOF_C188R3_NOTES_STARTUP` compile guard. The diagnostic uses static UTF-8 literals and the existing `host.TryLog` path; it adds no loop logging, heap-backed message construction, synchronization, thread, kernel hot-path marker, or lifecycle change. The ordinary production build rejects retained `C188R3-` strings from both its ELF and map.

The isolated diagnostic candidate at `out/dotnet/c188r3-notes-startup-diagnostics/build/candidate-007/` passed NativeAOT compilation and PE-to-ELF validation: ELF64 x86-64, seven load segments, zero relocations, and all diagnostic markers retained. The managed composite SHA-256 is `DACCAD7059F5EB5CD431728F1D3103FC2AE355B8C4BFD91173375D4B1315AF0C`. The diagnostic verifier's final full marker inventory is in `nativeaot/qualification-c188r3-final/nativeaot-qualification.json`.

The focused hosted C188 checks passed: 25 cases, including 1,000 identity cases, 500 valid resizes, 250 invalid-geometry cases, 250 stale-generation cases, 100 lifecycles, and zero resize-stress allocations. The candidate freestanding kernel compiled as ELF64 x86-64 with `-std=c++14`, `-ffreestanding`, `-fno-exceptions`, `-fno-rtti`, `-nostdlib`, `-nostdinc++`, and `-fno-builtin`; its SHA-256 is `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`. The proof-disabled production NativeAOT build passed with no C188R3 markers; its ELF SHA-256 is `4614E1115C8D23DF892241DAD67ACA6A0A0EC8D06D9505855909EC1CCF51869B`.

## Single QAC diagnostic guest

Expected observations were written before the runner queried live QAC status. The QAC preflight showed the coordinator online with zero QEMU processes and zero reservations. The run acquired one owner-verified `guideXOSServer` acceptance reservation (`d4378c96d6a5409d8fe8fef9b774115c`, owner PID 10120); its QEMU child PID was 17100. Admission took 1.3 seconds. QAC reported the one session as `Running` and then, after QMP quit, reported zero processes, reservations, and sessions. Both the QEMU child and QAC wrapper exited 0, and QAC logged the matching reservation release.

The guest used a 150-second startup bound, based on C187's accepted 131.5-second full run, and a separate 60-second admission bound. The QEMU command is preserved in `out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/qac-run-arguments.json`: TCG, `pc`, one vCPU, readonly OVMF pflash, isolated FAT ESP, 1024 MiB RAM, standard VGA with no display, file serial, loopback QMP on port 57608, `-no-shutdown`, and UTC RTC. No competing QMP observer was attached.

The isolated media hashes were:

| Artifact | SHA-256 |
|---|---|
| Kernel ELF | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Managed composite | `DACCAD7059F5EB5CD431728F1D3103FC2AE355B8C4BFD91173375D4B1315AF0C` |
| Staged ramdisk copy | `BC410739A1436CAC3847099B443F18B15DE6149E9A89679E2E470CD4524BAB0B` |
| EFI loader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

At timeout QMP returned `{"status":"running","running":true}`. The serial log contains this ordered checkpoint sequence:

```text
C150-APP-INSTANCE id=4 generation=1
C188R3-010 APPLICATION-LAUNCH-BEGIN
C188R3-100 NOTES-LAUNCH-ENTRY
C188R3-101 NOTES-CONTEXT-VALIDATED
C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS
C148-NOTES-CONTEXT settings=v2-migration-proof source=shared-runtime result=PASS
C188R3-102 NOTES-SETTINGS-CONTEXT-READY
C188R3-103 NOTES-STARTUP-STATE-RESET
```

**Last completed checkpoint:** `C188R3-103 NOTES-STARTUP-STATE-RESET`.

**First uncompleted checkpoint:** `C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN`.

The marker interval from `-103` to `-110` contains launch/context flag classification, Notes proof-context setup, and control state/layout preparation, including conditional C126/C127 layout calls. No checkpoint in that interval establishes which conditional path ran or which operation stopped. The serial log has no Notes surface request, layout, render, launch-return, C102 return, input-manager, or kernel input-loop marker. The listbox and renderer stages were not reached, so this boot does not support the earlier speculative ListBox or rendering theories.

The runner's original `diagnostic-manifest.json` reports `C150-APP-INSTANCE id=4` as its first missing checkpoint because its expected list incorrectly placed C148 before C150. The raw serial ordering above is authoritative. The checked-in runner now uses the corrected order, and `checkpoint-sequence-correction.json` preserves the corrected expected/observed comparison without rewriting the original run record; it identifies `C188R3-110` as the first missing checkpoint.

## Evidence and remaining work

Run evidence is isolated under `out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/`. Key hashes: serial log `D199AD79155CE58C8D2C4D56AB5238B6D0816661B28E879C6171660CAA8BAA98`; QMP log `473D183BF5BDDBB2DA84266A04C6B04DA70206E674B30F6D0AB94BCF90AE4DBE`; final-stage framebuffer PPM `8AED5BA950B864C62147A2D42FF6BF1284E372C61D108C0DAF02703FD4AB9C0D`. The run manifest records QAC reservation/child identities, QMP status, media hashes, and the missing input-loop predicate.

The before/after protected inventory covers the same 13 roots and 8,497 files. File path, length, and SHA-256 comparison reports zero added, removed, or changed files. `ESP/ramdisk.img` remains the pre-existing modified file with SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The starting 136 status paths were retained; added tracked changes are the bounded source instrumentation, qualification scripts, and reports. Historical evidence and untracked build outputs remain untouched. The C185 uncertainty about 212 earlier files remains unchanged.

There was no product behavior correction because no single defect or failing call was demonstrated. Per the one-guest bound, this run was not repeated. The next discriminating experiment is a bounded instrumented candidate that subdivides the interval between `-103` and `-110` around launch-counter/thread-static validation, context classification, and the conditional control/layout setup, followed by one authorized diagnostic guest. The six C188 acceptance scenarios remain pending: resume them only after the startup cause is identified and corrected or conclusively classified, a targeted candidate reaches the normal input loop, Notes renders and accepts input, selector identity is verified, QAC cleanup succeeds, and production diagnostics remain excluded. C188 is not accepted; C189 has not started.

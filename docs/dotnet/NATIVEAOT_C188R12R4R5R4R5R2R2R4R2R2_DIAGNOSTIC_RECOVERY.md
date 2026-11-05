# C188R12R4R5R4R5R2R2R4R2R2 — NativeAOT Diagnostic Recovery

**Outcome: `DIAGNOSTIC_RECOVERY_HOSTED_ACCEPTED`.** The safe-stop evidence has been reanalyzed with lossless sequence parsing, the bounded backend summary correction passes hosted regressions, and a final isolated candidate plus proof-disabled production NativeAOT and kernel builds pass. No guest was launched in this phase.

This outcome accepts the hosted recovery work. It does not change the original R2R1 guest’s `SAFE_HANDOFF_DIAGNOSTIC_INCOMPLETE` classification, and it does not accept a managed transition or runtime.

## Repository and preservation

| Item | Value |
|---|---|
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `03ef7b53082ebec11dfcec9b001ad919e250a24d` |
| Ending implementation HEAD | `3ca00c5d6acf01674ff74a80e5f12340ee09f30b` |
| Implementation commit | `3ca00c5d6acf01674ff74a80e5f12340ee09f30b` (`Recover NativeAOT safe-handoff diagnostics`) |
| Source ramdisk | `ESP/ramdisk.img`, SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The implementation HEAD above is the ending HEAD for source, scripts, and tests. This closeout is committed in a documentation-only follow-up; the final repository HEAD after that commit is reported in the completion note.

The three pre-existing tracked object files remain unchanged and were excluded from both commits:

| File | Preserved SHA-256 |
|---|---|
| `c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj` | `3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD` |
| `c188r12r4r5r4r5r2r2_native_alloc_record_test.obj` | `605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3` |
| `guidexos_nativeaot_native_alloc_record.obj` | `2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183` |

The prior raw serial, runner analyses, guest manifests, QAC records, QMP log, and prior candidate manifests were left intact. The new reviews and qualification outputs are under `out/dotnet/c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery/`. The machine-readable provenance record is `recovery-analysis/recovery-analysis-manifest.json`, SHA-256 `D6DCA0A6ECF24B0AE40AADF4A34276BA55F54C3705F57FD35B4F36337DE53E5D`; the final candidate manifest contains the full build artifact hash map.

## Recovered safe-stop evidence

The immutable R2R1 serial is `out/dotnet/c188r12r4r5r4r5r2r2r4r2-safe-handoff/qac-guest-r2r2r4r2r1-001/serial.log`, SHA-256 `27C653B08FCF0CBBD4696498FE86AD97591DA3EFFB7F27BD40A2230A18CE425C`. Its original runner analysis remains at SHA-256 `0662275A98FE6A05E06CF97172C433FD98A55661A7896ACCFF3F4F6DA4CA56A3`; its original native-allocation analysis remains at `FAD9D06FC8F84359C3051E39266AF36308DA0697A9740C7C82FF4FC168FF6895`. The guest manifest and previously booted candidate manifest remain at `A89A1319B0989B37B09E47E693E58EF45E1F48E6F4592B17EDF03138314A4524` and `48316C803A164C9C99B278617540A75FC2F1691FC39A52BAA919799D70B5BCA9`.

The corrected, separate review is `recovery-analysis/safe-handoff-reviewed-r2r1-final.json` (SHA-256 `150EE8075B23BC55F2BE8CE18F1F9995D091F497F97C7A12681AFBAFA60C8256`). It reports `SAFE_HANDOFF_INTENTIONALLY_STOPPED` and confirms:

- `RhInitialize`, Workstation GC initialization, TLS/FLS setup, OS module registration, and code-manager registration succeeded.
- The initialization trace has 29 stages and 93 represented events, with zero trace overflow and zero serial drops. No runtime error, tooling failure, or trace first-failure record is present.
- The safe-stop marker is present. `InitializeModules`, reverse-P/Invoke, and managed entry were not attempted in this R2R1 run.
- Eight thread-readiness sequence values parse in order with no gaps, duplicates, malformed markers, or out-of-order values. The raw TLS block and ThreadStore are published, but `GetCurrentThreadIfAvailable()` is null and attachment is not established.

The safe-handoff trace is complete. The separate legacy native-allocation record is not, so the original overall guest result remains `SAFE_HANDOFF_DIAGNOSTIC_INCOMPLETE` as required by its acceptance contract.

## Sequence-marker correction

The producer is `GuideXosNativeAotThreadReadinessMark` in `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`. It increments a `uint32_t` counter and formats it through a pointer-sized hexadecimal writer. On this AMD64 build, the wire token is 16 hexadecimal digits, padded with leading zeroes; the semantic producer domain is still `uint32_t`.

The old milestone-order expression accepted the 16-digit alternative without capturing the `seq=` prefix, producing a false ordering failure. The analyzer now preserves each token, parses it losslessly as `UInt64`, validates it against the producer’s 32-bit domain, and uses the preserved token for correlation. It does not use floating point or signed/narrow conversion. The hosted tests cover 8- and 16-digit wire forms, leading zeros, the preserved guest values, maximum producer values, high-bit and maximum 64-bit tokens, malformed tokens, duplicates, out-of-order input, and incomplete records.

## Native-allocation record and bounded correction

The old backend capture in `kernel/core/nativeaot_pal_qemu_test.cpp` retained nine detailed 24-byte VM-allocation rows. The tenth attempt set a boolean overflow and was no longer captured. The corrected review, `recovery-analysis/native-allocation-reviewed-r2r1-final.json` (SHA-256 `7CB5A5201AA4A5AE7413593B7D2322B32A7085D0D2146CF5C0775FA9BFA46209`), therefore reports `INCOMPLETE_DIAGNOSTIC_RECORD`: nine rows, at least ten attempts, first dropped attempt index 9, and no exact total, operation counts, terminal state, or failure status. The retained rows show no allocation failure. The dropped row cannot be assigned to an initialization phase because the old backend record has no stage identifier. This is diagnostic loss, not evidence of a runtime initialization failure.

The diagnostic harness now keeps the same nine-row detail cap and adds a fixed-storage v2 aggregate in `kernel/include/kernel/nativeaot_native_alloc_backend_summary.h`. It counts attempts, completions, outcomes, reserve/commit/rollback-release operations, the first failure tuple and up to 32 distinct failure tuples. It emits an explicit terminal state, exact dropped-detail count, and first dropped attempt. Saturation, failure-code capacity overflow, or inconsistent totals produce an incomplete status. The callbacks allocate no memory, take no locks, and perform no I/O. This change does not modify the accepted 256-slot PAL table, Workstation GC behavior, the runtime allocator, or production application startup.

The hosted regressions cover 2,048 successful attempts with nine detail rows and 2,039 summarized rows; repeated reserve/commit operations; failures before and after detail capacity; first-failure status, branch, and source; distinct cleanup failures; 64-bit outcome-total overflow; duplicate failure tuples; incomplete terminal state; and counter saturation. They also verify that a legacy boolean overflow remains “at least 10,” never an exact count. The current native-allocation hosted results are `candidate-20261105T224500000Z/hosted-tests/hosted-test-results.json` (SHA-256 `53BA0723D35E3A46DADE1C931A0E4E4FC9962291E2D225C0F009EADD10E2357A`); the linked nothrow `operator new`/PAL integration and the 256-slot table test passed, including 2,425 assertions and 1,024 reserve/release cycles.

## Earlier InitializeModules failure and source audit

The separate earlier R4 transition guest is preserved at `out/dotnet/c188r12r4r5r4r5r2r2r4-pal-capacity-fix/qac-guest-r2r2r4-001/serial.log`, SHA-256 `D9F14AC9DB9E39868D26F2CDBA798815D6A33C89A475DC9BEAFE998FFAD28FBF`. It entered `module_initialize_enter`, recorded a reverse-transition frame, then reported `FAIL_FAST reason=50414C06`; managed entry was not reached. Its thread sequence is complete through 14 events, but the older stage trace overflowed by 25 and the separate native-allocation record overflowed. The current reanalysis is `recovery-analysis/prior-transition-failfast-reviewed-r4.json` (SHA-256 `62412AE42ED29029AFBD360F87E2CEA4286B579B6D3F55EBBC3A5BE6F98461F9`). The newer safe-stop R2R1 serial contains no fail-fast marker.

The source and linked-binary audit of the final isolated candidate found:

- The candidate links generated `InitializeModules` at `0x100029020`. Its prologue calls `RhpReversePInvoke` at `0x10004d320`; the epilogue calls `RhpReversePInvokeReturn` at `0x10004d6d0`.
- `initializeNativeAotModules()` registers the OS module and code manager before calling generated `InitializeModules` with the module bookends and 16 class-library function slots. The safe-handoff path returns after registration, before module initialization.
- `RhpReversePInvoke` requires a non-null frame and TLS block plus a valid `_tls_index`, initializes runtime state, and calls the stock reverse transition. In `thread.cpp`, `Thread::ReversePInvokeAttachOrTrapThread` calls `ThreadStore::AttachCurrentThread()` when needed; that path calls `PalAttachThread`, constructs the `Thread`, takes the ThreadStore lock, marks it attached, and adds it to the store.
- `ThreadStore::RawGetCurrentThread()` returns the TLS object address. `GetCurrentThreadIfAvailable()` additionally checks `IsInitialized()`. A nonzero TLS object address alone does not demonstrate an attached thread.
- `PalPrintFatalError` in `guidexos_nativeaot_pal_minwin_replacement.cpp` discards its message and always fail-fasts with `0x50414C06`. The linked transition code has a callsite for the “UnmanagedCallersOnly method from managed code” condition when the current thread is already cooperative. The linked runtime also has fatal callsites for stack overflow and unsupported CPU features. The previous R4 guest recorded neither the message nor an instruction pointer, so the numeric code does not identify which condition occurred.

The R2R1 safe-stop is ready for the next investigation only through module registration. It has no attached current thread, observed reverse-transition frame, or qualified runtime stack-bound marker. No ThreadStore repair, forced attachment, automatic dispatch, or transition bypass was attempted.

The next phase should add a diagnostic-only fatal-reason capture before the existing fail-fast: a fixed 256-byte static buffer, first-writer claim, bounded copy with NUL termination and truncation flag, and emission through a bounded nonblocking diagnostic sink. It must not allocate, lock, wait, dispatch managed code, suppress the original message, or change fail-fast code/behavior; production must compile it out. Hosted tests should cover first-writer behavior, truncation, null input, and preservation of the original message. Before another transition attempt, record TLS/FLS validity, current-thread initialization and attachment, PAL stack bounds, frame state, ThreadStore state, and the cooperative/preemptive state. The transition itself remains a separate diagnostic-only phase.

## Runner timing and hosted qualification

The archived run followed the correct QAC contract: preflight census, exclusive one-boot guard, `qac run`, admission grant, owned QEMU child, ownership verification, owned shutdown, and release verification. The guard timestamp was `2026-11-05T16:25:20.8441188Z`; QAC granted admission at `2026-11-05T16:25:21.1260432+00:00`; the child started at `2026-11-05T16:25:21.1798928+00:00`. The guard preceded grant by 281.9244 ms, and grant preceded child start. Since `qac run` performs admission and launch, no grant-before-command requirement applies. The corrected runner contract passes against the final candidate; its captured result is `recovery-analysis/runner-contract-candidate-20261105T224500000Z.txt` (SHA-256 `8817205321E189D3FD64BCC7722420FC1D48DD94085AF77F15D53779AEC44CEF`).

Relevant hosted checks passed: sequence and safe-stop analyzer; native-allocation fixed record and summary; actual linked operator-new/PAL integration; 256-slot VM table; GC-init and semi-shared probes; ThreadStore readiness and init-return contracts; QAC admission, ownership, and stream predicates; QMP/serial preservation; C188 lifecycle and factory; and targeted PAL stack-bounds checks. The existing UI baseline was reused under project policy because no UI or product behavior changed: 40 suites and 969 cases. The managed GC-ownership test was not run because it performs managed GC collection, which this phase excludes.

The targeted PAL stack-bounds and ThreadStore adapter checks pass. The optional generic full-kernel substep in `smoke-native-stack-bounds.ps1` hit an unrelated existing compile error at `kernel/core/desktop.cpp:8941` (`nativeaot` undeclared); the required isolated proof-disabled production kernel build passed.

## Candidate and production isolation

The final candidate is `out/dotnet/c188r12r4r5r4r5r2r2r4r2r2-diagnostic-recovery/candidate-20261105T224500000Z/`. Its manifest is `SAFE_HANDOFF_HOSTED_READY`, SHA-256 `F92FC47BAFF7CE24653C65F141FF64276BDF60D6325FCC97560F10DD127AF912`; it records `nextGuestRunPerformed=false`. The diagnostic NativeAOT ELF hash is `BD14FC342102FCE48E39B94289CEB93A034732232B3B73BE09B1BAC3026A3B17`, and the diagnostic kernel hash is `6B30CF35DE03D0DF801EA75765ED981AE21A9639695038246D5FF99DA0D4B1A8`.

The proof-disabled production NativeAOT and kernel qualifications both pass. The production NativeAOT ELF is `F45F4D8CCF3E16E3BECCBAE00A4A59DDACB44A8F0F14A8088418B6440BA567F5`; the production kernel ELF is `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168`. `persistentCompositeLifecycle=true` remains set. Production qualification reports no diagnostic attribution profiles, no C188 selector route, and `standardKernelOutputTouched=false`.

An earlier isolated candidate at `candidate-20261105T200000000Z` was preserved and not used for qualification after its manifest showed an older summary-helper hash. The final candidate was rebuilt from the final source and its manifest hashes for the helper, native-allocation analyzer, and hosted summary test match the current files. Both candidates remain unlaunched.

## Final recommendation

Proceed with **Option B — InitializeModules transition attribution**. Reuse the accepted safe-stop milestones, add the bounded diagnostic-only fatal-reason capture and transition-state markers described above, and qualify attachment and stack-bound prerequisites. Do not treat `0x50414C06` as a proven root cause; the earlier guest did not preserve the message or callsite. No QEMU launch, QAC reservation, managed execution, managed GC collection, or ThreadStore repair occurred in this recovery phase.

# C188R16 — Native Unwind Caller Attribution

## Result

**Outcome C — NATIVE_UNWIND_CALLER_ATTRIBUTED.**

R15's exact paused instruction was in StackFrameIterator::NextInternal, immediately after its indirect code-manager unwind call returned true. R15 alone does not contain stack bytes, so its caller cannot be reconstructed from that capture.

R16 used the same R13 managed-GC probe with bounded stack capture. Its candidate-bound stack chain identifies the active walk as GC root enumeration, reached through the explicit RhCollect request in the managed probe. The sample is inside the NativeAOT runtime-function lookup called by CoffNativeCodeManager::FindMethodInfo. This attributes the R16 walk and the corresponding R13 collection path; it does not retroactively recover R15's missing caller frames.

The lookup is a bounded binary search followed by a short linear scan. R16's sampled bounds require at most three more midpoint updates before that scan. One snapshot cannot show temporal progress or prove collection completion. No unwind defect, invalid unwind record, infinite loop, fatal, or completed collection was established.

The smallest justified next step was the bounded, read-only stack capture added to the guest runner. That capture answered the caller question. The single authorized R16 boot is consumed; no retry or second boot was performed.

## Repository and preservation

- Branch: v1.1_DOTNET_SUPPORT
- Initial HEAD: 585ef4228fdd01df768d1787e4ac1fded276f1ad
- R16 candidate source HEAD: 585ef4228fdd01df768d1787e4ac1fded276f1ad
- Pinned NativeAOT runtime source: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3

R15 and R16 evidence were each resolved against their own candidate. R15's candidate was built from c56d68e1d8138023897f143f5c0b52dc454080e4; R16 used a newly built candidate from the expected 585ef... HEAD. No R15 guest address was resolved against the R16 image.

The three pre-existing modified object files retain their recorded SHA256 hashes:

| File | SHA256 |
|---|---|
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

R15 logs, recovered metadata, candidate, and consumed one-boot guard were preserved. The R16 candidate, one-boot guard, raw run artifacts, and hosted analysis are under the external artifact root shown below. No push was made.

## R15 snapshot reconstruction

R15 run root:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T165632338Z-871e2c199adb44efa5884f2771d6cdc0

Recovered metadata: guest-cpu-state-timeout-recovered.json.

R15 candidate root:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z

R15 candidate hashes:

| Artifact | SHA256 |
|---|---|
| Candidate manifest | 1BFCE8D0B0F15CF2990D35C55E8101E635B68766C2DAEA38AB2AB35C51C63592 |
| Managed PE | CAB3253A7934C39A4C2FE67E28431E4A6B3FB9B69FDD555A4D281E92065BB9E1 |
| Managed map | 96219304BABCA30929431DAC730AABCF6A257A78945414C58B5663103A4561F8 |

R15 captured CPU 0 at CPL 0, 64-bit CS, with CR0 0x80010033, CR3 0x3DE55000, CR4 0x668, EFER 0xD00, and RFLAGS 0x202.

| Register | Value | Register | Value |
|---|---:|---|---:|
| RIP | 0x100057ECC | RSP | 0x52B5200 |
| RBP | 0x52B5360 | RAX | 0x1 |
| RBX | 0x52B5340 | R14 | 0x52B5568 |

Resolving RIP against the R15 PE and map places it at ?NextInternal@StackFrameIterator@@AEAAXXZ. The instruction at 0x100057EC9 is call *0x28(%rax); RIP 0x100057ECC is the following test %al,%al. The ABI slot is ICodeManager::UnwindStackFrame. AL is 1, so that call returned true and the fail-fast branch was not taken. The active .pdata range is RVA 0x57e28–0x581e8, with unwind info RVA 0xf3284 chained to function base RVA 0x57e20.

R15 had no captured stack memory. Its RBP value is not used as a substitute for compiler unwind metadata, and no caller is inferred from a plausible function name. The raw register capture SHA256 is 0E055B7A96B0C50262B78DA99113EFDC1F27FFABFC6D625889F69DD1A52E5ACC; the raw serial SHA256 is AC10CBB77C83A5CACC4CB7AD32E2818877667DCEB30C525515E27475A7B7B591; the QMP log SHA256 is 30FDABD89F474E21E30C5B46C54A7F42C2E3802DAB0AAC2C27F5D138BFD7BC12.

The guest was captured after the bounded 180-second wait expired without terminal evidence. The host paused and captured it, then shut it down. There was no recorded fail-fast, fatal, watchdog, or explicit runtime failure.

## R16 candidate and capture

R16 candidate root:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z

It was built in NativeAotManagedGcRootProbe mode with expected result 1123 and the pinned runtime commit. The candidate manifest SHA256 is 9820206C1E892EC9446EE4CF845AFD52E8D242879FAE6F73F0B8EA85B183B3C6; managed PE SHA256 is DE428F3C48E90179C9CED361220E88B054DBAF21979A36B024F3F7921C6367C1; map SHA256 is 5651EA4DC662035AEC2AB1B124EFEC9C2F7DEC26AF1243F70EFBF9B17EF0F7DA. The candidate inventory had 1,198 records, all verified with zero mismatches; the inventory file itself was the one expected unlisted file because it does not hash itself. Candidate preflight passed without querying or reserving QAC and without launching QEMU.

The candidate-specific launch guard has SHA256 BF6CC4B45447C1213BD1C32AD3846B4D9A5BD5C659D813BEC9F1147B727E02C8, maxGuestBoots=1, and automaticRetry=false. It was consumed exactly once for correlation C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165.

R16 run root:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165

The capture is bound to that manifest, PE, map, serial, and stack sidecar. The image base is 0x100000000 and the relocation directory is empty. The snapshot was CPU 0 at CPL 0, 64-bit CS, with CR0 0x80010033, CR3 0x3DE55000, CR4 0x668, EFER 0xD00, and RFLAGS 0x202.

| Register | Value | Register | Value |
|---|---:|---|---:|
| RIP | 0x10005D000 | RSP | 0x52B5228 |
| RBP | 0x52B5360 | RAX | 0x2F |
| RBX | 0x1004AA000 | RCX | 0x981 |
| RDX | 0x1004AA000 | RSI | 0x1004AA000 |
| RDI | 0x52B5498 | R8 | 0x32B |
| R9 | 0x35A | R10 | 0x32B |
| R11 | 0x1E956 | R12 | 0x10063C30 |
| R13 | 0x3D79360 | R14 | 0x52B5340 |
| R15 | 0x52B5610 | | |

The exact R16 PE/map resolve RIP to LookupUnwindInfoForMethod at RVA 0x5D000, offset +0x50, with .pdata function range RVA 0x5CFB0–0x5D053 and unwind info RVA 0xF34A8 (version 1, flags 0, prologue size 5). The sampled instruction is jg 0x10005cfd0. Register R10 gives low index 811, R9 gives high index 858, and RAX is 47; those values agree with high - low. RFLAGS 0x202 makes the conditional branch taken. The pinned candidate implementation binary-searches the runtime-function table while the span exceeds 10, then performs a bounded linear scan. At most three more midpoint updates are needed from this span before that scan. This is a bound on remaining work in the sampled search, not evidence of time-based progress or of a stuck loop.

The read-only guest stack capture starts at RSP 0x52B5228, remains inside the ThreadStore-reported bounds 0x52B2000–0x52B6000, and contains 3,544 bytes of the permitted maximum 8,192. It was obtained with QMP HMP memsave after the guest was paused. Stack bytes SHA256 is 870ACB763FDDD79083130803ACEB09447FB49A64E81F68A2D45FF1CD9FDD43D5; sidecar SHA256 is 68672C7D6CD653DE1D5574BE3C753F79DCB74746FD5B72507583596785EFDBC8. CPU register text SHA256 is 0F8CA4549C7ACB4498E4CC2FA0466A2AE26C7C55F9ED8DB1B36C92378A6E2E33; CPU metadata SHA256 is D73A0C5DF9CD275EF6B5A474AE0BDC9B5B7F6D70444A27962A58D2380D29A158; serial SHA256 is 64BC252EBB6D2E4FC1B947F3632303AEACACBAC3F4ED28891790B41D5F75815C; QMP log SHA256 is 06E890E32D6E27BF38CD072AEE84033B9601F8695E3BF93C7FEFD7269AB4D1C6.

## Recovered caller chain and stack-walk purpose

The offline analyzer applied the R16 candidate's AMD64 unwind metadata and validated return addresses against mapped executable code, instruction boundaries, and their call instructions. It recovered 13 verified caller returns with monotonically increasing RSP values inside the captured guest stack window. The terminal value 0x1D20F6 is outside the candidate executable mapping and remains unvalidated; the chain is therefore not claimed complete.

| Return address | Candidate-resolved caller |
|---|---|
| 0x10005C2ED | CoffNativeCodeManager::FindMethodInfo |
| 0x10005742D | StackFrameIterator::CalculateCurrentMethodState |
| 0x1000534BD | Thread::GcScanRootsWorker |
| 0x100053378 | Thread::GcScanRoots |
| 0x10008E371 | GCToEEInterface::GcScanRoots |
| 0x10007E7AF | WKS mark_phase |
| 0x100076CA9 | WKS gc1 |
| 0x100076B2C | WKS garbage_collect |
| 0x10006272D | GCHeap::GarbageCollectGeneration |
| 0x10006222A | GCHeap::GarbageCollect |
| 0x100052C1C | RhpCollect |
| 0x10001E965 | __managedcode_a runtime wrapper |
| 0x100001A8E | __managedcode_a generated managed entry |

The active iterator is stored in R14 at 0x52B5340. Its candidate-layout fields decode to ControlPC 0x10001E956, REGDISPLAY SP 0x52B5980, and REGDISPLAY IP 0x10001E956, all within captured stack/candidate executable bounds. The previous transition-frame pointer is null. MethodInfo remains opaque. The code-manager pointer value 0x104666010 is outside the candidate PE sections, so it is not used to claim an exact object or vtable identity; the stack itself independently resolves FindMethodInfo.

This is a genuine GC root-enumeration path, not merely a generic code-manager unwind. The exact R16 generated managed disassembly places the request marker 0x52313351 at 0x100001A75, the RhCollect call at 0x100001A89, and the return at 0x100001A8E. The wrapper then calls RhpCollect at 0x10001E960, returning at 0x10001E965. Both return addresses are present in the bounded guest stack and validate against the exact R16 PE/map. The outer chain reaches the GC heap and mark_phase, then the thread root scanner and stack iterator.

Since execution entered RhCollect after the generated managed body's earlier sequential instructions, the R16 guest reached the R13 managed entry and collection request path. The earlier generated sequence contains the managed entry store, RhpNewFast allocation, probe constructor, Calculate() result of 1122, and pre-collection counter read. These are candidate/disassembly-bound path milestones, not separate serial events. Serial output ends at transition_attach_complete, and its R13 analyzer consequently reports the collection request as not observed in serial. That missing terminal record is not contrary evidence: the native record is printed on return, and this guest was captured inside the request's collection path.

The snapshot proves entry into RhpCollect and GC mark/root enumeration. It does not show collection return, a post-collection live-object read, the incremented collection counter, the result 1123, or clean managed/native return. The R16 call chain therefore does not prove one completed collection or object survival after collection.

## Runner, QAC, and evidence packaging

The R16 runner used during the boot had SHA256 91551B8116C7BE39555E3DEDB605AF51A35697A4865546572029C8443F5657D9. QAC strict admission granted reservation 013c1e532dfc45d3bbe5f72050669efd. The bounded wait expired with trigger bounded-managed-gc-wait-expired-without-terminal-evidence; the guest was paused, CPU and stack captures were saved, QMP quit was observed, QAC-owned QEMU exited with code 0, and the reservation was released. The post-run census completed with zero reservations and zero QEMU processes. An incomplete in-run process census was transient; the final census was complete and clear.

The runner then exited with code 1 while constructing its final guest-manifest.json: the version used for the boot referenced an undefined Hash-IfPresent function. This occurred after QAC shutdown and ownership release. The raw serial, CPU, QMP, stack, candidate, and QAC evidence remained intact. No guest retry was made. An external recovery record was written at:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r16-hosted\r16-run-packaging-recovery.json

That recovery record identifies the packaging error and binds the raw hashes and analysis. The in-repository runner now defines Hash-IfPresent, and the R13 hosted suite covers both missing and present paths. Its current source SHA256 is 20783988134FBBBB043538CE53E13BDAB93CA50A95602349FE31835DD3C5B801; it differs from the source hash used for the consumed boot because the helper fix was applied afterward. The runner's existing behavior captures CPU state before shutdown, defers serial hashing until after QAC-owned QEMU exits, and records bounded stack capture metadata without touching runtime-owned memory.

The analyzer output is at r16-hosted\r16-run-analysis.json, SHA256 E1F65F6866B559295B2A4A7529BCE043850DD2673008784E35FBECA73883DE40. The analyzer requires candidate-manifest binding, exact PE/map resolution, register and serial hash binding, bounded stack sidecar/hash checks, and AMD64 unwind reconstruction. Hosted synthetic stack frames are test fixtures only and are not used as evidence for the actual guest call chain.

## Changes in this work

- Updated scripts/dotnet/run-c188r12r4r5-gc-ownership-qac-guest.ps1 to add the missing Hash-IfPresent helper used by final manifest construction. Its existing R16 bounded stack capture is read-only, performed only after bounds/RSP validation and guest pause, capped at 8 KiB, and bound to candidate, CPU, serial, and stack hashes.
- Updated scripts/dotnet/test-c188r13-managed-gc-root.ps1 with the hash helper regression and hosted cases for bounded stack capture failure modes, incomplete snapshots, missing metadata, and deferred serial hashing.
- Added scripts/dotnet/analyze-c188r16-native-unwind.py for candidate-specific symbol, PE .pdata, AMD64 unwind, call-site, stack-bound, iterator-context, stack-walk classification, and bounded lookup analysis.
- Added scripts/dotnet/test-c188r16-native-unwind.py for R15-format parsing, relocation, unwind reconstruction, invalid returns, bounds and missing evidence, classification, R14 iterator-register fallback, and bounded lookup fixtures.
- No runtime GC, transition-frame, or unwind metadata was changed. No root cause repair was indicated.

## Hosted qualification

No hosted suite below launches a guest or reserves QAC. R16 was the one native boot, as authorized.

Focused commands and results:

    python scripts/dotnet/test-c188r16-native-unwind.py --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-20261111T164842868Z --capture-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T165632338Z-871e2c199adb44efa5884f2771d6cdc0\guest-cpu-state-timeout-recovered.json

Result: PASS, 30 assertions. The R15 registers-only input remains explicitly caller-unresolved in this test; synthetic unwind frames are not treated as guest evidence.

    python scripts/dotnet/analyze-c188r16-native-unwind.py --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z --capture-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165\guest-cpu-state-timeout.json --stack-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165\guest-stack-state-timeout.json --output E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r16-hosted\r16-run-analysis.json

Result: PASS; 13 verified caller returns, GC root-enumeration classification, and bounded lookup state written to the named JSON.

    pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\test-c188r13-managed-gc-root.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -OutputDirectory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r16-hosted\final-r13-after-hashfix -RuntimePackManifest E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\runtime-pack\runtime-pack.manifest.json -ManagedMap E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\managed\artifacts\HostLogProof.map -ManagedMainDisassembly E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\managedEntryProbe.disassembly.txt -GcCollectWrapperDisassembly E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\gcCollectWrapper.disassembly.txt -ConstructorDisassembly E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\displaySettingsProbeConstructor.disassembly.txt -CalculateDisassembly E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\displaySettingsProbeCalculate.disassembly.txt -KernelBuildLog E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\kernel-build.log -ProductionNativeAotMap E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\production\nativeaot-build\nativeaot\artifacts\HostLogProof.map -R11HostedTestResults E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\managed-entry-probe-regression-hosted-tests\hosted-test-results.json -CallSiteMapPath E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z\transition-callsite-map.json

Result: PASS, 75 assertions; hosted-only evidence (qemuLaunched=false, qacReserved=false).

The R16 candidate builder was also run with the R13 managed-GC-root probe enabled and ran its candidate-contained hosted checks and production-isolation checks:

    pwsh -NoProfile -ExecutionPolicy Bypass -File scripts\dotnet\build-c188r12r4r5r1-init-attribution-candidate.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -ArtifactRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT -CandidateRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z -ThreadStoreReadiness -GcInitProbes -SemiSharedAttribution -NativeAllocationAttribution -SafeHandoff -InitializeModulesAttribution -ManagedEntryProbe -ManagedGcRootProbe

| Suite / actual runner | Result |
|---|---|
| R12 managed object regression, test-c188r12r4r5r4r5r2r2r4r2r3r12-managed-settings-object.ps1 | PASS, 23 assertions |
| R11 managed-entry regression, candidate-contained hosted runner | PASS, 23 assertions |
| Transition attribution, candidate-contained runner | PASS, 44 assertions |
| ThreadStore/PAL, test-c188r12r4r5r4-threadstore-readiness.ps1 | PASS, 7 mode-model and 6 telemetry assertions |
| QAC admission, test-c188r12r4r5r4-qac-admission.ps1 | PASS, 32 cases |
| Native allocation, c188r12r4r5r4r5r2r2_native_alloc_hosted_tests.ps1 | PASS; 2,425 PAL slot-table assertions, 1,024 reserve/release cycles, allocation-record and backend-summary checks |
| Safe-handoff analyzer/source, test-c188r12r4r5r2r2r4-safe-handoff.ps1 | PASS, 22 analyzer cases and 10 source-contract checks |
| Safe-handoff runner contract, test-c188r12r4r5r2r2r4-safe-handoff-runner-contract.ps1 on preserved transition candidate | PASS |
| Production isolation | PASS in builder and candidate qualification |

Two exploratory safe-handoff runner-contract attempts were incompatible with their supplied candidates: the stale transition candidate failed with Transition analyzer hash mismatch; the R13 candidate was rejected as an unsupported mode. The runner-contract check passed against the preserved NativeAotSafeHandoff transition candidate. These fixture mismatches did not indicate a safe-handoff source regression.

## R11/R12 comparison and next phase

R11 already proves successful native entry and return from the compiled managed entry. R12 proves real managed object allocation, construction, field access, and return of 1122; it did not request collection. R16 reaches the first new R13 operation that requires stack walking: the explicit RhCollect request. Therefore the additional operation explaining the recovered GC root walk is the R13 collection request, not ordinary R12 allocation or generic transition attachment.

Still unproven:

- Return from the in-progress collection and completion of one real collection.
- Survival and access of the same live managed object after collection.
- Post-collection collection-counter evidence.
- Managed calculation and native return of 1123.
- Valid post-return runtime invariants and cleanup.

If another native qualification is separately authorized, the smallest useful discriminator is two timestamped CPU snapshots with the same bounded stack window during one guest run, comparing lookup indices/RIP/SP and the GC phase. This can distinguish movement through the lookup from a repeated state without modifying GC or unwind state. The current R16 boot authorization is exhausted; no additional boot was made here.

## References

- [Pinned NativeAOT StackFrameIterator.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp) — NextInternal and current-method-state calculation.
- [Pinned CoffNativeCodeManager.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp) — method lookup, binary search, and AMD64 unwind implementation.
- [Pinned thread.cpp](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/thread.cpp) — GcScanRoots and GcScanRootsWorker.
- [Pinned ICodeManager.h](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/ICodeManager.h) — code-manager interface and unwind slot.
- [Pinned StackFrameIterator.h](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0bba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/StackFrameIterator.h) and [regdisplay.h](https://github.com/dotnet/runtime/blob/9d5a6a9aa463d6d10b0bba6d5982cc82f363dc3/src/coreclr/nativeaot/Runtime/regdisplay.h) — iterator and register-display layout.
- [QEMU monitor documentation](https://www.qemu.org/docs/master/system/monitor.html) — HMP memsave guest-virtual-memory capture behavior.

# C188R17 — GC Stack-Walk Progress and Runtime-Function Lookup Qualification

## Result

**Overall outcome: E — GC_QUALIFICATION_GUEST_DEFERRED.** The lookup received offline qualification (Outcome D) and maps to **Case A: correct and bounded** for the preserved R16 target. The fresh candidate passed hosted readiness and its own runtime-function-table checks. Strict QAC preflight then refused admission because the coordinator/process census was not online, empty, and internally consistent. The one authorized runner invocation did not acquire a reservation, launch QEMU, or consume a one-boot guard. No second attempt was made.

No native runtime correction was justified. No full GC acceptance or time-separated native progress was established. The R13 acceptance contract remains unchanged.

## Repository, pinned source, and candidate identity

- Branch: v1.1_DOTNET_SUPPORT
- Initial HEAD: c36bbaa6c3c4f611cfd387d6bb51ed7078cfe757
- Pinned NativeAOT runtime checkout: out/dotnet/gc-feasibility-baseline/nativeaot-runtime
- Pinned runtime commit: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3
- Final R17 commit: the commit containing this report; its exact SHA is recorded in the closeout response because a commit cannot embed its own hash.
- No push was made.

R16 candidate and run evidence were preserved at:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z
    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165

R16 candidate manifest SHA256 is 9820206C1E892EC9446EE4CF845AFD52E8D242879FAE6F73F0B8EA85B183B3C6. Its managed PE SHA256 is DE428F3C48E90179C9CED361220E88B054DBAF21979A36B024F3F7921C6367C1 and map SHA256 is 5651EA4DC662035AEC2AB1B124EFEC9C2F7DEC26AF1243F70EFBF9B17EF0F7DA.

The fresh R17 candidate was built in NativeAotManagedGcRootProbe mode from the requested R13 probe and pinned runtime. It has result MANAGED_GC_ROOT_PROBE_HOSTED_READY, R13 probe enabled, and expected managed return 1123. Candidate root:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z

| Artifact | SHA256 |
|---|---|
| Candidate manifest | 25B3C278B35AAD1A0B09060AE079C50112C78BA656F1339745EF4AE15D8D2619 |
| Managed PE | A82AFB9CDA5D9D39A7E31598CA8883CB2FB1ABA49CF78BE8F5D3D1A235385A76 |
| Managed map | 216AE93CADF1140161ABFE6EF2780CD3A83E222C87551CE9F06DE43D470379A1 |
| Specialized kernel | 1188B5EBB8686F70310A31E2742B7D092A638B5304FD7CEEC277C1795426C90D |
| Candidate integrity inventory | 0A63EF85EBBB9AC20E02971B7EA77F86DE1867C89339E4557E24937734B124A5 |

The inventory contains 1,198 file records. Candidate hosted preflight passed with production isolation intact and reported no QAC query, reservation, QEMU launch, or attempt guard. The candidate's source provenance is the pinned runtime commit above. Inspection found no reference to the three pre-existing modified object files in the candidate builder. Their SHA256 values remained unchanged after candidate build and hosted validation:

| Preserved modified object | SHA256 |
|---|---|
| c188r12r4r5r4r5r2r2_native_alloc_operator_new_test.obj | 3507A24E07257422C98CAB4F7430E094A2C83DF93647E6A02621010DD585A2BD |
| c188r12r4r5r4r5r2r2_native_alloc_record_test.obj | 605D6F7F066BAD3669B52C21A89722A7C9750F9C6595EDFDA26BC06E3125AEC3 |
| guidexos_nativeaot_native_alloc_record.obj | 2CD941199D5F34F81F72B0B718A8D1C1023CEAD831464724DEBFEA435F044183 |

## R16 caller-chain evidence

R16's exact image and map resolve its CPU 0 sample to LookupUnwindInfoForMethod at RVA 0x5D000, instruction offset +0x50. The sample was taken at CPL 0 in 64-bit mode with RFLAGS 0x202. Its relevant registers were:

| Register | Value | Register | Value |
|---|---:|---|---:|
| RIP | 0x10005D000 | RSP | 0x52B5228 |
| RBP | 0x52B5360 | RAX | 0x2F |
| RBX | 0x1004AA000 | RCX | 0x981 |
| RDX | 0x1004AA000 | RSI | 0x1004AA000 |
| RDI | 0x52B5498 | R8 | 0x32B (811) |
| R9 | 0x35A (858) | R10 | 0x32B (811) |
| R11 | 0x1E956 | R12 | 0x10063C30 |
| R13 | 0x3D79360 | R14 | 0x52B5340 |
| R15 | 0x52B5610 | | |

RDX and RBX point at the candidate's runtime-function table. R11 supplies the relative target PC 0x1E956. R10 and R9 supply the current low and high indices, 811 and 858; RAX is their difference, 47. The sampled conditional branch is consistent with that state. The iterator object is at R14; its decoded ControlPC and REGDISPLAY IP are 0x10001E956 and REGDISPLAY SP is 0x52B5980. MethodInfo remains opaque, and the code-manager pointer is outside the PE sections, so neither is used to claim object identity.

The R16 stack analyzer reconstructed 13 candidate-bound caller returns, validated against the saved stack and executable call sites:

| Return address | Candidate-resolved symbol | Pinned source identity |
|---|---|---|
| 0x10005C2ED | CoffNativeCodeManager::FindMethodInfo | src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:254 |
| 0x10005742D | StackFrameIterator::CalculateCurrentMethodState | src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1913 |
| 0x1000534BD | Thread::GcScanRootsWorker | src/coreclr/nativeaot/Runtime/thread.cpp:442 |
| 0x100053378 | Thread::GcScanRoots | src/coreclr/nativeaot/Runtime/thread.cpp:393 |
| 0x10008E371 | GCToEEInterface::GcScanRoots | src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:94 |
| 0x10007E7AF | gc_heap::mark_phase | src/coreclr/gc/gc.cpp:29637 |
| 0x100076CA9 | gc_heap::gc1 | src/coreclr/gc/gc.cpp:22328 |
| 0x100076B2C | gc_heap::garbage_collect | src/coreclr/gc/gc.cpp:24077 |
| 0x10006272D | GCHeap::GarbageCollectGeneration | src/coreclr/gc/gc.cpp:50960 |
| 0x10006222A | GCHeap::GarbageCollect | src/coreclr/gc/gc.cpp:50079 |
| 0x100052C1C | RhpCollect | src/coreclr/nativeaot/Runtime/GCHelpers.cpp:98 |
| 0x10001E965 | __managedcode_a runtime wrapper | exact R16 PE/map generated code |
| 0x100001A8E | __managedcode_a generated managed entry | exact R16 PE/map generated code |

The sampled lookup lies inside the real root-enumeration call chain: managed GC request, RhpCollect, Workstation GC, mark phase, GCToEEInterface, Thread::GcScanRootsWorker, StackFrameIterator, and CoffNativeCodeManager::FindMethodInfo. Thread::GcScanRootsWorker calls CalculateCurrentMethodState, enumerates GC references, reports any required conservative range, then advances the iterator. After the frame loop, it scans exception and GCFrame registrations before returning. The GC must continue beyond this worker and mark phase, finish the collection, return through RhpCollect, and then resume the managed probe before the post-collection object and result checks can be observed.

The terminal stack value 0x1D20F6 did not validate against the candidate executable image, so the 13-frame chain is not called complete. The captured R16 lookup itself had not completed; R16 proves where it was executing, not temporal progress or collection completion.

R15 remains separate. Its RIP 0x100057ECC was immediately after the indirect UnwindStackFrame call in StackFrameIterator::NextInternal; AL=1 showed that call returned true. R15 had no saved stack bytes, so its caller remains unresolved. R16's root-scanner chain does not retroactively identify R15's caller or prove that R16's sampled lookup returned.

## Runtime-function lookup qualification

The pinned implementation is in nativeaot/Runtime/windows/CoffNativeCodeManager.cpp. LookupUnwindInfoForMethod is at line 201. RhRegisterOSModule reads the PE exception directory at line 1187 and registers the table at module base plus its RVA, with entry count equal to directory size divided by sizeof(RUNTIME_FUNCTION), at lines 1191–1192. FindMethodInfo calls the lookup at line 268. The lookup source performs:

1. Inclusive low/high binary search while high minus low is greater than 10.
2. Each midpoint is low plus half the span. If target is below BeginAddress[mid], high becomes mid minus one; otherwise low becomes mid.
3. A forward scan from low to high compares the target with the next entry's BeginAddress and selects the preceding entry at the first greater BeginAddress.
4. A final target-at-or-after-BeginAddress check returns the selected entry; otherwise the source asserts an invalid code address and returns -1.

This is not a logarithmic-only implementation. Its initial interval is reduced in a finite number of midpoint updates, followed by at most ten forward comparisons and the final check for the validated 3,063-entry table. The offline verifier compares its replay with the literal source algorithm and tests entry boundaries, malformed and unsorted tables, overlaps, invalid unwind headers, image-address bounds, overflow, and base handling.

R16 PE identity and table:

- Preferred and loaded image base: 0x100000000
- Relocation directory: RVA 0, size 0; no relocations
- PE AMD64 exception directory / .pdata: RVA 0x4AA000, VA 0x1004AA000
- Table size: 36,756 bytes; entry size: 12 bytes; 3,063 entries
- Sample target RVA: 0x1E956; absolute target: 0x10001E956
- Matching entry: index 814, [0x1E920, 0x1E980)
- Saved search interval: low 811, high 858

Literal replay of that interval requires two further midpoint updates, leaving the linear phase to scan entries 811 through 814. Four linear comparisons and the final lower-bound check identify entry 814. The remaining path therefore performs seven BeginAddress checks, including the two midpoint tests. The matched half-open range contains the target. For the full R16 table, exhaustive table replay measured at most eight midpoint updates, ten forward comparisons, and one final check: 19 BeginAddress comparisons total. This is a count of source-equivalent operations, not a wall-clock latency bound.

The fresh R17 image has different PE and map hashes, so the R16 target is not presented as a sample from R17. Its candidate-only qualification independently parsed the R17 image and found the same table location, 3,063 entries, strict BeginAddress ordering, no overlap, executable function ranges, and valid unwind headers. The source-equivalent maximum on that exact R17 table is also eight midpoint updates, ten forward comparisons, and one final check, for 19 total. No R17 guest target was available.

**Lookup classification: Case A — correct and bounded for the preserved data.** The actual R16 target maps to the expected entry; the table is structurally valid; the translated lookup terminates; the full table work is finite and modest. The result does not establish a time bound or guest progress, and it does not establish that the GC stack walk completed.

## R13 completion contract

The probe source remains samples/managed/HostLogProof/ManagedGcRootProbe.cs. It constructs DisplaySettingsProbe(640, 480, 2), checks the initial calculation equals 1122, requests exactly one synchronous GC.Collect, requires a native collection-count increase, mutates Scale on the same still-live managed object to 3, checks the post-collection calculation equals 1123, keeps the reference alive through its last use, and returns 1123. The candidate contract keeps the managed reference live in RSI across RhCollect and uses it after collection. No rooting, collection, or result requirement was weakened.

Evidence of UnwindStackFrame returning true, an entered GC root scanner, or an iterator frame being enumerated is only intermediate evidence. Root scanning is complete only after GcScanRootsWorker finishes its frame loop and remaining reference sources and returns to the GC. Collection acceptance additionally requires the collection to return and the native counter to advance, the same managed reference to remain valid and produce 1123, and the caller to receive 1123 with cleanup verified. The preserved R16 serial analyzer reported MANAGED_GC_ROOT_INCOMPLETE, with no serial-observed request/completion, counter delta, surviving-object check, or 1123 result. The native stack independently proves execution entered RhpCollect and mark-phase root scanning.

## Hosted validation and packaging

Focused R17 commands were run without guest execution:

    python scripts/dotnet/test-c188r17-runtime-function-lookup.py --repo-root D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z --capture-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165\guest-cpu-state-timeout.json --output-directory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r17-hosted\lookup-tests-final-v2

Result: PASS, 625 assertions. The input capture is the preserved R16 candidate-bound CPU snapshot. The suite's synthetic boundary, malformed-table, relocation, overflow, and source-replay fixtures are hosted tests, not guest evidence.

    python scripts/dotnet/verify-c188r17-runtime-function-lookup.py --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r17-progress-pair-20261112T034829000Z --candidate-table-only --output E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r17-hosted\r17-candidate-table-qualification-final.json

Result: GC_LOOKUP_OFFLINE_QUALIFIED for the fresh R17 candidate's table only; guestSampleAvailable=false.

    python scripts/dotnet/test-c188r17-stackwalk-progress.py --repo-root D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT --candidate-root E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z --capture-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165\guest-cpu-state-timeout.json --stack-json E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165\guest-stack-state-timeout.json --output-directory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r17-hosted\progress-tests-final-v2

Result: PASS, 8 assertions. The tests cover shrinking sampled lookup bounds, identical-sample inconclusiveness, mismatched targets, schema handoff, missing stacks, absent QAC ownership, too-short intervals, and the full R13 acceptance fields. The shrinking pair and completed-GC input are synthetic fixtures only.

The offline packaging rehearsal used the exact runner AST and preserved R16 candidate/run artifacts:

    pwsh -NoProfile -ExecutionPolicy Bypass -File scripts/dotnet/test-c188r17-runner-packaging.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -CandidateRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\candidate-r16-stack-capture-20261111T203000000Z -RunRoot E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261111T204626270Z-ad2338ff05a84e01a437d34d66cd7165 -OutputDirectory E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\r17-hosted\packaging-rehearsal-final-v6

Result: PASS, 29 assertions over success, incomplete, fatal, timeout, and QAC-blocked manifest cases. QMP and ownership calls were mocked in hosted tests; qemuLaunched=false and qacReserved=false. The cases cover helper scope, optional absent files, required candidate/serial/QMP hashes, post-QEMU serial hashing order, repeated CPU/stack records, R15/R16-to-R17 schema handoff, deferred result fields, and the no-analyzer summary path.

Other hosted results retained under the R17 hosted artifact root:

| Suite | Result |
|---|---|
| R16 native-unwind regressions | PASS, 30 assertions |
| R13 managed GC root probe contract and runner suite | PASS, 75 assertions |
| R12 managed settings-object regression | PASS, 23 assertions |
| R11 managed entry regression | PASS, 23 assertions |
| ThreadStore/PAL readiness and telemetry fixture suite | PASS, including seven mode-model and six telemetry cases |
| Shared QAC admission hosted suite | PASS, 32 cases |
| Safe-handoff analyzer | PASS, 22 analyzer cases plus 10 source-contract checks |
| Native allocation/PAL slot table | PASS, 2,425 slot-table assertions; backend fixture reports 2,048 attempts and 2,039 dropped detail records |

The R17 Python files compiled with python -m py_compile; the updated runner and packaging script parsed with the PowerShell AST parser; git diff --check passed. A historical transition-runner replay fixture had a stale staged analyzer hash and was not counted as a pass. It did not launch QAC or QEMU. The fresh R17 candidate passed its own inventory preflight and the R17 packaging rehearsal.

## Strict QAC decision and native evidence

The single R17 runner invocation used the fresh candidate and inventory, requested at most one guest boot, set automatic retry false, and allowed one strict QAC admission attempt. Its run root is:

    E:\CodexArtifacts\guideXOSServerV1.1_DOTNET_SUPPORT\c188r13-managed-gc-root\qac-guest-managed-gc-root-C188R13-20261112T040249235Z-eaa2309b8d8349c68e7f3c5bbfe913d2

The runner manifest records NATIVEAOT_GC_HOSTED_READY_GUEST_DEFERRED and error BLOCKED_QAC_PREFLIGHT: coordinator/census is not online, empty, and internally consistent. qacPreflightPassed=false, qacAdmissionGranted=false, qacReservationId is empty, qacReleaseVerified=false, automaticRetry=false, transitionOneBootGuardCreated=false, transitionGuestBootCount=0, and the candidate-specific guard file does not exist. No guest serial or new CPU/stack pair was produced. The R17 progress observation remains not-attempted. Because no QAC reservation was granted, there was no reservation to release; the false release field is not presented as failed cleanup.

The attempted runner wrote the deferred manifest, then returned exit code 1 when its final R13 console summary dereferenced collection fields on a missing analyzer object. That final summary is now null-safe. The exact blocked-manifest and blocked-summary paths pass in the 29-assertion packaging rehearsal, and the R13 hosted suite passes after the fix. QAC was not retried after this source-only correction. The current runner source SHA256 is 50E8B6C5385E005B557C4212C8DDDD81DDB6E5D6D5FD6DCF71DB59F4AA842C65; the run manifest retains the hash of the version used for that sole admission attempt.

The actual attempt used runner SHA256 756CF4681C346EFFF5E70F09603BEA18BDF50F74AF4B5DFB4F6A6EEB647ED804. That is the runner version recorded by the preserved manifest; the committed runner includes the subsequent safe-summary fix and regression case. No QEMU launch occurred during the R17 attempt.

## Changes and next step

R17 adds a narrowly scoped hosted PE/.pdata verifier, its malformed-input and source-equivalence tests, a bounded time-separated progress analyzer/test harness, and runner support for at most two CPU/stack samples under one verified QAC-owned guest process. Sampling resumes the owned QEMU guest only after checking QAC child identity, confirms QMP running state, waits a bounded 1–30 seconds, rechecks ownership, and captures a second read-only snapshot. The analyzer refuses progress claims without ownership metadata, time separation, matching target, and corresponding CPU/stack evidence. Identical samples stay inconclusive.

The R16 analyzer accepts the explicit R17 stack schema and checks CPU/stack sample-number agreement. The runner's final R13 summary now handles missing analysis objects without preventing deferred manifest closeout. No NativeAOT GC, code-manager, unwind, ThreadStore, transition-frame, or managed-root implementation was changed.

Recommended next step: when QAC can provide a fresh, internally consistent empty census, perform one newly authorized strict admission using this already hosted-qualified candidate and capture the two samples only within that one reservation. Do not retry automatically. If QAC remains unavailable, keep the lookup conclusion at offline qualification and do not claim native progress or GC acceptance.

The three modified object files, historical R11–R16 candidates, original captures, and consumed guards were preserved. The new R17 candidate's guard remains unconsumed. No push was made.

# C188R4 Notes startup operation isolation

**Result: Outcome D — Blocked.** C188R4 built and qualified a proof-only NativeAOT candidate with operation checkpoints, but the one targeted QAC guest did not launch. A different owner held the acceptance reservation when the runner reached its admission check. No operation-level guest sequence exists for R4, so this phase identifies no first incomplete R4 operation and establishes no startup root cause. No product correction was made.

## Scope and starting state

The task was to isolate the operation between `C188R3-103 NOTES-STARTUP-STATE-RESET` and `C188R3-110 NOTES-CONTROL-INITIALIZATION-BEGIN` in the real selector-4 `ManagedNotes.Launch` path. C188R3 reached `C188R3-103`, remained running through its 150-second diagnostic timeout, and did not reach C188R3-110, surface creation, render, launch return, or the main input loop.

The repository remained on `v1.1_DOTNET_SUPPORT`, starting at `f911de90783967418a59fa3ee8ef36a60e7fbf46`. The pre-build inventory contained 136 status paths, 13 protected roots, and 8,497 protected files. No repository `.phase` file or stale prompt file was present. The R4 candidate and build evidence are isolated under `out/dotnet/c188r4-notes-startup-operation/`.

## Audited operation order

The source interval is in [`ManagedNotes.cs`](../../samples/managed/HostLogProof/Applications/ManagedNotes.cs), beginning immediately after line 854 and ending immediately before line 1882. The table groups contiguous context assignments where they share the same bounded comparison behavior; each group retains the exact operation order and marker range.

| Order | Source lines and operation | Helpers and possible failure behavior | Diagnostic checkpoints |
|---|---|---|---|
| 1 | 858–938: classify C133, C134, C135, and C136 proof contexts; merge C151 into C135 where compiled. | `LaunchContext.Utf8.SequenceEqual` over fixed byte literals; assignments only. No early return, allocation, callback, pointer dereference, or loop. | 201–218, before/after each assignment or merge. |
| 2 | 941–980: classify C119; reset `_openButton`, `_saveButton`, and `_saveAsButton` in that order; optionally disable Save for `c119-disabled`. | `IsC119Context` is a fixed chain of context comparisons. `GuideXosButton.Reset` restores enabled/focus/visibility/rejected-input state and does not call a host or UI callback. The target `c147-native-notes` context skips the disabled branch. | 219–229; branch identity is marker 227. |
| 3 | 984–1157: classify C120–C128 contexts and their API/host-test contexts; merge C126, C127, and C128 into C125 in order. | `IsC120Context` through `IsC128NotesContext` are fixed comparisons and nested context predicates. Short-circuit boolean merges have no indirect behavior. | 230–277. |
| 4 | 1160–1217: classify C129 and its Shift+Tab test context; set `_c129ProofStage = 0`; classify C131/C132 proof and test contexts. | Static predicate helpers compare the launch span. The stage reset is a scalar assignment. No loop or callback. | 278–295. |
| 5 | 1218–1519: reclassify C133–C139 contexts and merge C134/C135/C136/C137/C138/C139/C151 state; propagate related flags to C120–C124 and C131. | Bounded context predicates and boolean assignments. For `c147-native-notes`, `_c147RuntimeConsumerContext` is merged into `_c120ProofContext` at lines 1474–1481; that sets the later C120 control-host path but does not enter it before C188R3-110. | 296–361. |
| 6 | 1520–1615: call C133, C134, and C135 host-test helpers only when their explicit test-context and not-yet-run flags are true; record results and emit test summaries. | The helpers can construct test fixtures and exercise host/control behavior. The `c147-native-notes` context leaves these test branches false. If entered, branch identity and before/after call markers bracket each call. | 370–381. |
| 7 | 1616–1694: conditionally reset the path label and separator; reset/register/select radio controls and, for C132, install change handlers and set the initial selection. | Control reset/registration/layout calls occur only under their proof/test contexts, which are false for the target C147 path. Radio reset can consult its group; that branch is skipped. | 382–390; branch markers identify taken/skipped setup. |
| 8 | 1695–1819: optionally reset and populate the C133 combo and path panel; reset and populate the C135 popup menu, install its command handler, and clear pending-command counters. | These are test/proof-only setup branches; the target path skips them. The nested C133 panel `TryAddChild` failure returns `InvalidArgument`. Menu/item APIs return status values that this setup does not use. No such call was reached in R4 because no guest launched. | 391–396. |
| 9 | 1820–1879: conditionally reset document usage; reset/layout the C126 group box and C127 panel. | The C126/C127 predicates are false for `c147-native-notes`. If reached, `ApplyC126RadioLayout` can return false on rejected geometry; `ApplyC127PanelLayout` can return false when child placement fails. `GuideXosPanel.Reset` iterates the panel's existing child list, but the C127 branch is not selected by the target context. | 397–405. |
| 10 | 1882: emit `C188R3-110`, immediately before the C120 control-host branch. | This is the next boundary marker, not a guest observation in R4. | C188R3-110. |

The C147 setup is performed before the interval: the context string `c147-native-notes` sets `_c147RuntimeConsumerContext` at line 784. The historical serial confirms the C147 and C148 context PASS messages, then C188R3-103. In the audited interval, the unconditional button resets are the only control resets taken on that context. C120 becomes true through the C147 merge, and C188R3-110 precedes the C120 control-host path.

The relevant branch/helper flow is:

```text
ManagedNotes.Launch
  -> IsC119Context / IsC120Context ... IsC132NotesContext
       -> LaunchContext.Utf8.SequenceEqual(fixed UTF-8 literal)
  -> merge _c147RuntimeConsumerContext into _c120ProofContext
  -> context-gated C133/C134/C135 tests and proof-control setup
  -> C188R3-110
  -> C120 control-host initialization
```

The original source-only audit expected the proof/test branches before C188R3-110 to be skipped on `c147-native-notes`. C188R4R1 confirms that the explicit C133/C134/C135 host-test branches are skipped, but it exposed a separate `_c133ProofContext`-gated test branch at lines 1592–1603. That branch was not represented in the table above; see the C188R4R1 execution result below.

### Helper and safety inspection

`GuideXosLaunchContext.Utf8` returns a span over a managed-owned copy. Dispatch validates the launch-context length against `GxAbi.MaxLaunchContextBytes` (96 bytes). The interval uses span equality against fixed UTF-8 literals; it does not index the underlying array, read a native pointer, allocate a diagnostic string, or invoke a host service. The ABI call table is 128 bytes, with callback/service offsets 104, 112, and 120; those values are unrelated to the 96-byte launch-context maximum.

The original audit omitted the separate C133 proof-test branch selected in C188R4R1. The explicit C133/C134/C135 host-test-context branches and C127 layout branch were skipped, but the C133 proof branch selected `GuideXosComboBoxC134HostTests.Run`. That helper contains 30 cases and emits one summary only after the suite, so this serial capture cannot identify an inner test case. The QAC result below also preserves the distinction between a visible synchronous before-marker and confirmed return from the logger callback.

Each diagnostic marker calls the existing `GuideXosHost.TryLog`. That helper validates the Log capability and a maximum 127-byte payload, uses `stackalloc` for the null-terminated buffer, and synchronously invokes the native `_host->log` function pointer. The native `managedLog` callback writes the complete line via `serial::puts`/`serial::putc` and then returns; on x86, `serial::putc` has an unbounded wait for the COM1 transmit-ready bit. A visible before-marker therefore records that logging reached the serial output, but is not itself an operation-completion marker and cannot prove the callback returned.

## Checkpoint ordering and instrumentation

[`c188-checkpoint-sequence.py`](../../scripts/dotnet/c188-checkpoint-sequence.py) now owns the ordered C188R3/C188R4 checkpoint definition used by both descriptions and serial analysis. The order follows the actual control flow: C150, launch begin, Notes entry/context validation, optional C147/C148, C188R3-102, C188R3-103, then R4 markers and C188R3-110. Optional branch pairs use their recorded branch outcome rather than being treated as universally required.

The validator retains serial order and reports last observed, last expected completed, first expected missing, duplicates, out-of-order markers, branch outcomes, and whether capture appears incomplete. On the unchanged C188R3 raw serial it reports last observed/completed `C188R3-103` and first expected missing `C188R3-110` (control initialization). This corrects the old C148/C150 ordering error. The focused test script [`test-c188-checkpoint-sequence.py`](../../scripts/dotnet/test-c188-checkpoint-sequence.py) passed 9 cases, including ordered optional omission, missing/duplicate/out-of-order markers, truncated capture, branch taken/skipped, historical R3 boundary, and a synthetic complete R4 skipped-branch sequence.

R4 markers 201–405 bracket the audited source operations and are compiled only under `HOSTLOGPROOF_C188R4_NOTES_STARTUP`. Branch checkpoints distinguish C119 disable, explicit C133/C134/C135 host-test calls, the separate C133 proof-test call, and the conditional control/setup/layout paths. No rendering or input loop was instrumented. Each marker is a fixed UTF-8 literal passed to the existing `host.TryLog` logger.

## Build and hosted qualification

All build and hosted evidence is under `out/dotnet/c188r4-notes-startup-operation/`.

| Check | Result |
|---|---|
| Managed diagnostic candidate / NativeAOT composite | PASS. Candidate ELF is ELF64 x86-64, seven load segments, no relocation entries; required lifecycle symbols and R3/R4 marker strings are present. ELF SHA-256: `B36857BF2F54AC81BA151648D688B9808D798184ED70D84B0E6453BD9C111776`. |
| Proof-disabled production NativeAOT check | PASS. ELF SHA-256: `941F8B6AA9C6432A778D820E2BDEA5CFF0883AE793DBB5526C7E9ECD352E7CFB`. No C188R3/R4 markers, CounterApp descriptor, or selector-9 route; production selectors 4–8 remain. |
| Freestanding kernel | PASS. ELF SHA-256: `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`. Freestanding flags include `-fno-exceptions`, `-fno-rtti`, `-nostdlib`, and `-nostdinc++`; standard kernel output was not touched. |
| C188 hosted identity suite | PASS, 25 focused cases; 1,000 identity stress operations; 500 valid resize operations; 250 invalid geometry requests; 250 stale-generation attempts; 100 launch/close lifecycles; zero resize hot-path allocated bytes. |
| Prior hosted platform regressions | PASS, 80 cases: C182 15, C185 28, C186 15, C187 22. |
| Checkpoint validator | PASS, 9 focused cases. |

The diagnostic NativeAOT build enables candidate selector 9 for the CounterApp proof. The proof-disabled production qualification has selectors 4–8 only. ABI v4/128-byte call table, callback offsets 104/112/120, and five managed residency slots were not changed. No product behavior fix or unrelated project rebuild was made.

## Targeted QAC guest result

The isolated candidate media was staged at `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/esp/`. Staging passed: ramdisk SHA-256 `AE9CC800D6A5B0FE6839B75DE01874CBB735BD2D5FAE9731C9107CF52E206AC2`; staged managed ELF SHA-256 `B36857BF2F54AC81BA151648D688B9808D798184ED70D84B0E6453BD9C111776`; kernel SHA-256 `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA`; bootloader SHA-256 `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B`. Staging preserved 185 unrelated files and changed zero other ramdisk files.

The fresh admission census found the QAC coordinator online with one owner-verified external acceptance reservation: `60aae845d5c1463cb9454239d72a4a81`, owner `LEON2_RDT\guideX` (verified owner PID 15840), QEMU child PID 17160, queue 0. The runner stopped at its QAC ownership guard. It did not queue, launch QEMU, or alter the active reservation. The saved `qac-after-block.status.txt` and `qac-after-block.list.txt` show the same external lease still running. The result is **blocked before guest execution**.

Therefore R4 has no serial log, QMP log, screenshot, QEMU command line, input-loop result, guest shutdown, or R4 last-completed/first-incomplete operation. Do not interpret this as a failed R4 checkpoint or evidence of an operation hang. For historical comparison, C188R3's raw `serial.log`, `qmp.log`, and `final-stage.ppm` hashes remain respectively `D199AD79155CE58C8D2C4D56AB5238B6D0816661B28E879C6171660CAA8BAA98`, `473D183BF5BDDBB2DA84266A04C6B04DA70206E674B30F6D0AB94BCF90AE4DBE`, and `8AED5BA950B864C62147A2D42FF6BF1284E372C61D108C0DAF02703FD4AB9C0D`. These raw files were not changed.

### Raw and qualification evidence

| Evidence path (relative to repository root) | SHA-256 |
|---|---|
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/diagnostic-manifest.json` | `EF6674F641935416D02187BA6BA83526632B3FA9AD58E1743BC86D1F2EF86BBD` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/ramdisk-staging.json` | `697E161EC619787E75C743B37E8E0C0D87E3EC651E6E051EB515AB0BCD9DEA8A` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/expected-observations.json` | `4394C1BDE26BC89273A84AA317675EA1B203A583C2494BF727B538DB528652F8` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/qac-before.status.txt` | `4FA016C0CF765E024BD07AEBD533C6616826DA757206A3614B9802F71CA14FE1` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/qac-before.list.txt` | `8D73CBEE3CF3064532C8C876792B4B62469F0D0852FC1EC4BDC26361AAFBFD87` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/qac-after-block.status.txt` | `EFB88EBEFF5994FE342C23DEE7B731246258630D5957FDF0F436618599B11C29` |
| `out/dotnet/c188r4-notes-startup-operation/runs/diag-001/qac-after-block.list.txt` | `8D73CBEE3CF3064532C8C876792B4B62469F0D0852FC1EC4BDC26361AAFBFD87` |
| `out/dotnet/c188r4-notes-startup-operation/build/candidate-001/c188-nativeaot-build.json` | `A2FD5EB6207359736777F6CE41A9EE21EA4A8904FD020EFEBFF09D12882B779A` |
| `out/dotnet/c188r4-notes-startup-operation/build/candidate-001/nativeaot/qualification/nativeaot-qualification.json` | `6ED3A3E0BE63728C681E8CCF5F801F8CAA2E34EF2CD4F6B07E3AB4B454BEEBB0` |
| `out/dotnet/c188r4-notes-startup-operation/build/production-check-001/production-nativeaot-qualification.json` | `43D76AC11B4680B43FF66E11A7C2108D104D3539E675D6DFF1A55D4950222166` |
| `out/dotnet/c188r4-notes-startup-operation/build/kernel-attempt-001/kernel-qualification.json` | `A9F8806B1088EF6B74131D9ADBCF349749E3785F25812568DEB5C56CA792F3BA` |
| `out/dotnet/c188r4-notes-startup-operation/host-tests/c188-identity-001/c188-host-test-manifest.json` | `D0B9AABE998A9C9AB3F12C29CA950CFE67188F4B3D668D015A3C27519EA8C049` |
| `out/dotnet/c188r4-notes-startup-operation/host-tests/prior-platform-001/prior-host-regressions.json` | `754DE9E466B366605E6F48055612851BD044B156A0AA47734AC1C4F3CE522CB2` |
| `out/dotnet/c188r4-notes-startup-operation/preflight/protected-comparison.json` | `AB73E32B977F580334878D8159D34A6525029B158369555DD2FD4A7BA68D0DDB` |
| `out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/serial.log` | `D199AD79155CE58C8D2C4D56AB5238B6D0816661B28E879C6171660CAA8BAA98` |
| `out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/qmp.log` | `473D183BF5BDDBB2DA84266A04C6B04DA70206E674B30F6D0AB94BCF90AE4DBE` |
| `out/dotnet/c188r3-notes-startup-diagnostics/runs/diag-001/final-stage.ppm` | `8AED5BA950B864C62147A2D42FF6BF1284E372C61D108C0DAF02703FD4AB9C0D` |

## Preservation and remaining work

The protected comparison passed: 13 roots and 8,497 files before and after; zero protected additions, removals, or modifications. All 136 pre-existing status paths remain with their original flags. `ESP/ramdisk.img` remains SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. The C188R4 output is isolated from the standard build directories. The historical C185 caveat remains: 212 generated files do not have pre-C186 byte identities, so their historical bytes cannot be retroactively verified.

At C188R4 closeout the guest had not run. The C188R4R1 continuation below records the subsequent bounded diagnostic boot and its precise pre-110 boundary. C188's six acceptance scenarios remain pending and were not run. Do not begin C189.

## C188R4R1 guest execution — 2026-10-24

**Outcome A — diagnostic operation boundary identified.** One QAC-authorized guest executed the retained C188R4 candidate. It reached selector 4 Notes startup, completed the three button resets, and entered a C133 proof-test branch before C188R3-110. The last observed marker was `C188R4-380 BEFORE C133-PROOF-TEST-CALL`; the matching after-marker `C188R4-381` was absent from a complete serial capture. This identifies an unexpected test-path selection and narrows the stall to the synchronous before-log return or the immediately following C134 host-test call. The trace does not prove that the helper body began, or identify a particular one of its 30 cases. C188 remains open and is not accepted.

### Admission and retained candidate

The branch and starting HEAD were `v1.1_DOTNET_SUPPORT` and `67d4e656ad81c6387626844e6dca14d9a0e57307`; that HEAD is the C188R4 implementation commit. The fresh QAC census showed the coordinator online, zero reservations, zero queued reservations, and zero QEMU processes. The runner then obtained reservation `659a1bdb05be45428bd1e41262a586cc` in acceptance mode in 0.8 seconds. QAC verified owner `LEON2_RDT\guideX` (PID 18836) and child QEMU PID 1956. The run used the existing `qac run --project guideXOSServer --mode acceptance` workflow. Its QEMU argument vector was:

```text
"C:\Program Files\qemu\qemu-system-x86_64.exe" -accel tcg -machine pc -smp 1 -drive if=pflash,format=raw,readonly=on,file=D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\OVMF.fd -drive file=fat:rw:D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c188r4-notes-startup-operation\continuation-C188R4R1-20261024\runs\diag-002\esp,format=raw,if=ide,index=0 -m 1024M -vga std -display none -serial file:D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c188r4-notes-startup-operation\continuation-C188R4R1-20261024\runs\diag-002\serial.log -qmp tcp:127.0.0.1:64189,server,nowait -boot order=c -no-shutdown -rtc base=utc,clock=host
```

The complete argument vector and before/during/after QAC status and lists are retained in `continuation-C188R4R1-20261024/runs/diag-002/`.

The candidate was reused without rebuilding or source changes. All six manifest comparisons passed:

| Artifact | SHA-256 |
|---|---|
| Diagnostic NativeAOT composite, selector 9 CounterApp proof | `B36857BF2F54AC81BA151648D688B9808D798184ED70D84B0E6453BD9C111776` |
| Proof-disabled production NativeAOT comparison | `941F8B6AA9C6432A778D820E2BDEA5CFF0883AE793DBB5526C7E9ECD352E7CFB` |
| Freestanding kernel | `9C12F856569E4FE40E1D70CEB56252D3D8E0138DCD4016F83E6F8627DAC261FA` |
| Staged RAM disk | `AE9CC800D6A5B0FE6839B75DE01874CBB735BD2D5FAE9731C9107CF52E206AC2` |
| UEFI bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |

The copied run media matched the retained stage byte-for-byte. The NativeAOT proof composite's selector 9 is distinct from the Notes launch observed at selector 4. The guest emitted `C147-NOTES-CONTEXT startup=loaded list=enabled result=PASS` and the C148 settings-context PASS marker, confirming the intended `c147-native-notes` path.

### Observed checkpoint boundary

The branch-aware validator processed a complete capture: 170 checkpoints recognized, zero duplicates, zero out-of-order markers. Optional branch omissions were accepted according to their recorded outcomes. The complete ordered sequence is in `runs/diag-002/observed-operation-sequence.txt`; the validator JSON is `runs/diag-002/observed-checkpoints.json`, and the raw serial file is preserved unchanged.

The sequence began with `C150-APP-INSTANCE id=4 generation=1`, application launch, Notes entry/context validation, optional C147 and C148 PASS markers, C188R3-102, and C188R3-103. All R4 context assignments and merges through C188R4-361 were observed. The three reset pairs all completed:

| Operation | Before | After |
|---|---|---|
| Open button reset | C188R4-221 | C188R4-222 |
| Save button reset | C188R4-223 | C188R4-224 |
| Save As button reset | C188R4-225 | C188R4-226 |

The explicit C133, C134, and C135 host-test branches were recorded as skipped at markers 370, 373, and 376. The separate C133 proof-test branch was recorded as taken at marker 379, followed by marker 380 immediately before the source call. No C188R3-110 marker followed.

**Last observed checkpoint:** `C188R4-380 BEFORE C133-PROOF-TEST-CALL`.

**First expected missing checkpoint:** `C188R4-381 AFTER C133-PROOF-TEST-CALL`.

**Last completed source operation confirmed:** the branch predicate at `ManagedNotes.cs:1592` evaluated true and the C133 proof-test branch was selected; the marker at line 1595 was emitted. The full line for the synchronous before-log at line 1596 was also emitted.

**First source operation not confirmed complete:** the synchronous `host.TryLog` at line 1596 may not have returned; if it returned, the next operation is `GuideXosComboBoxC134HostTests.Run(host)` at line 1599. The absent after-marker at line 1601 means that helper did not produce a confirmed return. Because the logger runs synchronously and the native serial writer busy-waits for COM1 transmit-ready, the serial line alone cannot distinguish a logger-return stall from a stall within the helper. No per-case progress marker exists, so no inner test case is identified.

### Source-level finding and next correction

The selector-4 context itself is confirmed by serial. The unexpected branch follows a separate context merge that the original audit did not account for: the diagnostic build includes C152 and C151 code; `_c152Enabled` is set true at `ManagedNotes.cs:771`, `_c151ProofContext` includes that value at lines 776–782, and `_c133ProofContext` is ORed with `_c151ProofContext` at line 1263. Later, the broad guard at line 1592 uses `_c133ProofContext` to call the C134 test helper at line 1599. This makes the C147 runtime-consumer launch enter a proof-test branch despite the explicit C133/C134/C135 host-test-context branches being skipped.

The smallest safe correction to review is to separate the C133/C151 control context from the test-invocation predicate, so `c147-native-notes` does not invoke the C134 host-test suite while explicit C133/C134 test contexts still do. Keep the C152 initialization and the C147-to-C120 behavior intact. A focused regression should assert that selector 4 with `c147-native-notes` skips the C134 proof-test branch and reaches the normal startup boundary, while explicit C133/C134 test contexts still execute their expected cases. The correction touches shared `ManagedNotes.Launch` and may affect production Notes startup; qualify selectors 4–8 and the production profile before accepting it. No product source was changed in C188R4R1.

### QMP, framebuffer, readiness, and cleanup

At the 150-second diagnostic deadline, QMP `query-status` returned `running=true`. The QMP transcript contains the status, screendump, and quit command/reply exchanges, with no asynchronous QMP events. The runner captured a final framebuffer showing the guideXOS desktop and “System started successfully”; the Notes launch had not returned, `[KERNEL] Input manager initialized` was absent, and the main input loop readiness predicate was false. QMP `screendump` succeeded, then the owned QMP `quit` request returned successfully. QEMU exited 0 under controlled shutdown; that exit is not a readiness pass. QAC logged the matching child exit and reservation release, and a fresh final census showed zero QEMU processes, reservations, and managed sessions.

Evidence hashes: raw `serial.log` `0A3446D3330265869B80697AB16D3AB1BBD4E9D56DCE93657A2C809A91697F46`; `qmp.log` `F6E8BCA56B1F099128F5767D922A3EFFD86817383C3BB1AD4D9E6701CD2CED26`; final framebuffer `final-stage.ppm` `B8149639F40E6A94339B59178BB659EA7AA47EBA0FC3EF1915DF74C36B5867B8`. The complete diagnostic manifest records admission, PID ownership, command line, readiness, QMP status, all candidate hashes, validator result, and cleanup.

### Regression and preservation

The retained diagnostic NativeAOT, freestanding kernel, proof-disabled production marker-exclusion, C188 hosted identity suite (25 cases), and prior hosted platform regressions (80 cases) remain qualified by the C188R4 evidence and were reused without rebuilding. The focused checkpoint validator suite was run and passed all 9 cases. The six C188 acceptance scenarios were not run, no second guest was started, and C189 was not started.

The fresh protected-tree inventory contains the same 13 roots and 8,497 files as the R4 baseline. The final comparison records zero protected additions, removals, or hash changes. `ESP/ramdisk.img` remains SHA-256 `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8`. All 136 pre-existing worktree status paths retain their original flags and file hashes. The historical uncertainty concerning 212 older C185 generated files remains. Guest media copies are retained under this continuation evidence directory and are not committed.

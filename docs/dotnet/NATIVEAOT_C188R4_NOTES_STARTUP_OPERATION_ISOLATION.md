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

On `c147-native-notes`, the test and proof-control branches before C188R3-110 are skipped; the C119 button resets still run. The C120 merge selects the next control-host branch only after the C188R3-110 marker.

### Helper and safety inspection

`GuideXosLaunchContext.Utf8` returns a span over a managed-owned copy. Dispatch validates the launch-context length against `GxAbi.MaxLaunchContextBytes` (96 bytes). The interval uses span equality against fixed UTF-8 literals; it does not index the underlying array, read a native pointer, allocate a diagnostic string, or invoke a host service. The ABI call table is 128 bytes, with callback/service offsets 104, 112, and 120; those values are unrelated to the 96-byte launch-context maximum.

No loop, recursive call, raw memory access, allocator call, lock, or UI callback is present in the selected `c147-native-notes` operation path before C188R3-110. The gated C127 `GuideXosPanel.Reset` helper does contain a loop over its existing child count, but its C127 context is false on this path. Static inspection narrows the executed pre-110 work to bounded flag comparisons/merges, three `GuideXosButton.Reset` calls, and diagnostic logging. It does not establish why the previous guest stopped before C188R3-110.

Each diagnostic marker calls the existing `GuideXosHost.TryLog`. That helper validates the Log capability and a maximum 127-byte payload, uses `stackalloc` for the null-terminated buffer, and synchronously invokes the native `_host->log` function pointer. It performs no managed heap allocation and does not call the application UI, but it is a host logger callback and may affect timing; a guest run is needed to assess that observer effect.

## Checkpoint ordering and instrumentation

[`c188-checkpoint-sequence.py`](../../scripts/dotnet/c188-checkpoint-sequence.py) now owns the ordered C188R3/C188R4 checkpoint definition used by both descriptions and serial analysis. The order follows the actual control flow: C150, launch begin, Notes entry/context validation, optional C147/C148, C188R3-102, C188R3-103, then R4 markers and C188R3-110. Optional branch pairs use their recorded branch outcome rather than being treated as universally required.

The validator retains serial order and reports last observed, last expected completed, first expected missing, duplicates, out-of-order markers, branch outcomes, and whether capture appears incomplete. On the unchanged C188R3 raw serial it reports last observed/completed `C188R3-103` and first expected missing `C188R3-110` (control initialization). This corrects the old C148/C150 ordering error. The focused test script [`test-c188-checkpoint-sequence.py`](../../scripts/dotnet/test-c188-checkpoint-sequence.py) passed 9 cases, including ordered optional omission, missing/duplicate/out-of-order markers, truncated capture, branch taken/skipped, historical R3 boundary, and a synthetic complete R4 skipped-branch sequence.

R4 markers 201–405 bracket the source operations above and are compiled only under `HOSTLOGPROOF_C188R4_NOTES_STARTUP`. Branch checkpoints distinguish C119 disable, C133/C134/C135 test calls, and the conditional control/setup/layout paths. No rendering or input loop was instrumented. Each marker is a fixed UTF-8 literal passed to the existing `host.TryLog` logger.

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

The exact managed startup operation is still unknown because no R4 guest ran. After QAC has no active owner and exclusive admission is available, the next step is one bounded targeted boot with this candidate, capture the R4 serial/QMP/framebuffer evidence, and classify the first missing after-marker. C188's six acceptance scenarios remain pending and were not run. Do not begin C189.

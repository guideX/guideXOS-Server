# NativeAOT C188R12R4R5R2 Runtime Initialization Guest

**Outcome B — `RUNTIME_INIT_FAILURE_LOCALIZED`**

The one authorized QAC guest returned `RhInitialize = 1`. The stage trace did not arrive: the footer reports unavailable trace getter values, and the parser found zero stage events. A separate runtime singleton snapshot nevertheless narrows the failure to after RuntimeInstance, ThreadStore, GC heap, and GC handle-manager pointers were published. The exact first failing operation remains unproven.

The guest performed no managed entry, experimental allocation, callback, or collection. No production allocator or resident-object ownership changed.

## Revisions and preserved worktree

| Item | Value |
|---|---|
| Starting branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `c4c24ed0e50e37987ce70cfaf34c134843534af7` |
| Ending implementation HEAD | `6ea714b71778d6430900aa95b1be7ea49c7f64a3` |
| Diagnostic correction commit | `6ea714b7` — `Fix C188 R1 trace and QMP diagnostics` |
| Source `ESP/ramdisk.img` SHA-256 | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |

The ramdisk was not edited. The pre-existing modified ramdisk and untracked sample `bin`/`obj` outputs remained in place. The new candidate and guest evidence remain under ignored `out/dotnet` output paths. No reset, clean, stash, branch switch, history rewrite, or push occurred. The closeout document is committed separately from the implementation correction.

The prior R4R5 guest evidence was reviewed in place: raw serial SHA-256 `2FD460E80F57FD664C25D7D5686204668C069C938C2E5CBE233BECDCE9BF941C`, `RhInitialize return=1`, no managed entry or target allocation, the BOM-rejected QMP record, the successful BOM-free manual shutdown, and the serial-file rewrite error. That earlier guest used reservation `3cd7ce6691f448cd9ed08a8bde038b1b`; its raw serial was retained and it was not retried.

## Candidate verification

The originally staged candidate was reused for the single guest. The reported hash `CAE5B5F308784ED8DB439754B7A69D139E180AA01F18AB3A8A3D3460CBC92B6A` identifies **`candidate-manifest.json`**.

| Artifact role | Original guest candidate | Corrected, unbooted candidate |
|---|---|---|
| Candidate root | `candidate-20261031T055308282Z` | `candidate-20261031T150933816Z` |
| Candidate manifest SHA-256 | `CAE5B5F308784ED8DB439754B7A69D139E180AA01F18AB3A8A3D3460CBC92B6A` | `F414A55F9D706B65C41EA86E7B6583F465F7DBBA5DA78B6577FA297DD3B02BB3` |
| Runtime-pack platform object | `71A50BCA2BA654E9E6C5B634F28799DE4DB90379935532E6F893700F8129E763` | `497F540CB0641C932527AEFE2DBC9F16EC8193525D018AE65D5038866ACD19CD` |
| Isolated NativeAOT ELF | `4D0A3993C5EC8291275514AE8FBF86A75FD4B88B7461CD61AE014C34E39401AA` | `E0AFF663386925F18FE580F34A975CC36070300F21191130CECB96035EC2B57F` |
| Specialized kernel and staged ESP kernel | `4D64C305A9E8D415849F49442D17655482FA782EA8AEA22E43B82AF398BA64B1` | `D2432436AF14FBDDA4B0C33BFF4857ADC390E1A88E8F2FECD19A02F0863F1208` |
| Embedded NativeAOT object | `58F62383300EBCB5AD5DA9D0C953F619782327B8F8D7AD839952CD20EB9AD005` | `EA50B1CC385B231CC865F973CEE77057955014B8D8496C2C1843F74C2FE0346B` |
| Staged ramdisk | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` | `AABE4E849B4FB7E4C47E0CA0D573C753879DB8612449DA39E848964E7A726FD8` |
| Staged bootloader | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` | `D72A35DA882892BA97356A1C006562ABA2908D9DD909152B96DCBB655D6C2D9B` |
| Proof-disabled production NativeAOT ELF | `FC9420A82064870625A4F01E907A1210A84DFFD0503504DD077F422DCC00A10E` | `95BBE4CAAE89E466170DE4B873A4F67E24994BF9A1DA02116677DEB6E069D303` |
| Proof-disabled production kernel | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` | `A21A951C8DF337CEA08A7FE93580C556F0AC87F33D1B1766070FFF204B1EA168` |

All 20 checked roles and hashes matched for each candidate, including the NativeAOT map, production map/build log/runtime-pack manifest, source ramdisk, staged media, runtime-pack source, lock file, workstation archive, and runtime-version header. Both use the pinned NativeAOT runtime-pack source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. Both diagnostic ELFs are ELF64 AMD64 images using the expected fixed image mapping. Neither candidate claims QEMU launch or QAC reservation in its build manifest.

The corrected candidate was built after the source fix at HEAD `6ea714b7`; its build result is `PASS_BUILD_READY_FOR_INIT_DIAGNOSTIC_QAC_PREFLIGHT`. It remains unbooted. Its production isolation checks pass: diagnostic and production outputs are separate, diagnostic instrumentation is absent from production, the production persistent composite lifecycle remains enabled, and the GC ownership proof profile remains disabled in production. Managed entry remains fail-closed because ThreadStore attachment is unqualified.

## Hosted preflight

| Regression | Result |
|---|---|
| R1 initialization trace/parser and failure gating | PASS before the guest; PASS again after the trace-argument and QMP fixes |
| Hosted R4R5 GC ownership graph | PASS — 3 callbacks, 17 ownership observations, 2 compacting collections |
| C188R12R4 factory predicates | PASS — 9 cases |
| C188R12R4R1 link-boundary predicates | PASS — 9 cases |
| C188R11 runtime lifecycle | PASS — 7 cases |
| QAC ownership predicates | PASS — 15 cases |
| QAC stream capture | PASS — 2 cases on sequential rerun |
| C188R11R2 runner predicates | PASS — 24 cases, including after the QMP change |
| PowerShell AST parsing for changed scripts | PASS |
| `git diff --check` | PASS |

The QAC stream-capture suite once failed when launched concurrently with other hosted tests (`stdout redirection did not write its live line`); the sequential rerun passed. The retained UI baseline of 40 suites / 969 cases was reused because this phase changed no UI code.

## QAC admission and guest

Before admission, QAC status and list both reported an online coordinator, zero QEMU processes (zero foreign), zero reservations, and zero sessions, queued, or blocked runs. The direct Windows QEMU process census was empty.

One guest was admitted on 2026-10-31 at `14:58:02Z`:

- Reservation: `df8693857caa4cf8937dce94ddd787aa`
- QAC owner PID: `26928`
- QEMU child PID: `18820`
- Candidate: `candidate-20261031T055308282Z`
- Run directory: `candidate-20261031T055308282Z/qac-guest-001`
- Phase-wide attempt guard SHA-256: `206F59225E4287EB5317A31D0D402B1A082952E921B06B2190ECF3A08121F302`
- Guest boot limit: one; no retry

The adapter logged successful runtime foundation, artifact staging, legacy PAL installation, PAL v1 installation, and GC platform installation. It then printed:

```text
[nativeaot-gc-first-allocation] RhInitialize return=00000001 state=00000001 preGcState=0000001F diagnosticStage=00000001 startupNativeAllocations=00000000 lastSize=00000000
[C188R12R4R5R1_INIT] trace_end invocation=00000001 count=FFFFFFFF overflow=FFFFFFFF serialDropped=00000000 rhResult=00000001
[C188R12R4R5R1_INIT] managed_main_gate=blocked rhResult=00000001
```

The raw serial is 7,008 bytes, SHA-256 `0F40D24DEB8BB9E527961BC47F8698E111DCD74902A074BE8210ECC1F4DEC53E`. It was not rewritten. The separate parser report is `serial-analysis.json`, SHA-256 `1CCC4859A3834C9E4708717A755A984EE295C2C41EBD142E2D799BF4CAD4660D`; the raw hash was the same before and after analysis.

### Trace limitation and source correction

The parser found zero stage records, one footer, no terminal event, and an incomplete trace. `count=FFFFFFFF` and `overflow=FFFFFFFF` are unavailable-value sentinels; no `C188R12R4R5R1_INIT` stage event was emitted. Therefore there is no observed adapter-stage record, `RhInitialize` begin/pass/fail record, explicit failing stage, module-identity observation, or GC substage status. The footer and legacy serial line both preserve the final `RhInitialize` status as `1`.

Source review found the concrete reporting defect. `runFirstRealAllocation` has two optional parameters between generation and the trace sink: `beginExperimentAddress` and `standaloneNativeUnwindAddress`. The diagnostic call in `kernel/core/main.cpp` supplied the three trace addresses immediately after generation without two zero placeholders. C++ argument binding therefore placed the addresses in the wrong slots and left the trace count and overflow getter addresses at their default zero values. The kernel could not install the sink and printed the unavailable getter sentinels. The generated candidate header and map did contain the trace functions at nonzero addresses.

Commit `6ea714b7` adds the two zero placeholders and regression assertions for their placement. It also corrects the separate runner failure described below. The corrected candidate was rebuilt and its manifest and artifact hashes were checked; it was not booted.

### Singleton boundary and runtime result

The adapter's separate safe singleton accessor returned `preGcState=0000001F`. From the source-defined bit layout:

- Bits 0–4 are set: RuntimeInstance, ThreadStore pointer, GC heap pointer, GC handle-manager pointer, and RhConfig pointer are available.
- Bits 5–7 are clear: the NativeAOT module-initialization guard and code-manager registration are unset, and the current-thread accessor is null.

Source shows that `GCHeapUtilities::InitializeGC()` publishes the heap and handle-manager pointers when it succeeds. This snapshot therefore localizes the returned failure to after that pointer-publication boundary. Pointer publication does not prove that `g_pGCHeap->Initialize()`, finalization initialization, or GC handle-manager initialization completed. No trace event is available to distinguish those operations or identify a first explicit false return. This is a localized interval, not a proven GC defect.

Module registration occurs later in the reverse-P/Invoke path and was not reached because `RhInitialize` failed. Even if initialization had succeeded, this candidate would still block `ManagedMain` because ThreadStore attachment is unqualified. No managed allocation, callback, or guest collection occurred; the hosted graph result is not guest GC acceptance.

## Runner and QAC recovery

The runner's QMP log (SHA-256 `FD2B0197609770975013D93627058FDB24F9DEBEBDB82B77407A9A86652B02DC`) contains greeting and capability responses but no `query-status` or `quit` command. Under `Set-StrictMode -Version Latest`, the parser accessed `$message.error` on a normal QMP response containing `return` but no `error`; this raises `PropertyNotFoundException`. The regression now checks optional JSON fields through `PSObject.Properties` and verifies a response without an `error` property.

The runner then exited with code 1 while `Get-FileHash` tried to read the still-open raw serial file. It did not write `guest-manifest.json` or capture the QAC CLI exit code. The failure and recovery are recorded in `runner-failure.txt` (SHA-256 `12E16C58F6066A70EC4A41B703F2DA28E6C877C0EC422359603F2B65F7A62CA9`). The original QMP log and raw serial were preserved.

After verifying the QAC reservation, owner, child PID, candidate serial path, and QMP port `62734`, a separate BOM-free QMP connection returned `query-status: running=true` and then `quit` successfully. The recovery log is `qmp-recovery.log`, SHA-256 `20680F9A5B86DFF6BFEF410E45900BD490EDD56572D1EB4520E775B5DECB8B4F`. QEMU PID `18820` and the QAC CLI owner exited. Fresh recovery snapshots report QEMU `0 (0 foreign)`, reservations `0`, managed sessions `0`, queued `0`, blocked `0`; the direct process census is empty. Snapshot hashes: status `D9E7D38447891BF9E938D49BACFD807D0CEE1C4F9F59857F34E1EA3DD2910D31`, list `152201354443030DB19021C440110A78DCBB8C70A4C6072E6AE09D457827BBC5`.

## Outcome and next boundary

**Outcome B — `RUNTIME_INIT_FAILURE_LOCALIZED`.** `RhInitialize` returned failure after the runtime and GC singleton pointers were published. The guest did not identify the first internal failure because the kernel passed the trace addresses in the wrong optional argument slots. No GC graph or collection acceptance is claimed.

The smallest next phase is one separately authorized QAC boot using a corrected candidate and a fresh phase-wide one-boot guard. The existing R2 guard permanently records the guest already used here, so this runner must not be invoked again against another candidate in this phase. Keep `ManagedMain` blocked until ThreadStore attachment safety is qualified. If the corrected trace still produces only a footer or an incomplete interval, stop at that evidence without retrying.

## R4R5R3 return-contract addendum

The R4R5R3 audit confirms that the `1` in the preserved lines above is the C188 adapter's zero-success/one-failure status, not an unmodified NativeAOT `bool`. The linked adapter maps native `RhInitialize(true)` to status `0` and native `RhInitialize(false)` to status `1`. Thus this guest's status indicates native initialization failure; it was not a successful native `true` misclassified by the kernel. Because the R2 trace getters were unavailable, its serial does not contain the raw Boolean or an internal failing-stage record. The new R3 diagnostic writes the raw Boolean separately and leaves missing R2 stage evidence missing. The raw serial and its SHA-256 remain unchanged. See the [R4R5R3 return-contract closeout](NATIVEAOT_C188R12R4R5R3_INIT_RETURN_CONTRACT.md).

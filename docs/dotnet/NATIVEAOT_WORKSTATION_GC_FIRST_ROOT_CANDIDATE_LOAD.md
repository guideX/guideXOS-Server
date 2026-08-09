# NativeAOT Workstation GC: first root-candidate load

## Outcome

**Outcome A — bounded first candidate-value load.** The locked NativeAOT 9.0.0
AMD64 Workstation GC path entered the real `EnumGcRef` call chain, read the
first real candidate slot exactly once as one AMD64 machine word, observed a
null raw value, and stopped before candidate interpretation, callbacks,
promotion, marking, restart, or managed resumption. This is not a claim that
root enumeration or collection is functional.

## Checkpoint and identity

The actual starting committed HEAD was
`5ccd6c1d13359a5942e21bca6befe2218a5eeb89`; the initial `git status --short`
was empty. No commit was created. The locked identity remained:

* NativeAOT 9.0.0, AMD64, Workstation GC;
* GC interfaces 5.3 / 2;
* locked NativeAOT source commit
  `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`;
* locked `gcenv.ee.cpp` SHA-256
  `EA603651D418F45F6847B7F3EC57C23BEB0A153ECCF94C2107EB5EB40B6C9A6B`;
* locked `GcEnum.cpp` SHA-256
  `65BA356294C47F55105104D3800224C0311411042FA95AAFA83E3BAE91DEBFE6`;
* active PAL archive SHA-256
  `C617D95647A20862947B52A1301DF96FE9104E5A13F11BB0C016B8370DDE115F`.

## Exact locked-source trace

The declaration is `nativeaot/Runtime/GcEnum.h:13` and the locked definition is
`nativeaot/Runtime/GcEnum.cpp:84-96`. Its parameters are:

| Parameter | Locked contract |
| --- | --- |
| `pRef` | `PTR_OBJECTREF`, an `Object**` address: the candidate slot, not the candidate value |
| `kind` | `GCRefKind`; the real thread-static path uses `GCRK_Object` |
| `fnGcEnumRef` | `ScanFunc*`, `void(Object**, ScanContext*, unsigned)` callback |
| `pSc` | `ScanContext*` passed through to the callback |

`GcEnum.h` and `forward_declarations.h:40-45` establish the pointer and
callback types. `GcEnum.cpp:84-96` initializes flags to zero, adds
`GC_CALL_INTERIOR` only for `GCRK_Byref`, and calls `GcEnumObject`. The locked
`GcEnumObject` body is `GcEnum.cpp:68-82`; it asserts the flags and then either
promotes an interior value or invokes the callback. The locked source does
not load the candidate value in `EnumGcRef` itself.

The real provider is `nativeaot/Runtime/gcenv.ee.cpp:114-115`:
`EnumGcRef(pThread->GetThreadStaticStorage(), GCRK_Object, fn, sc)`. The
provider implementation is `nativeaot/Runtime/thread.cpp:1251-1254`:
`return &m_pThreadLocalStatics;`. There is no thread-identity parameter in
`EnumGcRef`; the proof records the owning runtime `Thread*` from the actual
`FOREACH_THREAD` provider path and records the callback/scan-context pointers
passed by the real call.

## One-load implementation and slot contract

The proof-mode generated source inserts the following statement in the
locked `GcEnumObject` body, immediately before the original semantic tail:

```cpp
const uintptr_t candidateRawValue = reinterpret_cast<uintptr_t>(*ppObj);
```

The exact generated source is
`out/dotnet/gc-first-root-candidate-load/build/runtime-pack/GcEnum.first-root-candidate-load.cpp:77`.
The request hook precedes it and the `__declspec(noreturn)` machine-word hook
follows it. The generated object proves the order in
`out/dotnet/gc-first-root-candidate-load/run-20260808-222526478/machine-code-disassembly.txt`:
request hook, one `mov rdx,qword ptr [rbx]`, then the safe-stop hook. The
generated source SHA-256 is
`B60A9CE3D4E54BAC811AD0FB8D191657E4613C3E7E9EDCE6B3B702E4B8ED453F`; the
object SHA-256 is
`E00326C790A518898A82B946EFDADAEF8E043BDD3C9D6EFA8E16FF2B4412B6C4`.

The real provider and candidate identities were:

* runtime thread / owner: `0x000000000392DC00`;
* native thread ID: `0x00000000100D18C0`;
* metadata container and candidate slot:
  `0x000000000392DC90`;
* slot offset from the owning `Thread`: `0x90`;
* slot width: `0x8` bytes; alignment remainder: `0x0`;
* mapped: `1`; committed: `1`; mutable `Object**` contract: `1`;
* stable: `1`; expected thread-storage return: `1`;
* overlap with managed heap: `0`; overlap with the owning runtime `Thread`
  object: `1` because the field is inside that object; overlap with allocation
  context and native stack: `0`.

The source contract, successful single load, and zero fault count establish
safe readability for this proof. No write probe was performed. The exact
known-address comparison was only against null and the bounded known runtime,
metadata, slot, allocation-context, stack, and recorded-object addresses; it
was not a general heap-membership test.

## Fresh QEMU result

The fresh evidence root is
`out/dotnet/gc-first-root-candidate-load/run-20260808-222526478/`. QEMU was
`11.0.0 (v11.0.0-12122-ga4bb4b10)`. All three disposable boots reached
`C011EC05` and were harness-terminated after the bounded stop:

| Run | Safe stop | Serial SHA-256 |
| --- | --- | --- |
| `first-run` | `C011EC05` | `543DFAA68BE48A6A29237E780CF7C3A9F225875816A0F4336E13A94269898067` |
| `repeat-1` | `C011EC05` | `849C625B4F1487E558F9EA161D4CE960CD5F7FF18ED81894186CC7487E5850A2` |
| `repeat-2` | `C011EC05` | `7739C672CA2EA7D9BF355B805F4E4A8F44C7E37E775E63272780EE6902A32706` |

The proof-kernel SHA-256 was
`4FDF87AFC40A05C422B6B64049E7F86A6A9FE39D7A44A06E1918ABBDB78B0361`.
The exact commands and source/build logs are in `commands.txt` and the other
files in the run root; the machine-readable record is `manifest.json`.

## Workload and boundary state

The workload was unchanged: 40 completed `byte[4096]` allocations, 19 fast
paths, 22 rare paths, 21 refills, two same-segment commits, zero segment
transitions, and four live sentinels. The one collection request and entry
were generation 1, `reason_oos_soh (5)`, blocking, noncompacting. Suspension
entered once, returned once, held the ThreadStore lock at depth 1, exempted
the current thread, prohibited managed entry, and left EE suspension active.
Allocation-context fixup visited one context, changed and cleared it once,
completed once, and recorded zero object-memory mutation.

The real ThreadStore enumeration recorded one registered thread before and
after, one enumerated, one included, zero excluded, zero duplicates, and zero
integrity or registry-mutation failures in the candidate proof. Provider
totals were two requests, one entry, one skip, and one metadata container.

The candidate load totals were exactly one request, one entry, and one
machine-word load; duplicate loads and load faults were both zero. The raw
value at the load address was
`0x0000000000000000`, so the result is **null**. The raw value matched the
known null address classification. It is not described as a valid managed
root.

The prohibited semantic counters were all zero:

* candidate pointee dereferences: `0`;
* heap-membership tests: `0`;
* object-header inspections: `0`;
* method-table inspections: `0`;
* root-flag applications: `0`;
* root candidates discovered: `0`;
* root callbacks delivered: `0`;
* promotion callbacks delivered: `0`;
* marking entries: `0`;
* object-memory mutations: `0`;
* restart requests/entries: `0/0`;
* managed resumes: `0`.

All 40 objects validated before and after the load and at the stop. There were
160 sentinel checks (`0xA0`), four live sentinels, zero duplicate addresses,
and object addresses, contents, and layouts were unchanged. The safe-stop
reason was one explicit pointer-width load from the real `EnumGcRef` slot,
before callback and semantic processing. The marker is `C011EC05`, emitted by
`firstRootCandidateLoadSafeStop()` in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.

## Regression and validation results

| Check | Result and retained evidence |
| --- | --- |
| First candidate load | PASS, 3/3 fresh QEMU runs, run above |
| C011EC03 allocation-context/root-boundary | PASS, fresh committed-runner replay in `out/dotnet/gc-allocation-context-fixup-root-boundary/run-20260808-223951962/` |
| C011EC02 single-thread SuspendEE | PASS, fresh committed-runner replay in `out/dotnet/gc-single-thread-suspend-ee/run-20260808-224154109/` |
| First allocation | PASS, `out/dotnet/gc-first-allocation-closure/run-20260808-222906374/` |
| First refill | PASS, `out/dotnet/gc-first-refill/run-20260808-223223898/` |
| Multiple refills / first segment boundary | PASS, `out/dotnet/gc-multiple-refills-first-segment-boundary/run-20260808-223350355/` |
| First segment transition | PASS through the committed smoke runner; retained evidence in `out/dotnet/gc-first-segment-transition/run-20260804-200804865/` |
| C011EC04 provider replay | NON-CLEAN validator rejection in `out/dotnet/gc-first-per-thread-root-provider/run-20260808-223747371/`; the runtime safe stop was reached, but the validator rejected an IRQ-split integrity record. Not counted as a clean pass. |
| C011EC01 collection-boundary replay | NON-CLEAN validator rejection in `out/dotnet/gc-first-collection-boundary/run-20260808-223022180/`; the first run has exact request/entry `1/1`, while repeat-1 split the record around an IRQ line. Not counted as a clean pass. |
| Static checks | PASS: PowerShell parse, manifest parse, serial checks, and `git diff --check`. |

Retained candidate attempts include the compile failures in
`run-20260808-220850440`, `run-20260808-221003169`, and
`run-20260808-221056379`; the successful-but-misparsed attempt
`run-20260808-221137346`; the non-clean boot attempt
`run-20260808-221637602`; and the pre-final QEMU-only replay
`run-20260808-222040159`. No failed evidence was erased.

The historical first-64-KiB execution failure, stale-cache attempts, initial
runtime-pack identity mismatch, native-stack wrapper exit 1 caused by
PowerShell compiler-stderr promotion, early provider compile/watchdog
attempts, and runtime-pack static-nonallocating identity block remain
classified as historical/non-clean or blocked. The broad regression suite,
hosted/native-stack assertions, and the runtime-pack static-nonallocating
check were not counted as passes.

## Ordinary-kernel isolation and restoration

The candidate instrumentation is gated by the existing
`GUIDEXOS_NATIVEAOT_FIRST_ROOT_CANDIDATE_LOAD_ALLOCATION` proof selector and
the candidate source is generated only by the focused runner. The ordinary
kernel was restored after every specialized run. After the final run both
`kernel/build/amd64/bin/kernel.elf` and `ESP/kernel.elf` had SHA-256
`D68791B66BF268425B6E646F3EA94CE7B3777B97D9CCED6682C10A9E1389066C`, matching
the expected ordinary hash and the before/after values in `manifest.json`.
No ordinary-kernel proof marker, candidate load, callback suppression, or
suspended state was deployed as ordinary runtime behavior.

## Files and next milestone

The focused implementation changes are the append-only diagnostics fields in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`,
the candidate request/load/safe-stop implementation in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and
the proof-mode build, generated-source, QEMU, validation, manifest, and
restoration logic in `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`.
The five related reports now cross-reference this report. The focused report,
fresh manifest, serial logs, generated source, and machine-code evidence are
all left in the reviewable working tree.

The next smallest bounded milestone is a separately gated source/ABI probe of
the callback boundary on a workload that genuinely supplies a non-null slot.
It should stop immediately before callback invocation and promotion, and must
not fabricate a root or claim root coverage. This report does not authorize
that probe, callback delivery, marking, restart, or managed resumption.

The next managed-proof attempt is recorded separately in
`NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md`. It
reached Outcome E in the normal NativeAOT `[ThreadStatic]` initialization path
before the managed sentinel assignment and before any candidate value load;
the `C011EC05` evidence above remains the historical candidate-load checkpoint.

The standalone normal `[ThreadStatic]` runtime correction and primitive/reference
proof are documented in
[NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md).
That report preserves this `C011EC05` frontier and does not claim callback,
promotion, marking, restart, or managed-resume support.

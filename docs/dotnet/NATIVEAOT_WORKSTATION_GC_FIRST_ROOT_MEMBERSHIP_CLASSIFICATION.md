# NativeAOT Workstation GC first-root membership classification

## Result

Outcome A. The first genuine NativeAOT GC root reached the real Workstation GC
`WKS::GCHeap::Promote` callback, loaded the genuine `[ThreadStatic]` storage
object, completed exactly one real `gc_heap::is_in_find_object_range` check,
returned `true`, and stopped before `HEAP_FROM_THREAD`, generation
classification, object metadata inspection, promotion, marking, or mutation.

The proof-only stop marker is `C011EC08`.

## Task checkpoint and locked identity

The actual task-start checkpoint was:

- HEAD: `b71b16d591158cfb8499bf0907abc7e2e35ad960`
- branch: `v1.1_DOTNET_SUPPORT`
- worktree: clean
- C011EC07: already committed at task start
- ordinary kernel and ESP kernel SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

The locked runtime identity was NativeAOT `9.0.0`, AMD64, Workstation GC,
GC interfaces `5.3 / 2`, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. Production `InitializeModules`
correction and normal `[ThreadStatic]` support remained enabled.

C011EC07 was the prerequisite: it established the real callback ABI and
stopped after the callback-side `*ppObject` load. This pass preserved that
root and advanced only through the real membership helper.

## Locked source contract

The declaration is
`src/coreclr/gc/gcpriv.h:3049`:

~~~cpp
PER_HEAP_ISOLATED_METHOD bool is_in_find_object_range (uint8_t* o);
~~~

The locked definition is
`src/coreclr/gc/gc.cpp:8363-8387`:

~~~cpp
inline
bool gc_heap::is_in_find_object_range (uint8_t* o)
{
    if (o == nullptr)
    {
        return false;
    }
#if defined(USE_REGIONS) && defined(FEATURE_CONSERVATIVE_GC)
    return ((o >= g_gc_lowest_address) && (o < bookkeeping_covered_committed));
#else
    ...
#endif
}
~~~

This build has `USE_REGIONS` from `gcpriv.h:147-149` and
`FEATURE_CONSERVATIVE_GC`, so the active path is the line-8373 expression.
It consumes only `o`, after the null guard. The lower boundary is the global
`g_gc_lowest_address`; the upper boundary is the active heap's
`bookkeeping_covered_committed` field, declared at `gcpriv.h:4319`.

The helper therefore performs a null test and numeric half-open range
comparison:

`g_gc_lowest_address <= o && o < bookkeeping_covered_committed`

with C++ short-circuit behavior. It does not call a segment lookup, inspect a
generation, read an object header, read a method table, read a field or child
reference, or mutate state. The check exposes no explicit heap or segment
identity. The upper boundary is a per-heap field, so the proof records one
consulted heap-range field when the lower comparison succeeds, but it does not
invent a heap pointer or segment identity.

The callback source is `src/coreclr/gc/gc.cpp:49474-49544`, included by
`src/coreclr/gc/gcwks.cpp`. The first source operation is the root load at
`gc.cpp:49481`:

~~~cpp
uint8_t* o = (uint8_t*)*ppObject;
~~~

The membership check is at `gc.cpp:49483`. If false, the locked source branch
would return at `gc.cpp:49485`; this proof stops before that return and does
not skip the root. If true, the first post-check source operation is
`HEAP_FROM_THREAD` at `gc.cpp:49494`, after the optional
`DEBUG_DestroyedHandleValue` block at `gc.cpp:49488-49492`. The next semantic
operation after that is `gc_heap::heap_of(o)` at `gc.cpp:49496`, followed by
condemned-generation/range logic at `gc.cpp:49498-49505`. The first
conservative object metadata read is `CObjectHeader::IsFree` at
`gc.cpp:49520-49524`; debug validation is at `gc.cpp:49527-49531`; promotion
and marking begin at `gc.cpp:49533` and `gc.cpp:49541`.

The callback type is `promote_func(Object**, ScanContext*, uint32_t)` at
`src/coreclr/gc/gcinterface.h:24-25`. The real NativeAOT provider is
`src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115`; the real
`EnumGcRef`/callback call site is
`src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96`, with the machine call at
`GcEnum.cpp:81`.

## Machine-code trace

The final proof artifact is
`out/dotnet/gc-first-root-membership-classification/run-20260809-220619595/artifact-disassembly.txt`.
The final callback is the map symbol
`?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z` at
`0x100245C0`.

The relevant final AMD64 sequence is:

- `0x100245EA`: load `*ppObject` into `RBX`; this is the genuine object input.
- `0x10024613`: load the lower-bound global.
- `0x1002461D`: compare `RBX` (`o`) with the lower bound.
- `0x10024631`: `setae`, producing the lower comparison result.
- `0x10024632`: branch to the false result path when `o < low`.
- `0x1002462D`: load `bookkeeping_covered_committed`.
- `0x10024635`: compare `o` with the upper bound.
- `0x10024636`: `setb`, producing the upper comparison result.
- `0x1002467C`: test the completed membership result.
- `0x10024684`: false branch to the C011EC08 boundary.
- `0x1002468B`: true branch to the C011EC08 boundary.

There is no out-of-line membership-helper symbol in the final map; the helper
is inline in `Promote`. The completion observer returns to
`0x1002467C`, and the post-membership boundary observer returns to
`0x10024690`. The source-level next operation remains
`HEAP_FROM_THREAD@gc.cpp:49494`; it is not reached.

## Real root and range result

The managed proof field remained `[ThreadStatic] byte[]? s_gcProofThreadRoot`.
The selected sentinel was `0x100A01F38`. The genuine NativeAOT thread-static
storage object was `0x100A02F50`, and its real inline root slot was
`0x3943BE0`.

At callback entry, Microsoft x64 arguments were:

- `RCX = 0x3943BE0` (`Object**`, exact root slot)
- `RDX = 0x4E7F440` (live `ScanContext*`)
- `R8D = 0` (raw flags)

The callback-side load and membership input were both
`0x100A02F50`; the equality check was true. The recorded bounds were:

- lower: `0x100000000`
- upper: `0x102600000`
- lower comparison `o >= low`: true
- upper comparison `o < high`: true
- final `inFindObjectRange`: true
- source branch: true branch
- consulted per-heap boundary fields: 1
- heap identity: not exposed by this helper
- segment identity and source-required segment lookup: none / 0

The storage object is inside the active half-open range. No sentinel child
reference was traversed or inspected.

## C011EC08 boundary and counters

The proof recorded exactly one callback and exactly one membership check:

| Counter | Result |
|---|---:|
| callback requests / call sites / invocations / entries | 1 / 1 / 1 / 1 |
| callback returns / second callback attempts | 0 / 0 |
| membership requests / entries / completions / returns | 1 / 1 / 1 / 1 |
| duplicate membership checks | 0 |
| object dereferences in the helper | 0 |
| object-header / method-table reads | 0 / 0 |
| generation classification / condemned-generation comparison | 0 / 0 |
| post-membership segment lookup | 0 |
| promotion start / promotions | 0 / 0 |
| marking start / graph traversal | 0 / 0 |
| promotion / mark writes | 0 / 0 |
| object / GC metadata / segment mutation | 0 / 0 / 0 |
| restart / managed resume | 0 / 0 |

The live `ScanContext` was preserved without mutation: promotion `1`,
concurrent `0`, thread count `1`, thread number `0`, thread-under-crawl `0`,
and stack limit `0`. The thread-store invariants were one registered and
enumerated managed thread, collection initiator equal to the enumerated
thread, lock owner equal to that thread, lock held, EE suspended, managed
entry prohibited, unchanged registry mutation count, and fixed/cleared
allocation context.

Sentinel contents and known user objects were unchanged at assignment,
pre-collection, post-suspension, post-fixup, pre-dispatch, callback entry,
membership completion, and C011EC08. The selected sentinel readback matched,
the storage-object address was unchanged, no duplicate or overlap was found,
and object mutation remained zero.

## QEMU evidence

QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. The exact commands are in
`out/dotnet/gc-first-root-membership-classification/run-20260809-220619595/commands.txt`.
Each run used one AMD64 CPU with TCG single-thread acceleration, the locked
OVMF image, one GiB RAM, and the proof kernel.

| Run | Marker | Object | Lower / upper | Result | Serial SHA-256 |
|---|---|---|---|---|---|
| first-run | C011EC08 | `0x100A02F50` | `0x100000000` / `0x102600000` | true | `8FAF1DA11AA5BA5325142C27F5E8B6EDFE6DF9BDD3646FD79570772ACB8904B6` |
| repeat-1 | C011EC08 | `0x100A02F50` | `0x100000000` / `0x102600000` | true | `AACD261C1642F99FC4E03BB404AAE16840D50D523DF6A039ECCD97D7B28F0BA1` |
| repeat-2 | C011EC08 | `0x100A02F50` | `0x100000000` / `0x102600000` | true | `0C57FF78536E444A61EDFDA3A031143087C4D11F05C36728F212E9A8C5ADA107` |

Proof-kernel SHA-256:
`E21191097071394E3A8CE61FB94C0BAD566D0C7A02CC0D0B18DEC2AAC6BE8380`.
The serial logs are retained below the evidence directory.

## Regression and retained checks

The explicit C011EC07 callback-entry regression was rerun 3/3 and passed. Its
evidence is
`out/dotnet/gc-first-root-callback-entry/run-20260809-215911474/manifest.json`.
The C011EC06, C011EC05, C011EC04, C011EC03, C011EC02, and C011EC01
prerequisites, thread-static primitive/reference/combined proofs,
segment-transition, commitment, refill/allocation, FLS/local storage,
native-thread, runtime-pack, ELF, stack-bound, build, parsing, manifest,
serial, and ordinary-restoration evidence remain retained from their
historical artifacts. They were not relabeled as fresh PASS results here.

The broad regression suite was not rerun in this focused pass. Historical
non-clean checks remain non-clean, including the retained first-64-KiB
execution issue, stale-cache attempts, the initial runtime-pack identity
mismatch, and the native-stack PowerShell-wrapper/compiler-stderr
non-clean result. Completion/promotion is intentionally blocked by the
C011EC08 stop.

## Restoration and next milestone

The proof kernel and ESP deployment were restored after the runs. Final
ordinary hashes were:

- `kernel/build/amd64/bin/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- `ESP/kernel.elf`:
  `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

The smallest next milestone is to permit exactly one source-required
post-membership `HEAP_FROM_THREAD`/`heap_of(o)` transition for this same
root, record the resulting heap identity, and stop before condemned-generation
logic or any object metadata read. No promotion or marking should be enabled
in that pass.

## Continuation verification on 2026-08-11

This report was already present at the start of the continuation. The actual
continuation checkpoint was HEAD
`94753af9bcfe1f9540930b8714e0e638924afafa` on branch
`v1.1_DOTNET_SUPPORT`, with a clean worktree. C011EC07 was already committed
(and the current HEAD also contains the committed C011EC08 Outcome A pass).
Both ordinary kernel and ESP hashes at entry were
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

The focused harness was rerun from that clean checkpoint. The fresh
C011EC08 evidence is retained in
`out/dotnet/gc-first-root-membership-classification/run-20260811-083505150/`.
It again produced three C011EC08 boots with the same storage object
`0x100A02F50`, slot `0x3943BE0`, bounds `0x100000000` and `0x102600000`,
true/true comparisons, and final `true` result. Each run recorded one
request, entry, completion, and result boundary, zero duplicates, zero object
dereferences, zero generation-classification starts, zero condemned-generation
comparisons, zero promotion/marking/mutation, and zero restart/resume.

The fresh membership artifact's inline sequence is recorded in
`artifact-disassembly.txt`: callback `0x100245C0`; root load `0x100245EA`;
lower-bound load/compare `0x10024613`/`0x1002461D` with `setae` at
`0x10024620`; upper-bound load/compare `0x10024626`/`0x1002462F` with `setb`
at `0x10024632`; result test `0x10024635`; false branch
`0x10024684`; true boundary call `0x1002468B`; and the next source operation
remaining `HEAP_FROM_THREAD@gc.cpp:49494`.

The explicit C011EC07 regression was also rerun 3/3 in
`out/dotnet/gc-first-root-callback-entry/run-20260811-083939919/` and passed.
QEMU was `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. Both runs restored the ordinary
kernel and ESP to the expected hash. This continuation did not relabel the
historical broad regression artifacts as fresh passes; their retained
non-clean and blocked statuses remain as described above.

Cross-references:

- [first callback entry](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md)
- [first non-null callback boundary](NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md)
- [thread-static runtime support](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md)
- [first candidate load](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md)
- [GC startup readiness](NATIVEAOT_GC_STARTUP_READINESS.md)
- [Workstation GC feasibility](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md)

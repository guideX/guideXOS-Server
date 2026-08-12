# NativeAOT Workstation GC First-Root Heap Resolution — C011EC09

## Final result

Outcome B. The first genuine NativeAOT `[ThreadStatic]` storage-object root passed the already-proven managed-range membership test and entered exactly one real `HEAP_FROM_THREAD` / `gc_heap::heap_of(o)` transition. In the locked Workstation source, that transition returns the null `gc_heap*` sentinel because Workstation has no per-heap instance and `__this` is defined as null. The proof recorded the source result, performed no heap-0 fallback, and stopped before the next source operation, `gc_heap::is_in_condemned_gc(o)`.

No generation classification, generation query, condemned-generation comparison, ephemeral-generation test, object-header read, method-table read, promotion, marking, graph traversal, GC mutation, restart, or managed resume was executed.

## Starting state and prerequisite

The task started from:

- HEAD: `ad51ac169600ef1ce56649a58b56275abd311cdc`
- branch: `v1.1_DOTNET_SUPPORT`
- starting worktree: clean
- C011EC08: already committed; its report and evidence were present in the starting HEAD
- ordinary kernel SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- ordinary ESP kernel SHA-256: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

C011EC08 supplied the prerequisite true membership result for the same real root. Its locked range facts were preserved: `g_gc_lowest_address = 0x100000000`, `bookkeeping_covered_committed = 0x102600000`, object `0x100A02F50`, lower comparison true, upper comparison true, and membership true.

The implementation changes in this pass remain uncommitted. The proof harness starts after those intentional changes, so its per-run `startingDirtyState` records the implementation worktree; the task-start checkpoint above records the actual repository state before this pass.

## Locked runtime

The runtime identity remained locked to NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`. Production `InitializeModules` correction and real NativeAOT `[ThreadStatic]` support remained active.

## Genuine root identity

The managed proof field was `[ThreadStatic] byte[]? s_gcProofThreadRoot`.

- selected sentinel: `0x100A01F38`
- NativeAOT thread-static storage object: `0x100A02F50`
- inline root slot: `0x3943BE0`
- callback-loaded raw root: `0x100A02F50`
- membership input: `0x100A02F50`
- heap-resolution input: `0x100A02F50`

All four object identities were equal in every successful C011EC09 boot. The sentinel, storage object, user objects, and object contents were unchanged; duplicate object addresses and overlap failures remained zero. The proof did not traverse storage-object child references.

## Exact source contract

The locked `WKS::GCHeap::Promote` definition is `src/coreclr/gc/gc.cpp:49474-49544`. At the relevant boundary it:

1. loads `o = (uint8_t*)*ppObject` at `gc.cpp:49481`;
2. calls `gc_heap::is_in_find_object_range(o)` at `gc.cpp:49483`;
3. executes `HEAP_FROM_THREAD` at `gc.cpp:49494`;
4. evaluates `gc_heap* hp = gc_heap::heap_of(o)` at `gc.cpp:49496`;
5. in this build, next asks `gc_heap::is_in_condemned_gc(o)` at `gc.cpp:49499` under `USE_REGIONS`.

`HEAP_FROM_THREAD` is a conditional preprocessor macro, not a helper call or local declaration pattern invented by the proof:

- `src/coreclr/gc/gcpriv.h:315-327` defines the conditional contract;
- with `MULTIPLE_HEAPS`, it is `gc_heap* hpt = gc_heap::g_heaps[thread];`, after `THREAD_NUMBER_FROM_CONTEXT` obtains `thread = sc->thread_number`;
- without `MULTIPLE_HEAPS`, the thread macros are empty and it is exactly `gc_heap* hpt = 0;`.

Therefore, in this Workstation build `HEAP_FROM_THREAD` produces a null local `hpt`. It obtains no heap from thread state. The source `thread` is also the Workstation constant `0` at `gc.cpp:49478`, but the proof passes the source-produced value to the observer and does not substitute a heap identity.

The declaration is `PER_HEAP_ISOLATED_METHOD gc_heap* heap_of(uint8_t* object);` at `src/coreclr/gc/gcpriv.h:2620`. `PER_HEAP_ISOLATED_METHOD` is the source's static/per-heap conditional declaration mechanism (`gcpriv.h:97-104`). The locked implementation is `src/coreclr/gc/gc.cpp:26693-26707`:

```cpp
gc_heap* gc_heap::heap_of(uint8_t* o)
{
#ifdef MULTIPLE_HEAPS
    if (o == 0)
        return g_heaps[0];
    gc_heap* hp = seg_mapping_table_heap_of(o);
    return (hp ? hp : g_heaps[0]);
#else
    UNREFERENCED_PARAMETER(o);
    return __this;
#endif
}
```

Workstation's `__this` is `(gc_heap*)0` at `src/coreclr/gc/gcpriv.h:1540-1548`. Thus the exact Workstation operation ignores the object address and returns null. It does not inspect a heap table, segment map, brick/card bookkeeping, object-address range, segment metadata, allocation context, object header, or method table. It does not dereference `o`, call another helper, perform a segment lookup, classify a generation, compare against a condemned generation, or mutate state.

The Server alternative is materially different: `HEAP_FROM_THREAD` reads `g_heaps[thread]` and the `MULTIPLE_HEAPS` `heap_of(o)` branch calls `seg_mapping_table_heap_of(o)` and falls back to `g_heaps[0]` (`gcpriv.h:320` and `gc.cpp:26698-26702`). That shared multi-heap infrastructure is not compiled into this Workstation path. `gcwks.cpp:19-20` undefines `SERVER_GC`; the Workstation `gc.cpp` branch defines `const int n_heaps = 1` at `gc.cpp:2308-2312`, and `heap_number` is the source macro `(0)` at `gcpriv.h:3931-3937`.

The proof passed the source expression `heap_number` and the source `n_heaps` value to diagnostics. It did not hard-code heap 0. Runtime evidence independently reported `heapNumber=0` and `totalHeapCount=1`.

## Heap-resolution result

The source-defined result for object `0x100A02F50` was:

- request count: 1
- entry count: 1
- completion count: 1
- duplicate resolutions: 0
- input object: `0x100A02F50`
- `hpt` from `HEAP_FROM_THREAD`: `0x0000000000000000`
- returned `gc_heap*`: `0x0000000000000000`
- heap number: `0`
- total logical Workstation heap count: `1`
- heap table/slot: not applicable; zero reads and zero identities
- segment map: not consulted; zero reads and no segment identity
- brick/card bookkeeping: zero reads
- object-address/range lookup by `heap_of`: zero reads
- allocation-context heap owner: not consulted; zero
- failure count: 1
- proof failure reason: `0xB001`, meaning the source result was null and no fallback was permitted

This is a source-derived no-heap result, not a fabricated heap identity. The runtime does have one logical Workstation heap, but the locked internal Workstation `heap_of` implementation does not return its public/runtime heap object at this point; it returns the null `__this` sentinel. Consequently, equality with the SOH segment owner was not evaluated: segment identification is a later/out-of-scope operation, and this source path performed no segment lookup.

## Machine-code path

The final proof artifact is AMD64 and uses the Microsoft x64 callback ABI. The callback symbol is `?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z` at `0x10026F90`.

The final disassembly records this path:

- range lower bound is loaded at `0x10026FE3`; the lower-failure branch is `jb 0x1002701F` at `0x10026FF4`;
- upper-bound comparison is evaluated at `0x10026FF6-0x10027005`; the upper-failure branch is `je 0x10027018`;
- the true range path sets the true result at `0x10027009` and reaches the membership-result test at `0x1002704C`;
- membership true falls through at `0x10027056` to the heap-resolution request call at `0x10027059`;
- the Workstation `HEAP_FROM_THREAD` result is materialized as zero for the observer at `0x10027061` (`xor edx,edx`); thread number zero is materialized at `0x1002705E`;
- heap-resolution entry is called at `0x10027066`, with the object in `RCX` and null `hpt` in `RDX`;
- `heap_of(o)` is an out-of-line call at `0x1002706E`, with object input in `RCX`;
- the folded `heap_of` function address is `0x1000EE30`, whose body is `xor eax,eax; ret` at `0x1000EE30-0x1000EE32`;
- the null heap result is returned in `RAX` and moved to the completion observer's heap argument in `R8` at `0x10027073`;
- source heap number zero is passed in `R9` and source total heap count one is passed in the fifth-argument stack slot at `0x10027076`;
- completion is called at `0x10027086`; its captured return address is `0x1002708B`.

The completion observer is `noreturn`, so no post-completion machine instruction is executed. The next semantic source operation is still precisely the locked `USE_REGIONS` operation `gc_heap::is_in_condemned_gc(o)` at `gc.cpp:49499`; generation-related code was not entered by the proof.

Machine-code artifacts: `artifact-disassembly.txt`, `callback-symbol.txt`, and the generated source are retained in the evidence directory named in the manifest.

## C011EC09 boundary evidence

All three fresh boots emitted `C011EC09`. Each independently showed:

- one callback request, entry, and no return;
- one membership request, entry, and completion, with membership result true;
- one heap-resolution request, entry, completion, no duplicate, and one null-result failure;
- unchanged root identities and sentinel validation;
- one registered/enumerated managed thread, with managed thread = current thread = lock owner;
- lock held at depth one, EE suspended, managed entry prohibited;
- live `ScanContext`: promotion `1`, concurrent `0`, thread count `1`, thread number `0`, thread-under-crawl null, stack limit `0`;
- zero generation, header, method-table, child-reference, promotion, marking, traversal, mutation, callback-return, restart, and resume counters.

The exact marker reason was: the genuine first root passed true managed-range membership and exactly one real `HEAP_FROM_THREAD` / `heap_of(o)` transition completed; stop before `gc_heap::is_in_condemned_gc(o)`.

## QEMU evidence

QEMU version: `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`.

The exact command was:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 -ProofMode first-root-heap-resolution -TimeoutSeconds 90
```

Final C011EC09 run: `run-20260812-075319360`.

| Boot | Marker/outcome | Root | Heap result | Serial SHA-256 |
|---|---|---|---|---|
| first-run | C011EC09 / B | `0x100A02F50` | null, heap 0, count 1 | `763A5AC74C7FC2FBC46F2941F7376822237570D7BC7A33C8A851B2519C1AD894` |
| repeat-1 | C011EC09 / B | `0x100A02F50` | null, heap 0, count 1 | `62E8DDFE672350D8EFB128CC7E25FC22FDCA8CB370176A8E269B799BD3E27319` |
| repeat-2 | C011EC09 / B | `0x100A02F50` | null, heap 0, count 1 | `C6EA68B40A5D6DF2DBEE1585CB1538EAA0A2BCEE612E6FFECD2E72BE25C18B1F` |

Proof kernel SHA-256: `B8CBB2EE337CA89D093F21E73F194E69F35B7DFBD2AE345FADA30FFC6DAF4E46`.

After every proof run, the ordinary kernel and ESP were restored and verified as `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

## Regressions and retained non-clean checks

Focused regressions run after the implementation:

- C011EC08: PASS, 3/3 fresh QEMU boots, run `out/dotnet/gc-first-root-membership-classification/run-20260812-072127212`.
- C011EC07: first attempt non-clean because an IRQ interleaved fields in the existing validator; retry PASS, 3/3 fresh boots, run `out/dotnet/gc-first-root-callback-entry/run-20260812-072636808`.
- C011EC06: PASS, 3/3 fresh boots, run `out/dotnet/gc-first-non-null-root-callback-boundary/run-20260812-073003997`.
- C011EC05: PASS, 3/3 fresh boots, run `out/dotnet/gc-first-root-candidate-load/run-20260812-073235316`.
- C011EC04: retained non-clean validator result; the runtime reached the provider boundary but emitted sentinel validation count `0xA0` instead of the historical expected count.
- C011EC03: retained non-clean validator result; the bounded object-history assertion did not match the emitted evidence.
- C011EC02: retained non-clean validator result; the preserved allocation-count assertion did not match the emitted evidence.
- C011EC01, primitive/reference/combined `[ThreadStatic]`, segment-transition, commitment, refill, first-allocation, no-collection, FLS/local storage, native-thread, runtime-pack, ELF, stack-bound, exact/general build, parsing, serial, and broader checks retain their historical classifications. They were not relabeled as passes in this focused pass.

Additional retained failures include the historical first-64-KiB execution failure, stale-cache attempts, initial runtime-pack identity mismatch, and the native-stack PowerShell wrapper's non-clean compiler-stderr promotion. The first two C011EC09 validator-format attempts are also retained as harness non-clean evidence; the final run passed without weakening validators.

## Restoration and worktree

The proof kernel is not deployed. The final ordinary build and ESP hashes both equal the expected ordinary baseline. No commit was created, amended, squashed, pushed, or merged. The worktree contains only the reviewable proof instrumentation, harness changes, this report, and retained evidence.

## Cross-references

- [C011EC08 membership classification](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_MEMBERSHIP_CLASSIFICATION.md)
- [C011EC07 callback entry](NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CALLBACK_ENTRY.md)
- [C011EC06 first non-null callback boundary](NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md)
- [NativeAOT ThreadStatic runtime support](NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md)
- [NativeAOT GC startup readiness](NATIVEAOT_GC_STARTUP_READINESS.md)
- [Workstation GC feasibility](NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md)

## Next smallest bounded milestone

The next smallest safe milestone is source/machine-code inspection of the first `is_in_condemned_gc(o)` operation and its Workstation null-heap contract, without executing it. Any attempt to bridge the internal null `__this` sentinel to a public/runtime heap object should be a separately authorized milestone; this pass intentionally did not fabricate or substitute that identity.

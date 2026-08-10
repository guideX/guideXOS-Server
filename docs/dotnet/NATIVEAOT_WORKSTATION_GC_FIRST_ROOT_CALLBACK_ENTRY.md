# NativeAOT Workstation GC: First Root Callback Entry

## Result

Outcome A. The real Workstation GC callback `GCHeap::Promote` accepted control exactly once for the first genuine non-null NativeAOT thread-static storage root. The proof validated the live AMD64 arguments and stopped after the locked callback's required root-slot load, immediately before candidate heap classification. No promotion, marking, graph traversal, object mutation, GC metadata mutation, restart, or managed resume occurred.

This is proof-only behavior. The production `InitializeModules` startup path remains active, and no commit was created.

## Starting checkpoint

The development pass started from:

| Item | Actual value |
|---|---|
| HEAD | `51b303a39ad28d8fedcc020e4bb43113060ed712` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting worktree | clean |
| Ordinary kernel SHA-256 | `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550` |
| Ordinary ESP SHA-256 | `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550` |

The final proof harness also records this immutable task-start snapshot in `taskStartCheckpoint` in the manifest. The working tree is intentionally dirty at handoff because this pass is uncommitted.

Locked identity: NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`, locked source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## C011EC06 prerequisite and root identity

C011EC06 remains the prerequisite proof: the normal managed `[ThreadStatic] byte[]? s_gcProofThreadRoot` workload assigned and read back the selected ordinal-0, `0x1000`-byte sentinel, then the normal NativeAOT root provider presented the first non-null inline root slot without invoking the callback.

In the final run, the run-specific addresses were:

| Identity | Value |
|---|---|
| Selected sentinel | `0x0000000100A01F38` |
| NativeAOT thread-static storage object | `0x0000000100A02F50` |
| Real inline root slot | `0x000000000393FBE0` |
| Real managed/runtime thread | `0x000000000393FC00` |
| Live `ScanContext` | `0x0000000004E7B440` |

The historical C011EC06 slot was `0x392DBE0` and historical context was `0x4E694E0`; those are stack/build-address instances, not source-level identities. The final callback argument was checked against the live provider's exact slot and context. The storage object, not the sentinel address itself, is the callback root. The storage object's child field was not inspected by native GC instrumentation.

Managed validation remained normal: assignment `1`, readback `1`, exact readback match, four live sentinels, unchanged selected sentinel, unchanged storage-object address, unchanged known user-object contents, no duplicate addresses, and zero object mutation. The bounded object-history counters were `0x25` before/after/at stop and sentinel checks were `0x94`; these are the current workload's existing counts.

## Locked callback source trace

The callback typedef is at locked `src/coreclr/gc/gcinterface.h:24-25`:

```cpp
typedef void promote_func(PTR_PTR_Object, ScanContext*, uint32_t);
```

With the NativeAOT aliases, the effective signature is:

```text
void GCHeap::Promote(Object** ppObject, ScanContext* sc, uint32_t flags)
```

The `ScanContext` layout is locked `gcinterface.h:1081-1110`: `Thread* thread_under_crawl`, `int thread_number`, `int thread_count`, `uintptr_t stack_limit`, `bool promotion`, `bool concurrent`, followed by unused metadata fields. The final run read those six prefix fields without rewriting them: promotion `1`, concurrent `0`, thread count `1`, thread number `0`, and zero thread-under-crawl/stack-limit values for this provider phase.

The callback is generated and assigned at locked `src/coreclr/gc/gc.cpp:29899-29901`, where `GCScan::GcScanRoots` receives `GCHeap::Promote`:

```cpp
GCScan::GcScanRoots(GCHeap::Promote,
                    condemned_gen_number, max_generation,
                    &sc);
```

The normal provider path is locked `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:114-115`:

```cpp
EnumGcRef(pThread->GetThreadStaticStorage(), GCRK_Object, fn, sc);
```

`Thread::GetThreadStaticStorage` is locked `src/coreclr/nativeaot/Runtime/thread.cpp:1251-1254` and returns `&m_pThreadLocalStatics`. The root enumeration implementation is locked `src/coreclr/nativeaot/Runtime/GcEnum.cpp:68-96`. `EnumGcRef` reaches `GcEnumObject`; for this object root, the exact source callback call is line 81:

```cpp
fnGcEnumRef(ppObj, pSc, flags);
```

The callback pointer in the final linked image is the exact map symbol:

```text
?Promote@GCHeap@WKS@@SAXPEAPEAVObject@@PEAUScanContext@@I@Z
address: 0x0000000010022560
```

The prior C011EC06 image's callback pointer `0x000000001001FE10` resolves to this same `WKS::GCHeap::Promote` function; the address changed to `0x0000000010022560` in this pass because the proof-only entry instrumentation changed the linked layout. The symbol, locked source body, callback type, and real provider call path are unchanged.

The locked callback body is `src/coreclr/gc/gc.cpp:49474-49544`, included by locked `src/coreclr/gc/gcwks.cpp`. Its source order is:

1. `gc.cpp:49476` — `THREAD_NUMBER_FROM_CONTEXT`; in this single-heap build the macro expands to no code (`gcpriv.h:317-324`), followed by the `thread = 0` single-heap declaration.
2. `gc.cpp:49481` — the first candidate-value load, `uint8_t* o = (uint8_t*)*ppObject;`.
3. `gc.cpp:49483` — the first candidate classification/heap-membership operation, `gc_heap::is_in_find_object_range(o)`.
4. `gc.cpp:49494-49496` — `HEAP_FROM_THREAD` and `gc_heap::heap_of(o)`; this is the first heap/segment-dependent metadata path after classification.
5. `gc.cpp:49499-49502` — condemned-generation/range membership.
6. `gc.cpp:49509-49515` — interior-root classification and possible `find_object`.
7. `gc.cpp:49520-49523` — conservative object-header/free-object inspection when enabled.
8. `gc.cpp:49533-49534` — the first possible promotion-state/object-related mutation, `pin_object` for `GC_CALL_PINNED`.
9. `gc.cpp:49541` — `hpt->mark_object_simple`, the first direct mark operation.
10. `gc.cpp:49543` — root-promotion logging and method-table access in the logging expression.

The proof inserts only administrative entry diagnostics before the original body and a noreturn safe-stop immediately after the source-required load at line 49481. It does not replace the callback pointer, call the callback manually, synthesize arguments, or scan child references.

## AMD64 machine-code ABI

The final evidence is the linked PE disassembly in `artifact-disassembly.txt` under the final evidence directory. The PE `.text` was disassembled with MinGW `objdump -d -Mintel`; the converted ELF is retained for the existing kernel embedding workflow but has no disassemblable sections in this conversion.

### Real call site

The map places `GcEnumObject` at `0x0000000010016B40`. Its final instructions before the real indirect callback call are:

```text
0x10016BA4  mov  rcx,rdi          ; Object** ppObj
0x10016BAC  mov  r9,rsi           ; ScanContext* pSc saved in rsi
0x10016BAF  mov  r8,rbp           ; callback pointer saved in rbp
0x10016BB2  mov  edx,ebx          ; flags
0x10016BB4  add  rsp,0x30
0x10016BB8  pop  r14
0x10016BBA  pop  rdi
0x10016BBB  pop  rsi
0x10016BBC  pop  rbp
0x10016BBD  pop  rbx
0x10016BBE  jmp  0x10016BE0       ; interior path
...
0x10016BC3  mov  r8d,ebx
0x10016BC6  mov  rdx,rsi
0x10016BC9  call rbp              ; exact callback call
0x10016BCB  add  rsp,0x30         ; return address after callback
```

For the final object-root run, the non-interior branch is selected. The actual callback call instruction is `0x10016BC9`, and its machine-code return address is `0x10016BCB`. The call-site diagnostic helper's own return address (`0x10016B83`) is retained separately; it is not misreported as the callback call's return address.

### Callback entry

The callback entry is `0x0000000010022560`:

```text
0x10022560  push rbx
0x10022562  sub  rsp,0x30
0x10022566  lea  rax,[rsp+0x38]   ; original entry RSP / return-address slot
0x1002256B  mov  r8d,r8d          ; flags remain in R8D
0x1002256E  mov  [rsp+0x28],rax
0x10022573  lea  r9,[rip-0x1a]     ; callback identity = 0x10022560
0x1002257A  mov  rax,[rax]         ; actual callback return address
0x1002257D  mov  rbx,rcx           ; preserve Object**
0x10022580  mov  [rsp+0x20],rax
0x10022585  call 0x10009C70        ; entry diagnostics
0x1002258A  mov  rcx,[rbx]         ; locked source *ppObject load
0x1002258D  call 0x10009A40        ; noreturn C011EC07 boundary
```

This is Microsoft x64 AMD64 calling convention: RCX, RDX, and R8/R8D carry the three parameters; the caller reserves 32 bytes of shadow space; RAX is not a return value for this `void` callback. The callback-entry diagnostic captured the original entry stack pointer `0x0000000004E7B368`, return address `0x0000000010016BCB`, and raw registers:

```text
RCX = 0x000000000393FBE0
RDX = 0x0000000004E7B440
R8  = 0x0000000000000000
```

The normalized arguments were identical. Argument 1 equals the real inline root slot. Loading that slot in the callback still yields `0x0000000100A02F50`. Argument 2 equals the live provider `ScanContext*`. Argument 3 equals the expected root flags (`0`, root kind `1`).

## C011EC07 boundary and counters

The safe-stop marker is `C011EC07`: “first genuine GC root callback entered successfully with validated live ABI arguments; stopped before candidate classification/promotion/mark mutation.” The final marker records callback `0x10022560`, slot `0x393FBE0`, raw storage object `0x100A02F50`, context `0x4E7B440`, flags `0`, and the following counts:

| Counter | Value |
|---|---:|
| Callback requests | 1 |
| Callback call-site entries | 1 |
| Actual callback invocations | 1 |
| Callback entries | 1 |
| Callback returns | 0 |
| Duplicate callback invocations | 0 |
| Callback-side root-slot loads | 1, value `0x100A02F50` |
| Null tests | 0; source callback has no pre-load null test |
| ScanContext fields read | 6, administrative validation only |
| First semantic-operation marker | 1 = source-required `*ppObject` load |
| Candidate-classification start | 0 |
| Heap-membership tests | 0 |
| Segment lookup | 0 |
| Object-header reads | 0 |
| Method-table reads | 0 |
| Promotion start/count | 0 / 0 |
| Marking start/count | 0 / 0 |
| Graph traversal | 0 |
| Mark-state writes | 0 |
| Promotion-state writes | 0 |
| Object-memory writes | 0 |
| GC metadata writes | 0 |
| Segment metadata writes | 0 |
| Restart requests/entries | 0 / 0 |
| Managed resumes | 0 |

The first prohibited semantic operation is `gc_heap::is_in_find_object_range(o)` at locked `gc.cpp:49483`. Execution stops immediately before it. Entering the callback and reading the source-required slot do not alter GC state. The callback does not return.

Thread invariants at the marker: one registered managed thread; managed/current/lock-owner identity `0x393FC00`; thread-store lock held at depth 1; EE suspended; managed entry prohibited; allocation-context fixup complete; registry mutation unchanged; no restart or managed resume.

## Fresh QEMU evidence

QEMU: `QEMU emulator version 11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. Command lines are retained in `out/dotnet/gc-first-root-callback-entry/run-20260809-203719803/commands.txt`. The proof kernel hash is `D27A953C2DA764AC77F1D8B7AC09693849DB910E9E9DB77181C6B28C4CFB0FD7`.

| Run | Serial SHA-256 | Marker | Requests/calls/entries/returns | Slot | Raw root | Context | Flags |
|---|---|---|---|---|---|---|---|
| first-run | `005A1FFF894F53AACCE16FC9B0022E028FFA5F9ECFEE6C362387D026F8F56612` | C011EC07 | 1 / 1 / 1 / 0 | `0x393FBE0` | `0x100A02F50` | `0x4E7B440` | `0` |
| repeat-1 | `9DA53482736EE22AA30964B4B8E4F4B8A681FC8AB4ECB7096307E1E29F9509B5` | C011EC07 | 1 / 1 / 1 / 0 | `0x393FBE0` | `0x100A02F50` | `0x4E7B440` | `0` |
| repeat-2 | `52DF0873484EC7AFEA600AF9BBDAB33E0239270A57C23F557097B21F73F18639` | C011EC07 | 1 / 1 / 1 / 0 | `0x393FBE0` | `0x100A02F50` | `0x4E7B440` | `0` |

All three runs independently showed the managed ThreadStatic assignment/readback, suspension, allocation-context fixup, real provider, non-null storage root, exact-one callback entry, correct ABI values, C011EC07, zero promotion/marking/object mutation, and zero restart/resume.

## Regression and check status

Fresh current-worktree results:

- C011EC06 first non-null callback-boundary proof: PASS, 3/3 fresh QEMU runs.
- C011EC05 candidate-load proof: PASS, 3/3 fresh QEMU runs.
- C011EC04 provider proof: reached C011EC04, but validator non-clean because current legitimate `objectBefore/objectAfter=0x25`, `sentinelChecks=0x94` did not match its historical `0x28`/`0xA0` assertion. Not counted as PASS.
- C011EC03 allocation-context/root-boundary proof: reached the bounded boundary, but the historical object-count validator rejected `0x25` versus `0x28`. Not counted as PASS.
- C011EC02 suspension proof: reached the bounded proof path, but its historical allocation-count validator rejected the current count. Not counted as PASS.
- New C011EC07 proof: PASS, 3/3 fresh QEMU runs.
- PowerShell script parsing: PASS.
- Final manifest JSON parsing: PASS.
- `git diff --check`: see final handoff; generated historical evidence files are kept separate from the reviewable source diff.

The remaining listed historical checks (primitive/reference/combined ThreadStatic, segment transition, commitment, refill/allocation variants, 4 KiB/64 KiB no-collection, FLS/local storage, native-thread, runtime-pack, ELF, stack bounds, exact/general build, and parser suites) remain retained historical results or blocked/non-clean checks in the evidence; they were not relabeled as passes by this focused callback-entry pass. The known historical native-stack PowerShell-wrapper non-clean result and prior runtime-pack identity mismatch remain retained.

## Ordinary restoration

After every proof/regression harness run, the ordinary kernel and ESP were restored. Final hashes:

```text
kernel/build/amd64/bin/kernel.elf  161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ESP/kernel.elf                     161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
```

No production GC policy, runtime identity, write barrier, thread storage design, or `InitializeModules` path was changed.

## Evidence and cross-references

Final manifest and retained logs:

`out/dotnet/gc-first-root-callback-entry/run-20260809-203719803/manifest.json`

The manifest also retains the generated callback source, PE disassembly, callback symbol record, build commands, QEMU commands, serial logs/hashes, proof-kernel hash, regression status, and ordinary restoration hashes.

Related reports:

- `NATIVEAOT_WORKSTATION_GC_FIRST_NON_NULL_ROOT_CALLBACK_BOUNDARY.md`
- `NATIVEAOT_THREAD_STATIC_RUNTIME_SUPPORT.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_ROOT_CANDIDATE_LOAD.md`
- `NATIVEAOT_WORKSTATION_GC_FIRST_PER_THREAD_ROOT_PROVIDER.md`
- `NATIVEAOT_GC_STARTUP_READINESS.md`
- `NATIVEAOT_WORKSTATION_GC_FEASIBILITY.md`

## Next bounded milestone

The smallest next step is to characterize the first callback-side classification operation at `gc_heap::is_in_find_object_range(o)` with a proof-only read barrier, still stopping before generation checks, object-header access, pinning, marking, child scanning, and all mutation. Promotion completion remains out of scope until that boundary is independently understood.

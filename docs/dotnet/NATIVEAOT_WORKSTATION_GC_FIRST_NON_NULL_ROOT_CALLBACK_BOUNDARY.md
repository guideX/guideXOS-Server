# NativeAOT Workstation GC: first non-null root callback boundary

Status: proof-only Outcome A, 3/3 fresh QEMU boots, no commit created.

## Checkpoint and locked identity

The work started from the clean committed checkpoint below. The ordinary kernel and ESP were both verified before deployment and restored after every runner:

- branch: `v1.1_DOTNET_SUPPORT`
- HEAD: `5175ce93287b7d9827af3c4e25104637e3d2a326`
- starting worktree: clean (`git status --short` was empty)
- ordinary `kernel\build\amd64\bin\kernel.elf` and `ESP\kernel.elf`: `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`
- locked runtime: NativeAOT 9.0.0, AMD64, Workstation GC, interfaces 5.3/2, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

The production startup path invokes generated `InitializeModules` with the real module range and classlib table before `ManagedMain`. The historical pre-fix failure is preserved: Outcome E, fault around RIP `0x1008E2BE`, CR2 `0xFFFB5FF9`, instruction `48 8B 52 10`, before managed assignment and before root enumeration.

## Proof boundary

Normal managed code assigns one existing `byte[4096]` sentinel to genuine `[ThreadStatic] byte[]? s_gcProofThreadRoot`, immediately reads it back once, and then reaches a real blocking Workstation collection. Four existing sentinels are retained for object validation. No slot, object, heap membership, header, method table, callback, promotion, marking, or object mutation is fabricated or performed by the proof.

The final evidence is [run-20260809-181634024](../../out/dotnet/gc-first-non-null-root-callback-boundary/run-20260809-181634024/), with [manifest.json](../../out/dotnet/gc-first-non-null-root-callback-boundary/run-20260809-181634024/manifest.json). QEMU is 11.0.0.

| boot | serial SHA-256 | result |
|---|---|---|
| first | `8B652F82340C2FC98828FEA2D807E958032A6A93970A55362E70EEA475013657` | C011EC06 |
| repeat-1 | `923BC53CA38456D1442EF6D09F68F08B817C1784A9C41016711143C87934E99C` | C011EC06 |
| repeat-2 | `624595682825D848052FDEA49DCE71DAEF15B2F42409F03AC1579079C11962F` | C011EC06 |

All three runs report `rootFailures=0`, `fixupFailures=0`, `lockDepth=1`, and `eeSuspended=1`. The managed proof reports assignment/readback `1/1`, exact readback match `1`, initialization `3`, sentinel ordinal `0`, payload size `0x1000`, sentinel/readback address `0x0000000100A01F38`, and managed thread `0x000000000392DC00`.

The runtime created and published exactly one ThreadStatic storage object: allocation/publication `1/1`, object `0x0000000100A02F50`, inlined root/slot `0x000000000392DBE0`. This is the ordinary NativeAOT slow path traced through `ThreadStatics.cs:36-50`, `RhNewObject`, `Thread::GetThreadStaticStorage` (`thread.cpp:1251-1261`), `InlinedThreadStaticRoot` (`thread.h:76-84`), and `gcenv.ee.cpp:94-133`.

The locked provider source enumerates inline thread-static roots before the `GetThreadStaticStorage()` field and stack roots. Therefore the first candidate is the inline root's real `m_threadStaticsBase` slot, not the array sentinel itself:

| field | value |
|---|---|
| provider | `thread-static-provider`, first inline thread-static root, provider function code `0x1` |
| bound / visited | `8` / `1` |
| first slot / raw value | `0x000000000392DBE0` / `0x0000000100A02F50` |
| null / non-null candidates | `0` / `1` |
| known-address classification | `0x8` = runtime ThreadStatic storage object |
| proof-root match / storage-object match | `0` / `1` |
| load requests / entries / machine loads | `1` / `1` / `1` |
| duplicate loads / load faults | `0` / `0` |

The direct managed relationship is proven by the runtime's own storage allocation/publication and inline-root address, so this is Outcome A even though the first non-null value is the storage object that owns the managed ThreadStatic base. The callback pointer is `0x000000001001FE10`; the locked callback ABI is `ScanFunc`/`promote_func(Object**, ScanContext*, uint32_t)`, as used by `GcEnum.cpp:68-96`. The captured `ScanContext` is `0x0000000004E694E0`, with condemned generation `0`, max generation `2`, promotion `1`, concurrent `0`, raw root flags `0`, and root kind `1`.

C011EC06 stops immediately before callback invocation. Callback, promotion, marking, candidate pointee dereference, heap membership, object-header inspection, method-table inspection, root-flag application, object mutation, restart, and managed resume counts are all zero. Lock depth remains `1`; EE suspension entry/suspension/return are `1/1/1`, but execution stops before resume.

The real collection is generation `1`, `reason_oos_soh (5)`, blocking, non-compacting. Collection request/entry and fixup request/entry/completion are `1/1` and `1/1/1`. The collection occurs after `0x25` completed user array records because the genuine ThreadStatic storage allocation consumes allocation-context work; the proof does not alter counters or invent ordinal `0x28`. Object validation is `0x25` before/after/at-stop, with `0x94` sentinel checks and zero history overflow. User allocation markers are `userAllocations=0x2B`, requests `0x2C`, fast/rare `0x16/0x16`, refills `0x15`, same-segment commits `0x03`, transitions `0`, and total observed requests `0x2D`. All four sentinel addresses, contents, and layouts remain unchanged.

Thread enumeration saw `registeredBefore=1`, enumerated/included/excluded `1/1/0`, and no duplicates, integrity failures, or registry mutation. The serial marker reports `registeredAfter=0` at this transient pre-callback iterator boundary; this post-iterator view is not used as the registered-thread proof. The actual enumerated thread, current thread, collection initiator, and lock owner all match.

## Regression record

- ThreadStaticCombined, ThreadStaticPrimitive, and ThreadStaticReference: PASS, 3/3 fresh QEMU boots each.
- C011EC05 first-root-candidate-load: PASS, one real bounded machine-word load and safe stop.
- C011EC01 first-collection-boundary: PASS, safe bounded stop.
- C011EC07 segment-transition: PASS.
- C011EC04 first-per-thread-root-provider, C011EC03 allocation-context-fixup-root-boundary, and C011EC02 single-thread-suspend-ee: retained non-clean historical validators; each still expects the old exact `0x28`/`0xA0` bookkeeping after the production startup fix.
- first-allocation, first-refill, and multiple-refill boundary runners: retained non-clean stale log-pattern assertions. Their attempts and serial evidence remain under their respective `out\dotnet` evidence directories.
- The historical pre-`InitializeModules` Outcome E and earlier stale-cache/runtime-identity/native-stack wrapper failures remain documented and were not relabeled as passes.

The ordinary kernel/ESP were restored after the final run and regression attempts; both currently hash to `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`. The smallest next bounded milestone is to characterize the callback ABI and callback-side `ScanContext` consumption from this exact proof boundary, still with a proof-only stop before invoking or mutating through the callback.

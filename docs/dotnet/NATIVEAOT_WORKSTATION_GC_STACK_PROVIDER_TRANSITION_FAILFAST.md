# NativeAOT Workstation GC stack-provider transition fail-fast isolation

## Result

This milestone is Outcome D. The final proof reaches the category-3 transition and the entry instruction of `Thread::GcScanRoots`, then fails inside the inseparable `StackFrameIterator` prologue before the first stack-map/root callback. No stack frame was walked, no GCInfo/root map was read, no stack root was promoted, and no C011EC16 marker was emitted.

The exact selected condition is the locked NativeAOT runtime guard:

```text
RuntimeInstance::GetCodeManagerForAddress(m_ControlPC) == nullptr
```

In the live final image, `RuntimeInstance::m_CodeManager == 0`, the managed-code range is empty, and the transition-frame control PC is `0x10004D66`. This is a real runtime stack/unwind/code-manager precondition failure, not a proof-instrumentation allocation or logging failure. It was not fixed in this milestone.

## Starting Git and runtime state

The repository was inspected before changes. The starting state was:

```text
repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT
branch: v1.1_DOTNET_SUPPORT
HEAD: ce765316236577e84dce82b34a6184f5229ec9f8
upstream: origin/v1.1_DOTNET_SUPPORT
ahead/behind: 0/0
tracked worktree entries: 0
untracked entries visible to git status: 0
ordinary kernel SHA-256: 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ESP kernel SHA-256: 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
```

C011EC15 proof edits were already committed in `ce765316236577e84dce82b34a6184f5229ec9f8`; they were not dirty worktree edits at preflight. The earlier C011EC15 Outcome E evidence remains under `out/dotnet/gc-next-genuine-root-provider` and was not cleaned.

The locked runtime identity remained NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The managed proof field was `[ThreadStatic] byte[]? s_gcProofThreadRoot`. The final live values were sentinel `0x100A01F38` and NativeAOT thread-static storage object `0x100A02F50`. The first queue insertion remained slot index `0`, cursor `0 -> 1`, old slot `null`, new value `0x100A02F50`. The queue was not drained and no mark-bit or child traversal operation occurred.

## Source order

The locked provider order is:

```text
GCToEEInterface::GcScanRoots
  FOREACH_THREAD / ThreadStore::Iterator
  skip GC-special threads
  allocation-context heap check
  inline ThreadStatic list
    EnumGcRef(inline root)
  ordinary ThreadStatic storage
    EnumGcRef(thread-static storage)
  sc->thread_under_crawl = pThread
  pThread->GcScanRoots(fn, sc)
  sc->thread_under_crawl = NULL
```

Locked source ranges:

* `src/coreclr/nativeaot/Runtime/gcenv.ee.cpp:94-133` contains the ordinary provider and stack-provider call order.
* The generated proof source is `out/dotnet/gc-stack-provider-transition-failfast/build-run-20260815-140749534/runtime-pack/gcenv.ee.single-thread-suspend-ee.cpp:123-181`.
* `Thread::GcScanRoots` is `src/coreclr/nativeaot/Runtime/thread.cpp:393-403`; its first operations are `CrossThreadUnhijack`, `StackFrameIterator frameIterator(this, GetTransitionFrame())`, and `GcScanRootsWorker`.
* The first stack callback/GCInfo path is later in `thread.cpp:442-569`, beginning after iterator method-state calculation.
* `StackFrameIterator::CalculateCurrentMethodState` is `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1913-1935`.
* `RuntimeInstance::GetCodeManagerForAddress` and its managed-range test are `src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp:96-109`.

The ordinary ThreadStatic `EnumGcRef` returned one genuine null candidate. The minimal proof then recorded `stack-provider-transition-start`; this is the source transition immediately before `sc->thread_under_crawl` assignment and `Thread::GcScanRoots`.

## Fail-fast value and source path

`0x47435354` is loaded by the guideXOS startup probe at `guidexos_nativeaot_gc_startup_probe.cpp:87`:

```cpp
guidexos_nativeaot_pal_fail_fast(0x47435354u);
```

The locked runtime assertion macro in `rhassert.h:63-65` expands `RhFailFast()` to `RaiseFailFastException(...)`. The selected runtime guard is `FAILFAST_OR_DAC_FAIL(m_pCodeManager)` at `StackFrameIterator.cpp:1933`. The path is:

```text
StackFrameIterator::CalculateCurrentMethodState
  -> RuntimeInstance::GetCodeManagerForAddress
  -> RhFailFast
  -> RaiseFailFastException
  -> guideXOS startupFailFast
  -> guidexos_nativeaot_pal_fail_fast(0x47435354)
```

The integer is not an official .NET or NativeAOT diagnostic tag. In little-endian memory byte order it is `54 53 43 47`, which reads as ASCII `TSCG` in memory order; reading the integer's most-significant byte first gives `GCST`. The source gives it only the guideXOS startup-probe diagnostic meaning above. It is not the local proof `guideXosFailFast` reason path.

## Final machine-code trace

The final minimal image map is `out/dotnet/gc-stack-provider-transition-failfast/build-run-20260815-140749534/artifact/NativeAotGcSingleThreadSuspendEe.map`. Important final addresses are:

```text
GCToEEInterface::GcScanRoots:                 0x100139E0
ordinary EnumGcRef call:                      0x10013B66 -> 0x100144B0
category-3 hook call:                         0x10013B76 -> 0x10006FD0
thread_under_crawl store:                     0x10013BA0
Thread::GcScanRoots call:                     0x10013BA9 -> 0x10009910
Thread::GcScanRoots entry:                    0x10009910
StackFrameIterator::CalculateCurrentMethodState: 0x1000D730
RuntimeInstance::GetCodeManagerForAddress:   0x1000ABE0
RaiseFailFastException / abort / _wassert:   0x10007F40
startupFailFast:                              0x10007FB0
guidexos_nativeaot_pal_fail_fast:             0x1000FFF0
```

The final GDB trace shows:

```text
0x1000D7A8: call 0x1000ABE0
0x1000D7AD: mov [iterator+0x150], rax
0x1000D7B4: test rax, rax
0x1000D7B7: jne 0x1000D7C8       ; not taken because RAX == 0
0x1000D7C3: call 0x10007F40
return address: 0x1000D7C8
0x10007FB4: mov ecx, 0x47435354
0x10007FB9: call 0x1000FFF0
```

The static disassembly closes the provider-transition gap between the ordinary null return and the stack call: `0x10013B66` calls the ordinary `EnumGcRef`, `0x10013B6B` is its continuation, `0x10013B76` calls the category-3 transition hook, `0x10013BA0` stores `sc->thread_under_crawl`, and `0x10013BA9` calls `Thread::GcScanRoots`. The exact dump is retained at `out/dotnet/gc-stack-provider-transition-failfast/run-20260815-140749534/static-GCToEEInterface-GcScanRoots-disassembly.txt`.

At the fail-fast call, `RCX=0`, `RDX=0`, and `R8=1`; the helper result in `RAX` is zero. The immediate caller is `StackFrameIterator::CalculateCurrentMethodState`. The last helper executing before the branch is `RuntimeInstance::GetCodeManagerForAddress`.

The final live iterator was `0x4E79170`; `m_ControlPC` at offset `+0x18` was `0x10004D66`; `m_pCodeManager` at `+0x150` was null; and `m_dwFlags` was `0x8A`. The runtime instance was `0x1013C8C0`; its code-manager field and managed-code range fields were zero.

The final GDB artifacts are:

* `out/dotnet/gc-stack-provider-transition-failfast/run-20260815-140749534/live-gdb-final4/live-gdb-final4.gdb-output.txt`
* `out/dotnet/gc-stack-provider-transition-failfast/run-20260815-140749534/static-StackFrameIterator-failfast-disassembly.txt`

## Thread and ScanContext state

The final live state was:

```text
current Thread*:          0x393DC00
enumerated Thread*:       0x393DC00
collection initiator:     0x393DC00
ThreadStore lock owner:   0x393DC00
lock held / recursion:    1 / 1
state flags:              0x00000001 (attached)
cooperative / preemptive: 1 / 0
native thread-id field:   0x100D88D0
ScanContext*:              0x4E79440
thread_under_crawl:       0x393DC00
thread_number / count:    0 / 1
promotion / concurrent:   1 / 0
ScanContext.stack_limit:  0x0
current RSP at fail-fast: 0x4E79088
Thread stack-low field:   0x0
Thread stack-high field:  0x0
transition-frame RIP:     0x10004D66
```

`thread_under_crawl` was assigned before the call, as required by the generated source order. The stack-limit and stack-base fields were captured read-only but were not read by the failing prologue. Current frame state was read: `GetTransitionFrame` supplied the transition frame and the iterator loaded its RIP into `m_ControlPC`. No stack descriptor, unwind frame, GCInfo, root-map metadata, or stack-limit scan range was consumed before the code-manager guard.

## Allocation and instrumentation audit

The transition interval had zero observed allocation attempts after EE suspension, zero proof diagnostic allocations, zero NativeAOT object allocations, zero thread-static allocations, zero temporary runtime allocations, and zero managed-entry attempts. The minimal hooks use scalar counters, fixed string literals, fixed-size hexadecimal output, and direct serial I/O. They do not use vectors, dynamic strings, formatting libraries, managed state, unsafe thread-local allocation, or C++ runtime allocation.

The controlled minimization suppressed optional `nativeaot-gc-next-genuine-root-provider` serial lines and retained only scalar GcScanRoots/category-2-return/category-3-transition markers, root scalar identity capture, and the native fail-fast line. The full C011EC15 behavior and the minimal behavior were identical: `0x47435354` after category 3 and `Thread::GcScanRoots` entry. Therefore the fail-fast is not caused by diagnostic logging or proof instrumentation. No production runtime source was changed and no fix was made.

The failure is already present in ordinary non-proof NativeAOT behavior as the locked `FAILFAST_OR_DAC_FAIL` contract guard. The proof exposed it at a controlled boundary; the guideXOS tag is only the reporting shim.

## Stack advancement and queue preservation

`Thread::GcScanRoots` was called once and its entry instruction was reached once. The prologue failed before `GcScanRootsWorker` reached the frame loop. Stack frames walked: `0`; stack-root callbacks: `0`; stack-root slots: `0`; stack non-null candidates: `0`; stack `Promote` calls: `0`; GCInfo/root-map reads: `0`; second `Promote` attempts/entries: `0/0`; second queue insertion: `0`; queue drain: `0`; mark-bit writes: `0`; child-reference reads: `0`; graph traversal: `0`; restart/resume: `0/0`.

The first queue slot remained the original storage object, cursor remained `1`, and no slot 1 write occurred. The sentinel and storage object identities were unchanged in the retained object-validation evidence. This milestone did not attempt to find the second genuine root.

## QEMU and regression evidence

QEMU was `11.0.0` (`v11.0.0-12122-ga4bb4b10c9`). The exact three commands are retained in `out/dotnet/gc-stack-provider-transition-failfast/run-20260815-140749534/commands.txt`; each used `-accel tcg,thread=single`, `-machine pc`, `-smp 1`, the locked OVMF image, the run-specific FAT ESP, `-no-reboot`, `-no-shutdown`, serial capture, and `-d int,guest_errors`.

| boot | result | serial SHA-256 |
|---|---|---|
| first-run | Outcome D, `0x47435354` | `9A61EDCDDD8B1714E518F5C20ACAF44C6DB5AD10B88D2BB697F329477B40A530` |
| repeat-1 | Outcome D, `0x47435354` | `7E3B7EAA948768199BD3162A08A398C034B2863607D3EEAC9A22C090BC154729` |
| repeat-2 | Outcome D, `0x47435354` | `D9C0C84F1C590824D95BA908FD60122112669E31395BD4808D234CB8E05D20D5` |

Proof-kernel SHA-256: `2F84D1C04F022517341E584DE215B918EF4F9CA816D45CBD01BF40B8EF9A025C`.

The focused regression result is PASS 3/3 for this failure-isolation proof. C011EC15 Outcome E, C011EC14 Outcome D, C011EC13 Outcome B, C011EC12 Outcome D, C011EC11 Outcome A, C011EC10 Outcome A, C011EC09 non-clean status, and the older C011EC08-C011EC05 evidence remain retained and were not relabeled. Historical primitive/reference/combined ThreadStatic, runtime-pack, ELF, native-thread, parser, serial, exact-kernel, and broader build evidence remains retained. The broad suite was not relabeled as freshly rerun by this focused pass.

## Restoration and Git result

The script restored both ordinary images in its `finally` block. Final ordinary hashes are again:

```text
kernel/build/amd64/bin/kernel.elf: 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ESP/kernel.elf:                     161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
```

No QEMU process remains. Generated evidence was preserved. The intentional milestone changes are the proof-harness mode and allocation-free minimal diagnostic hook in:

* `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`
* `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
* this report

The evidence manifest is `out/dotnet/gc-stack-provider-transition-failfast/run-20260815-140749534/manifest.json`.

`git diff --check` is required before completion. Because this is a cleanly classified Outcome D with ordinary restoration intact, the intended milestone files may be staged, checkpointed, and pushed under the requested Git policy after the final diff check. No amend, squash, reset, rebase, force-push, or unrelated staging is permitted.

## Next smallest bounded milestone

Do not enter stack-root enumeration yet. The next milestone should independently establish the smallest production-correct contract that registers the NativeAOT code manager and managed code range before a suspended stack walk, then stop at the same entry-only boundary. Separately audit why the live Thread stack-low/high fields are zero. Only after both preconditions are independently validated should a later milestone observe the first stack-map callback.

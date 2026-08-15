# NativeAOT Workstation GC: Next Genuine Root Provider

## Final classification

This pass is **Outcome E — unexpected failure before the next provider**. The
locked source continuation was traced and the first genuine root callback was
allowed to return through its normal bounded path. The authentic ordinary
thread-static slot was then visited and was null. Execution failed during the
subsequent allocation path before the source-required `Thread::GcScanRoots`
provider entry. No second genuine non-null root was found, no second
`Promote` ran, and no child/reference graph traversal occurred.

The C011EC15 marker was therefore not reached. This is intentionally a
non-clean milestone, not an Outcome A/B/C/D relabel.

## Starting Git and runtime state

The task-start repository was:

* repository: `D:/dev/guideXOSServerV1.1_DOTNET_SUPPORT`;
* branch: `v1.1_DOTNET_SUPPORT`;
* HEAD: `0850f6a4be7c20cef17d82471444d9bfd0300886`;
* upstream: `origin/v1.1_DOTNET_SUPPORT`;
* ahead/behind: `0/0`;
* tracked worktree: clean;
* untracked entries: 2,180, all preserved. They include prior generated
  evidence and were never staged.

The C011EC14 prerequisite and handoff commits were present. The historical
commit recorded in the repository is
`93c04e809ba43fa5464c17a2c66e686df6b7b826`; the user-supplied
`93c04e80b4c4a02e9e17e9b2e896d262823e3a75` is not an object in this
repository. The C011EC14 handoff is
`0850f6a4be7c20cef17d82471444d9bfd0300886`.

Both ordinary starting images had SHA-256
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

The runtime identity remained locked to NativeAOT `9.0.0`, AMD64, Workstation
GC, GC interfaces `5.3 / 2`, source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

## C011EC14 prerequisite

C011EC14 remains Outcome D. Its bounded mark queue is a 16-entry circular
linear exchange table, so natural non-null displacement requires insertion
17. The prior first-root path made one authentic insertion into slot 0,
advanced the cursor `0 -> 1`, observed `old_o == nullptr`, and stopped. This
pass did not seed the queue, force the cursor, drain slot 0, or pursue
insertion 17.

## Locked root-provider source order

The exact locked-source order is:

1. `gcscan.cpp:149-154`, `GCScan::GcScanRoots`, forwards to
   `GCToEEInterface::GcScanRoots`.
2. `nativeaot/Runtime/gcenv.ee.cpp:94-133`,
   `GCToEEInterface::GcScanRoots`, enters `FOREACH_THREAD`.
3. A GC-special thread is skipped. A thread that is not using the current
   allocation-context heap is skipped.
4. `Thread::GetInlinedThreadStaticList()` is walked. Each list entry has one
   `m_threadStaticsBase` root slot and a `m_next` link. Each entry calls
   `EnumGcRef`.
5. The ordinary `Thread::GetThreadStaticStorage()` slot is enumerated with
   `EnumGcRef`. `thread.cpp:1251-1254` returns the address of
   `m_pThreadLocalStatics`.
6. Only after those thread-static providers does the source set
   `sc->thread_under_crawl` and call `pThread->GcScanRoots(fn, sc)`.
7. `thread.cpp:393-403` creates a `StackFrameIterator` and calls
   `GcScanRootsWorker`; `thread.cpp:442-569` handles hijacked return values,
   conservative stack reporting when enabled, GC stack-map frames, exception
   objects, registered GC frames, and the thread-abort exception.
8. On Windows, `windows/CoffNativeCodeManager.cpp:434-496` decodes the native
   GC info and calls `GcInfoDecoder::EnumerateLiveSlots`.
9. In the normal collection phase, `gc.cpp:29897-29926` scans EE roots, drains
   the mark queue, scans finalizer roots, drains again, scans handles, and
   drains again. Dependent handles are a later phase at `gc.cpp:30051-30063`.
   `CFinalize::GcScanRoots` is at `gc.cpp:52059-52080`.

There is no separate module/static-root provider in this
`GCToEEInterface::GcScanRoots` path before stack scanning. Handles and
finalizer structures are not interleaved with the initial thread provider
sequence. The first source-valid next major category after thread statics is
the thread stack/root-map provider.

`GcEnum.cpp:68-96` confirms that `GcEnumObject` performs the raw slot load,
then invokes the callback for an object reference; `EnumGcRef` has no hidden
second slot. A null callback is a normal source path: `GCHeap::Promote`
returns from its find-range guard, so the ordinary null slot must not be
treated as a failure.

## First root and callback return

The managed proof field remained `[ThreadStatic] byte[]? s_gcProofThreadRoot`.
The selected workload sentinel remains `0x100A01F38`, and the real NativeAOT
thread-static storage object in the final proof boots was
`0x100A02F50`. The sentinel was validated by the preceding C011EC14 proof;
the C011EC15 run failed before a safe-stop line that would have reprinted the
managed assignment/readback record.

The final address-bearing serial observation was identical in all three
valid fresh boots:

| item | value |
|---|---:|
| first root field | `[ThreadStatic] byte[]? s_gcProofThreadRoot` |
| first root provider | inline thread-static provider, category `1` |
| first root slot/provider identity | `0x393CBE0` |
| first root raw value | `0x100A02F50` |
| callback | `0x1001F2F0` |
| callback context | `0x4E78440` |
| flags | `0x00000000` |
| ordinary thread-static slot | `0x393CC90` |
| ordinary slot raw value | `0x0000000000000000` |

The first root is genuine managed runtime/workload state: the NativeAOT
inline thread-static storage object holding the real thread-static data. It is
not a proof-only slot or a manually invoked callback.

The exact return sequence was:

`GcEnumObject` raw load -> `Promote` entry -> real WKS find-range and
condemned-generation path -> `mark_object_simple` -> one `queue_mark`
insertion -> queue null-displacement return -> mark-helper return -> Promote
return -> `EnumGcRef` return -> ordinary thread-static provider.

The current proof instrumentation observed one first-root callback return.
The first root therefore completed its bounded queue path before the ordinary
provider was entered. The null ordinary callback also entered `Promote`, took
the source null/find-range return, and returned normally.

## Provider and root accounting

The final valid serial sequence was:

```text
GcScanRoots entry
FOREACH_THREAD entry
thread iterator initialized
thread enumerated
thread included
inline provider observed
candidate non-null slot=000000000393CBE0 raw=0000000100A02F50 provider=000000000393CBE0 category=00000001 callback=000000001001F2F0 context=0000000004E78440 flags=00000000
Promote non-null
EnumGcRef returned non-null slot
ordinary provider entered
candidate null slot=000000000393CC90 raw=0000000000000000 provider=000000000393CC90 category=00000002
Promote null
EnumGcRef returned null slot
FAIL_FAST reason=47435354
```

Accounting through the failure boundary was:

| counter | result |
|---|---:|
| GC root-scan requests | 1 |
| provider requests | 1 initial request plus the source providers |
| provider entries | 2 |
| root slots visited | 2 |
| null candidates | 1 |
| non-null candidates | 1 |
| Promote callback attempts | 2 total, one first-root and one null ordinary slot |
| first-root callback returns | 1 |
| second-root callback attempts | 0 |
| second Promote entries | 0 |
| second queue mutation attempts/executions | 0 / 0 |
| queue insertions | 1 first-root insertion |
| mark-bit writes | 0 from the first insertion (`old_o == nullptr`) |
| child-reference reads | 0 |
| graph traversal count | 0 |

The internal `EnumGcRef` return observer has one actual slot-return event per
slot plus one wrapper completion notification per `EnumGcRef`; the serial
sequence proves two actual slot returns and two wrapper completions. No
second non-null candidate exists in this pass.

The current inline-root list had one observed entry. The structure is
capable of multiple entries because `InlinedThreadStaticRoot::m_next` is a
linked-list pointer, but this runtime thread had no second inline entry before
the ordinary provider. The ordinary provider also has exactly one slot.

## Stop/failure boundary

The preferred C011EC15 candidate stop was not reachable. The generated source
did reach the exact next source boundary:

```text
EnumGcRef(threadStaticStorage, GCRK_Object, fn, sc);
guideXosNativeAotC011EC15ProviderEntered(3u, pThread, pThread);
sc->thread_under_crawl = pThread;
pThread->GcScanRoots(fn, sc);
```

The serial log stopped after the ordinary `EnumGcRef` null return and before
the category-3 provider marker. Thus stack/root-map enumeration was not
entered. Static/module-root enumeration was not entered. Handle enumeration
was not entered. Finalizer enumeration was not entered. No returned/displaced
queue object was processed and no child traversal started.

The failure is the NativeAOT startup/failed-allocation path:

```text
[nativeaot-pal-qemu-test] FAIL_FAST reason=47435354 detail=0000000000000000
failfastStage=00000B03 ... currentRip=000000001000467B ...
```

This is not evidence of a missing stack-map decoder contract: the decoder is
present in the locked source, but its provider was never entered. It is also
not evidence of a second root. The exact blocking boundary is the return
from the ordinary null thread-static callback to the caller, before the
source-required `Thread::GcScanRoots` transition.

The first queue state remains the proven state: slot index `0` contains the
original first storage object, cursor `0 -> 1`, `old_o == nullptr`, no slot 1
write, no queue drain, and no mark-bit write. The current failed boot did not
reach the C011EC15 safe-stop line that prints the queue address, so an exact
current queue-slot address is intentionally not inferred from an older run.

ThreadStore remained held and the EE remained suspended in the completed
first-root path. No restart or managed resume occurred. The failure occurs
while the allocation remains unsatisfied, after root enumeration returned;
there is no evidence that managed entry was re-enabled.

## QEMU evidence

The focused build command was:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 -RepoRoot D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT -ProofMode next-genuine-root-provider -FreshBootCount 1 -TimeoutSeconds 90
```

That command produced the proof kernel in
`run-20260815-120624518/first-run/esp/kernel.elf`, SHA-256
`60A06D57A61004E5D06D2F34075CD17071F10B514C3C22BD303E9161E4C5FF3A`, and a
first serial log with SHA-256
`E9726175C4F873D9C1A9105DE302DE2BA618744FF6C1046F5B07CAC66EDDA350`.
It timed out at the same post-root boundary.

Three additional fresh QEMU 11.0.0 boots used that exact proof kernel and
fresh complete ESP trees. All independently reproduced the same root order,
addresses, `FAIL_FAST reason=47435354`, zero second queue mutation, and zero
child traversal:

| boot | evidence directory | serial SHA-256 |
|---|---|---|
| repeat 1 | `run-20260815-121655010-valid-repeat-1/` | `C6AD5E244E95B00D9C4515955487FC7B5B58E2A08DB5286AD1DFD6DED4B1F91A` |
| repeat 2 | `run-20260815-121835119-valid-repeat-2/` | `4A9E7A65DD49998F92707E095849EDA2C6A1C9919C83A879977E57524A9CD6D5` |
| repeat 3 | `run-20260815-122015218-valid-repeat-3/` | `75CABA61E988E0D07BCC54BBAA2ED143103ECB376C6C2C0F5ED9DEC1325F5246` |

The QEMU command used for each repeat was the exact locked form emitted by
the harness, with `-accel tcg,thread=single`, `-machine pc`, `-smp 1`, the
locked OVMF image, a fresh `fat:rw` ESP directory, one GiB RAM, serial-file
capture, `-no-reboot -no-shutdown`, and `-d int,guest_errors`. Each hung
proof process was terminated by the bounded watchdog; no proof QEMU process
remained afterward.

## Regressions and historical preservation

Static checks completed:

* PowerShell parser: PASS;
* locked runtime-pack/native proof build and exact ELF conversion: PASS;
* proof serial classification: PASS for the reproduced failure sequence;
* `git diff --check`: PASS, with only Git's LF/CRLF advisory warnings;
* ordinary image restoration: PASS.

The focused C011EC15 set is three valid fresh boots plus the harness first
boot, all Outcome E at the same boundary. C011EC14 Outcome D remains
preserved. C011EC13 Outcome B, C011EC12 Outcome D, C011EC11 Outcome A,
C011EC10 Outcome A, and the C011EC09 non-clean interpretation remain
historical and were not relabeled. The broad combined regression matrix was
not rerun after this non-clean stop; it is retained as historical evidence,
not claimed as a fresh pass. This report therefore does not claim fresh
results for every primitive/reference/combined `[ThreadStatic]` run,
C011EC03/C011EC02, runtime-pack/ELF/native-thread checks, or every old
validator.

Retained historical failures include the 64 KiB failure, stale-cache and
runtime-pack identity mismatch evidence, native-stack wrapper failure,
local-storage teardown evidence, and historical race/serial-wrap failures.
No old validator was weakened or relabeled.

## Evidence and restoration

The evidence root is:

`out/dotnet/gc-next-genuine-root-provider/`

The manifest for this pass is:

`out/dotnet/gc-next-genuine-root-provider/run-20260815-120624518/manifest.json`

At completion, both ordinary images were restored and verified as:

`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

No QEMU proof process remained. All generated evidence, including unrelated
prior evidence and the three fresh repeat directories, remains untracked and
preserved.

## Git completion status

The intentional tracked changes are the bounded C011EC15 diagnostics,
platform callbacks, harness/source injection, and this report. Because the
milestone is Outcome E rather than a clean completed Outcome A/B/C/D, no
files were staged, no commit was created, and nothing was pushed. Unrelated
and generated untracked evidence was not staged.

## Recommended next milestone

The smallest next bounded milestone is to isolate the post-ordinary-null
transition into `Thread::GcScanRoots` with one source-valid continuation
observer around `sc->thread_under_crawl`, `StackFrameIterator`, and the first
`GcScanRootsWorker` entry. It must preserve the current first-root queue
state, remain before any stack-map slot callback, and determine why the
allocation path fails before category 3. It should not add synthetic roots,
decode broad stack maps, traverse children, drain the queue, or pursue queue
wraparound.

# NativeAOT Workstation GC: C011EC19 unwind / GC-info boundary

## Result

C011EC19 is Outcome A. The locked NativeAOT 9.0.0 AMD64 Workstation runtime consumed the genuine `CoffNativeCodeManager` unwind blob and genuine GC-info for the first managed frame. The GC-info decoder reported three register roots and one stack root, and all four authentic stack-derived `Promote` callbacks entered and returned. The run stopped at the next real boundary: the reverse-P/Invoke transition-frame unwind return.

This is a continuation of C011EC18, not a reinterpretation of C011EC17 or C011EC18. The original ThreadStatic/storage-object queue insertion remains separate from the four stack-derived insertions.

## Repository and locked identity

- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `4f6a1f2ea5b38694260bf6deb4d0dfcaff96054f` (`Fix NativeAOT transition frame control PC`)
- Starting upstream: `origin/v1.1_DOTNET_SUPPORT`; starting divergence: ahead 1, behind 0
- Locked runtime: NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`
- Locked runtime source commit: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- Production manager: `CoffNativeCodeManager`
- Managed range: `[0x10001000, 0x10050950)`

The ordinary kernel and ESP were verified before and after the proof. Both restored to SHA-256 `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.

## Locked upstream control flow

The first-frame path is distinct at each semantic boundary:

1. `StackFrameIterator::CalculateCurrentMethodState` calls `FindMethodInfo`, sets the effective safe point to `ControlPC`, and calls `GetFramePointer` (`src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1913-1939`). C011EC18 proved this boundary; its frame-pointer result was zero.
2. `Thread::GcScanRootsWorker` calls `EnumGcRefs` with the current manager, method info, effective safe point, register display, callback, scan context, and active-frame state (`src/coreclr/nativeaot/Runtime/thread.cpp:442-515`). It advances with `frameIterator.Next()` only after root enumeration (`:517-540`).
3. `StackFrameIterator::NextInternal` calls `UnwindStackFrame` with `USFF_StopUnwindOnTransitionFrame` and `USFF_GcUnwind` as applicable (`src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:1554-1566`).
4. `CoffNativeCodeManager::GetCodeOffset` locates the unwind blob, skips associated/EH data, returns the GC-info pointer, and computes `address - methodStart` (`src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:375-398`).
5. `CoffNativeCodeManager::EnumGcRefs` constructs the real `GcInfoDecoder` with `DECODE_GC_LIFETIMES | DECODE_SECURITY_OBJECT | DECODE_VARARG` and calls `EnumerateLiveSlots` (`:434-496`).
6. `EnumGcRefs` in `GcEnum.cpp` sets `ScanContext.stack_limit` to the register-display SP and passes the real callback context to the manager (`src/coreclr/nativeaot/Runtime/GcEnum.cpp:125-144`). The decoder callback reaches `GcEnumObject` (`:68-96`).
7. `CoffNativeCodeManager::UnwindStackFrame` consumes the block flags and, for `UBF_FUNC_REVERSE_PINVOKE`, decodes the reverse-P/Invoke frame slot. With `USFF_StopUnwindOnTransitionFrame`, the locked source returns at `:703-705`, before the AMD64 `RtlVirtualUnwind` path (`:732` onward).

C011EC19 therefore proves method lookup, unwind metadata consumption, GC-info lookup/decoding, genuine root callbacks, and stack-derived promotion. It does not claim a distinct caller frame or preserved-register restoration: the reverse-P/Invoke transition return is the exact next stop.

## First managed method metadata

The independent PE/map symbol identifies the method as `HostLogProof_HostLogProof_Program__ManagedMain`, also emitted as `ManagedMain`, at `0x10001C20`. This name is from the map, not fabricated from stripped NativeAOT method metadata.

For `ControlPC = 0x10001D3F`:

| Field | Value | Provenance |
|---|---:|---|
| Method-info pointer | `0x4E82538` | successful `FindMethodInfo` |
| Method start | `0x10001C20` | runtime-function `BeginAddress` |
| Method end | `0x10001E84` | runtime-function `EndAddress` |
| Span | `0x264` bytes | runtime-function table |
| ControlPC offset | `0x11F` (`287`) | `GetCodeOffset` |
| Runtime-function entry | RVA `[0x1C20, 0x1E84)` | PE exception directory |
| Unwind-info RVA | `0x106320` | PE exception directory |
| Unwind-info address | `0x10106320` | fixed-base PE/ELF image |
| Unwind-info size | `0x16` (`22`) | NativeAOT blob header |
| GC-info address | `0x10106337` | blob end plus block metadata |
| EH-info | absent | block flags have no `UBF_FUNC_HAS_EHINFO` |
| Block flags | `0x08` | `UBF_FUNC_REVERSE_PINVOKE`, root-kind bits |
| Frame pointer | absent; result `0` | `GetFramePointer`, no EH/funclet |
| Prologue | `0x10` bytes | unwind header |
| Region | body, not prologue/epilogue | offset `0x11F` |
| Interruptibility | decoder result false; no ranges | `DECODE_INTERRUPTIBILITY` |
| GC refs | expected and reported at this active-frame decode | `EnumerateLiveSlots` returned success |
| Method flags | `executionAborted=false`; root method; reverse-P/Invoke | `FindMethodInfo` and block flags |

The final PE payload contains unwind bytes:

`01 10 09 00 10 A2 0C 30 0B 60 0A 70 09 C0 07 D0 05 E0 03 F0 01 50`

The GC-info prefix begins:

`81 04 B2 02 E0 56 1F 5A 48 82 8A 2F E1 10 E4 D1 4A 3A 19 05 57 80 1F EE B8 E5 A6 E8 08 90 20 D0 32 E9 7C BE 57 2B D7 C3 E9 FD FE 70 3F C0 00 00`

## Unwind result

Per fresh boot, the allocation-free scalar boundary recorded:

- unwind entered: `1`
- input `ControlPC/SP/FP`: `0x10001D3F / 0x4E82B80 / 0x4E82C10`
- method info: `0x4E82538`
- unwind info: `0x10106320`, 22 bytes, block flags `0x08`
- unwind completed/result: `1 / 1`
- `RtlVirtualUnwind` calls: `0`
- preserved registers restored: `0`
- previous transition frame: `0`
- caller fields at the bounded return: `ControlPC=0x10001D3F`, `SP=0x4E82B80`, `FP=0x4E82C10`

Those caller fields are the transition-frame inputs recorded at the exact early return; they are not presented as a new caller frame. The genuine next operation is the locked reverse-P/Invoke transition stop, reason `C0190002`.

## GC-info and root semantics

Per fresh boot:

- GC-info lookup: `1`, pointer `0x10106337`
- safe point: `0x10001D3F`
- code offset: `0x11F`
- decode attempts/result: `1 / 1`
- interruptible / interruptible ranges: `0 / 0`
- GC-info root reports: `4`
- register roots: `3`
- stack roots: `1`
- first decoder-reported register slot/value: `0x4E82B38 / 0x100A02FF0`
- first decoder-reported stack slot/value: `0x4E82BB8 / 0x100A01F38`
- stack frames walked: `1`
- stack-provider callbacks: `1`
- total root slots visited: `6` (the four C19 stack/register-map roots plus the two earlier provider visits)
- thread-stack root count: `4`

The four category-3 callbacks are the first authentic stack-derived root sequence. No arbitrary stack word was classified as a root; each slot came from `GcInfoDecoder::EnumerateLiveSlots`.

## Promotion chronology and queue

The C011EC18/C011EC15 counters retain their original meaning. The first two promotions are earlier provider activity: one non-null inline ThreadStatic/storage-object root and one ordinary-provider null root. They are not stack-derived.

The chronological C011EC19 sequence is:

1. Inline ThreadStatic provider: non-null storage object `0x100A02F50`; original queue insertion, slot `0x10230560`, cursor `0 -> 1`, value `0 -> 0x100A02F50`.
2. Ordinary provider: null root; no queue insertion.
3. NativeAOT GC-info decoder: three register roots, then one stack root. Each category-3 root entered and returned from `Promote`.
4. The four stack-root promotions produced four later queue insertions. The first recorded later insertion was slot `0x10230580`, cursor `4 -> 5`, old `0`, new `0x100A01F38` (the sentinel).

Counts are therefore:

- total `Promote`: `6` entries / `5` returns at the transition stop
- first stack-derived `Promote`: `4` attempts / `4` entries / `4` returns
- retained C011EC15 legacy second-Promote threshold: `4` attempts / `4` entries; this is the historical non-null-after-first-callback threshold crossed by the same four category-3 roots, not a replacement for the explicit C19 stack-derived counters
- second queue insertion count: `4` (this means insertions after the original queue insertion, not a claim that only one stack root existed)
- mark writes: `0`
- child reads: `0`
- graph traversal: `0`
- restart/resume: `0 / 0`

Existing object graph integrity remained: sentinel `0x100A01F38`, storage object `0x100A02F50`, original queue slot `0x10230560`, cursor `1`, and original queue value `0x100A02F50`.

## Runtime state, bounds, and allocation discipline

The final C19 record reports current, enumerated, initiator, and ThreadStore-owner Thread identity all as `0x3946C00`; ThreadStore recursion is `1`, EE is suspended, managed entry is prohibited, and the thread is cooperative (`cooperative=1`, `preemptive=0`). Restart/resume are `0 / 0`.

The first inline provider callback observes `thread_under_crawl=0` because locked `gcenv.ee.cpp:118` assigns `sc->thread_under_crawl` only immediately before `Thread::GcScanRoots`. The post-assignment crawling Thread is the enumerated/current Thread `0x3946C00`, and the genuine stack walk uses that Thread. This timing is retained rather than relabeled as a missing stack walk.

The C19 scalar record contains `stackBase=0`, `scanContextStackLimit=0`, and `stackBoundsConsumed=0`. Its diagnostic `stackLimit=0x3946BE0` is the existing root-record high-field snapshot; it was not consumed by this frame. The locked GC-info path reports non-interior roots here, so `PromoteCarefully` does not read the NativeAOT thread stack bounds. `GcEnum.cpp:137` assigns `ScanContext.stack_limit` from the current SP, but the first frame does not consume that field for these non-interior decoder roots. No stack-bound redesign was made.

Instrumentation remained fixed-storage, scalar, allocation-free, and deterministic. No managed re-entry, diagnostic allocation, scheduler mutation, or heuristic unwind was introduced. Sensitive-path allocation attempts: `0` observed after EE suspension.

## PE-to-ELF audit

The converter was not changed. The final PE/ELF inspection showed:

- PE image base and ELF fixed base: `0x10000000`
- first ELF `PT_LOAD` retains the PE headers at the fixed image base
- PE `.pdata` runtime-function table remains addressable at the expected image geometry
- the final runtime-function entry still resolves `[0x10001C20, 0x10001E84)` and unwind RVA `0x106320`
- the converted fixed-base address resolves to unwind `0x10106320` and GC-info `0x10106337`
- ELF is `ET_EXEC`, AMD64, with no relocations and no section table, as expected by the loader

C011EC19 provides no evidence of a PE-to-ELF loss or relocation defect. The concrete harness defect found during bring-up was only an incorrect archive member removal path; it was corrected to the actual `...\__\windows\CoffNativeCodeManager.cpp.obj` member so the specialized C19 object replaced the stock object.

## Reproducibility and hashes

Evidence root: `out/dotnet/gc-stack-provider-unwind-gc-info/run-20260816-134439895`

QEMU: `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`, three fresh processes, all Outcome A with identical semantic checkpoints.

Serial SHA-256 values:

1. `E19719F2DB7C0093027F092F89CDDF610F95ED0A946D601FE032FE2729FDCBE1`
2. `C7D91080FBC200A93349315F7083D18511E7D4FFAC1CDC3AC2562473988F75B3`
3. `FAF95FD9C926E59FE405FC01ECBC22428BD627165BD0C679461DE04CE0AC6961`

Payload SHA-256 values:

- PE: `8F4F14C9A2F8E21E0BC4602929F89A35F0F3BFB340594AF0B03A559C9951CC8E`
- ELF: `819FE97D4FB03A0C062437BB88FB9EA519C4A3D29D340D700932A1AD80EA546E`
- proof kernel: `7DB098B90E8FF0A5697B38881D4A1A3F34B1A36928AABEAD3825AC71B6DEF17F`

The harness emitted `C011EC19-PREFLIGHT` and the genuine `C011EC19` marker on all three boots. No fail-fast occurred. All QEMU processes were cleaned up.

## Validation and next milestone

- PE-to-ELF geometry unit test: PASS (`1/1`)
- PowerShell harness parse and source-injection guards: PASS during the runnable proof
- `git diff --check`: PASS (only normal Git LF-to-CRLF warnings)
- ordinary kernel and ESP restoration: PASS; both restored to the expected SHA-256 above
- proof payload is no longer staged in ESP; current kernel and ESP both hash to the ordinary baseline
- no C011EC17/C011EC18 history was rewritten

The smallest next milestone is to consume the reverse-P/Invoke transition-frame result as a distinct caller state, then test the non-transition `RtlVirtualUnwind` path (including real preserved-register restoration) before expanding graph traversal or queue draining.

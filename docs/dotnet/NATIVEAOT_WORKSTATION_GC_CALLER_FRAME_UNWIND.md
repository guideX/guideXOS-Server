# C011EC20 — NativeAOT caller-frame unwind boundary

Date: 2026-08-16
Repository HEAD at start: `49c260438b6ee539f0a212514812faa3da417c30`
Result: **Outcome C — transition crossing is the blocker**

## Locked identity and starting boundary

The experiment retained NativeAOT `9.0.0`, AMD64, Workstation GC, runtime interfaces
`5.3 / 2`, and locked source commit
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

C011EC19 remains the historical starting boundary. Its first managed ControlPC was
`0x10001D3F`, with method interval `[0x10001C20, 0x10001E84)`, method-info pointer
`0x4E82538`, unwind metadata at `0x10106320` (22 bytes, flags `0x08`), and GC-info
at `0x10106337` (offset `0x11F`, non-interruptible). It proved one frame, one stack
provider callback, six roots, four register roots, one stack root, and four authentic
category-3 GC-info root callbacks.

The C011EC19 chronology is preserved: the original queue transition was `0 -> 1`
at `0x10230560`, and the later first stack-derived insertion was `4 -> 5` at
`0x10230580`. Mark writes, child reads, and graph traversal remained zero.

## Locked decision tree traced

The authoritative NativeAOT path was traced rather than inferred:

* `CoffNativeCodeManager::FindMethodInfo` — locked `CoffNativeCodeManager.cpp:254`.
* `CoffNativeCodeManager::UnwindStackFrame` — locked `CoffNativeCodeManager.cpp:651`.
* Reverse-P/Invoke handling and the `USFF_StopUnwindOnTransitionFrame` decision —
  locked `CoffNativeCodeManager.cpp:671-707`.
* AMD64 `RtlVirtualUnwind` call and nonvolatile-register context wiring — locked
  `CoffNativeCodeManager.cpp:735-842`, with the ordinary call at `778`.
* Iterator continuation — locked `StackFrameIterator.cpp:1523-1590`; it calls the
  code-manager unwind and then either follows `m_pPreviousTransitionFrame` or
  advances the ordinary managed/native path.

The C011EC19 safe stop was an intentional harness stop at the reverse-P/Invoke
decision. C011EC20 removed that injected return and attempted controlled continuation.

## Transition evidence

C011EC19's independently recorded transition-associated frame was:

| Field | Value |
|---|---:|
| transition kind | reverse-P/Invoke (`UBF_FUNC_REVERSE_PINVOKE`, type `1`) |
| frame address | `0x4E83B18` |
| saved RIP | `0x10001D3F` |
| saved SP | `0x4E83B80` |
| saved FP | `0x4E83C10` |
| thread | `0x3947C00` |
| flags | `0x80F7` |
| previous transition frame | `0x0` |

On the controlled C011EC20 continuation, the locked reverse-P/Invoke lookup returned
`PInvokeTransitionFrame* == nullptr`. The C20 trace therefore recorded one crossing
attempt but zero valid crossing results:

```text
frameType=0x1 frame=0x0 savedRIP=0x0 savedSP=0x0 previous=0x0
crossingAttempts=0x1 crossingResults=0x0
```

The harness now stops at this exact condition with reason `C0200003` rather than
feeding a fabricated or guessed frame into ordinary unwind. This is Outcome C.

## Ordinary-unwind probe and why it is not proof

Before the guard was added, a controlled exploratory continuation reached the real
AMD64 call boundary. It captured:

```text
input ControlPC = 0x10001D3F
input RSP       = 0x4E83B80
input RBP       = 0x4E83C10
runtime-function pointer = 0x1024E288
unwind-info pointer      = 0x101075A0
```

The call did not return to the instrumentation boundary and the resulting execution
entered invalid control flow. No output register state from that probe is accepted as
evidence. It demonstrated why bypassing the missing transition frame would violate
the milestone's no-heuristic/no-fake-caller requirement.

Therefore the final C20 proof runs report:

* first non-transition ControlPC/SP/FP: not recovered;
* runtime-function/unwind-info output: not accepted;
* `RtlVirtualUnwind` calls: `0` in the classified final runs;
* caller code-manager/FindMethodInfo/GC-info: not reached;
* restored nonvolatile registers: not reached.

## Final C20 marker and root semantics

The final marker is `C011EC20-SAFE_STOP`, with `outcome=0x00000003` and
`safeStopReason=C0200003`. It retains the C011EC19 evidence immediately before the
blocker:

* frames walked: `1`;
* stack-provider callbacks: `1`;
* current-frame category-3 roots: `4` (three register roots and one stack root);
* total roots: `6`;
* current-frame Promote attempts/entries/returns: `4 / 4 / 4`;
* caller-frame roots and Promote activity: `0 / 0`;
* queue chronology: unchanged from C011EC19;
* mark writes: `0`;
* child reads: `0`;
* graph traversal: `0`.

Stack bounds were observed as stack base `0`, stack limit `0x3947BE0`, and scan-context
stack limit `0`; `stackBoundsConsumed=0`. They were not redesigned or consumed by the
new blocker.

ThreadStore/EE invariants remained `threadStoreRecursion=1`, `eeSuspended=1`, and
`managedEntryProhibited=1`; sensitive-path allocations observed: `0`.

## Reproducibility and hashes

Three fresh QEMU 11.0.0 boots all classified Outcome C with the same semantic
checkpoint. Serial hashes were:

```text
first-run  2F5DC5AE8DA4F49DEDA9D696B80C1E029CE0F7C124C1935EAB6C7DF36097EEE1
repeat-1   045EB54E9208132E7789218D9D81587149F8B479EF85F6543C1F13F2031812D0
repeat-2   8CD2C80ECC6152445E9427737333C8AB3AA67B5528477C3506B0BB9BB1A3676C
```

Final evidence directory:
`out/dotnet/gc-stack-provider-unwind-caller-frame/run-20260816-145306889/`

Latest proof artifact hashes:

```text
PE   4F165255C00198BE3F178D4A0A751CF3A75E063E62ABAFA874937E8DA6473CD8
ELF  7B27A1EBC1F1F82FD2EDA906D4E6B1BC0B166EA7DDCC784C95C609CD9C73924E
MAP  97E5DD9A1E0EB9FD8BCD4E71F304EC86C06F63FA68F07F48335A4FDC1F8EEE8C
```

The PE→ELF converter and preserved `.pdata`/`.xdata` relationship were not changed;
the converter and source-injection guards passed. The ordinary kernel and ESP were
restored and verified to hash to:

`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`

All QEMU processes were terminated after validation. The proof artifact remains in
the ignored evidence tree for review; no proof kernel remains deployed in the normal
kernel or ESP locations.

## Validation and Git

PowerShell parsing, source-injection guards, converter execution, three fresh QEMU
boots, ordinary restoration, and `git diff --check` passed. C011EC19, C011EC18, and
C011EC15 chronology remained intact. No C011EC20 success marker was emitted.

Tracked source changes are limited to the C20 diagnostics structure, platform
instrumentation, and the QEMU smoke harness, plus this document. The next smallest
milestone is to characterize and repair the locked reverse-P/Invoke transition-frame
exposure—specifically why the valid C011EC19 stack-associated frame is not returned as
`m_pPreviousTransitionFrame` to `CoffNativeCodeManager::UnwindStackFrame`—before
attempting ordinary AMD64 unwind again.

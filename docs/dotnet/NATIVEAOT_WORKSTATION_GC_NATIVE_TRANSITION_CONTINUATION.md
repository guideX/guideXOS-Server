# NativeAOT Workstation GC - C011EC21 Native Transition Continuation

## Result

C011EC21 is a successful provenance/transition-contract milestone with Outcome E and success type 3. It proves the next NativeAOT continuation contract after the C011EC20 managed-frame unwind, but the linked native helper has no structural unwind metadata, so no native stack step is attempted.

The result is not a managed-range expansion, a heuristic stack walk, a managed-stack-bottom proof, or a reinterpretation of C011EC20.

## Locked identity and starting boundary

The runtime identity is unchanged: NativeAOT 9.0.0, AMD64, Workstation GC, interfaces 5.3 / 2, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

C011EC20 starts from `ManagedMain`, ControlPC `0x10001D3F`, method interval `[0x10001C20, 0x10001E84)`, input RSP `0x4E84B80`, and input RBP `0x4E84C10`. Its runtime-function entry is `0x1024F288`, image base `0x10000000`, unwind info `0x10107820`, size `0x16`, and flags `0x08`.

The authoritative C20 output remains RIP `0x1AE365`, RSP `0x4E84C20`, RBP `0x26635A`, establisher frame `0x4E84C20`, with restored RBX `0x1DB7D0`, RSI `0x1DB7E0`, RDI `0xA`, R12 `0x26635A`, R13 `0x3948CE0`, R14 `0x2662ED`, and R15 `0x3736353433323130`. `RtlVirtualUnwind` was called exactly once and succeeded.

## Native helper chain

Independent source, symbol, and disassembly evidence reconstructs:

```text
kernel::main
  -> nativeaot_pal_qemu_test::runFirstRealAllocation (public ABI adapter)
  -> (anonymous namespace)::runFirstRealAllocationImpl
  -> indirect ManagedMain call
```

The public wrapper at approximately `0x1AF060` tail-jumps to the implementation near `0x1ACFA0` in the historical C20 kernel. The recovered C20 RIP `0x1AE365` is the instruction after the indirect call at `0x1AE35E`, `call *0x388(%rsp)`, whose source statement is the `ManagedMain` call near line 2200 of `kernel/core/nativeaot_pal_qemu_test.cpp`.

In the final C21 linked kernel, the same source helper is symbolized at `0x1AD050`. The final run recovered RIP `0x1AE425`, call site `0x1AE41E`, and function offset `0x13D5`; the disassembly contains the same audited indirect-call shape. The changed linked addresses are a build-local effect of C011EC21 instrumentation. Symbol ownership and source provenance are unchanged.

The helper saves nonvolatile registers and reserves stack space, then uses RBP as a data pointer. It is not a leaf or frame-pointer/simple ABI frame. The helper is in `kernel.elf`, executable `.text`, and outside the managed interval `[0x10001000, 0x10050950)`.

## Transition topology and locked source rule

The final C21 run observed:

| Field | Value |
| --- | --- |
| transition type | `PInvokeTransitionFrame` |
| reverse-P/Invoke type | `1` |
| transition address | `0x4E85B18` |
| saved RIP | `0x10001D3F` |
| saved SP | `0x4E85B80` |
| saved FP | `0x4E85C10` |
| previous transition | `0x0` |

The historical C20 run used the same topology at transition address `0x4E84B18`, with the same saved managed context.

Locked `CoffNativeCodeManager.cpp` reads the reverse-P/Invoke frame's previous-transition slot and returns it through `ppPreviousTransitionFrame`. Locked `StackFrameIterator.cpp` sets `UnwoundReversePInvoke` only when that predecessor is non-null. With a non-null predecessor and `SkipNativeFrames`, the iterator initializes from the predecessor; a top-of-stack marker can end the managed walk. With a null predecessor, it takes the ordinary native unwind/classification path.

Therefore null predecessor interpretation code `2` means: no older transition record was supplied; ordinary native unwind/classification is the next contract. It does not mean managed stack bottom. No guideXOS transition-linking defect was found.

## Native continuation result

The first and only candidate frame record is:

| Field | Value |
| --- | --- |
| index | `0` |
| RIP | `0x1AE425` |
| RSP | `0x4E85C20` |
| RBP | `0x26635A` |
| symbol | `runFirstRealAllocationImpl` |
| module / section | `kernel.elf` / `.text` |
| runtime-function | none in kernel `.pdata` |
| unwind info | none; no covering `.xdata` or `.eh_frame` FDE |
| unwind attempted | no; metadata blocker |
| caller RIP/RSP | not recovered |

The candidate is outside the managed interval, so production code-manager lookup and `FindMethodInfo` were not attempted. No later managed ControlPC was found. Managed-stack bottom is not proven because null predecessor alone is not that proof. No additional native frame was traversed, and no arbitrary stack memory was scanned.

## Retained GC chronology

C011EC21 retains the C011EC19/C20 chronology: one genuine managed frame, successful production `FindMethodInfo`, successful unwind and GC-info decode, four category-3 roots (three register and one stack), four stack-derived Promote attempts/entries/returns, and six total roots. The historical first stack-derived slot/value evidence is retained. The later queue insertion remains `4 -> 5`; mark writes, child reads, and graph traversal remain zero.

Final C21 accounting is frames walked `1`, stack-provider callbacks `1`, total roots `6`, Promote entries `6`, Promote returns `5`, mark writes `0`, child reads `0`, graph traversal `0`. Stack bounds remain diagnostic only: stack base `0`, final run diagnostic limit `0x3949BE0` (historical C20 diagnostic `0x3948BE0`), `ScanContext.stack_limit=0`, and `stackBoundsConsumed=0`.

The post-suspension path performs only bounded scalar/pointer diagnostics. It introduces no allocations, dynamic strings, collections, managed re-entry, scheduler transitions, arbitrary scans, or large dumps.

## Locked NativeAOT source references

The proof uses the locked source from commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`:

* `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp`: transition continuation around lines 1529-1600, native-frame classification around 1720 and 1913-1948, and managed stack iteration.
* `src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp`: `FindMethodInfo` around lines 254-295, transition unwind around 651-711, and AMD64 unwind/register restoration around 778-842.
* `src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp`: managed-range and code-manager ownership around lines 96-109.
* `src/coreclr/nativeaot/Runtime/thread.cpp`: GC root iterator entry path.
* `kernel/core/nativeaot_pal_qemu_test.cpp`: helper, wrapper, and indirect managed-entry call.

## Reproducibility and hashes

Final evidence root: `out/dotnet/gc-stack-provider-native-transition-continuation/run-20260818-071738251`.

The harness passed three fresh QEMU 11.0.0 boots, all with Outcome E and marker `C011EC21`. Serial SHA-256 values were:

```text
AA6AED37E7939C3596B1C2414453BBB3B6295B7455E560EC4DAC8949C6EEF96D
9D717792F8AC5BC5976AA033A1E68420A28BB1B711623D1631B7A85702950D49
2808E34C043034267D30020282946C1CC72B4E7E972E4ECF8C6AA7DB8862AE94
```

Payload hashes:

```text
PE     6C634EC55E47CFB16418E96C4E8FF7D01A003E1EABDDF5B285337F84439990B8
ELF    497D1C33C0670941BF1CB98C36B3E62EADC14149732B47EC486682B279FF67F6
MAP    28ED05FE927937E60B036C9A68F2CC8710F54A567A5AAD755F14BE46D617F4F8
proof  55888209238E9597D9BF8EF7E348E700D2D8FF77C69DC4CA071D9E397D8E72BA
```

The converter, PowerShell parse, source guards, manifest checks, ordinary-payload restoration, serial checkpoints, and `git diff --check` passed. The ordinary kernel and ESP were restored to SHA-256 `161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`. No QEMU process remained after cleanup.

## Git and next milestone

C011EC20 history was not rewritten. The final commit and push state are recorded in this section after the C011EC21 milestone commit.

The next smallest milestone is to provide or implement the exact NativeAOT native unwind/transition record required to continue from `runFirstRealAllocationImpl`, with independently valid metadata for every native step. Do not widen the managed range or add a heuristic walker.

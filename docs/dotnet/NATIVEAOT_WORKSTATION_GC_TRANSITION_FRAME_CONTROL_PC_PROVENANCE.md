# C011EC18 — NativeAOT Workstation GC transition-frame control-PC provenance

## Result

Outcome A. The C011EC17 null-manager boundary was crossed naturally after the
guideXOS allocation interposer was removed from the NativeAOT `RhpNewArray`
entry contract. The locked stock AMD64 NativeAOT entry now constructs the
transition frame directly. The saved `PInvokeTransitionFrame::m_RIP` was
passed to `StackFrameIterator`, `CoffNativeCodeManager` lookup succeeded, and
`FindMethodInfo` succeeded. The run then stopped at the real C011EC15 next
provider boundary.

The repair does not widen the managed range, clamp a PC, fabricate a PC, skip
`IsManaged`, skip `GetCodeManagerForAddress`, or provide a custom stack walker.

## Identity and repository baseline

The locked identity was unchanged:

| Item | Value |
|---|---|
| NativeAOT | 9.0.0 |
| Architecture | AMD64 |
| GC | Workstation |
| Runtime interfaces | 5.3 / 2 |
| NativeAOT source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |
| Production manager | `CoffNativeCodeManager` |

Starting repository state was branch `v1.1_DOTNET_SUPPORT`, HEAD
`e28779207a97409ed023e63aedd2c1a76f22ae27`, upstream
`origin/v1.1_DOTNET_SUPPORT`, ahead 1 / behind 0, with a clean tracked
worktree and no untracked entries. The pre-commit evidence run intentionally
recorded the three task files as the only working changes. No prior commit was
amended, reset, rebased, restored, discarded, or rewritten.

## Exact ownership of the C011EC17 failing addresses

The C011EC17 linked image used image base `0x10000000`.

| Address | PE RVA | PE section / offset | Ownership |
|---|---:|---|---|
| `0x100547F6` | `0x547F6` | `.text`, offset `0x537F6` from `.text` VMA `0x10001000` | guideXOS C++ `RhpNewArray` interposer, map symbol `0x10054350` |
| `0x100547EB` | `0x547EB` | `.text`, offset `0x537EB` | same guideXOS C++ `RhpNewArray` interposer |

The PE map proved:

```text
.managedcode$A  0x10001000  size 0x20
.managedcode$I  0x10001020  size 0x4F930
.managedcode$Z  0x10050950  size 0x8
.text           0x10050958  size 0x578
RhpNewArray     0x10054350
guideXosStockRhpNewArray 0x1005AB70
RhpNewArrayRare 0x1005ABE0
```

Therefore the legitimate managed range remained `[0x10001000,
0x10050950)`, and `0x100547F6 >= 0x10050950` was not managed code. The
converted ELF has no section headers; its executable `R-E` `LOAD` segment is
VA `0x10001000`, file offset `0x2000`, so the ELF classification is the
executable load segment rather than a named ELF section.

The disassembly around the two addresses was:

```text
0x100547E6: call 0x10053750       ; recordAllocationStage
0x100547EB: mov  rdx,r14
0x100547EE: mov  rcx,r15
0x100547F1: call 0x1005AB70       ; guideXosStockRhpNewArray
0x100547F6: mov  r12,rax
```

`0x100547EB` was the value published by `recordAllocationStage` through
`_ReturnAddress()`: it is the continuation after the diagnostic call, not an
interrupted managed instruction. `0x100547F6` is the return continuation after
the C++ interposer calls stock `RhpNewArray`; it is native guideXOS allocation
wrapper code. It is not a transition stub, reverse-P/Invoke wrapper,
suspension wrapper, GC probe, managed return address, or valid omitted managed
code.

## NativeAOT setup and transition-frame contract

The locked NativeAOT path is:

```text
managed NativeAOT RhpNewArray
  -> AMD64 PUSH_COOP_PINVOKE_FRAME
  -> RhpGcAlloc(MethodTable*, flags, elements, PInvokeTransitionFrame*)
  -> Thread::SetDeferredTransitionFrame
  -> GcAllocInternal / Workstation GC
  -> SuspendEE / Thread::GcScanRoots
  -> Thread::GetTransitionFrame
  -> StackFrameIterator(Thread*, PInvokeTransitionFrame*)
  -> InternalInit
  -> SetIP(frame->m_RIP), SetControlPC
  -> CalculateCurrentMethodState
  -> GetCodeManagerForAddress / FindMethodInfo
```

The guideXOS C++ `RhpNewArray` interposer violated the stock assembly
contract: `PUSH_COOP_PINVOKE_FRAME` records the immediate caller as the
managed frame. With the interposer present, the immediate caller was native
guideXOS code, so the iterator was seeded with `0x100547F6` and stopped at a
null manager. The fix enables the reusable
`GUIDEXOS_NATIVEAOT_USE_STOCK_RHP_NEW_ARRAY_ENTRY` contract option and links
the locked stock `RhpNewArray` entry directly. C011EC18 evidence hooks remain
allocation-free observers around the locked path.

For AMD64, the locked `PInvokeTransitionFrame` layout is:

| Field | Offset | Final observed value | Writer / reader |
|---|---:|---:|---|
| `m_RIP` | `0x00` | `0x10001D3F` | stock `PUSH_COOP_PINVOKE_FRAME`; `InternalInit` reads it as IP/control PC |
| `m_FramePointer` | `0x08` | `0x4E80C10` | stock frame push; `InternalInit` seeds RBP |
| `m_pThread` | `0x10` | `0x4E80B80` | stock macro temporary slot; stack crawler does not use it |
| `m_Flags` | `0x18` | `0x80F7` | stock frame push; iterator selects saved registers/RSP |
| `m_PreservedRegs` | `0x20` | frame payload | stock pushes RBX, RSI, RDI, R12, R13, R14, R15, RAX, RSP |

The frame address was `0x4E80B18`. The actual NativeAOT `Thread*` supplied by
`ThreadStore::GetCurrentThread()` was `0x3944C00`; this is distinct from the
unused `m_pThread` macro slot value. The thread layout used for transition
selection was `m_ThreadStateFlags=0x38`, `m_pTransitionFrame=0x40`,
`m_pDeferredTransitionFrame=0x48`, `m_pCachedTransitionFrame=0x50`, and
`m_pNext=0x58`. The prior transition frame was null. During suspension,
`GetTransitionFrame` selected the deferred frame at `0x4E80B18`; the live
cooperative `m_pTransitionFrame` was not treated as the suspended frame.

The final preflight values were:

```text
frame             = 0x0000000004E80B18
current native RIP= 0x0000000010058EB8  (inside locked RhpGcAlloc)
return-slot proxy = 0x0000000004E80A88  (_AddressOfReturnAddress diagnostic slot)
transition RIP    = 0x0000000010001D3F
transition RBP    = 0x0000000004E80C10
saved RSP         = 0x0000000004E80B80
flags             = 0x00000000000080F7
previous frame    = 0x0000000000000000
current native in managed range = 0
current native manager           = 0x0000000000000000
transition manager               = 0x000000001021CFA0
```

The fixed diagnostic return-slot proxy is reported separately from the
structural suspended-context RSP. The iterator’s actual initial SP came from
the saved transition-frame RSP, `0x4E80B80`.

Saved preserved-register values were:

```text
RBX=0x0000000100A02FF0  RSI=0x0000000100A04020
RDI=0x0000000100A05038  R12=0x0000000000000001
R13=0x0000000000000001  R14=0x0000000000000100
R15=0x0000000000000025
```

## Recovered managed PC and iterator result

The authentic managed control PC was `0x10001D3F`, directly from
`PInvokeTransitionFrame::m_RIP`. It has structural provenance from the stock
AMD64 NativeAOT frame construction; it was not inferred from stack scanning or
fabricated from a range check. It is inside the real registered range
`[0x10001000, 0x10050950)`.

The C011EC18 evidence showed:

```text
iterator initial ControlPC = 0x0000000010001D3F
iterator initial SP        = 0x0000000004E80B80
iterator initial FP        = 0x0000000004E80C10
code manager               = 0x000000001021CFA0
method metadata pointer    = 0x0000000004E80538
FindMethodInfo             = success
frame-pointer calculation  = reached once; result 0
unwind step                = not reached before the intentional C011EC15 stop
stack frames               = 1
direct C011EC18 marker     = emitted
```

`GetCodeManagerForAddress(0x10001D3F)` returned the registered production
`CoffNativeCodeManager` at `0x1021CFA0`. `FindMethodInfo` was attempted once,
returned true once, and produced non-null method metadata. The marker was
emitted only after the manager identity, managed-range membership, and method
metadata conditions all succeeded.

## GC evidence retained at the next genuine boundary

The real C011EC15 safe-stop was reached after the managed iterator boundary:

| Evidence | Value |
|---|---:|
| stack-provider callbacks | 1 thread-stack callback |
| thread-stack root slots | 1 |
| all root slots visited | 3 |
| first Promote entries | 2 |
| Promote returns | 1 |
| second Promote attempts / entries | 0 / 0 |
| second queue insertions | 0 |
| queue slot | `0x1022E410` |
| queue slot index | 0 |
| queue cursor before / after | 0 / 1 |
| queue old / new | `0x0` / `0x100A02F50` |
| sentinel | `0x100A01F38` |
| storage object | `0x100A02F50` |
| first root raw value | `0x100A02F50` |
| next root raw value | `0x100A02FF0` |
| mark-bit writes | 0 |
| child-reference reads | 0 |
| graph traversal | 0 |

The original queue insertion remains ThreadStatic/storage-object evidence. It
was not relabeled as stack-root evidence. ThreadStore lock ownership was 1,
EE suspension was 1, managed entry was prohibited, and restart/resume counts
were both 0.

Stack bounds remained zero and were not consumed:

```text
stack base/low       = 0
stack limit/high     = 0
ScanContext limit    = 0
c18StackBoundsConsumed = 0
```

This is not bundled into C011EC18 as a stack-bound redesign. It is the next
smallest boundary after transition provenance and method lookup.

## Validation

The final proof run was `run-20260816-120638222` using QEMU 11.0.0
(`v11.0.0-12122-ga4bb4b10c9`) and three fresh boots. All three runs emitted
`C011EC18-PREFLIGHT`, `C011EC18-ITERATOR`, successful manager lookup,
successful `FindMethodInfo`, direct `C011EC18`, and `C011EC15`.

Proof payload hashes:

```text
PE  NativeAotGcSingleThreadSuspendEe.exe = F773E4D996DC6600969F02519FBC9D0BAFCAA1C322A963783653599D60E50A3C
ELF NativeAotGcSingleThreadSuspendEe.elf = 85A3E32623E56FB43845119F17FCAB1D5982EB95250E109ECCFB6F8A2A66AAB4
kernel.elf                              = 0F4CAAA3B5E67E67864A6B6F9856A202131FC2F62A1E2E911C2ADE109449D7AF
serial 1                                = 88CC6DFE43597FEDA37F55E1548704D492E06B23083D49AFA12C755064FA63D7
serial 2                                = 6F95A29C3F3273346ED7F27DBDBFC002DA58B4D09A8B7758E6E48476AFEE4398
serial 3                                = BADB5911B75D8D552BE3948EACD78B16223BB99BFA2971DCE7E928CDC8293FF7
```

Serial hashes differ because of IRQ interleaving, but the semantic C011EC18
fields were deterministic across all three boots. The ordinary payload was
restored by the harness finalizer and verified for both kernel and ESP:

```text
expected ordinary kernel = 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ordinary kernel after     = 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
ordinary ESP after        = 161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550
```

No QEMU instances remain. C011EC17 remains historical Outcome D evidence: its
code-manager registration succeeded, and its null-manager result was caused by
the out-of-range wrapper continuation, not by failed registration.

## Changed files and handoff

The implementation consists of bounded C011EC18 diagnostics in
`guidexos_nativeaot_allocation_diagnostics.h` and
`guidexos_nativeaot_platform.cpp`, the stock-entry/runtime-pack and QEMU proof
harness wiring in
`scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, and this
document. The final Git commit, push result, final ahead/behind, and clean
tracked-worktree verification are recorded in the task handoff immediately
after this document is staged. The next smallest milestone is to consume the
first real NativeAOT unwind/GC-info boundary after C011EC15, without widening
the managed range or redesigning stack bounds.

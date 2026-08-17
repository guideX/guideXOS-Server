# C011EC20 - NativeAOT non-transition unwind and caller-frame restoration

Date: 2026-08-17

Result: Outcome E. The locked NativeAOT stack walk crossed the intentional
C011EC19 reverse-P/Invoke safe-stop boundary, consumed the real AMD64 unwind
metadata, invoked the existing `__imp_RtlVirtualUnwind` call boundary through
the guideXOS PAL adapter, restored seven encoded nonvolatile registers, and
recovered a distinct native caller frame. The caller is not managed, so no
caller GC-info or caller root enumeration was attempted.

The central answer is therefore yes for the first ordinary AMD64 unwind and
caller-frame restoration, and no caller managed-frame proof is claimed yet.

## Starting boundary and locked identity

C011EC19 remains historical evidence. Its first managed ControlPC was
`0x10001D3F`, independently owned by `ManagedMain`, with method interval
`[0x10001C20, 0x10001E84)`, historical method-info pointer `0x4E82538`,
unwind metadata `0x10106320` of 22 bytes with block flags `0x08`, and GC-info
at `0x10106337` with ControlPC offset `0x11F`, non-interruptible. It proved
one frame, one provider callback, six roots, four category-3 roots, three
register roots, and one stack root.

The locked identity was unchanged: NativeAOT `9.0.0`, AMD64, Workstation GC,
runtime interfaces `5.3 / 2`, source
`9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

The repository was clean at the actual starting HEAD
`6cb7a563fe3cb0326aef66a7a5e9d3c79bda15a0` on branch
`v1.1_DOTNET_SUPPORT`, synchronized with `origin/v1.1_DOTNET_SUPPORT`.
The user-supplied older expected HEAD was not restored or rewritten.

## Locked decision tree

The source trace used the locked NativeAOT source, not diagnostic inference:

* `StackFrameIterator::InternalInit`, locked
  `src/coreclr/nativeaot/Runtime/StackFrameIterator.cpp:134-330`, seeds the
  register display and the current transition-frame pointer.
* `StackFrameIterator::NextInternal`, `:1529-1600`, selects
  `USFF_StopUnwindOnTransitionFrame` for `SkipNativeFrames`, calls
  `GetCodeManager()->UnwindStackFrame`, follows a non-null previous transition
  frame when one is returned, and otherwise advances through the ordinary
  managed/native path.
* `StackFrameIterator::CalculateCurrentMethodState`, `:1913-1948`, performs
  the managed-range/code-manager and method-state classification.
* `CoffNativeCodeManager::FindMethodInfo`, locked
  `src/coreclr/nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:254-300`,
  owns runtime-function/method-info lookup. `GetFramePointer` is `:323-337`,
  `GetCodeOffset` is `:375-398`, and `EnumGcRefs` is `:434-496`.
* `CoffNativeCodeManager::UnwindStackFrame`, `:651-842`, first reads the
  unwind blob and block flags. For `UBF_FUNC_REVERSE_PINVOKE` it decodes the
  reverse-P/Invoke frame slot, loads the previous `PInvokeTransitionFrame`,
  and honors `USFF_StopUnwindOnTransitionFrame` at `:671-707`.
* For the ordinary path, the same function builds the AMD64 `CONTEXT` and
  `KNONVOLATILE_CONTEXT_POINTERS`, calls `RtlVirtualUnwind` at locked line
  `:778`, copies `RSP`/`RIP` back into the register display, and maps the
  preserved-register locations back at `:735-842`.
* `RuntimeInstance::IsManaged` and `GetCodeManagerForAddress`, locked
  `src/coreclr/nativeaot/Runtime/RuntimeInstance.cpp:96-109`, provide the
  independent caller ownership test.

The distinctions in this tree are explicit. Reverse-P/Invoke is the only
transition bit in this Windows code-manager path (`UBF_FUNC_REVERSE_PINVOKE`);
the decoder reads the embedded frame pointer and
`USFF_StopUnwindOnTransitionFrame` returns at that boundary. A P/Invoke
transition is represented by the `PInvokeTransitionFrame` consumed by
`StackFrameIterator::InternalInit`, `StackFrameIterator.cpp:134-330`, and by
the `m_pPreviousTransitionFrame` branch at `:1570-1592`; there is no second
P/Invoke flag that bypasses ordinary AMD64 unwind. A non-transition managed
frame takes the no-reverse-bit path and reaches `RtlVirtualUnwind`, while
`UBF_FUNC_KIND_ROOT/HANDLER/FILTER` and associated/EH-data bits affect method
metadata interpretation rather than selecting a different AMD64 register
restore primitive. After unwind, `CategorizeUnadjustedReturnAddress`,
`StackFrameIterator.cpp:2040-2075`, distinguishes managed code from the
universal-transition, throw-site, funclet-invoke, and filter-invoke thunks;
`UnwindNonEHThunkSequence`, `:1764-1814`, handles the universal-transition
thunk sequence. A recovered native helper has no `ICodeManager` and is
classified by `CalculateCurrentMethodState`, `:1913-1948`, as the native
branch; NativeAOT does not invent a managed frame for it. There is no separate
leaf/no-unwind success branch in this AMD64 manager: a recognized method still
uses the runtime-function/unwind-info path, and the ordinary call maps the
nonvolatile locations back through `CONTEXT_TO_REGDISPLAY`. With
`USFF_GcUnwind`, only the XMM copy is suppressed; the integer nonvolatile
restore mapping remains active.

The PE-to-ELF converter was not changed. The concrete adaptation gap was the
existing startup probe definition of `__imp_RtlVirtualUnwind` as null. The
probe now binds that existing import to an allocation-free exact Win64
`UNWIND_INFO` operation interpreter in
`tools/dotnet/runtime-pack/src/probes/guidexos_nativeaot_gc_startup_probe.cpp`.
The locked code-manager call site and decision tree remain the authority.

## Reverse-P/Invoke transition boundary

The transition frame type was `PInvokeTransitionFrame`, reverse-P/Invoke type
`1`, with `UBF_FUNC_REVERSE_PINVOKE` represented by block flags `0x08`.
Fresh proof-run addresses were:

| Field | Value |
| --- | ---: |
| current transition frame | `0x4E84B18` |
| saved RIP | `0x10001D3F` |
| saved SP | `0x4E84B80` |
| saved FP | `0x4E84C10` |
| thread pointer | `0x3948C00` |
| flags | `0x80F7` |
| previous transition frame | `0x0` |

The C011EC19 historical frame address was `0x4E83B18`; the difference is the
expected fresh stack placement. The locked reverse-P/Invoke decoder returned
the previous transition pointer as null. This is the legitimate top-level
reverse-P/Invoke result, not a missing current frame: C011EC19 had already
independently captured the current transition-associated frame. C20 recorded
one crossing attempt and one crossing result, retained the null previous-frame
value, and continued into the locked ordinary unwind path. No caller frame was
fabricated and no arbitrary stack range was searched.

## First non-transition frame and metadata

The current frame remained the distinct managed `ManagedMain` frame before
unwind. Its fresh-run method-info pointer was `0x4E84538` (stack-local
relocation of the historical `0x4E82538`), interval
`[0x10001C20, 0x10001E84)`, ControlPC `0x10001D3F`, SP `0x4E84B80`, and FP
`0x4E84C10`.

The first ordinary unwind used:

| Field | Value |
| --- | ---: |
| image base | `0x10000000` |
| runtime-function entry | `0x1024F288` |
| begin RVA | `0x1C20` |
| end RVA | `0x1E84` |
| unwind-info address | `0x10107820` |
| unwind-info size | `0x16` bytes |
| unwind block flags | `0x08` |

The runtime-function entry maps exactly to `ManagedMain` and its preserved
`.pdata`/`.xdata` relationship. The C011EC19 historical metadata addresses
remain `0x10106320` and `0x10106337`; the fresh artifact's relocated addresses
are recorded above.

## RtlVirtualUnwind evidence

The call was attempted once and returned once. The C20 scalar evidence was:

| Field | Value |
| --- | ---: |
| input RIP | `0x10001D3F` |
| input RSP | `0x4E84B80` |
| input RBP | `0x4E84C10` |
| input RBX | `0x100A02FF0` |
| input RSI | `0x100A04020` |
| input RDI | `0x100A05038` |
| input R12 | `0x1` |
| input R13 | `0x1` |
| input R14 | `0x100` |
| input R15 | `0x25` |
| output RIP | `0x1AE365` |
| output RSP | `0x4E84C20` |
| output RBP | `0x26635A` |
| establisher frame | `0x4E84C20` |
| handler data | `0x0` |
| result | `1` |
| `RtlVirtualUnwind` calls | `1` |
| `RtlVirtualUnwind` returned | `1` |
| raw `RtlVirtualUnwind` result | `0x0` |

The restored preserved registers were:

| Register | Restored value |
| --- | ---: |
| RBX | `0x1DB7D0` |
| RSI | `0x1DB7E0` |
| RDI | `0xA` |
| R12 | `0x26635A` |
| R13 | `0x3948CE0` |
| R14 | `0x2662ED` |
| R15 | `0x3736353433323130` |

`restoredRegisterCount=7`. The 22-byte unwind record contains eight integer
`UWOP_PUSH_NONVOL` dispositions: RBX, RSI, RDI, R12, R13, R14, R15, and RBP.
C20's preserved-register counter reports the seven RBX-through-R15 locations;
RBP is restored as the output frame register. The adapter records the actual
stack location for each restored nonvolatile in
`KNONVOLATILE_CONTEXT_POINTERS`; the values above are direct metadata-driven
restores, not guessed register values.

## Caller validation

The recovered caller SP changed from `0x4E84B80` to `0x4E84C20`, a positive
`0xA0` unwind distance, and remained 16-byte aligned. The caller RIP changed
from `0x10001D3F` to `0x1AE365`, so the frame is structurally distinct.

The caller was outside the registered managed range. Independent validation
returned:

* managed-range membership: false;
* code-manager pointer: null;
* caller `FindMethodInfo`: not attempted, correctly, because the caller is
  native;
* caller method interval and method-info pointer: not applicable;
* caller GC-info: not attempted;
* caller owner: native kernel helper
  `kernel::nativeaot_pal_qemu_test::(anonymous namespace)::runFirstRealAllocationImpl`
  (the kernel symbol table places the helper at `0x1ACFA0`; the recovered
  `0x1AE365` lies inside that native helper before the public wrapper at
  `0x1AF060`).

This is Outcome E: the native caller is legitimate, but another native-frame
or transition contract is required before a later managed frame can be
claimed. C20 therefore stops at `C0200005` after validating this caller.

## Root and queue semantics

C011EC19 chronology is retained and not renumbered:

1. inline ThreadStatic/storage-root activity;
2. the ordinary null root;
3. four current-frame category-3 GC-info roots.

The final C20 proof runs retained one walked frame, one stack-provider callback,
six total roots, four current-frame roots, three register roots, one stack
root, and four current-frame category-3 Promote attempts/entries/returns.
Caller-frame roots and caller-frame category-3 Promote activity were zero
because the recovered caller is native.

The existing queue/object evidence remains unchanged: sentinel
`0x100A01F38`, storage object `0x100A02F50`, original queue slot
`0x10230560`, original transition `0 -> 1`, later first stack-derived cursor
transition `4 -> 5`, mark writes `0`, child reads `0`, and graph traversal `0`.
The final marker also retained total Promote entries/returns `6 / 5`; this is
the historical aggregate and is not relabeled as caller activity.

## Bounds, EE invariants, and sensitive path

Stack base remained `0`, stack limit was `0x3948BE0`, and
`ScanContext.stack_limit` remained `0`. `stackBoundsConsumed=0`, so stack
bounds were not the blocker and were not redesigned.

ThreadStore/EE invariants were `threadStoreRecursion=1`, `eeSuspended=1`, and
`managedEntryProhibited=1`. Sensitive-path allocations observed after EE
suspension: `0`; diagnostics remained bounded and scalar/pointer oriented.

## Three fresh QEMU boots and hashes

Evidence directory:
`out/dotnet/gc-stack-provider-unwind-caller-frame/final3raw-20260817/run-20260817-111104493/`

All three QEMU 11.0.0 boots produced Outcome E, one crossing result, one
ordinary unwind attempt, one `RtlVirtualUnwind` call, caller RIP `0x1AE365`,
caller SP `0x4E84C20`, seven restored registers, one current-frame callback,
and the same root/queue checkpoint. Each boot emitted the preflight marker
`C011EC20-PREFLIGHT` and one proof marker `C011EC20`; no fail-fast occurred.

Serial SHA-256 values:

* first-run: `58CF7BD4524102B193456E111A7C2EF9F2A6D5F9465176BF7988EBC3D89BBE52`
* repeat-1: `CA3AC26DA2E736ADFEE9F74CD5896903713CB51C20EB66ADF468562A9E562F86`
* repeat-2: `85EA48701C7E653DC35AC373FEEDAA4F83BE98E1D5BDDA976A804CF3E850529D`

Final payload SHA-256 values:

* PE: `8AC06C7B71A191D3874A138C518CFF6515605DA804C1EB7D04E404986506AEEE`
* ELF: `3CB75C5832592E6DBDF0FC5B0B42CEBDEBAE2F657B7361BF8CDE356CCA78C5A0`
* MAP: `8ECFC34CF956F646A3C98E19126D80EA8B9E655CD7A6633F181B8BD4B74E7E48`
* proof kernel: `DCAD4C1A31435F019A4D94C8E96EC150E94C57C34D06379C2F9082080EAFC4E7`

The ordinary kernel and ESP were restored after the runs and both match:
`161B83E992154422665B59D7E10447D6CFDC1B1AE2B33F3B54E349C3E10AA550`.
No QEMU process remains and no proof payload remains in the ordinary kernel
or ordinary ESP.

## Validation, audit, and next milestone

PowerShell parse, source-injection guards, serial classification, manifest
JSON parsing, converter test (`tools/dotnet/test_pe_to_elf_v2_fixed_base.py`),
three fresh QEMU boots, ordinary restoration, and `git diff --check` passed.
The PE-to-ELF converter was unchanged; the audit found preserved PE headers,
`.pdata`, `.xdata`, runtime-function, unwind-info, and GC-info relationships.
Only the missing runtime import adapter for the already-present unwind data was
added.

The next smallest milestone is to characterize the legitimate native helper
continuation after `runFirstRealAllocationImpl` and expose the next valid
transition/native-frame contract. Only after that boundary should the walker
attempt to prove a distinct caller managed frame and caller GC-info.

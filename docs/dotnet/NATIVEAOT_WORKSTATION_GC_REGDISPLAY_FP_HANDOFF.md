# C011EC46 — NativeAOT Workstation GC REGDISPLAY FP Handoff

## Result

C011EC46 is Outcome A at Success Level 4. The reverse-P/Invoke caller-frame
handoff is repaired. The authentic unwind result now reaches the iterator's
REGDISPLAY, the `-0x70` reverse slot resolves from the caller logical frame,
C18 accepts valid managed state without source changes, and authentic root
scanning advances through `C011EC26 COMPLETE`.

The run stops at a later, independent relocation-root path: after root scan,
the retained relocation workload reaches `C011EC34-PREFLIGHT` and then hits a
kernel read page fault at `0xFFFFFFFFFFFFFF90`. No relocation, planner,
compaction, or managed-resume change was made to fold that later issue into
C46.

## Locked identity and preflight

- Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
- Branch: `v1.1_DOTNET_SUPPORT`
- Starting HEAD: `406edf4aea4b41478035c57b927cc4c497889c94`
- Starting subject: `Trace NativeAOT reverse-PInvoke unwind slot provenance`
- Upstream: `origin/v1.1_DOTNET_SUPPORT`
- Starting divergence: ahead 0 / behind 0
- Starting worktree: clean
- C45: present at starting HEAD and retained as the historical control
- C45 push state: the configured upstream was already at the same C45 HEAD;
  no C46 push was performed
- Final divergence: ahead 1 / behind 0 after the focused C46 commit
- Runtime: NativeAOT 9.0.0, AMD64, Workstation GC, GC interfaces 5.3 / 2
- Locked runtime source: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`

## Exact root cause

The defect is Code 1 — stale REGDISPLAY RBP pointer, with the associated
computed RBP value not being published through that pointer.

On AMD64, `REGDISPLAY.pRbp` is a pointer to register storage, not an RBP
value. `GetFP()` returns `*pRbp`; `SP` and `IP` are copied scalar fields.
`StackFrameIterator::InternalInit` initially points `pRbp` at the transition
frame's `m_FramePointer`. The old `CoffNativeCodeManager` path then used
`CONTEXT_TO_REGDISPLAY`, which assigns `pRegisterSet->pRbp` from
`contextPointers.Rbp`. For `UWOP_SET_FPREG`, Windows virtual unwind computes
the logical RBP in `context.Rbp`; `contextPointers.Rbp` is only a location
associated with the input register and is not the computed logical-frame
storage. The old path therefore left the iterator consuming the prior
physical-frame location.

The earliest correct value is `context.Rbp`, immediately after
`RtlVirtualUnwind` and before the RBP `CONTEXT_TO_REGDISPLAY` publication. The
old implementation loses the handoff at the nonvolatile-register promotion
around `CoffNativeCodeManager::UnwindStackFrame`, then the reverse-P/Invoke
consumer calls `GetFP()` on the stale representation.

The narrow repair is:

1. At `StackFrameIterator::Next`, re-home `m_RegDisplay.pRbp` to the
   iterator-owned `&m_FramePointer` before `NextInternal` invokes the code
   manager.
2. After virtual unwind and the normal nonvolatile-register reconstruction,
   publish the authentic `context.Rbp` through that incoming iterator-owned
   pointer and retain the pointer in `REGDISPLAY`.
3. Leave the reverse slot offset, transition-frame lifetime, SP, IP, and C18
   validation unchanged.

No address, symbol, frame name, `+0xA0`, transition-frame pointer, or slot
offset was special-cased.

## Representation and ownership

| State | Representation | Producer | Consumer | Lifetime |
| --- | --- | --- | --- | --- |
| Physical RBP register location | `REGDISPLAY.pRbp`, `PTR_uintptr_t` | iterator/context initialization and unwind promotion | `GetFP`, code manager, root scanner | live register storage for the current walk |
| Physical/logical RBP value | `*pRbp`, returned by `GetFP()` | storage owner or C46 virtual-unwind publication | reverse slot calculation and iterator state | valid while the pointed storage is live |
| SP | `REGDISPLAY.SP`, `uintptr_t` scalar | `RtlVirtualUnwind` result | code manager and iterator | copied current unwind state |
| PC | `REGDISPLAY.IP`, `PCODE` scalar | `RtlVirtualUnwind` result | code manager lookup and iterator | copied current unwind state |
| Virtual unwind RBP | `CONTEXT.Rbp`, scalar | Windows `RtlVirtualUnwind`, including `UWOP_SET_FPREG` | C46 publication | local unwind result; must be published before return |
| Unwind RBP pointer | `CONTEXT_POINTERS.Rbp` | Windows unwind API | old macro only; not authoritative for computed FP | local pointer association |
| Iterator logical frame | `StackFrameIterator::m_FramePointer` | `GetFramePointer` or C46 publication | next method-state calculation and root walk | iterator-owned storage |
| Caller transition state | `*ppPreviousTransitionFrame` | reverse-P/Invoke slot read | `StackFrameIterator::NextInternal` | valid for the iterator transition boundary |

The seven C46 snapshots are fixed-size records. Diagnostics do not allocate,
lock, recurse, or dereference an untrusted address. The pre-handoff physical
RBP is dereferenced only while `pRbp` is the current REGDISPLAY source.

## Failing and repaired pipeline

The failing path is:

`StackFrameIterator::Next` → `NextInternal` → code-manager lookup →
`CoffNativeCodeManager::UnwindStackFrame` → `RtlVirtualUnwind` →
nonvolatile-register promotion → caller-frame state → next
`CalculateCurrentMethodState` → reverse-P/Invoke slot calculation.

The C46 run proves the state transition with fresh QEMU addresses:

| Boundary | PC | SP | FP | `pRbp` | Meaning |
| --- | --- | --- | --- | --- | --- |
| Pre-handoff captured by iterator | `0x10002293` | `0x4EC0AE0` | `0x4EC0AD0` | `0x4EC09F0` | old physical frame state |
| After virtual unwind computation | `0x10002293` | `0x4EC0AE0` | `0x0` through the not-yet-filled iterator slot | `0x4EC0220` | iterator storage is authoritative; `context.Rbp=0x4EC0B70` |
| After caller-FP publication | `0x10002293` | `0x4EC0AE0` | `0x4EC0B70` | `0x4EC0220` | logical caller frame is published |
| Before return | `0x10002293` | `0x4EC0AE0` | `0x4EC0B70` | `0x4EC0220` | stable caller state |
| Reverse slot calculation | `0x10002293` | `0x4EC0AE0` | `0x4EC0B70` | `0x4EC0220` | `0x4EC0B70 - 0x70 = 0x4EC0B00` |

The historical C45 control remains authoritative for the malformed state:

`0x4EBFAD0 - 0x70 = 0x4EBFA60`, whose value was
`0x100811F38`, a legitimate `PInvokeTransitionFrame.m_PreservedRegs[1]`
saved RSI. The repaired run uses the production unwind result:

`0x4EC0B70 - 0x70 = 0x4EC0B00`, whose value is `0`. The `-0x70` offset is
unchanged. This is the C45 Branch-B conclusion reproduced after repair: the
problem was base-state handoff, not corruption or a wrong reverse-slot rule.

## Correct FP source and known-good comparison

The correct caller FP is not read from a saved-RBP slot and is not synthesized
from the transition-frame address. It is the scalar `CONTEXT.Rbp` produced by
the real Windows virtual unwind rule `UWOP_SET_FPREG`, corresponding to the
ManagedMain frame-pointer establishment:

`context.Rsp + 0x90 = 0x4EC0AE0 + 0x90 = 0x4EC0B70`.

The associated production prologue is the frame-pointer establishment
`lea rbp,[rsp+0x90]`. The value represents the ManagedMain logical frame base.
`contextPointers.Rbp` is not used as the source of that value.

The known-good control is the ordinary unwind immediately before reverse
slot consumption: its `context.Rbp`, SP, IP, and all preserved registers are
valid, and the following C18 lookup returns a non-null manager and
`FindMethodInfo` result 1. RBX, RSI, RDI, R12, R13, R14, and R15 continue to be
restored through their normal REGDISPLAY pointers. RBP is unique because its
value is established arithmetically by `UWOP_SET_FPREG`, rather than restored
from a saved nonvolatile-register slot.

The reverse-P/Invoke flag is `0x8`. It exposes the defect because its branch
uses `GetFP()` to find the reverse transition slot. The underlying pointer /
computed-value defect is not a slot-offset defect and is not limited to the
meaning of the reverse flag.

## C18 and root scanning

C18 source and fail-closed predicates are unchanged.

- Valid corrected state: executable managed control PC, non-null
  `CoffNativeCodeManager`, successful `FindMethodInfo` result 1.
- Historical malformed C45 state: remains invalid and would fail closed with
  C18 reason `0xEC1801`.
- Arbitrary invalid state: remains rejected by the unchanged manager,
  executable-PC, and `FindMethodInfo` checks.

The final three boots reached authentic root scanning and the stack-scan
completion boundary in all runs:

- managed frames scanned: `1`
- total roots promoted at root scan: `4`
- promotion attempts: `4`
- promotion entries: `4`
- register roots: `1`
- stack roots: `1`
- mark queue closure: reached; `8` mark writes and `8` objects scanned
- relocation-root preflight: reached at `C011EC34-PREFLIGHT`
- planner/COMPACT/sweep: not reached in this focused milestone
- `RestartEE`: not reached
- managed resume: later C37 continuation marker was observed in the retained
  workload, but C46 does not claim a complete collection

The later page fault at read address `0xFFFFFFFFFFFFFF90` is the remaining
independent relocation-path blocker. It is outside the FP handoff repair.

## Validation

QEMU: three independent fresh QEMU 11.0.0 boots, semantically agreeing.

Evidence root:
`out/dotnet/c011ec46-regdisplay-fp-handoff-final/run-20260825-073331797`

Serial SHA-256:

- `A53D183FE5E0A9A0BDBC29580D0A47BB629643232AF5C7884D01A0726A849B53`
- `7BC65E73E4A180A9E7F6F8DE24B52AA22ADC174B9EBB9BC925A5A9DDEDA8C003`
- `49A9A60AD1B5CCCE8D50751A53427978986D7AA8D9384931FA2004B4A819BF91`

Proof-kernel SHA-256: `2018170718EE0AAACAD6245FBD0820C9E8086049C760A3DFB9B9036F8A36DA8A`.

The C46 markers are bounded and fixed-size:
`C011EC46-REGDISPLAY`, with retained `C011EC44`, `C011EC45`, `C011EC18`,
and `C011EC43` markers. Sensitive diagnostic allocations: `0`. Fail-fast:
no C18/C46 fail-fast in the corrected state; the later relocation page fault
is recorded as the next causal frontier.

Regression matrix: C18, C37, C39, C40, C41, C42, C43, C44, and C45 chronology
guards passed or were retained and revalidated by the focused harness;
PE-to-ELF conversion and linker/source/table guards passed; MASM was not
applicable because no assembly changed; ordinary boot smoke passed; and
`git diff --check` passed.

Ordinary artifact restoration completed in `finally`:

- kernel: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- ESP: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`
- QEMU cleanup: no `qemu-system-x86_64.exe` processes remained

## Source regions and handoff repair

Locked runtime source regions audited:

- `Runtime/regdisplay.h:11-54`: REGDISPLAY fields and accessors
- `Runtime/StackFrameIterator.cpp:303`, `1523-1566`, `1913-1939`:
  iterator ownership, `NextInternal`, and method-state calculation
- `Runtime/windows/CoffNativeCodeManager.cpp:651-839`:
  reverse slot read, virtual unwind, and `CONTEXT_TO_REGDISPLAY`

C46 generated-source repair/instrumentation is driven from
`scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1` in the
StackFrameIterator injection around lines 1005-1010 and the Coff unwind
injection around lines 1320-1327 and 1616-1791. Fixed-size C46 records are in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`;
bounded serial emission is in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.

This is a focused C46 repair. The next smallest milestone is to isolate the
post-root-scan relocation-root page fault while preserving this caller-FP
handoff and C18 validation unchanged.

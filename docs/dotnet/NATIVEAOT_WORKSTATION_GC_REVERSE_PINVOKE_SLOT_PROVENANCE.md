# C011EC45 — NativeAOT Workstation GC reverse-P/Invoke slot provenance

## Result

C45 is **Outcome B — exact root cause proven, repair deferred**, at **Success
Level 3**.

The observed address `0x4EBEA60` is not a corrupted copy of a transition-frame
field. In the failing run it is the legitimate `RSI` save slot inside the live
AMD64 `PInvokeTransitionFrame`. `CoffNativeCodeManager::UnwindStackFrame`
applies the correct `-0x70` reverse-P/Invoke slot offset to the wrong base:
the prior `Run`/transition-frame RBP (`0x4EBEAD0`) instead of the
`ManagedMain` RBP (`0x4EBFB70`). The correct `ManagedMain` slot is
`0x4EBFB00` and contains zero. The wrong-base read therefore returns saved
RSI, `0x100811F38`, and consumes it as a transition-frame pointer.

> C45 proved that the value at the observed stack slot was legitimate for its
> actual stack/register role, but `UnwindStackFrame` reconstructed the
> reverse-P/Invoke transition frame from the wrong base/offset/state.

The exact classification is **Code 4 — Wrong unwind base**. The offset and
metadata row are correct; the `REGDISPLAY` FP/base state supplied at the
reverse-P/Invoke read is not.

No runtime repair was made. C18 remains unchanged and fail-closed.

## Handoff, repository, and locked identity

C44 established the first valid-to-invalid transition in the later root scan:
the later transition frame was valid at C44 through frame creation, pre-GC,
suspend, root source, and iterator input. C44 identified
`PInvokeTransitionFrame`, frame base `0x4EBEA38`, captured managed PC
`0x10001EDB`, unwind FP `0x4EBEAD0`, offset `-0x70`, observed slot
`0x4EBEA60`, and value `0x100811F38`. C45 preserved that chain.

| Item | C45 value |
| --- | --- |
| repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| starting branch | `v1.1_DOTNET_SUPPORT` |
| starting HEAD | `b085970d15703b41a15d5b9d056ccb0628a671fb97` |
| starting subject | `Trace NativeAOT malformed transition-frame provenance` |
| upstream | `origin/v1.1_DOTNET_SUPPORT` |
| starting divergence | ahead 0 / behind 0; C44 was already a clean pushed ancestor |
| starting worktree | clean; no untracked files |
| final branch | `v1.1_DOTNET_SUPPORT` |
| final HEAD | recorded after the focused C45 commit |
| final push state | not pushed |
| NativeAOT | `9.0.0` |
| architecture | AMD64 |
| GC | Workstation GC |
| GC interfaces | `5.3 / 2` |
| runtime source | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

The locked runtime/compiler/GC revisions were not updated.

## AMD64 frame layout

The authoritative layout is the assembly macro, not only the C++ type. The
pinned `PUSH_COOP_PINVOKE_FRAME` macro in
`nativeaot/Runtime/amd64/AsmMacros.inc:286-305` saves the caller state by
push order, then allocates `0x28` bytes of scratch space. If `F` is the address
after the `m_RIP` push, the physical `PInvokeTransitionFrame` layout is:

| Address | Offset from `F` | Field/register | Construction |
| --- | ---: | --- | --- |
| `F+0x00` | `0x00` | `m_RIP` / return PC | `push_vol_reg trashReg` after loading `[rsp+11*8]` |
| `F+0x08` | `0x08` | `m_FramePointer` / saved RBP | `push_nonvol_reg rbp` |
| `F+0x10` | `0x10` | `m_pThread` storage | macro stores caller RSP; stackwalker treats it as unused here |
| `F+0x18` | `0x18` | `m_Flags` | `DEFAULT_FRAME_SAVE_FLAGS = 0x80F7` |
| `F+0x20` | `0x20` | `m_PreservedRegs[0]` / saved RBX | `push_nonvol_reg rbx` |
| `F+0x28` | `0x28` | `m_PreservedRegs[1]` / saved RSI | `push_nonvol_reg rsi` |
| `F+0x30` | `0x30` | `m_PreservedRegs[2]` / saved RDI | `push_nonvol_reg rdi` |
| `F+0x38` | `0x38` | `m_PreservedRegs[3]` / saved R12 | `push_nonvol_reg r12` |
| `F+0x40` | `0x40` | `m_PreservedRegs[4]` / saved R13 | `push_nonvol_reg r13` |
| `F+0x48` | `0x48` | `m_PreservedRegs[5]` / saved R14 | `push_nonvol_reg r14` |
| `F+0x50` | `0x50` | `m_PreservedRegs[6]` / saved R15 | `push_nonvol_reg r15` |
| `F+0x58` | `0x58` | `m_PreservedRegs[7]` / saved caller RSP | first `push_vol_reg trashReg` |
| `F+0x60` | `0x60` | `m_PreservedRegs[8]` / saved RAX | only when `PTFF_SAVE_RAX` is enabled |
| `F+0x68` | `0x68` | end of maximum frame | `PInvokeTransitionFrame_MAX_SIZE` |

The pinned native header `nativeaot/Runtime/inc/rhbinder.h:462-496` agrees:
`m_RIP=0`, `m_FramePointer=8`, `m_pThread=0x10`, `m_Flags=0x18`,
`m_PreservedRegs=0x20`, and nine AMD64 saved-register words. C45’s compile-time
assertions passed for those offsets, the `RuntimeThreadLocals` transition links,
and the saved-register count. No ABI/layout mismatch was found.

For the authoritative frame `F = 0x4EBFA38`:

* `F+0x28 = 0x4EBFA60` is saved RSI.
* `F+0x58 = 0x4EBFA90` is the saved caller RSP by the literal offset map;
  the runtime marker’s saved-RSP value is `0x4EBFAA0` because that is the
  caller RSP value stored in the frame, not the address of the slot.
* The captured `m_pThread` storage value is `0x4EBFAA0`, also the caller RSP
  value produced by the macro in this portable-helper configuration.

The frame fields and the macro’s register order are also consistent with the
`PInvokeTransitionFrame` static assertions added for C44/C45.

## Meaning and provenance of `0x4EBEA60`

The current C45 run uses the same semantic addresses as the C44 handoff:

| Question | Proven value |
| --- | --- |
| authoritative frame base | `0x4EBFA38` |
| observed unwind FP/base | `0x4EBFAD0` |
| actual calculation | `0x4EBFAD0 - 0x70 = 0x4EBFA60` |
| actual slot offset from frame | `+0x28` |
| actual symbolic field | `PInvokeTransitionFrame.m_PreservedRegs[1]`, saved RSI |
| actual value | `0x100811F38` |
| correct `ManagedMain` FP/base | `0x4EBFB70` |
| correct calculation | `0x4EBFB70 - 0x70 = 0x4EBFB00` |
| expected symbolic field | `ManagedMain` reverse-P/Invoke local at `[RBP-0x70]` |
| expected value | `0x0000000000000000` |
| delta actual vs expected slot | `-0xA0` |
| delta actual vs expected base | `-0xA0` |

Thus the observed slot is inside the live transition frame, not caller-owned
storage, not a nested helper’s frame, and not an unrelated reused location.
It is a correct saved-register slot used with an incorrect logical frame base.

The exact first writer is:

* source: pinned `nativeaot/Runtime/amd64/AsmMacros.inc:286-305`;
* macro: `PUSH_COOP_PINVOKE_FRAME`, used by `AllocFast.asm`’s
  `RhpNewArrayRare` path;
* instruction: `push_nonvol_reg rsi`;
* effective destination: `[frame+0x28]`;
* source value: entry RSI, before `RhpNewArrayRare` executes `mov rsi,rcx`;
* call context: the reverse-P/Invoke transition around `RhpNewArrayRare` and
  its `RhpGcAlloc`/`GcAllocInternal` allocation path.

No legitimate later writer to this live slot was observed. `POP_COOP_PINVOKE_FRAME`
reads/restores the saved value during teardown; it does not overwrite the slot.
C45’s bounded frame snapshots found zero write events between frame creation and
unwind consumption. No write was observed before/after C40, `RestartEE`, or the
64-KiB pressure path. C45 therefore proves no live-frame overwrite.

## Saved-register image

The full target-frame image was captured at frame creation, pre-GC, suspend,
root-source, and unwind entry. The frame identity, `m_RIP`, saved RBP, caller
RSP value, flags, and the saved-register image remained stable for the target
frame across those phases. The final target image was:

| State | Slot/address | Value |
| --- | --- | ---: |
| return PC / `m_RIP` | `F+0x00 = 0x4EBFA38` | `0x10001EDB` |
| saved RBP / `m_FramePointer` | `F+0x08` | `0x4EBFAD0` |
| `m_pThread` storage value | `F+0x10` | `0x4EBFAA0` |
| flags | `F+0x18` | `0x80F7` |
| saved RBX | `F+0x20` | `0x0000000000000002` |
| saved RSI | `F+0x28 = 0x4EBFA60` | `0x0000000100811F38` |
| saved RDI | `F+0x30` | `0x0000000100821F50` |
| saved R12 | `F+0x38` | `0x0000000100A00028` |
| saved R13 | `F+0x40` | `0x000000000000004B` |
| saved R14 | `F+0x48` | `0x0000000100831F68` |
| saved R15 | `F+0x50` | `0x0000000000000100` |
| saved caller RSP | `F+0x58` | `0x0000000004EBFAA0` |
| saved RAX | `F+0x60` | `0x0000000000000000` |

The phase values were unchanged at frame-create, pre-GC, suspend, and
root-source checkpoints. The unwind-entry register input had the same target
RSI/RDI/RBX/RBP/R12-R15/RSP/RAX values. After the wrong-base reconstruction,
`ppPreviousTransitionFrame` became `0x100811F38`; the subsequent C18 iterator
state was malformed and fail-closed.

## Unwind calculation and metadata selection

The pinned `CoffNativeCodeManager::UnwindStackFrame` is
`nativeaot/Runtime/windows/CoffNativeCodeManager.cpp:651-711`. Its reverse
P/Invoke branch:

1. decodes `DECODE_REVERSE_PINVOKE_VAR`;
2. obtains the reverse-P/Invoke stack slot (`-0x70`);
3. obtains the stack-base register (`RBP`, marker `5`);
4. selects `pRegisterSet->GetFP()` as the base; and
5. executes `*(PInvokeTransitionFrame**)(basePointer + slot)`.

For the failing call the exact arithmetic is therefore:

```text
basePointer = pRegisterSet->GetFP() = 0x4EBFAD0
slot        = -0x70
read       = *(PInvokeTransitionFrame**)(0x4EBFAD0 - 0x70)
           = *(PInvokeTransitionFrame**)(0x4EBFA60)
           = 0x100811F38
```

C45 also captured an ordinary same-row unwind for the expected base:

| Metadata/state | Failing reverse-P/Invoke read | Same-row expected read |
| --- | ---: | ---: |
| method range | `[0x10002240, 0x10002457)` | same |
| current PC | `0x1000242E` | `0x100023AA` |
| relative PC | `0x1EE` | within same row |
| runtime function | `0x10313324` | `0x10313324` |
| main runtime function | `0x10313324` | `0x10313324` |
| unwind info | `0x1013316C`, size `0x16` | same |
| block flags | `0x8`, reverse-P/Invoke | same |
| stack-base register | `5`, RBP | `5`, RBP |
| offset | `-0x70` | `-0x70` |
| base | `0x4EBFAD0` | `0x4EBFB70` |
| slot | `0x4EBFA60` | `0x4EBFB00` |
| loaded value | `0x100811F38` | `0x0` |

The method start/end, relative PC, main/runtime-function identity, unwind-info
address/size, block flags, and RBP-based state all match the selected
`ManagedMain` metadata. The row selection and offset are consequently correct.
The defect is already present in the base before `-0x70` is applied.

The `ManagedMain` prologue is visible in the generated disassembly:

```asm
push rbp
...
sub  rsp, 0x58
lea  rbp, [rsp+0x90]
mov  [rbp-0x70], rax
```

With `RBP=0x4EBFB70`, that initialization is exactly the expected zero-valued
slot `0x4EBFB00`. The failing `0x4EBFAD0` is the prior reverse-P/Invoke
frame’s saved RBP, not `ManagedMain`’s active base. The narrowed source boundary
is the `REGDISPLAY` FP state passed from `StackFrameIterator::Next` at its
`UnwindStackFrame` call (`StackFrameIterator.cpp:1565-1566`) into the
`GetFP()` consumer above. Repair of that state transition is deferred until it
can be changed without weakening C18.

## C40 correlations

The final three-run C45 evidence recorded:

| Value | C40/current meaning | C45 observation |
| --- | --- | --- |
| `0x100811F38` | `neighboringLiveDestinationEnd` | exactly saved RSI at `F+0x28`; therefore legitimate data in its actual saved-register role |
| `0x10310FF0` | `targetEEType` | equals the later bogus control PC after the wrong pointer is interpreted as a transition frame; no separate target-frame preserved-register copy was observed |

The saved-RSI equality is not an overwrite signature: it is the value that the
AMD64 macro legitimately saved. The target EEType equality is a downstream
malformed-PC correlation retained from C44; C45 did not prove it was a distinct
saved nonvolatile register in this frame. These facts support wrong-slot/base
selection, not corruption of the correct transition-frame slot.

## Lifetime and nested-frame audit

The target frame was pushed by `PUSH_COOP_PINVOKE_FRAME` before the
`RhpNewArrayRare` managed-to-native allocation path and remained present in the
thread’s transition-frame chain through pre-GC, suspend, root-source, iterator
input, and the failing unwind. `POP_COOP_PINVOKE_FRAME` had not executed when
`0x4EBFA60` was read. The target frame’s physical address and saved fields were
stable, so this is not Code 6 stale lifetime.

`RhpNewArrayRare`, `RhpGcAlloc`, `GcAllocInternal`, suspension helpers, and the
stack-walk helpers execute around or inside this path, but none owns
`F+0x28` for the target frame. C45 observed no nested helper write to that
address. The wrong logical frame/base is selected by the unwind state; the
physical transition frame is live. This is Code 4, not Code 7 or Code 6.

## Branch and root-cause classification

**Branch B — correct/legitimate data in the wrong slot.**

The architecture says the observed physical address is `F+0x28`, saved RSI,
and the value there is exactly the saved entry RSI. The reverse-P/Invoke
metadata row says the intended read is `ManagedMain RBP-0x70`; because the base
is shifted by `-0xA0`, those two addresses coincide with the wrong physical
role. C45 captured zero write events and identical target snapshots across all
four live phases.

Root cause: **Code 4 — Wrong unwind base**. The exact repair source is not
changed in C45. The consuming code is
`CoffNativeCodeManager::UnwindStackFrame` at the base-selection/read block
(`CoffNativeCodeManager.cpp:681-691` in the pinned source); the upstream state
boundary is the `REGDISPLAY` FP passed by `StackFrameIterator::Next` at
`StackFrameIterator.cpp:1565-1566`. No hard-coded frame pointer, slot patch,
frame skip, C18 bypass, or code-manager substitution was made.

## C18 and regression status

The C45 diagnostics are fixed-size, bounded, allocation-free in the GC-sensitive
path, non-recursive, and fail-closed. No sensitive diagnostic allocations were
observed; invariant failures remained zero.

| Check | Result |
| --- | --- |
| C18 fail-closed gate | PASS; malformed state stops with PAL `0x47435354`, C18 reason `0xEC1801` |
| C18 valid-state regression | PASS historically and in the same run’s valid preflight: manager lookup and `FindMethodInfo` succeed for `0x10001EDB` |
| C37 repeated GC | retained predecessor chronology |
| C39 authentic `COMPACT` | retained predecessor chronology |
| C40 authentic reclamation | retained predecessor chronology; C40 correlation retained |
| C41 allocator provenance | retained predecessor chronology |
| C42 natural later collection | retained natural later-collection entry |
| C43 C18 provenance | PASS; gate and malformed-state provenance retained |
| C44 first divergence | PASS; divergence remains in `UnwindStackFrame` consumption |
| C19–C44 chronology guards | PASS; locked source guards retained |
| PE→ELF conversion | PASS; `test_pe_to_elf_v2_fixed_base.py` |
| linker/source/table guards | PASS existing guards |
| PowerShell parser | PASS |
| MASM | not applicable; no assembly source was changed |
| ordinary boot smoke | PASS after harness `finally` restoration |
| `git diff --check` | PASS; only Git’s LF/CRLF normalization warnings |

No post-repair code-manager lookup, `FindMethodInfo`, C18 pass, root-scan
advancement, mark/planner/restart, or full GC completion is claimed: C45
correctly stops at the historical malformed state.

## Three-boot authoritative validation

The final validator run used three fresh independent QEMU processes. QEMU was
`11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. All three runs captured the same target
frame `0x4EBFA38`, failing base `0x4EBFAD0`, actual slot `0x4EBFA60`, expected
slot `0x4EBFB00`, saved RSI `0x100811F38`, C44 divergence, and C18 fail-closed
result.

| Run | Serial SHA-256 |
| --- | --- |
| first-run | `FAEA53B56605293B2E4DC1524F41DDF02033678346AFDCFF08E7507BD93AB111` |
| repeat-1 | `6C5C2DF6AEA457BE7B1FF143001AB6D48163DD8041B18BA61C40595F766665D2` |
| repeat-2 | `8E0FADDF543932499AC1E5B8C7B0E824DE060EF1CDC291C8B56C8F56F7BC45EF` |

The proof-build kernel hash was
`A8D37EDA9E3AD6DD195814C2CC0D7E7E3D66D3A72A4E2A5A4CF845070BA53D51`.
After the harness `finally` block, the live ordinary kernel and ESP both hash
to the required baseline:

`75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`

No QEMU process from the C45 run remained. An unrelated pre-existing QEMU
process was observed and was not terminated.

## Artifacts and outcome

The authoritative final evidence is in:

`out/dotnet/c011ec45-reverse-pinvoke-slot-provenance/run-20260824-231954653`

The JSON manifest records Outcome B / Level 3, the layout, all snapshots,
unwind metadata, C40 correlations, three serial hashes, C18 result, and
ordinary restoration. The focused C45 changes are the bounded diagnostics in
`guidexos_nativeaot_allocation_diagnostics.h` and
`guidexos_nativeaot_platform.cpp`, plus the validator path in
`smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`.

No repair was made because the remaining safe milestone is to correct the
`REGDISPLAY` FP/base transition at the stack-walk boundary and then prove that
the real unwind path reads `0x4EBFB00` without weakening the C18 gate.

**Next smallest milestone:** isolate and repair the `REGDISPLAY` base-state
handoff between `StackFrameIterator::Next` and
`CoffNativeCodeManager::UnwindStackFrame`, then rerun the same three-boot
matrix with C18 still fail-closed for malformed historical state.

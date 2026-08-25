# C011EC48 — Iterator-Owned FP Publication and Reverse-Slot Consumption

## Result

C011EC48 is Outcome A at Success Level 4. Three fresh QEMU 11.0.0 boots
reached the authentic NativeAOT stack-root path, completed C18 method lookup,
C34 relocation preflight, C26 root scanning with four promoted entries, and
C28 mark-queue closure. The former C47 page fault at
`0xFFFFFFFFFFFFFF90` did not recur, and no independent QEMU fault was
observed.

The proof deliberately stops at the C48 frontier. The planner-return marker
was not present in every boot, so this result does not claim a later planner or
managed-resume milestone. The repair itself is stable across all three boots.

The run manifest is:

```text
out/dotnet/c011ec48-iterator-fp-ownership/run-20260825-112545296/manifest.json
```

## Repository and locked runtime identity

| Item | Value |
| --- | --- |
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting and ending committed HEAD | `bcc5097f05f8d48806594e49f2a1582de4360ef7` |
| Upstream divergence at start | ahead `0`, behind `0` |
| NativeAOT | `9.0.0` |
| Architecture | AMD64 |
| GC | Workstation |
| GC interfaces | `5.3 / 2` |
| Locked source commit | `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3` |

No runtime, GC flavor, architecture, ABI, or interface version was changed.

## Root cause classification

This is Case C, root-cause Code 1: a generated transformation defect in the
C46 `StackFrameIterator::Next` replacement.

The locked runtime's `REGDISPLAY.pRbp` is a pointer to the storage containing
the live frame pointer. `GetFP()` dereferences that pointer. The iterator's
`m_FramePointer` is separate iterator-owned output storage. For a reverse
P/Invoke frame, `CalculateCurrentMethodState` can legitimately leave
`m_FramePointer` null because the current method is a root method, while the
incoming `pRbp` still points to authoritative external/current-frame storage.

C46 redirected `pRbp` to `&m_FramePointer` before copying the incoming FP. On
the failing second-collection frame, the sequence was therefore:

```text
authoritative incoming FP = 0x0000000004EC1B70
m_FramePointer            = 0x0000000000000000
pRbp before C46 rehome    = 0x0000000004EC1A80
pRbp after C46 rehome     = &m_FramePointer
FP seen by reverse unwind  = 0x0000000000000000
signed slot offset         = -0x70
computed slot address      = 0xFFFFFFFFFFFFFF90
```

The `-0x70` metadata and production reverse-P/Invoke slot arithmetic were
correct. The invalid address was null-base plus the valid signed offset.

## Repair

The C48 generated `Next` handoff now captures `m_RegDisplay.GetFP()` while
`m_RegDisplay.pRbp` still names the authoritative incoming storage, publishes
that scalar into `m_FramePointer`, and only then re-homes `pRbp`:

```cpp
incomingRbpPointer = reinterpret_cast<uintptr_t>(m_RegDisplay.pRbp);
incomingFp = m_RegDisplay.pRbp != NULL
    ? static_cast<uintptr_t>(m_RegDisplay.GetFP()) : 0u;

// The source pointer is still authoritative here.
m_FramePointer = (PTR_VOID)incomingFp;

// Only after publication does iterator-owned storage become authoritative.
m_RegDisplay.pRbp = (PTR_uintptr_t)&m_FramePointer;
```

There is no zero fallback for a valid non-null incoming `pRbp`; zero is only
the scalar result of the existing null-pointer conditional. The C48 harness
requires the active reverse-frame source and all post-rehome values to be
non-zero and equal.

## Ownership proof

The final run's C48 markers show the required order:

| Event | Evidence |
| --- | --- |
| FP-IN | `incomingP=0x0000000004EC1E10`, `incomingFp=0x0000000004EC2B70` |
| FP-PREPARE | `sourceFp=0x0000000004EC2B70`, `after=0x0000000004EC2B70` |
| FP-REHOME | `rehomeP=0x0000000004EC1E10`, `getFp=0x0000000004EC2B70` |
| FP-CONSUME | `fp=0x0000000004EC2B70`, `base=0x0000000004EC2B70` |

The live `pRbp` storage pointer may be reused by the iterator, so its address
is not required to equal the prior C47 address. The invariant is that the
value obtained before rehome is copied into iterator-owned storage and remains
the value returned by `GetFP()` afterward.

## Reverse-slot proof

The locked `CoffNativeCodeManager::UnwindStackFrame` reverse-P/Invoke path still
uses the authentic decoder-selected signed offset:

```text
base + sign-extended slot
= 0x0000000004EC2B70 + 0xFFFFFFFFFFFFFF90
= 0x0000000004EC2B00
```

The production slot read completed and returned a null transition-frame value
(`0x0000000000000000`), which is a valid observed slot value and is distinct
from the former invalid address. C48 records the scalar slot result only after
the production read; it does not dereference the computed address itself.

Across all three boots, the parser rejected any relocation calculation or root
update with `base=0` or slot `0xFFFFFFFFFFFFFF90` and rejected any QEMU fault
with CR2 `0xFFFFFFFFFFFFFF90`.

## Progression and preserved contracts

Every boot retained these predecessor checkpoints:

* C18 preflight and `FindMethodInfo` success;
* C34 relocation-root preflight;
* C26 completion with `promoteEntries=4`;
* C28 mark-queue closure;
* ordinary-kernel restoration to SHA-256
  `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.

No C18 predicate was weakened, no root was skipped, no callback was suppressed,
no page-fault guard was added, and no production slot read was replaced. The
diagnostic state is fixed-size and scalar-only: C48 reserves 16 bounded
ownership records and performs no sensitive diagnostic allocation.

## Three-boot evidence

| Boot | Serial SHA-256 | C48 result |
| --- | --- | --- |
| first-run | `CD9B22E38C2EA6C967A1F2FD693E9394EE1791CB8A3DB472A4805BEF24FBEDA8` | PASS |
| repeat-1 | `44A389FB647DD47A6EFB4555DBA73B60CDC671B4E9B6FB8D61C5D6B1D1A5F16E` | PASS |
| repeat-2 | `CA2B1FA7CB6B1C49D3C1850084C38E358407A0EF364C03B44A7C8F0508E6E949` | PASS |

Evidence root:

```text
out/dotnet/c011ec48-iterator-fp-ownership/run-20260825-112545296/
```

The proof kernel SHA-256 was
`C722A5E7EE22D12162753C49997F0EA270E189DEC1422E84CAE77C927BDBA6A0`.

## Reproduction

From the repository root:

```powershell
pwsh -NoProfile -File .\scripts\smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1 `
  -ProofMode iterator-fp-ownership -FreshBootCount 3
```

The C48 proof mode is implemented in
`scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`. The bounded
diagnostic schema is in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`,
and its serial emission is in
`tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.

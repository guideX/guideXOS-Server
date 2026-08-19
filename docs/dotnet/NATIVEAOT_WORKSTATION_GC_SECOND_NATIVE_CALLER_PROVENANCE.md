# NativeAOT Workstation GC: Second Native Caller Provenance and Unwind Validation

Date: 2026-08-18
Milestone: C011EC24
Outcome: C — second native frame genuinely unwound

## Result

`C011EC24-PREFLIGHT` passed only after the complete helper unwind program, an independent return-slot derivation, and an authoritative executable-range audit agreed. The recovered caller is:

```text
0x356767AA
```

It is the physical identity-mapped alias of the kernel’s linked address `0x1A57AA`, inside `kernel_main`. It is not a separate loaded module and is outside the NativeAOT managed range. The native registry now represents the linked kernel range and its physical alias as two bounded descriptors, still within capacity 2. One legitimate second native unwind consumed the kernel’s genuine metadata and recovered `0x355D101E` with `RSP=0x4E96000`.

The emitted markers were:

```text
C011EC24-PREFLIGHT
C011EC24
```

## Locked identity and C011EC23 boundary

The runtime identity remained NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`, using locked runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

C011EC23 had established the first production kernel-native provider and stopped after one native frame with caller `0x3567A7AA` and safe-stop `0xC0230003`. Its linked kernel range did not include the physical alias used by the loader. C011EC24 established that the caller was real before extending the provider.

The final relinked helper used for this milestone was:

| Item | Value |
|---|---:|
| Suspended helper PC | `0x1AE445` |
| Helper function interval | `[0x1AE030, 0x1B0033)` |
| Covering runtime-function entry | `0x218260` |
| Begin RVA | `0xAE030` |
| End RVA | `0xB0033` |
| Unwind-data RVA | `0x120E20` |
| Resolved UNWIND_INFO | `0x220E20` |
| `.pdata` | `0x214000..0x21D1D4`, 3111 entries |
| `.xdata` | `0x21D1D4..0x224BFC` |

## Complete helper unwind program

The helper UNWIND_INFO bytes at `0x220E20` were:

```text
01 13 0A 00 13 01 6F 00 0C 30 0B 60 0A 70 09 50
08 C0 06 D0 04 E0 02 F0
```

The header is version 1, flags 0, prologue size `0x13`, ten unwind-code slots, and no frame register. The raw records are stored in reverse prologue order; the execution-order unwind program is:

| Execution order | Opcode | Operation | Code offset | Allocation / source |
|---:|---|---|---:|---|
| 1 | `UWOP_ALLOC_LARGE` | discard large stack allocation | `0x13` | opinfo 0, extra word `0x006F`, `0x6F * 8 = 0x378` bytes |
| 2 | `UWOP_PUSH_NONVOL` | restore RBX | `0x0C` | `[live RSP + 0x378]` |
| 3 | `UWOP_PUSH_NONVOL` | restore RSI | `0x0B` | `[live RSP + 0x380]` |
| 4 | `UWOP_PUSH_NONVOL` | restore RDI | `0x0A` | `[live RSP + 0x388]` |
| 5 | `UWOP_PUSH_NONVOL` | restore RBP | `0x09` | `[live RSP + 0x390]` |
| 6 | `UWOP_PUSH_NONVOL` | restore R12 | `0x08` | `[live RSP + 0x398]` |
| 7 | `UWOP_PUSH_NONVOL` | restore R13 | `0x06` | `[live RSP + 0x3A0]` |
| 8 | `UWOP_PUSH_NONVOL` | restore R14 | `0x04` | `[live RSP + 0x3A8]` |
| 9 | `UWOP_PUSH_NONVOL` | restore R15 | `0x02` | `[live RSP + 0x3B0]` |

The corresponding prologue pushes R15, R14, R13, R12, RBP, RDI, RSI, RBX and then subtracts `0x378` from RSP. There is no `UWOP_SET_FPREG`, no chained unwind record, and no XMM save record. Total stack advance before reading the return slot is `0x378 + 8 * 8 = 0x3B8`.

The guideXOS AMD64 primitive implements every opcode used by this function: `UWOP_ALLOC_LARGE` opinfo 0 and all eight `UWOP_PUSH_NONVOL` register forms. No helper opcode was unsupported, ignored, approximated, or mis-decoded. The primitive also contains handling for other Win64 records (`ALLOC_LARGE` opinfo 1, `ALLOC_SMALL`, `SET_FPREG`, `SAVE_NONVOL`, `SAVE_NONVOL_FAR`, and `PUSH_MACHFRAME`); chained unwind flags are not implemented, but this helper has flags 0 and no chained record, so that is not an active defect at this boundary.

## Independent return-slot derivation

The authentic suspended input was:

```text
RIP = 0x1AE445
RSP = 0x04E95B80
RBP = 0x04E95B70
```

Using only the decoded program and genuine stack memory:

```text
derived return-slot = 0x04E95B80 + 0x3B8 = 0x04E95F38
value at slot       = 0x356767AA
expected caller RSP = 0x04E95F40
expected caller RIP = 0x356767AA
```

The production primitive returned `RIP=0x356767AA` and `RSP=0x04E95F40`. The independent derivation and primitive output agree exactly. No stack scan was used.

## Restored nonvolatile-register provenance

Each recovered value was checked against the metadata-described stack slot:

| Register | Opcode | Source address | Pre-unwind value | Recovered value |
|---|---|---:|---:|---:|
| RBX | `UWOP_PUSH_NONVOL` | `0x04E95EF8` | `0x100A02FF0` | `0x3DF47000` |
| RSI | `UWOP_PUSH_NONVOL` | `0x04E95F00` | `0x100A04020` | `0x0` |
| RDI | `UWOP_PUSH_NONVOL` | `0x04E95F08` | `0x100A05038` | `0x356E4CF0` |
| RBP | `UWOP_PUSH_NONVOL` | `0x04E95F10` | `0x04E95B70` | `0x3FE65770` |
| R12 | `UWOP_PUSH_NONVOL` | `0x04E95F18` | `0x1` | `0x355D1000` |
| R13 | `UWOP_PUSH_NONVOL` | `0x04E95F20` | `0x1` | `0x3DF47000` |
| R14 | `UWOP_PUSH_NONVOL` | `0x04E95F28` | `0x100` | `0x210000` |
| R15 | `UWOP_PUSH_NONVOL` | `0x04E95F30` | `0x25` | `0x3DE50000` |

The first unwind was therefore fully correct, not merely a nonzero-RIP result.

## Authoritative caller ownership audit

The loader’s authoritative state reports the kernel physical load range `[0x355D1000, 0x3BB6B770)` and identity-maps that physical range. The kernel’s linked provider executable range is `[0x100000, 0x213EB0)`. Translating that bounded range to the physical alias gives `[0x355D1000, 0x356E4EB0)`, which contains `0x356767AA`.

| Executable candidate | Base / range | `0x356767AA` inside? |
|---|---|---|
| Kernel linked provider | `[0x100000, 0x213EB0)`; kernel ELF R-E load `[0x100000, 0x2B31E0)` | No, linked address space |
| Kernel physical identity alias | `[0x355D1000, 0x356E4EB0)` | Yes |
| NativeAOT managed image | registered `[0x10001000, 0x10050950)`; ELF R-E `[0x10001000, 0x100C9C00)` | No |
| Boot/loader executable mapping | `[0x3DE33000, 0x3DE45000)` | No |
| Separate runtime/native support image | none loaded; support code is embedded in the kernel artifact | No |
| Firmware runtime executable mapping | none exposed in the post-EBS authoritative mapping list | No evidence of ownership |

The caller is not owned by an unrelated module. The kernel callsite bytes at linked `0x1A57A4` contain the call whose return address is linked `0x1A57AA`. The physical caller is therefore `kernel_main+0x12A` after the loader’s physical relocation/aliasing. The linked symbol starts at `0x1A5680`; the next symbol/function boundary is `0x1A5820`.

The C23 range model was incomplete because it published only the linked kernel range. C24 repairs the executable-range description by publishing the loader-provided physical alias as a second bounded descriptor. This is not a special case for the caller and does not enlarge the range without loader/linker proof.

## Second native frame

There is no second separately loaded module. The second registry descriptor is the kernel’s physical alias:

```text
base             = 0x355D1000
executable start = 0x355D1000
executable end   = 0x356E4EB0
```

The second provider lookup succeeded and found genuine metadata:

```text
runtime-function = 0x356E8DC8
unwind-info      = 0x356F1A48
input RIP/RSP/RBP = 0x356767AA / 0x04E95F40 / 0x3FE65770
output RIP/RSP/RBP = 0x355D101E / 0x04E96000 / 0x3FE65770
```

The linked second runtime-function record is at `0x217DC8`, with raw bytes:

```text
80 56 0A 00 1F 58 0A 00 48 0A 12 00
```

It describes linked interval `[0x1A5680, 0x1A581F)` and linked unwind data `0x220A48`; its physical alias is the descriptor above. Its unwind program has version 1, prologue `0x0A`, five codes, a `0xA0` allocation, and `UWOP_PUSH_NONVOL` for RBX, RSI, and RDI. The second output is distinct and the native walk crossed exactly two native frames. No arbitrary multi-frame walk was attempted.

## Preserved GC chronology and sensitive-path state

The managed proof evidence remained unchanged:

```text
managed frames       = 1
total roots          = 6
category-3 roots     = 4
register roots       = 3
stack roots          = 1
Promote              = 4 / 4 / 4
queue cursor         = 4 -> 5
mark writes          = 0
child reads          = 0
graph traversal      = 0
managed re-entry     = 0
native managed roots = 0
```

Stack bounds remained out of scope: stack base `0`, `ScanContext.stack_limit=0`, and bounds consumed `0`. After EE suspension there were no allocations, registration, table construction, strings/collections, arbitrary stack scans, managed re-entry, or scheduler transitions. The proof stopped with safe-stop `0xC0240004` after the legitimate second native unwind.

## Validation

The standalone bounded cross-check passed for the real helper and a second kernel-native function with different nontrivial unwind metadata. It proved function lookup, metadata decode, derived return slot, caller PC/SP, and restored registers. Provider linked/alias registration and deterministic lookup passed; `.pdata` ordering/table validation passed; PE-to-ELF conversion and fixed-base conversion regression passed; PowerShell parsing, locked-runtime/source guards, and `git diff --check` passed.

Three fresh QEMU boots used QEMU `11.0.0 (v11.0.0-12122-ga4bb4b10c9)`. All semantic values agreed:

| Run | Serial hash | Helper PC | Derived slot/value | First output | Second output | Safe stop |
|---:|---|---|---|---|---|---|
| 1 | `3AD6BB6D412892493117D1917666201FDECEDECA4D0268B7C509FD663A93E0D3` | `0x1AE445` | `0x4E95F38 / 0x356767AA` | `0x356767AA / 0x4E95F40` | `0x355D101E / 0x4E96000` | `0xC0240004` |
| 2 | `72054931BD72B0458E67021B9239DC95DDFAAE8C9E506F606F556B8C950A613D` | `0x1AE445` | `0x4E95F38 / 0x356767AA` | `0x356767AA / 0x4E95F40` | `0x355D101E / 0x4E96000` | `0xC0240004` |
| 3 | `2CBD0C7D0834476933689C5D1DA971A2AC5B7965338BAE80CAD80A689B9F5BDD` | `0x1AE445` | `0x4E95F38 / 0x356767AA` | `0x356767AA / 0x4E95F40` | `0x355D101E / 0x4E96000` | `0xC0240004` |

Final C24 payload hashes:

```text
proof kernel = 5592C1745BB708110CCFB4B7F96304BABE6542811FE90092D0C422A773E3DABC
PE           = 998D284C3C8239F9C7B76B528A12F2CA0B23411183AEB5F53DD211B76A29DF91
ELF          = CA7230F92376204DCCA5F25F6935C562F958A8A5CF96EFC85A8DBF83E9B818B0
MAP          = 596356D186F6BB4445B049A7B8A3539AC83A957E335C75F10760FE9B79D2A588
```

An ordinary non-proof kernel build and boot smoke also passed. The intentionally rebuilt ordinary source-state kernel and ESP deployment were restored to hash:

```text
A5B634F9D034FE2FFFB11048693321ECA387902E95C5E8CAE4624D63F52CD68B
```

The ordinary smoke showed normal boot, valid unwind tables, and no loader/layout regression. Navigator/server smoke remained healthy where covered. QEMU processes were terminated by the harness and the proof payload was removed after capture; the ordinary deployment was left in the source-state build, not an obsolete historical binary.

## Git state and next milestone

Starting state was branch `v1.1_DOTNET_SUPPORT` at `85410809deafeb33d8ea50b66c6b27ccb847e27b`, upstream `origin/v1.1_DOTNET_SUPPORT`, clean, with no untracked entries. Actual starting divergence was `0 ahead / 0 behind`, differing from the expected `7 ahead / 0 behind` supplied for the task. C011EC17–C011EC23 history was not rewritten. The final commit, push result, and final divergence are reported by the C011EC24 handoff after this document and the intended source changes are committed.

The smallest next milestone is to preserve the two-descriptor kernel alias model while validating any later native boundary only with the same independent metadata/return-slot/ownership checks. Do not broaden the registry or perform an arbitrary walk without a new authoritative executable-range and unwind contract.

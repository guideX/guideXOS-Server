# C011EC57 — NativeAOT Workstation GC Direct Gen1 Budget Condemnation

Status: complete as a bounded race characterization. Outcome C, Success Level 1.

The final authoritative evidence is the three-boot S2 manifest:

`out/dotnet/c011ec57-direct-gen1-budget-condemnation/run-20260830-060408988/manifest.json`

The run used the locked C52 production runtime-pack and three fresh QEMU 11.0.0 boots. It did not prove a direct gen1 collection: the B12/OOS-SOH path won while the gen1 budget was still positive, and the resulting decision started with `n_initial == 2`, so the B02 loop was not eligible in that decision.

## Outcome and continuity

| Field | Result |
| --- | --- |
| Outcome | C — B02 race characterized; B12 still preempts |
| Success Level | 1 — exact threshold ordering and timing characterized |
| Final strategy | S2 |
| Three-boot agreement | PASS; all boots Outcome C / Level 1 |
| B02 crossed | No |
| Direct gen1 selected | No |
| B12 control | Yes; final generation 2, `reason_oos_soh` (`0x5`) |
| Invariant failures | 0 |
| Sensitive diagnostic allocations | 0 |
| Fail-fast / page fault | 0 / 0 |

C56 remains the control. C56 ended with gen1 desired allocation `0x1CDB68`, gen1 new allocation `0x4D448`, signed remaining budget `+316488`, B02 false, and a later B12-driven gen2 selection. C57’s S2 shape made additional policy observations possible, but did not make T1 win before T2.

## Repository and locked runtime

The live preflight was measured before edits:

| Field | Value |
| --- | --- |
| Repository | `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT` |
| Branch | `v1.1_DOTNET_SUPPORT` |
| Starting HEAD | `7489df06d708e623c2ab918cd4de9560072b79be` |
| Starting subject | `Prove NativeAOT natural gen1 condemnation` |
| Starting upstream | `origin/v1.1_DOTNET_SUPPORT` |
| Starting divergence | ahead 0 / behind 0 |
| Starting worktree | clean; no untracked files |
| C56 ancestry | C56 `7489df06`; parent C55 `5792da53` (`Trace NativeAOT natural older-generation GC transition`) |
| C56 pushed at handoff | Yes; local HEAD and origin matched |

The runtime identity was preserved exactly:

- NativeAOT/.NET 9.0.0, AMD64, Workstation GC
- GC interfaces 5.3 / 2
- locked source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`
- locked `nativeaot-amd64-fp-handoff.patch`
- FP patch SHA-256 `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`
- locked `gc.cpp` SHA-256 `5BD029B77A973145B12C142E870A549C527DAC19AEB527012F7CC6F99D362FF9`

The semantic rewrite guard passed. No C46/C47/C48 semantic proof rewrite or GC policy mutation was introduced.

## Source audit: B02, B12, and allocation path

The locked source audit covers `gc_heap::generation_to_condemn(int n_initial, BOOL*, BOOL*, BOOL)` in `src/coreclr/gc/gc.cpp:21486-21920`.

### B02 — direct allocation-budget elevation

- Location: `gc.cpp:21593-21607`.
- Exact expression: `get_new_allocation (i) <= 0`.
- `n` and `i` are signed `int`; `dynamic_data::new_allocation` is signed `ptrdiff_t`; `get_new_allocation` returns signed `ptrdiff_t`.
- In the non-check-only path, the loop evaluates `i = n + 1` through `max_generation` (or `max_generation - 1` when the max-generation check is disabled), tests the budget before assigning `n = i`, and breaks on the first positive budget.
- With `i == 1`, a true comparison promotes `n` to generation 1. The later fragmentation, low-ephemeral, memory, OOS, induced, and gen2 logic remains active and can supersede that value.
- C56 branch ID 2 remains the B02 identity.

### B12 — free-region/OOS-SOH control

- `try_get_new_free_region()` is implemented at `gc.cpp:21345` and called by the policy at `gc.cpp:21724-21730`.
- Exact trigger:

  `if (!try_get_new_free_region()) { last_gc_before_oom = TRUE; }`

- The final `last_gc_before_oom` elevation occurs later at `gc.cpp:21862-21875`.
- B02 is evaluated before B12. In the final winning C57 decision, however, `n_initial == 2`, so the B02 loop would start at `i == 3` and is not entered on this two-generation platform. B12 then records a false free-region result and the ordinary source path selects gen2.

The managed allocation audit follows ordinary `new byte[65536]` through NativeAOT `RhpNewArray` / `RhpNewArrayRare`, `RhpGcAlloc`, `GcAllocInternal`, `GCHeap::Alloc`, `gc_heap::allocate`, `try_allocate_more_space`, `allocate_soh`, `soh_try_fit`, region refill, and the normal GC trigger into `generation_to_condemn`. C57 records the result of the original `try_get_new_free_region()` call without suppressing, replacing, or retrying the decision.

## T1/T2 diagnostics

| Threshold | Definition | Final result |
| --- | --- | --- |
| T1 | `get_new_allocation(1) <= 0` | Not reached; no B02 observation or crossing record |
| T2 | `try_get_new_free_region()` returns false, making `last_gc_before_oom` eligible | Reached at policy 3, allocation ordinal `0xAD`, cohort 4 |

C57 emits bounded fixed-size policy, race, cohort, B02, B12, collection, override, resume, and completion records. Policy records are emitted only at actual policy observations; allocation-boundary state is retained scalarly, not logged as a per-allocation UART record. The source-backed B02 callback emits `C011EC57-B02-THRESHOLD` only when the exact signed source comparison is true and the direct `n: 0 -> 1` transition is observed.

## Workload search and caps

All strategies use ordinary managed allocations, deterministic sentinel/readback validation, rooted byte arrays, and no `GC.Collect`.

| Strategy | Survivor shape | Pressure shape | Result |
| --- | --- | --- | --- |
| S1 | 6 cohorts × 8 survivors | 24 transient allocations/cohort; 144 total | Clean bounded negative; two policy observations |
| S2 | 16 survivors × 3 active cohorts, then 3 pressure-tail cohorts holding 48 roots | 48 transient allocations/cohort; 288 total | Selected final profile; stable B12 preemption at policy 3 |
| S3 | 24 survivors × 2 active cohorts, then 4 pressure-tail cohorts holding 48 roots | 48 transient allocations/cohort; 288 total | Front-loaded run reached the QEMU bound before completion; no result used as final proof |

Hard caps:

- maximum strategies: 3
- maximum cohorts: 6
- maximum survivors: 48
- maximum retained aligned bytes: `0x300480` / 3,146,880
- maximum transient bytes: `0x1200000` / 18,874,368
- maximum explicit allocations: 288
- payload: `0x10000` / 65,536 bytes
- maximum natural collections: 256
- maximum QEMU runtime per boot: 90 seconds
- maximum diagnostic records: 2,048

The final S2 survivor sequence was `16, 32, 48, 48` roots. Retained aligned bytes were `0x100180`, `0x200300`, `0x300480`, and `0x300480`. Each cohort-end record reported promoted bytes `0x41F58`, sentinel/readback validity, and initial generation 0 followed by authentic post-collection generation observations.

## Final S2 policy progression

The three final boots agreed on this semantic sequence; absolute addresses vary by boot.

| Policy | Initial/final generation | Branch / reason | gen1 desired | gen1 new signed | T1 distance | Free-region state |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 0 → 0 | `0x1` | `0x1CDB68` | `+1891176` (`0x1CDB68`) | `+1891176` | observed 1, result 1 |
| 2 | 0 → 0 | `0x1` | `0x1CDB68` | `+1758808` (`0x1AD658`) | `+1758808` | observed 1, result 1 |
| 3 | 2 → 2 | `0xC`, reason `0x5`, blocking 1 | `0x1CDB68` | `+840968` (`0xCD508`) | `+840968` | observed 1, result 0; B12 eligible |

The policy-3 record also reports `n_alloc == 2`, `selectionCheckMemory == 1`, memory load `0x3B` (59%), high/very-high/medium thresholds 90%/97%/95%, gen1 fragmentation `0x198`, and `gen1BudgetDepletion == 0x100660`. Low-ephemeral, elevation, high-memory, very-high-memory, and high-fragmentation selection flags were false. The B12 marker carries `gen1NewAllocation == 0x1AD658`, the previous scalar policy value captured at the free-region call; the later policy record reports the final recomputed value `0xCD508` for the same decision.

No B02 record exists, so the following crossing fields are intentionally not applicable: pre-crossing value, crossing value, crossing cohort, crossing allocation ordinal, crossing collection ordinal, `b02NInitial`, pre-B02 `n`, post-B02 `n`, and later override. The authoritative state is `b02Observed=0`, `b02Crossed=0`, `directGen1Selected=0`, and `laterOverride=0`; `n_initial=2` and `n` remained 2 without a B02 evaluation.

## Collection, planner, generations, and tail

The observed older-generation collection was the C56 control path, not a direct C57 gen1 collection:

- trigger: ordinary managed `byte[65536]` allocation-context exhaustion/refill
- collection ordinal: `0x3`
- final condemned generation: `2`
- reason: `reason_oos_soh` (`0x5`)
- winning production path: B12 false free-region result, then the normal `last_gc_before_oom` blocking selection
- planner observed: yes (`0x1`); compacting/relocating chronology retained from C39/C40
- `fix_generation_bounds`: observed (`0x1`)
- `adjust_ephemeral_limits`: observed false (`0x0`)
- `RestartEE`: observed and valid
- managed resume: observed and valid
- bounded post-GC continuation: sentinel/readback validation and `GC.KeepAlive` completed

The C54 boundary publication remained healthy. Generation-boundary and ephemeral-segment fields are present in the manifest’s completion records. The representative final S2 tail was `[0x100900028, 0x100943000)`, segment `0x104010668`; the tail remained mapped and allocator-visible, eligibility remained false, and its recorded generation changed from 1 to 2 during the gen2 control collection. No segment retirement, recycling, tail consideration, tail selection, tail consumption, or forced reuse was performed. This secondary tail effect does not change the C57 classification.

Root scan, register/stack root handling, promotion, mark closure, final queue state, managed survivor integrity, and readback all passed through the inherited C26/C28/C39/C40/C54/C56 path. Survivor references remained valid and their identifying patterns remained valid.

## Regression and proof gates

The final manifest reports PASS for C18, C26, C28, C34, C37, C39, C40, C41, C46/C48, C49, C50, C53, C54, and C55. C56 remains a B12 control. C18 retained a valid transition frame, non-null `CoffNativeCodeManager`, `FindMethodInfo == 1`, correct FP publication/ownership, and fail-closed invalid-state behavior.

Validation completed:

- managed NativeAOT build/publish/link: PASS
- PowerShell parse: PASS
- XML and final manifest JSON parse: PASS
- locked runtime-source identity: PASS
- FP-patch identity: PASS
- semantic rewrite guard: PASS
- PE → ELF conversion: PASS
- linker/source/table guards: PASS
- `git diff --check`: PASS; Git reported only expected LF/CRLF normalization warnings
- ordinary artifact restoration: PASS

Final proof artifact hashes:

| Artifact | SHA-256 |
| --- | --- |
| proof kernel | `8DD0460173D6A0FDC9BCC37C05B7006DD9AEC752604C488A7ACD90B91834EC19` |
| PE | `87BE1134579B1EEF1D4E13597D1A714A93222FB85D1021A731412687B7084D8E` |
| ELF | `86B90FA57E31AF9C9DDF769647577067DFF6B66284378278E81D174209C40542` |
| map | `B853170CE9D1E2812EB481A99C327A6CB2E439242966FDC23B83D4636F03171F` |

Three fresh serial SHA-256 values:

1. `DD54358140D24986FD563D18FD105D7A41FA91AC5EBB84847AA0E523325957A7`
2. `C40C401547DBEADBA55EAAA98E11AB7884EBDC923D8BE531E23AAC3195C96555`
3. `3EA5EDE13F4A55C0066930C5C54571FB96A607456307E045DDBCB45823195473`

Ordinary restoration was verified by the harness: kernel and ESP both equal the canonical SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`; the proof-only artifact is inactive. Only C57-owned QEMU processes were cleaned up and unrelated QEMU was preserved.

## Closeout

Files changed for C57:

- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`
- `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`
- `samples/managed/HostLogProof/HostLogProof.csproj`
- `samples/managed/HostLogProof/Program.cs`
- `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`
- this document

The closeout commit is the focused C57 commit containing this document. It is not pushed. Final worktree and final divergence are reported by the closeout command and final response.

Remaining limitation: under the locked AMD64 Workstation/USE_REGIONS topology and the three predefined bounded profiles, the next older-generation decision begins at `n_initial == 2`; B02 therefore does not get a chance to consume gen1’s remaining positive budget before B12 escalates to gen2.

Next smallest milestone: preserve this exact source-backed T1/T2 instrumentation and vary only one bounded allocation-boundary/topology parameter that can produce a subsequent decision with `n_initial == 0`, without changing GC policy or the OOS path.

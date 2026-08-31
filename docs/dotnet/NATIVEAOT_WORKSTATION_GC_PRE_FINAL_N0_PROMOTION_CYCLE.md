# C011EC61 — NativeAOT pre-final-N0 promotion cycle

Date: 2026-08-31
Result: **Outcome C — a natural gen1 promotion/debit cycle was observed, but it remained after the final normal N0**
Success level: **2**

## Scope and locked identity

C011EC61 is the narrow follow-up to C011EC60's pre-last-N0 timing trace. The work adds bounded, scalar, source-accounting-only diagnostics and an ordinary managed workload. It does not change GC policy, generation selection, promotion accounting, free-region state, segment state, OOS behavior, or collection timing through explicit GC requests.

The proof uses the C52 productionized runtime-pack: NativeAOT/.NET 9.0.0, AMD64 Workstation GC, interfaces 5.3 / 2, runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and the unchanged durable `nativeaot-amd64-fp-handoff.patch` (`4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`). The synced C60 baseline was present at the start of the cycle; no prior history was rewritten.

## Definitions and temporal model

The C61 probe keeps the C58 caller-supplied `nInitial` and origin values. It defines:

* A **dynamic final N0 candidate** as the most recent normal policy entry with `nInitial=0` and `checkOnly=0`.
* **FINAL_N0** as that candidate frozen at the first later `nInitial=2`, `checkOnly=0`, `ORIGIN_FULL_OOS` policy entry.
* **FIRST_N2** as that first full/OOS N2 entry. The final C58 sequence remains `0,0,2`.
* A **promotion cycle** as a source-observed gen1 `PROMOTE` followed by the matching source-observed gen1 `GEN1-DEBIT`. The debit is attributed to promotion only when the chronology proves that preceding promotion.

The intended ordering is:

`PROMOTE < PROMOTION-TRANSFER < promotion-derived GEN1-DEBIT < FINAL_N0 < FIRST_N2`

The transfer marker is an explicit scalar correlation marker for the same global event ordinal as the source gen1 promotion. C61 records event ordinals from one fixed-size 512-record table; overflow is fail-closed.

## Bounded strategies

All strategies use the inherited C57 S2 source-accounting callbacks. P0 is the C60/C59 control workload. P1 and P2 use ordinary managed byte-array allocations, sentinel/readback validation, bounded survivor cohorts, and no explicit collection.

| Strategy | Workload shape | Purpose |
| --- | --- | --- |
| P0 | C57 direct-gen1 control | Preserve the C60 control path and confirm the harness boundary. |
| P1 | 16 early survivors; five main cohort attempts; 48 transient allocations per attempt; 48 retained survivors maximum; three 48-allocation plain-managed tail waves | Selected smallest bounded topology that produced a natural gen1 promotion/debit and then tested for another policy entry. |
| P2 | 16 early survivors; two main cohort attempts with 16 survivors per attempt; same transient and tail bounds | Optional survivor-topology check with a larger second cohort. |

The native C61 preflight bounds were fixed at 512 events, 48 survivors, `0x300480` retained bytes, and 48 transient allocations / `0x1200000` transient bytes per bounded wave. P1's tail is three separate 48-allocation waves and is plain managed pressure only after the promotion-derived debit is observed. The workload has no `GC.Collect`, internal GC entrypoint, GC-stress mode, condemned-generation override, policy mutation, promotion-counter write, free-region manipulation, segment manipulation, or OOS suppression.

## Source and shim audit

C61 is connected at the existing C58/C56 observation points:

* C58 entry and policy observations preserve the caller's generation/origin inputs and feed the dynamic N0 candidate/final-N0 state machine.
* C56's B02 observation is correlated to the C61 policy record but remains observational.
* The C57 source callbacks report survivor snapshots, promotion transfers, `compute_in` debit state, dynamic-data publication, and natural collection restart/resume.
* `guideXosNativeAotC011EC61PromotionReady` is an observational scalar readiness query used only to start the bounded post-promotion tail. Its NativeAOT ELF-side P/Invoke slot is explicitly bound in the runtime-pack table.

The authoritative accounting path remains the C60 source path: survivor bytes are read from source accounting, promotion transfer bytes are observed at source transfer points, and `compute_in` / dynamic-data callbacks report the natural `gc_new_allocation` and `new_allocation` state. C61 does not add a second accounting increment or claim byte-for-byte linearity between independent source counters.

## P1 result and chronology

The authoritative result is the three-fresh-boot P1 run in `out/dotnet/c011ec61-pre-final-n0-promotion-cycle/run-20260831-151442677`. All three runs reached the C61 safe stop with Outcome C and Level 2; the marker semantics agreed.

The first boot's key chronology was:

| Event | Ordinal | Entry / collection | Meaning |
| --- | ---: | ---: | --- |
| generic GEN1-DEBIT | `0x0C` | entry 1 / collection 2 | Earlier generic gen1 debit, not attributed to a preceding C61 gen1 promotion. |
| dynamic N0 candidate | `0x0D` | entry 2 / collection 3 | Candidate later frozen as FINAL_N0. |
| policy observation | `0x0E` | entry 2 / collection 3 | Normal N0 policy entry; free-region path remained healthy. |
| gen1 PROMOTE | `0x15` | entry 2 / collection 3 | First source-observed gen1 promotion transfer. |
| PROMOTION-TRANSFER | `0x15` | entry 2 / collection 3 | Explicit transfer correlation marker. |
| promotion-derived GEN1-DEBIT | `0x19` | entry 2 / collection 3 | First promotion-attributed debit; `0xE0150` debit bytes. |
| FIRST_N2 | `0x1A` | entry 3 / collection 4 | First `nInitial=2`, full/OOS policy entry; it freezes entry 2 as FINAL_N0. |

The C61 completion marker reported 88 events, no overflow, three policy entries, two N0 entries, one N2 entry, `promotionCycleCount=2`, first promotion `0x15`, first promotion-derived debit `0x19`, FINAL_N0 event `0x0D`, FINAL_N0 policy event `0x0E`, FIRST_N2 event `0x1A`, and zero post-promotion normal N0 entries. The second counted promotion cycle occurred during the later N2 collection; it does not make the first final N0 post-promotion.

The required temporal invariant failed with `failedRelation=6`: `firstPromotionDebitEventOrdinal 0x19 >= finalN0EventOrdinal 0x0D`. This is a precise topology result: after the first promotion-derived debit, the next observed policy entry was directly full/OOS N2. There was no distinct ordinary N0 policy entry after the debit. P2 reproduced the same direct-N2 topology; P0 remained the control.

## Final-N0 budget and B02

At FINAL_N0, the observed gen1 values were:

* desired allocation: `0x1CDB68`
* `gc_new_allocation`: `0x1AD658`
* raw `new_allocation`: `0x1AD658`
* signed `new_allocation`: `+0x1AD658`
* B02 margin: `0x1AD658`
* promoted bytes incorporated: `0x10018`
* free-region observed/result: `1/1`
* `try_get_new_free_region`: `1`
* B12 eligible: `0`
* `last_gc_before_oom`: `0`

The C60 control FINAL_N0 budget was also `0x1AD658`. Therefore the measured C61 timing gain is zero: C61 did not move a promotion-derived debit ahead of FINAL_N0 and did not improve the final-N0 budget.

B02 was not observed or crossed. Its event ordinal, policy-entry ordinal, raw/signed values, pre/post `n`, and later override fields remained zero. The first full/OOS N2 selected final condemned generation `2`; no direct-gen1 claim is made.

The first promotion cycle's source values were `survivorCount=0x10`, retained bytes `0x100180`, `generationAllocationSize=0xE0150`, gen1 `gc_new_allocation` before `0x1AD658`, debit bytes `0xE0150`, and gen1 `gc_new_allocation` / raw-signed new allocation after `0xCD508`. The complete lifecycle totals were promoted bytes `0x210318`, promotion-derived debit bytes `0x2E0450`, and two observed promotion/debit cycles.

## Collection, tail, and regressions

The authentic later N2 collection completed through the inherited C54/C55/C56 path. The final condemned generation was `2`; planner selection, compaction/relocation, generation-bound repair, `RestartEE`, managed resume, and bounded continuation were observed. No forced collection, generation override, GC policy mutation, free-region manipulation, segment manipulation, or OOS suppression was used.

The C54 tail interval was `[0x100900028, 0x100943000)`, segment `0x104010668`, generation `1 → 2`, still mapped and allocator-visible, but not eligible, selected, retired, or recycled. Ordinary reclamation/reuse was not forced.

The manifest regression gate passed C18, C26, C28, C34, C37, C39, C40, C41, C53, C54, C55, C56, C57, C58, and C60. The semantic rewrite guard passed. Invariant failures, sensitive diagnostic allocations, fail-fast, and page-fault fields were zero.

## Reproduction and artifacts

QEMU was version 11.0.0 (`v11.0.0-12122-ga4bb4b10c9`). The final P1 run used three fresh boots and `semanticAgreement=true`.

Serial SHA-256 values:

* `6D1D5475643C559CA70D86913E7B1D1BA1973B8A945FA96582A0FE45FEE1617F`
* `CD9C2E102193962086490B7E3968F1861B346DF43B1AD8124F084C56A0B6C116`
* `C9B47FBBB14F3FD576F4B811C181B8AE708AEF727D2EA068F080C2B03B30EA08`

Proof payload hashes:

* kernel: `4613A254BD2D76A78E590B2393AAFA04C947F08B69C6D174DF9D3EA9C2AC7BD1`
* PE: `4A86B6EABC7F1E1E95998054C1732738AF724F3F0667B6A7D6EC1F39AFB0B434`
* ELF: `EF98BABFFFA2DB0EABF5F491CD82C95021EA2FE9DA53EB44E8BA65E4A5B1DC4F`
* map: `D34E7CB52D3CBCB537455DDE5A390DBD81111E954F68D90B4622F7485F80F0F9`

The evidence manifest and exact command log are:

* `out/dotnet/c011ec61-pre-final-n0-promotion-cycle/run-20260831-151442677/manifest.json`
* `out/dotnet/c011ec61-pre-final-n0-promotion-cycle/run-20260831-151442677/commands.txt`

Static/release checks passed: managed NativeAOT build, PowerShell parse, JSON/XML parse, locked source/runtime identity, FP patch identity, semantic rewrite guard, PE→ELF, linker/source/table guards, and `git diff --check`. MASM was not applicable. Ordinary kernel and ESP restoration completed in `finally`; both live files and their expected restored image matched SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`. The proof-only artifact is inactive, C61 cleanup stopped only C61-owned QEMU processes, and unrelated QEMU was preserved.

## C62 recommendation

The smallest next experiment is not more raw pressure. C62 should find the smallest ordinary survivor-lifetime or cohort-schedule change that creates a distinct normal N0 policy entry after the `0x19` promotion-derived debit and before full/OOS N2, while preserving `0,0,2`, M2 headroom, source accounting, and the C54/C55/C56 continuation. The current C61 result classifies the missing transition as a promotion/refill topology that goes directly to N2; a future cycle should isolate that transition causally instead of increasing tail pressure broadly.

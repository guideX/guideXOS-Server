# NativeAOT Workstation GC C90 — Allocation Fit Ledger, Four-Region Prediction, and Commit Closure

Status: **Outcome F — narrowed quantitative residual remains**
Success level: **Level 2**
Date: 2026-09-07

## Executive result

C89 already located the authentic T320 region events and the exact one-page endpoint. C90 was therefore a source audit plus offline arithmetic exercise, not another allocator-observer phase.

The locked production fit rule is:

```text
(allocation_limit - allocation_pointer) >=
    (request_size + Align(min_obj_size, align_const))
```

For this workload, `request_size = 0x4018` and `Align(min_obj_size, align_const) = 0x18`, so every fit consumes exactly `0x4030`. The 104-request difference is therefore:

```text
104 * 0x4018 = 0x1A09C0       raw requested bytes
104 * 0x4030 = 0x1A1380       fit-cost bytes
104 * 0x18   = 0x0009C0       fit-pad delta
```

C90 closes the carry-in arithmetic, the authenticated region capacities, and the `0x21000` versus `0x1000` commit divergence. It does not honestly close the requested *independent four-region prediction from the 104-request delta alone*: at the exact T320 request-217 carry-in, only the `+0x1600000` acquisition remains ahead, and it is predicted after 46 requests. The other three observed acquisitions are earlier in the T320 chronology (`tail 80`, `tail 143`, and `tail 203`) and depend on the already-consumed/reset allocation topology. Thus the remaining limitation is chronology/topology, not an unexplained byte arithmetic error.

## 1. Repository and Git state

1. Outcome: **F — narrowed quantitative residual remains**.
2. Success Level: **Level 2** — commit-growth divergence is source-backed; the exact four-region count is not independently derivable from the 104-request delta without using observed chronology/topology.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `bb383ce55ccf50d49d61287f2c5d026f7f6ecbf8`.
6. Starting subject: `Quantify NativeAOT allocation OOM arithmetic`.
7. Final HEAD: recorded after the local C90 commit in the final closeout section.
8. Final subject: recorded after the local C90 commit in the final closeout section.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0` (live state; this differed from the requested expected `1/0`).
11. Final ahead/behind: `1/0` after the local commit; not pushed.
12. Starting worktree: clean.
13. Final worktree: clean.
14. Runtime identity: NativeAOT 9.0.0 AMD64 Workstation GC, GC interfaces 5.3/2.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. FP patch SHA: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. C88 SHA: `ad0b9ece1c624fc3fa3f68d0282380e6fb3250ca`.
18. C89 SHA: `bb383ce55ccf50d49d61287f2c5d026f7f6ecbf8`.
19. C90 SHA: the local commit recorded after commit; source/runtime SHA remains unchanged.
20. Exact C90 question: can authentic allocator state and existing sparse records independently predict four T320 extra basic-region acquisitions, no corresponding T216 acquisitions, `0x21000` versus `0x1000`, and one-page-OOM eligibility? Answer: commit divergence and post-216 carry-in are closed; the four-count is not a demand-only consequence of the 104-request delta.

## 2. Baselines and fit arithmetic

21. T320 baseline: `15mid8`, 320 tails, 15 retained, promoted bytes `0xFD980`, result `1/1/1`, four authenticated additional ordinary acquisitions, final `committedSize = 0x1000`, `oomEligible = 1`, 3/3.
22. T216 baseline: same retained workload, 216 tails, result `1/6/6`, no T320 EXTRA0–EXTRA3 tail acquisitions, final `committedSize = 0x21000`, `oomEligible = 0`, 3/3.
23. Raw request size: `0x4018` (`0x4000` payload plus the recorded object/header request component).
24. Fit quantum: `0x4030`.
25. Exact `0x18` semantics: `Align(min_obj_size, align_const)`, the aligned minimum-object gap/pad required by the production allocation-context fit rule. It is not generic diagnostic overhead and is not the `allocation_quantum`.
26. Tail-count delta: `320 - 216 = 104 = 0x68`.
27. Raw request delta: `104 * 0x4018 = 0x1A09C0`.
28. Fit-cost delta: `104 * 0x4030 = 0x1A1380`.
29. Fit-overhead delta: `104 * 0x18 = 0x9C0`.
30. `104 * 0x4018` verification: `0x4018 * 0x68 = 0x1A09C0`.
31. `104 * 0x4030` verification: `0x4030 * 0x68 = 0x1A1380`.
32. `104 * 0x18` verification: `0x18 * 0x68 = 0x9C0`.

The exact source expression is in `src/coreclr/gc/gc.cpp`, `a_size_fit_p`:

```cpp
(alloc_limit - alloc_pointer) >= (size + Align(min_obj_size, align_const))
```

On AMD64, `min_obj_size` is two pointers plus `ObjHeader`: `8 + 8 + 8 = 0x18`, already aligned to the 8-byte object alignment. `adjust_limit_clr` names the same value `aligned_min_obj_size`, and its contiguous-allocation-context path describes making the minimum-object gap. `limit_from_size` adds that pad to the requested size. The production semantic name used here is therefore **aligned minimum-object gap**, not “overhead”.

## 3. Exact carry-in state at T320 request 217

33. Tail request 216 ending allocation pointer: `0x1015382B0` from the accepted T320 C64 return record (the observer’s post-return context fields are not used as the fit limit).
34. Tail request 216 ending allocation limit: the accepted C64 `allocationLimitAfter` field is `0x1015382B0` (equal to its serialized `allocationPointerAfter`). That observer field is not used as the production fit limit; the source-derived fit end for the current committed region is `0x1015F0FE8`, equal to `0x1015F1000 - 0x18`.
35. Carry-in headroom before request 217: `0x1015F0FE8 - 0x1015382B0 = 0xB8D38`.
36. Carry-in `0x4030` quanta available: `floor(0xB8D38 / 0x4030) = 46` (`0x2E`).
37. Carry-in remainder: `0xB8D38 % 0x4030 = 0x498`.

The exact logical point is the current `+0x1500000` basic region after T320 tail request 216 and immediately before request 217. Its normalized range is `[0x101500000, 0x101600000)`, and its selected committed end is `0x1015F1000`. The first 46 requests of the additional interval, requests 217 through 262, consume:

```text
46 * 0x4030 = 0xB88A0
0xB8D38 - 0xB88A0 = 0x498
```

Request 263 is therefore the first request that cannot fit in the carry-in context. This independently predicts the recorded `+0x1600000` acquisition at tail 263.

## 4. Authenticated region capacities and boundary ledger

The region table uses an exclusive usable end: usable bytes are `usable_end - usable_start`. The fit end is the committed end minus `0x18`; the region start includes the `0x28` aligned plug-and-gap header.

| Region | Nominal bytes | Usable start | Usable end | Usable bytes | Lost bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| EXTRA0 `+0x1400000` | `0x100000` | `0x101400028` | `0x1014F0FE8` | `0xF0FC0` | `0xF040` |
| EXTRA1 `+0x1500000` | `0x100000` | `0x101500028` | `0x1015F0FE8` | `0xF0FC0` | `0xF040` |
| EXTRA2 `+0x1600000` | `0x100000` | `0x101600028` | `0x1016F0FE8` | `0xF0FC0` | `0xF040` |
| EXTRA3 `+0x1700000` | `0x100000` | `0x101700028` | `0x1017FFFE8` | `0xFFFC0` | `0x40` |

The full-fit span is `0x100000 - 0x28 - 0x18 = 0xFFFC0`. EXTRA0–EXTRA2 have a committed end at `region_start + 0xF1000`, leaving a variable `0xF000` uncommitted suffix; their fixed geometric loss is `0x40` and their total observed loss is `0xF040`. EXTRA3 is committed through the nominal end, so its total loss is the fixed `0x40`. The source’s effective maximum request bound has a further second minimum-object term and is `0xFFFA8`; that bound is distinct from the observed committed fit span.

For a fresh region context, the effective fit budget is one `0x18` larger than the usable fit span because the first request’s limit calculation leaves the minimum-object gap at the end:

```text
EXTRA0–EXTRA2 context budget = 0xF0FD8 = 60 * 0x4030 + 0x498
EXTRA3 context budget         = 0xFFFD8 = 63 * 0x4030 + 0x3408
```

The authenticated chronological ledger is:

| Interval/context | Starting fit budget | T320 requests in interval | Fit cost | Remainder | Next acquisition |
| --- | ---: | ---: | ---: | ---: | --- |
| `+0x1700000` after reset/reuse, tail 80–142 | `0xFFFD8` | 63 | `0xFCBD0` | `0x3408` | `+0x1400000` at tail 143 |
| EXTRA0 `+0x1400000`, tail 143–202 | `0xF0FD8` | 60 | `0xF0B40` | `0x498` | EXTRA1 at tail 203 |
| EXTRA1 `+0x1500000`, tail 203–262 | `0xF0FD8` | 60 | `0xF0B40` | `0x498` | EXTRA2 at tail 263 |
| Carry-in `+0x1500000`, tail 217–262 | `0xB8D38` | 46 | `0xB88A0` | `0x498` | EXTRA2 at tail 263 |
| EXTRA2 `+0x1600000`, tail 263–320 | `0xF0FD8` | 58 | `0xE8AE0` | `0x84F8` | none by tail 320 |

The requested label order `Carry-in → EXTRA0 → EXTRA1 → EXTRA2 → EXTRA3` is not the authentic temporal order. The source records show `+0x1700000 → +0x1800000 → +0x1700000 (reset/reuse) → +0x1400000 → +0x1500000 → +0x1600000`. C90 preserves that distinction.

38. First EXTRA0-triggering request ordinal: T320 tail 143, C64 allocation ordinal `0xD2`; the region is normalized `+0x1400000`.
39. EXTRA0 headroom before that request: `0x3408` in the preceding `+0x1700000` context.
40. EXTRA0 shortfall: `0x4030 - 0x3408 = 0xC28`.
41. EXTRA0 nominal bytes: `0x100000`.
42. EXTRA0 usable bytes: `0xF0FC0` geometric fit span; `0xF0FD8` effective context budget.
43. EXTRA0 boundary remainder: `0x498` after tails 143–202.
44. EXTRA1 trigger ordinal: T320 tail 203, C64 allocation ordinal `0x10E`.
45. EXTRA1 usable bytes: `0xF0FC0` geometric span; `0xF0FD8` effective context budget.
46. EXTRA1 boundary remainder: `0x498`.
47. EXTRA2 trigger ordinal: T320 tail 263, C64 allocation ordinal `0x14A`.
48. EXTRA2 usable bytes: `0xF0FC0` geometric span; `0xF0FD8` effective context budget.
49. EXTRA2 boundary remainder: `0x84F8` final unused budget after the 58 requests through tail 320; no next boundary occurs in this run.
50. EXTRA3 trigger ordinal: T320 tail 80 in the chronological trace, C64 allocation ordinal `0x93`; it is the reset/reuse context that precedes the tail-143 EXTRA0 event.
51. EXTRA3 usable bytes: `0xFFFC0` geometric span; `0xFFFD8` effective context budget.
52. EXTRA3 boundary remainder: `0x3408` before tail 143.
53. Sum usable EXTRA0–3 bytes: `0xF0FC0 * 3 + 0xFFFC0 = 0x3D02A0` geometric spans; effective context budgets sum to `0x3D0358`.
54. Sum boundary waste at authenticated acquisition boundaries: `0x3408 + 0x498 + 0x498 = 0x3D38`. The post-tail-320 residual is tracked separately as `0x84F8`.
55. Requests accounted in carry-in: 46 (requests 217–262).
56. Requests accounted in EXTRA0: 60 (tails 143–202, earlier in the full run).
57. Requests accounted in EXTRA1: 60 (tails 203–262, earlier in the full run).
58. Requests accounted in EXTRA2: 58 (tails 263–320).
59. Requests accounted in EXTRA3: 63 (tails 80–142, after reset/reuse).
60. Total additional requests accounted: 104 in the exact post-216 interval (`46 + 58`); the full chronology’s region intervals account for 320 tail requests (`31 + 48 + 63 + 60 + 60 + 58`).

The exact post-216 conservation equation is:

```text
carry-in budget + EXTRA2 budget
  = 0xB8D38 + 0xF0FD8
  = 0x1A8D10

104 fit cost + final residual
  = 0x1A1380 + 0x8990
  = 0x1A8D10
```

Here `0x8990 = 0x498 + 0x84F8`: the carry-in boundary remainder plus the final EXTRA2 remainder. Equivalently, the consumed fit costs are `0xB88A0 + 0xE8AE0 = 0x1A1380`. No bytes are hidden in this post-216 ledger.

## 5. Independent four-region prediction and scale mismatch

61. Independent predicted extra-region count from the exact request-217 carry-in: **1** additional acquisition (EXTRA2 at request 263).
62. Observed extra-region count in the complete T320 run: **4** (`+0x1400000`, `+0x1500000`, `+0x1600000`, `+0x1700000`).
63. Prediction matches: **No, for the narrowly specified post-216 demand-only question**. The post-216 prediction is exact for its one remaining crossing; the observed total four is exact for the complete chronology.
64. Explanation for four regions despite approximately 1.6 MiB raw delta: the 104-request delta is not the complete T320 allocation chronology. Three observed acquisitions occur before the request-217 comparison point; the trace also switches collection/context and reuses `+0x1700000`. A nominal 1 MiB region is not a full 1 MiB fit span, and the `+0x140`–`+0x160` regions have only `0xF0FC0` usable committed span. The four count is therefore a topology/chronology result, not four fresh 1 MiB purchases paid for solely by `0x1A09C0`.
65. Interleaved allocator activity exists: **yes, in the authenticated allocation chronology** — context/region switching occurs at tails 32, 80, 143, 203, and 263, with collection 3 ending at tail 80 and collection 4 continuing thereafter.
66. Interleaved activity byte contribution: **`0x0` separately authenticated non-tail bytes**. The available C64 ledger authenticates tail allocations and context transitions, but does not expose a separate non-tail allocation-byte scalar. C90 assigns no invented non-tail bytes; the full-tail fit cost is `320 * 0x4030 = 0x503C00`, and the cross-cell delta is exactly `0x1A1380`.
67. Basic region nominal size: `0x100000`.
68. Basic region actual usable span: `0xF0FC0` for EXTRA0–EXTRA2 and `0xFFFC0` for EXTRA3, with effective context budgets `0xF0FD8` and `0xFFFD8` respectively.
69. Fixed per-region loss: `0x40` from the `0x28` region header plus the terminal `0x18` fit gap; the source effective maximum-request bound additionally exposes `0x58` (`0x28 + 2*0x18`).
70. Variable per-region loss: `0xF000` committed suffix on EXTRA0–EXTRA2; zero on EXTRA3 in the observed committed boundary records.

The impossible part is not arithmetic: treating all four acquisitions as sequential intervals beginning at request 217 contradicts the accepted C64 ordinals and pointers. That is the remaining C90 residual.

## 6. T216 commit-growth ledger

71. T216 commit-request source expression: the fit path computes the required committed end from the high address of the target allocation, subtracts the current committed end, then page-aligns the shortfall before `grow_heap_segment` commits it. The relevant source is `a_fit_segment_end_p` / `grow_heap_segment` in `src/coreclr/gc/gc.cpp`.
72. T216 raw commit shortfall: `0x1F190`.
73. T216 raw shortfall `0x1F190` verified: target high address `0x101A20190` minus pre-growth committed end `0x101A01000` equals `0x1F190`.
74. Page-aligned commit request: `align_on_page(0x1F190) = 0x20000`.
75. `align_on_page(0x1F190)` result: `0x20000`.
76. T216 pre-growth committedSize: `0x1000` (`0x101A01000 - 0x101A00000`).
77. T216 growth bytes: `0x20000`, 32 pages.
78. T216 post-growth committedSize: `0x21000`.
79. `0x21000` independently predicted: `0x1000 + 0x20000 = 0x21000`.
80. `0x21000` observed: yes, in all three accepted T216 confirmations.

The T216 target’s eight tail allocations are in its target context around `+0x1A00000`; the highest required address is `region_start + 0x20190`. The existing initial committed page is `region_start + 0x1000`, so the exact raw shortfall is `0x1F190`. Page alignment yields a successful `0x20000` commit, taking the normalized committed size to `0x21000`. Because the target region can satisfy the request after this successful commit, T216 does not consume T320’s EXTRA0–EXTRA3 sequence.

## 7. T320 commit path and the `0x20000` divergence

81. T320 equivalent commit operation reached: the terminal `a_fit_segment_end_p` / `soh_try_fit` no-region path on EXTRA4 is reached after the ordinary region sequence is exhausted; no successful commit-growth operation is reached for the target context.
82. T320 equivalent commit request: none successfully issued for the decisive EXTRA4 context; the target remains at its initial page.
83. T320 commit operation result: no commit growth; `commitFailed = 1`, `regionResult = 0`, `expansionAttempted = 1`, `expansionSucceeded = 0` in the accepted C65 terminal record.
84. T320 final committedSize: `0x1000`.
85. `0x1000` independently predicted: yes — the target region is created with `SEGMENT_INITIAL_COMMIT = OS_PAGE_SIZE` and the decisive path has no successful grow operand.
86. `0x1000` observed: yes, in all three accepted T320 confirmations.
87. Exact committedSize delta: `0x21000 - 0x1000 = 0x20000`.
88. `0x20000` divergence source operation: the successful T216 page-aligned commit of `0x20000`; there is no corresponding successful T320 target commit.
89. `0x20000` equals page count: yes, `0x20000 / 0x1000 = 0x20` pages.
90. Page count: 32 4-KiB pages.
91. Why T216 receives 32 pages: its high target allocation address produces the exact `0x1F190` shortfall, and `align_on_page` rounds that positive shortfall to `0x20000`.
92. Why T320 does not: T320 reaches the terminal no-region/OOM path on the target context with no successful commit-growth operation; it therefore retains only the initial page.

This is the exact source-backed explanation for the apparent `0x20000` scale mismatch. It is not an unexplained observed constant and is not attributed to a changed commit rule.

## 8. OOM flag terminology and one-page conjunction

| Name | Kind/meaning | T320 | T216 | Set | Consumed |
| --- | --- | ---: | ---: | --- | --- |
| `last_gc_before_oom` | `gc_heap` historical source flag: the last-GC-before-OOM condition | 1 | 1 | `trigger_full_compact_gc` and `generation_to_condemn` paths | assigned into the single-heap joined local; also exposed by the C65 diagnostic |
| `joined_last_gc_before_oom` | local joined flag used by aged-region distribution; in this single-heap build it is assigned `last_gc_before_oom` | 1 | 1 | `distribute_free_regions`: `joined_last_gc_before_oom = last_gc_before_oom` | C88 one-page predicate |
| `oomEligible` | C88/C89 derived observer/harness boolean, not a production source flag | 1 | 0 | derived from committed size plus joined flag | final semantic result |

93. `last_gc_before_oom` source variable: `gc_heap::last_gc_before_oom`.
94. T320 value: 1.
95. T216 value: 1.
96. `joined_last_gc_before_oom` source variable: local `BOOL` in `distribute_free_regions`.
97. T320 value: 1 by the single-heap source assignment and C89 source record.
98. T216 value: 1 by the same production assignment; the old C88 serialized/derived field must not be read as this local source flag.
99. Flag-setting source: `last_gc_before_oom` is set true in the full-compaction / failed-free-region paths; `distribute_free_regions` copies it to the joined local in the single-heap configuration.
100. Harness `oomEligible` derivation: `committedSize == 0x1000 && joined_last_gc_before_oom`; the C89 manifests report the resulting 1/0 values.
101. Flag terminology reconciled: historical source flag, joined local, and harness `oomEligible` are distinct. C88’s decisive branch consumes the joined local; `oomEligible` is the harness/reporting result. A T216 `oomEligible=0` does not imply its source `joined_last_gc_before_oom` was zero; its committed-size operand already fails.
102. Exact C88 predicate: `age >= 20 || (committedSize == 0x1000 && joined_last_gc_before_oom)`.
103. T320 age operand: age `0`; first branch false.
104. T320 committedSize operand: `0x1000`; second-branch size test true.
105. T320 joined-OOM operand: `1`; second-branch flag test true.
106. T320 result: `0 || (true && true) = 1`; EXTRA4 decommit/transfer eligibility and final result `1/1/1`.
107. T216 age operand: age `0`; first branch false.
108. T216 committedSize operand: `0x21000`; second-branch size test false.
109. T216 joined-OOM operand: production local is 1, but it is irrelevant because the size test is false.
110. T216 result: `0 || (false && true) = 0`; no one-page OOM eligibility and final result `1/6/6`.

## 9. Full ledger conservation and causal status

111. Full T320 ledger: the accepted C64 trace partitions all 320 tail requests as `31 (+0x170, tails 1–31) + 48 (+0x180, tails 32–79) + 63 (+0x170 reset/reuse, tails 80–142) + 60 (+0x140, tails 143–202) + 60 (+0x150, tails 203–262) + 58 (+0x160, tails 263–320) = 320`. The exact post-216 rows are requests 217–262 (46 fit quanta, `0xB88A0`) and 263–320 (58 fit quanta, `0xE8AE0`), followed by residual `0x84F8`.
112. Full T216 ledger: accepted C64 addresses partition tails as `31 (+0x170, 1–31) + 63 (+0x180, 32–94) + 63 (+0x190, 95–157) + 8 (+0x1A target, 158–165) + 51 (+0x190, 166–216) = 216`; no `+0x140`–`+0x160` tail acquisitions occur.
113. Cross-cell fit-cost conservation: `320 * 0x4030 - 216 * 0x4030 = 104 * 0x4030 = 0x1A1380`; raw requested-byte conservation independently gives `0x1A09C0`; their exact difference is `0x9C0`.
114. Ledger residual: post-216 carry-in plus EXTRA2 residual `0x8990` (`0x498 + 0x84F8`); full chronology has no unaccounted fit bytes in the authenticated ranges.
115. Residual explanation: `0x8990` is explicit unused fit budget, not unexplained demand. The remaining Level-3 residual is a missing topology/chronology scalar for predicting the three earlier crossings from the 104-request delta alone.
116. Four-region causal status: observed and authenticated, but not independently predicted by the post-216 delta alone.
117. Carry-in headroom causal status: closed; 46 quanta plus `0x498` exactly predicts tail-263 crossing.
118. Region usable-capacity causal status: closed for all four authenticated regions.
119. Fit overhead causal status: closed; exact aligned minimum-object gap `0x18`.
120. Commit growth causal status: closed; T216 `0x1F190 → 0x20000`, T320 no successful growth.
121. One-page state causal status: closed; initial page plus absent T320 growth predicts `0x1000`.
122. Joined-OOM causal status: closed at source-provenance level; single-heap joined local equals historical source flag, and the size operand separates T320/T216.
123. Unified allocator/OOM explanation: tail count increases exact fit demand; prior context consumption and reset/reuse determine four chronological crossings; T216’s target succeeds a `0x20000` page-aligned commit while T320’s target remains at one page; the C88 conjunction therefore accepts T320 and rejects T216, producing EXTRA4 decommit and `1` versus `6`.
124. First supported causal link: tail-count delta → exact `0x4030` fit-cost delta.
125. Strongest causal chain: `104 * 0x4030` → carry-in `0xB8D38` → 46 fits → tail-263 EXTRA2 acquisition; separately `0x1F190 → 0x20000` T216 commit versus no T320 commit → `0x21000` versus `0x1000` → one-page conjunction.
126. First unsupported causal link: deriving all four acquisitions solely from the post-216 104-request delta without importing earlier context/reset chronology.
127. Missing scalar required: exact pre-request-217 allocation-context/topology history sufficient to predict the three earlier T320 acquisitions from a counterfactual T216/T320 comparison.
128. Missing scalar source: not separately serialized by C64/C65; the accepted records expose the resulting context/region addresses and ordinals, not a complete free-region/context-consumption state before each earlier crossing.
129. Additional observer required: no; adding one would risk changing topology, and the existing evidence is enough for the supported Level-2 closure.
130. Observer BSS delta: `0x0`; no C90 observer was added.
131. Source-derived boundary test required: no; no exact four-region threshold can be derived without the missing topology scalar.
132. Derived tail count: not produced; selecting an adjacent value would violate the no-empirical-sweep restriction because the required threshold is not source-derived.
133. Predicted boundary behavior: the exact supported boundary is request/tail 263: 46 post-216 fits, then EXTRA2; EXTRA2 has 58 requests remaining and no next crossing by tail 320.
134. Observed boundary behavior: tail 263 does acquire `+0x1600000`; no later ordinary acquisition occurs before terminal EXTRA4.
135. Prediction correct: yes for the supported carry-in boundary; no claim is made for an ungrounded four-region threshold.
136. Candidate-path relevance: not causal to this arc; the evidence continues to support allocator/commit/OOM rather than candidate rejection.
137. B02 evaluated: no.
138. B02 status: **STILL_PREMATURE**.

## 10. Layout, mutation, and invariants

139. C90 baseline BSS: inherited per-cell baselines — ONE `0x4F0840`, SIX `0x3947E0`; proof-kernel BSS `0x062A8770`.
140. C90 BSS: unchanged from C89.
141. BSS delta: `0x0`.
142. Production mutation: none.
143. Allocator mutation: none.
144. Context mutation: none.
145. Commit mutation: none.
146. Request-size mutation: none.
147. Fit-rule mutation: none.
148. Region mutation: none.
149. Free-list mutation: none.
150. OOM mutation: none.
151. Decommit mutation: none.
152. Planner mutation: none.
153. Candidate mutation: none.
154. Promotion mutation: none.
155. Survivor fabrication: none.
156. Root fabrication: none.
157. C18: unchanged/inherited; no C90 semantic change.
158. Code manager: unchanged/inherited.
159. `FindMethodInfo`: unchanged/inherited.
160. Root scan: unchanged/inherited.
161. Mark closure: unchanged/inherited.
162. Planner authenticity: preserved.
163. Survivor integrity: preserved.
164. C90 invariant failures: none observed in imported confirmations.
165. Sensitive diagnostic allocations: no new diagnostic allocation.
166. C90 event capacity: unchanged from C89.
167. C90 peak events: unchanged from C89 accepted confirmations.
168. C90 overflow: none.
169. Fail-fast: preserved.
170. Page faults: no C90-induced page-fault change; imported run records remain valid.

## 11. Confirmations and validation

No runtime composition changed between C89 and C90. Under the accepted evidence policy, C89’s three fresh confirmations per cell are reused rather than rerun.

171. T320 Boot 1: accepted C89 run hash `88D652BA2D6C67D50F8388E8D85029F9C30ADBFFCD9E151C8F0C28DD8BAFC0EB`; `1/1/1`, 4 boundaries, `0x1000`, OOM eligible.
172. T320 Boot 2: accepted C89 run hash `FE252C974824AC4DF19AAB066E7CD70CD3439CE5ED8E340991D007248D7BB62E`; same semantics.
173. T320 Boot 3: accepted C89 run hash `FF1A738BAD5ECBF152DB0E69C70BAF2731ADA5A286E48B3A973E22AD144110F3`; same semantics.
174. T216 Boot 1: accepted C89 run hash `6C6856DCD76904067BE0C7693DD295764B043CD34E52C3C2CD446633C04FF8B3`; `1/6/6`, 0 EXTRA0–EXTRA3 tail boundaries, `0x21000`, not OOM eligible.
175. T216 Boot 2: accepted C89 run hash `847616C8C5A3C36DDA42BC5B0969EFDDFE2E17EF38FD28C8DFF4940B0D290D96`; same semantics.
176. T216 Boot 3: accepted C89 run hash `9924E275E9EDCC3F0C83F03E5F2F33F5AA7A906A87AFB97AD099FCA82B1892B3`; same semantics.
177. Semantic agreement: 3/3 per cell; T320 `1/1/1`, T216 `1/6/6`.
178. Nondeterminism: none in the accepted semantic fields; serial hashes differ as expected for fresh boots.
179. Serial hashes: the six hashes above are retained in the C90 evidence import manifest.
180. Artifact hashes: ordinary kernel SHA verified as `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`; ESP restored and verified against the same accepted ordinary artifact set.
181. Runtime-pack validation: inherited C89 pass; C90 made no runtime-pack change.
182. Managed build: inherited C89 pass; no managed source changed.
183. Native build: inherited C89 pass; no native source changed.
184. PowerShell syntax: inherited C89 pass; C90 added documentation/evidence only.
185. JSON/XML parse: accepted manifests and existing configuration parse successfully; no C90 JSON/XML source was added.
186. `git diff --check`: required and run at closeout; pass.
187. PE → ELF conversion: inherited C89 pass; no binary change.
188. Symbol checks: inherited C89 pass.
189. Linker/source/table/archive guards: inherited C89 pass.
190. C52 Tier-All result or reason omitted: omitted because C90 is documentation/offline arithmetic and does not alter the semantic runtime composition; B02 remains premature.
191. Ordinary restoration: ordinary kernel and ESP restored; C90-owned proof/QEMU state not left active.
192. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
193. Ordinary ESP SHA: verified against the accepted ordinary ESP artifact; no C90 mutation remains.
194. Proof artifact active: no.
195. C90-owned QEMU cleanup: count 0 at closeout.
196. Unrelated QEMU preservation: unrelated processes were not targeted or removed.

## 12. Evidence and Git closeout

197. Files changed: this tracked report; ignored C90 evidence files under `out/dotnet/c011ec90-allocation-fit-ledger-commit-closure/`.
198. Documentation path: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C90_ALLOCATION_FIT_LEDGER_COMMIT_CLOSURE.md`.
199. Evidence root: `out/dotnet/c011ec90-allocation-fit-ledger-commit-closure/`.
200. Final commit: recorded below after the local commit.
201. Push status: not pushed.
202. Remaining limitation: the exact arithmetic predicts the post-216 EXTRA2 crossing and closes both commit endpoints, but the earlier three crossings require the observed T320 context/reset chronology; one independently serialized topology scalar is unavailable without a potentially topology-changing observer.
203. Exact next-smallest milestone: do not start C91 automatically. Return to the larger NativeAOT-support roadmap; if this arc must be refined, add only the source-identified pre-request-217 topology scalar, with BSS delta 0 and immediate T320/T216 control rerun.

## Final closeout fields

- Final HEAD: to be filled after the local documentation commit.
- Final subject: to be filled after the local documentation commit.
- Final ahead/behind: `1/0`.
- Final worktree: clean.
- Push: **not pushed**.

## Conclusion

C90 turns the loose numbers into a balanced ledger where the available evidence supports it:

```text
104 * 0x4030 = 0x1A1380
0xB8D38 = 46 * 0x4030 + 0x498
0xB8D38 + 0xF0FD8 = 0x1A1380 + 0x8990
0x1F190 -> align_on_page -> 0x20000
0x1000 + 0x20000 = 0x21000       T216
0x1000 + 0x00000 = 0x1000        T320
```

The supported causal chain is therefore complete through the commit/OOM predicate. The one remaining quantitative limitation is precise and bounded: four total T320 region acquisitions cannot be predicted from the post-216 `0x1A1380` delta alone because three acquisitions precede that logical comparison point and the missing pre-point topology is not serialized independently. This is a narrowed residual, not a reason to reopen the GC provenance tree.

# NativeAOT Workstation GC C76 — Basic-Free-Region Eligibility and Geometry

Date: 2026-09-05
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`

## Executive result

C76 keeps the accepted C75 ONE and SIX controls intact and adds a bounded, diagnostics-only replay of the C67 region-list lifecycle. The source audit finds that `basic_free_region` classification is determined solely by the exact region-size predicate:

```text
region_size == BASIC_REGION_SIZE
```

All observed C76 eligibility records in both controls are basic (`regionSize=0x100000`, `basicRegionSize=0x100000`); no observed record selects the large or huge list. Therefore the five-region difference is not caused by a differing basic/large/huge classification result. The first comparable C76 divergence is earlier region state/occupancy metadata for the shared `0x10180028` range role (`09->0B` on ONE versus `0D->0F` on SIX, event `0x16`), followed by different removal targets (`0x10190028` versus `0x101B0028`, event `0x23`).

The C76 completion marker is `Outcome B / Level 1`: the bounded observation distinguishes supply/geometry/eligibility from the final list count, but it does not claim a closed total-heap census or Level 3 proof. The strongest supported result is that the class predicate is identical and that the remaining five-region difference is carried by earlier region state/geometry and list chronology. The exact upstream cause of the state difference remains the next milestone.

C75 remains valid: `survived_per_region` is authentic per-region promoted-byte accounting, but correlated with the earlier free-region cohort and not a direct controlling production input. B02 was not tested or justified.

## Source audit

The locked source is `out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/gc.cpp` and `gcpriv.h`.

* `free_region_kind` is `basic_free_region=0`, `large_free_region=1`, `huge_free_region=2` (`gcpriv.h:1417-1425`).
* `gc_heap` owns `free_regions[count_free_region_kinds]`, an array of `region_free_list` objects (`gcpriv.h:3883-3886`).
* `get_region_start` subtracts `sizeof(aligned_plug_and_gap)` from `heap_segment_mem` (`gc.cpp:3799-3803`).
* `get_region_size` is `heap_segment_reserved(region) - get_region_start(region)` (`gc.cpp:3806-3809`).
* `region_free_list::get_region_kind` compares only that size with the basic and large allocator alignments (`gc.cpp:12976-12991`).
* `region_free_list::add_region` and `add_region_descending` choose the class, then insert into the chosen list (`gc.cpp:13091-13101`).
* `is_on_free_list` recomputes the class from region size and compares the containing list (`gc.cpp:13103-13107`); it does not migrate a region.
* `return_free_region` clears region info and publishes the region through `add_region_descending`; it deliberately does not decommit and does not reset generation/plan-generation debugging fields (`gc.cpp:11860-11900`).
* `get_free_region` removes gen0-or-younger requests from the basic list with `unlink_region_front` (`gc.cpp:11906-11994`).
* `try_get_new_free_region` first checks `free_regions[basic_free_region].get_num_free_regions()` (`gc.cpp:21345-21371`).
* `find_first_valid_region` returns empty regions to the free list and otherwise updates plan/generation fields (`gc.cpp:34702-34806`); `thread_final_regions` threads the remaining regions into generation lists (`gc.cpp:34809-34890`).

There is no production state, generation, occupancy, origin, tail, expansion, or age predicate in the class choice. A basic-free region can have committed memory and zero allocated objects. Fully committed regions are inserted at the free-list head by descending insertion; partially committed regions are placed by committed-size order. A region can be empty yet non-basic only when its size is large or huge. The C76 evidence did not observe such a non-basic event.

## Comparison

| Field | ONE | SIX |
| --- | ---: | ---: |
| Promotion positive | yes | yes |
| Retained objects | 15 | 16 |
| Promoted bytes | `0xFD980` | `0x100180` |
| Total regions pre-GC | not closed; 12 relevant range bases observed | not closed; 12 relevant range bases observed |
| Relevant regions pre-GC | 12 unique range bases in bounded stream | 12 unique range bases in bounded stream |
| Eligible basic regions | 19 observations; all basic | 17 observations; all basic |
| Basic insertions | 19 | 17 |
| Basic removals | 19 | 12 |
| Post-Restart basic | 1 | 6 |
| Post-resume basic | 1 | 6 |
| Expansion regions | 0 created; attempted=1, succeeded=0 | 0 created; attempted=1, succeeded=0 |
| Tail-derived regions | 0 | 0 |
| Extra-region provenance | shared range-role supply observed; later removals consume the cohort | five-role SIX ledger has fewer removals before RestartEE |

The distinction matters: the bounded event stream proves the class result and relevant list genealogy, but it does not publish a closed total-heap region count for the ONE C71-managed control. The unresolved total is not inferred from the twelve observed range bases.

## Common role and extra-five role ledger

The common semantic role is the shared survivor/cohort range role `0x101800028..0x101900000` (C76 address-range base `0x10180028`). It is observed and classified basic on both sides. The diagnostic `region` pointer is per-boot metadata and is never used to match roles across boots.

The five SIX roles below are the accepted C75 range-role ledger, refreshed with C76 geometry. The ONE counterpart column describes the same semantic range-role family, not pointer equality.

| Extra role | SIX prior state/gen | SIX geometry | SIX eligibility predicate | SIX result | ONE counterpart | ONE predicate | ONE result |
| --- | --- | --- | --- | --- | --- | --- | --- |
| extra-1 survivor-pressure | `8->A`, gen 0 | `0x101400028..0x101500000`; allocated `0x1014F02E0`; live `0x900D8`; free `0xFD20` | `0x100000 == 0x100000` | basic; later SIX role remains in the accepted cohort ledger | same role observed; ONE geometry has live `0xA00F0`; consumed before final ONE count | same predicate, basic | not retained to final ONE cohort |
| extra-2 survivor-pressure | `8->A`, gen 0 | `0x101500028..0x101600000`; allocated `0x101570160`; live `0x700A8`; free `0x8FEA0` | `0x100000 == 0x100000` | basic; later SIX role remains in the accepted cohort ledger | same role observed; ONE geometry has allocated `0x101560130`, live `0x60090`, free `0x9FED0` | same predicate, basic | consumed before final ONE count |
| extra-3 empty basic | `9->B`, gen 0 | `0x101600028..0x101700000`; allocated at base; live 0; free `0xFFFD8` | `0x100000 == 0x100000` | basic | same range-role family observed | same predicate, basic | removed earlier |
| extra-4 fully committed | `D->F`, gen 0 | `0x101700028..0x101800000`; allocated at base; live 0; free `0xFFFD8` | `0x100000 == 0x100000` | basic | same range-role observed with earlier `9->B` state transition | same predicate, basic | removal chronology differs |
| extra-5 later empty | `9->B`, gen 0 | `0x101A00028..0x101B00000`; allocated at base; live 0; free `0xFFFD8` | `0x100000 == 0x100000` | basic | same later range-role family observed | same predicate, basic | removed before final ONE count |

The first comparable divergence is therefore not a class result. It is C76 event `0x16` (`C76_REGION_ELIGIBILITY` for `0x10180028`), where the state metadata differs while all predicate operands and the basic result agree. The first later removal-target divergence is event `0x23`: ONE removes `0x10190028`, while SIX removes `0x101B0028`.

## List accounting

The required identity is only partially closed because the C67 phase-start snapshot and the C76 finish-time replay have a one-region phase-boundary gap on each side:

```text
ONE: 0 + 19 - 19 = 0; C67 post-Restart snapshot = 1; gap = 1
SIX: 0 + 17 - 12 = 5; C67 post-Restart snapshot = 6; gap = 1
```

All basic operations are recorded with global event ordinal, local list ordinal, operation, region, list count/head/tail transitions, geometry, generation, state, and source function in `c76-census.json`. The full raw records remain in the accepted serial logs. C76 does not silently truncate: capacity is 4096, maximum observed event counts are 57 (ONE) and 46 (SIX), and overflow is zero.

The native C76 observer is source-accounting-only. It replays fixed-size C67 lifecycle records at the bounded finish boundary so the production classifier, insertion/removal paths, planner, allocator, and candidate selection remain byte/behaviorally undisturbed by a new hot-path callback.

## Validation and evidence

Discovery used one fresh boot per control. Confirmation used three fresh boots per control. The final manifests report semantic agreement for the accepted fields on all three boots per side. Serial hashes differ because boot-local addresses and serial details differ; semantic fields agree.

* C76 ONE: `Outcome B / Level 1`, post-Restart/resume `1/1`, event count 57.
* C76 SIX: `Outcome B / Level 1`, post-Restart/resume `6/6`, event count 46.
* Predicate counters: basic-only=1; non-basic eligibility=0 on both controls.
* C76 and inherited overflow, invariant failures, sensitive diagnostic allocations, fail-fast, and page faults are all zero.
* Authentic C18 managed-PC, `CoffNativeCodeManager`, `FindMethodInfo`, root scan, mark closure, planner, and survivor checks remain inherited from the accepted C75 controls and passed in each final run.
* Runtime-pack, managed build, native build, PE-to-ELF, symbol, linker/source/table/archive guards, PowerShell syntax, JSON/XML parsing, and `git diff --check` pass.
* Ordinary kernel and ESP are restored to `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`; proof-only artifacts are inactive; only C76-owned QEMU processes were cleaned up.

The canonical evidence root is `out/dotnet/c011ec76-basic-free-region-eligibility-geometry/`, separated into `discovery`, `instrumentation-development`, and `accepted-confirmation-runs`.

## Required numbered report

1. Outcome: `Outcome B / Level 1` as emitted by the successful bounded C76 completion marker; causal qualification is identical basic classification with an upstream state/chronology difference still open.
2. Success Level: Level 1; Level 2/3 are not claimed because total-heap supply and the phase-boundary list gap are not closed.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `97b898e569b56ab301eaf35d67442f46dbf0ddc4`.
6. Starting subject: `Trace NativeAOT survived-per-region provenance`.
7. Final HEAD: the local closeout commit reported in item 178.
8. Final subject: `Trace NativeAOT basic free-region eligibility`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0` as measured at execution start; the requested expected `ahead 1` was not present in the checkout.
11. Final ahead/behind: `1/0` after the local closeout commit; not pushed.
12. Starting worktree: clean.
13. Final worktree: clean after closeout.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC; interfaces `5.3 / 2`.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. FP patch SHA: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. C74 SHA: `0299283f0627f9744690c2e4e08b8769545acca3`.
18. C75 SHA: `97b898e569b56ab301eaf35d67442f46dbf0ddc4`.
19. C76 SHA: C76 platform-source blob `a2008d3be4a8f1e5a8591cedc66489a3d96ee275`; final local closeout commit is item 178.
20. Exact C76 question: what geometry, generation/state, occupancy, reclamation, or eligibility condition produces five additional SIX basic-free regions?
21. ONE reproduction: retained 15, `0x10E80`, promoted `0xFD980`, authentic promotion, post-Restart/resume basic `1/1`.
22. SIX reproduction: retained 16, `0x10018`, promoted `0x100180`, authentic promotion, post-Restart/resume basic `6/6`.
23. ONE post-Restart basic count: 1.
24. SIX post-Restart basic count: 6.
25. `basic_free_region` source definition: enum value 0 in `free_region_kind`; it is the first entry of `free_regions`.
26. Basic-list structure/type: per-heap `region_free_list`, containing count, total/free committed sizes, head/tail, and add/remove counters.
27. Eligibility source functions: `get_region_kind`, `add_region`, `add_region_descending`, `is_on_free_list`.
28. Eligibility predicates: exact size equality for basic; exact size equality for large; otherwise asserted `> large` and huge.
29. Free-list classification source: `gc.cpp:12976-12991`, `region_free_list::get_region_kind`.
30. Basic insertion source functions: `add_region_front`, `add_region_in_descending_order`, called by `add_region`/`add_region_descending`.
31. Basic removal source functions: `unlink_region_front`, `unlink_region`, and the basic branch of `get_free_region`.
32. ONE total region count pre-GC: not closed; 12 distinct relevant range bases are observed in the bounded stream.
33. SIX total region count pre-GC: not closed; 12 distinct relevant range bases are observed in the bounded stream.
34. ONE relevant region count: 12 unique range bases in C76 eligibility records.
35. SIX relevant region count: 12 unique range bases in C76 eligibility records.
36. ONE basic-eligible region count: 19 eligibility observations, all basic; 12 unique range bases.
37. SIX basic-eligible region count: 17 eligibility observations, all basic; 12 unique range bases.
38. Region supply differs before eligibility: not established as a total-heap claim; the bounded relevant range-base set is the same size on both sides.
39. Region eligibility differs with comparable supply: no; every observed eligibility result is basic.
40. First ONE/SIX region-level divergence: event `0x16` for range base `0x10180028`, state `09->0B` ONE versus `0D->0F` SIX.
41. Divergence source file: locked `src/coreclr/gc/gc.cpp`, with C76 observation in `guidexos_nativeaot_platform.cpp`.
42. Divergence source function: first observed at `region_free_list::get_region_kind`; later removal target differs in `region_free_list::unlink_region`.
43. Divergence predicate: `region_size == BASIC_REGION_SIZE`; it returns basic on both sides.
44. ONE predicate operands: `regionSize=0x100000`, `basicRegionSize=0x100000`, `largeRegionSize=0x800000`, result basic.
45. SIX predicate operands: `regionSize=0x100000`, `basicRegionSize=0x100000`, `largeRegionSize=0x800000`, result basic.
46. Common ONE/SIX basic-region role: shared survivor/cohort range role `0x101800028..0x101900000`.
47. Common region provenance: existing range-role region in the C67/C76 lifecycle; not matched by per-boot pointer.
48. Extra SIX region 1 role: survivor-pressure region `0x101400028..0x101500000`.
49. Extra SIX region 1 provenance: retained-cohort pressure geometry; allocated `0x1014F02E0`, live `0x900D8`, free `0xFD20`.
50. Extra SIX region 1 eligibility: basic, exact-size predicate true.
51. ONE counterpart/absence: same role observed with live `0xA00F0`; consumed before final ONE cohort.
52. Extra SIX region 2 role: survivor-pressure region `0x101500028..0x101600000`.
53. Extra SIX region 2 provenance: retained-cohort pressure geometry; allocated `0x101570160`, live `0x700A8`, free `0x8FEA0`.
54. Extra SIX region 2 eligibility: basic, exact-size predicate true.
55. ONE counterpart/absence: same role observed with allocated `0x101560130`, live `0x60090`, free `0x9FED0`; consumed before final ONE cohort.
56. Extra SIX region 3 role: empty basic region `0x101600028..0x101700000`.
57. Extra SIX region 3 provenance: allocated at base, live zero, free `0xFFFD8`, gen0 role.
58. Extra SIX region 3 eligibility: basic, exact-size predicate true.
59. ONE counterpart/absence: same range-role family observed and removed earlier.
60. Extra SIX region 4 role: fully committed basic region `0x101700028..0x101800000`.
61. Extra SIX region 4 provenance: state `D->F`, gen0, allocated at base, live zero, free `0xFFFD8`.
62. Extra SIX region 4 eligibility: basic, exact-size predicate true.
63. ONE counterpart/absence: same role observed with earlier state `9->B`; removal chronology differs.
64. Extra SIX region 5 role: later empty/basic region `0x101A00028..0x101B00000`.
65. Extra SIX region 5 provenance: allocated at base, live zero, free `0xFFFD8`, later range-role family.
66. Extra SIX region 5 eligibility: basic, exact-size predicate true.
67. ONE counterpart/absence: same later range-role family observed and removed before final ONE count.
68. ONE region states before: observed `08`, `09`, `0D`, and survivor-pressure `08` inputs; no state is a class predicate.
69. SIX region states before: observed `08`, `09`, `0D`, with `0D` on the common-role sequence where ONE has `09`.
70. ONE region states after: observed `0A`, `0B`, `0F`, and removal transitions including `0E->0C`.
71. SIX region states after: observed `0A`, `0B`, `0F`, and removal transitions including `0E->0C`.
72. ONE generations before: C76 eligibility records show gen0.
73. SIX generations before: C76 eligibility records show gen0.
74. ONE generations after: C76 eligibility records show gen0; final role ledger retains source generation metadata.
75. SIX generations after: C76 eligibility records show gen0; final role ledger retains source generation metadata.
76. ONE allocated geometry: active `0x100A32528`; pressure roles `0x1014F02E0` and `0x101560130`; empty roles at range bases.
77. SIX allocated geometry: active `0x100A30858`; pressure roles `0x1014F02E0` and `0x101570160`; empty roles at range bases.
78. ONE live geometry: active `0x221E0`; pressure `0xA00F0` and `0x60090`; empty roles zero.
79. SIX live geometry: active `0x20510`; pressure `0x900D8` and `0x700A8`; empty roles zero.
80. ONE free geometry: active `0xCDAD8`; pressure `0xFD20` and `0x9FED0`; empty roles `0xFFFD8`.
81. SIX free geometry: active `0xCF7A8`; pressure `0xFD20` and `0x8FEA0`; empty roles `0xFFFD8`.
82. ONE retained-object region count: bounded C76 does not publish a closed retained-object-to-region map; pressure/live geometry is observed.
83. SIX retained-object region count: bounded C76 does not publish a closed retained-object-to-region map; pressure/live geometry is observed.
84. ONE objects-per-region: not closed by C76; retained count is 15.
85. SIX objects-per-region: not closed by C76; retained count is 16.
86. Expansion attempted ONE/SIX: attempted=1 on each.
87. Expansion result ONE/SIX: succeeded=0 on each.
88. Expansion-created region count ONE/SIX: 0/0 observed.
89. Hard-limit-short ONE/SIX: no hard-limit-short expansion result; no expansion success; no C76 failure marker.
90. Tail reclaim ONE/SIX: tail reclaim observed=0/0.
91. Tail-derived region count ONE/SIX: 0/0.
92. Split/coalesce ONE/SIX: no split/coalesce or tail-derived region observed in C76; not a causal claim beyond the bounded scope.
93. Basic count phase-start ONE/SIX: `0/0` in the C76 checkpoint-1 snapshot.
94. Basic insertions ONE: 19.
95. Basic insertions SIX: 17.
96. Basic removals ONE: 19.
97. Basic removals SIX: 12.
98. Exact ONE list accounting: `0 + 19 - 19 = 0`; post-Restart snapshot is 1; precise gap is 1.
99. Exact SIX list accounting: `0 + 17 - 12 = 5`; post-Restart snapshot is 6; precise gap is 1.
100. First basic-list divergence ordinal: state divergence at C76 event `0x16`; first removal-target divergence at event `0x23`.
101. RestartEE ordinal: C67 checkpoint `0x7` / `C011EC67` post-Restart snapshot.
102. Managed-resume ordinal: accepted C75 lineage records managed resume at C72 ordinal `0x204`; C76 post-resume snapshot remains checkpoint `0x7`.
103. Post-Restart basic ONE: 1.
104. Post-Restart basic SIX: 6.
105. Post-resume basic ONE: 1.
106. Post-resume basic SIX: 6.
107. Region supply causal status: bounded relevant supply is same-sized; total heap supply is not closed.
108. Geometry causal status: supported upstream correlate; active and retained-pressure geometry differs before classification.
109. Eligibility causal status: ruled out as a differing class result; predicate is identical and basic on all observed records.
110. Expansion causal status: excluded in the bounded run; attempted but zero successful expansion-created regions.
111. Tail causal status: excluded in the bounded run; tail reclaim is zero.
112. List-chronology causal status: supported as the immediate membership mechanism; removal targets/counts diverge after the state difference.
113. Actual controlling determinant: `free_regions[basic_free_region]` cohort membership at the RestartEE boundary.
114. Strongest causal chain established: retained-object geometry/state -> same basic-size classification -> different list insertion/removal chronology -> `1` versus `6` at RestartEE.
115. First unsupported causal link: the source-level cause of the `09` versus `0D` state/occupancy difference before C76 event `0x16`.
116. Extra-five later candidate mapping: the five SIX ledger roles are earlier basic-list cohort roles; C76 does not alter later candidate behavior.
117. Candidate selected ONE/SIX: `0/0`.
118. Normal refill ONE/SIX: `0/0`.
119. `commit_failed` ONE/SIX: `0/0`.
120. OOS reason ONE/SIX: reason `5/5`.
121. Requested generation ONE/SIX: gen2/gen2.
122. Condemned generation ONE/SIX: gen2/gen2 from the C70/C67 source lineage; the finish-time C73 reconciliation field is not used to rewrite it.
123. B02 evaluated: no.
124. B02 future justification: only after a specific authentic basic-eligible candidate reaches the decisive stage and is rejected by B02's exact mechanism; C76 does not establish that.
125. Allocator mutation: 0.
126. Planner mutation: 0.
127. Region mutation: 0.
128. Region-list mutation: 0.
129. Eligibility mutation: 0.
130. Candidate mutation: 0.
131. Policy mutation: 0.
132. Survivor fabrication: 0.
133. Root fabrication: 0.
134. C18: authentic managed PC instrumentation remained active and passed.
135. Code manager: valid `CoffNativeCodeManager` path passed.
136. `FindMethodInfo`: passed in inherited C75/C18 validation.
137. Root scan: authentic root scan passed.
138. Mark closure: authentic mark closure passed.
139. Planner authenticity: authentic planner/root/mark path preserved; no planner mutation.
140. Survivor integrity: authentic survivor/promotion controls preserved.
141. C76 invariant failures: 0 on every accepted boot.
142. Sensitive diagnostic allocations: 0 on every accepted boot.
143. C76 event capacity: 4096 records (`0x1000`), fixed capacity.
144. C76 event count: ONE `57` (`0x39`); SIX `46` (`0x2E`).
145. C76 overflow: 0.
146. Inherited overflow: 0.
147. Fail-fast: 0.
148. Page faults: 0.
149. ONE Boot 1: pass, `1/1`, eligibility 19, insert/remove `19/19`, serial SHA `B5434D320D962952A7C1E8BE89D57BCF26E0185DCB3E927BF5F03AB4C2527760`.
150. ONE Boot 2: pass, `1/1`, eligibility 19, insert/remove `19/19`, serial SHA `CF3A8C1097FCC928A8075D439C8F01350B7AFDFE59F5490CE3F16B459C3CCC3B`.
151. ONE Boot 3: pass, `1/1`, eligibility 19, insert/remove `19/19`, serial SHA `F27193F840A24D6F61E20B6684E3EC3743F344376B808828687E42E14913ABB6`.
152. SIX Boot 1: pass, `6/6`, eligibility 17, insert/remove `17/12`, serial SHA `433726D1A61A83C0A5D2646F9680F7C42502E5683F49FEAD04B9B751B2E41733`.
153. SIX Boot 2: pass, `6/6`, eligibility 17, insert/remove `17/12`, serial SHA `6EDDDEE94171CFA4F5864C8C577344FDE90A0C8A84136C3F1FAFD184771A8FDE`.
154. SIX Boot 3: pass, `6/6`, eligibility 17, insert/remove `17/12`, serial SHA `C0EE68C8C8AB978424F914753013B202B4C42EF260BE3387607A0512A1809BBD`.
155. Semantic agreement: true for each 3/3 confirmation manifest.
156. Nondeterminism: pointer/address and serial hashes vary; semantic outcome, counts, predicate operands, and diagnostics agree.
157. Serial hashes: recorded in items 149-154 and in `c76-final-manifest.json`.
158. Artifact hashes: ONE proof kernel `312C1C3450F4AC8425C50AA84CCE0858081B83D749CB85C89B9C548A4E581B61`, PE `AD95477E55C4A39935285DB13720F1376E8690CBA6BE5667CD76C6736CF60ADC`, ELF `0BE9AC12A99E222ECD4436C0DD4EEBB0A3B8B42B8AB98D2741933EDECD88A392`, MAP `4FC9E5D4F271A403563EC285BAEFAD08612BEA0BFFE3765FD57A417755D97368`; SIX proof kernel `CE0C2684E357F1C1D30D6C87B536C0956B8F8BD1725B6B4E14DA53C112707823`, PE `2A23CDE14897B9BF6240D4DA199A3C75BFF2F6D004AF805AD91B15217F51111`, ELF `985F827AEA318DBEE24D79720BA65DF75F922B3066545CD7F8D7EC881D25B975`, MAP `D8509352244697A157F36B1AA4656F42273220B2B34947AC974474AC58BD39B6`.
159. Runtime-pack validation: pass in final ONE and SIX manifests.
160. Managed build: pass in final ONE and SIX manifests.
161. Native build: pass in final ONE and SIX manifests.
162. PowerShell syntax: pass for the smoke harness and C76 extraction script.
163. JSON/XML parse: pass; generated JSON and project XML parse cleanly.
164. `git diff --check`: pass.
165. PE -> ELF conversion: pass; final artifacts include conversion logs and ELF inspection.
166. Symbol checks: pass; required C76/C67 symbols and entry checks are present.
167. Linker/source/table/archive guards: pass; source guards, table guards, archive guard, linker, map, and symbol checks passed.
168. C52 Tier-All: omitted because C76 is an already controlled C67/C70/C71/C73 geometry/list census and C52 adds no semantically relevant crossover.
169. Ordinary restoration: pass; `restoredByFinally=true` and proof-only artifact inactive.
170. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
171. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
172. Proof artifact active: false.
173. C76-owned QEMU cleanup: only C76-owned processes stopped.
174. Unrelated QEMU preservation: preserved.
175. Files changed: `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `scripts/dotnet/Invoke-C011EC76BasicFreeRegionEligibilityGeometry.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this documentation.
176. Documentation path: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C76_BASIC_FREE_REGION_ELIGIBILITY_GEOMETRY.md`.
177. Evidence root: `out/dotnet/c011ec76-basic-free-region-eligibility-geometry/` with discovery, development, and accepted-confirmation subtrees.
178. Final commit: `Trace NativeAOT basic free-region eligibility`; exact SHA is filled in by the closeout command and returned with this report.
179. Push status: not pushed.
180. Remaining limitation: the total-heap region census and one-region phase-boundary list gap remain open; the upstream cause of the first state/occupancy divergence is not isolated.
181. Exact next-smallest milestone: add one bounded, diagnostics-only observation at the source that establishes the `state 09` versus `state 0D` difference for the shared `0x10180028` role, preserving the accepted ONE/SIX controls and avoiding a managed sweep.

## C77 decision

C76 does not justify B02. The smallest next step is to trace the first state/occupancy transition before C76 event `0x16`. If that transition proves a SIX-only five-role supply difference, C77 should follow the region-supply mechanism; if it proves a same-supply removal chronology difference, C77 should follow the earliest removal. A managed crossover remains out of scope until that source predicate is identified.

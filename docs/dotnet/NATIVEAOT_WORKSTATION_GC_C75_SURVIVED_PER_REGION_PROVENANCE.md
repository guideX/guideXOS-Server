# NativeAOT Workstation GC C75 — `survived_per_region` provenance

## Result

C75 is **Outcome C / Level 2**.

`survived_per_region` is **CORRELATED** with the ONE-vs-SIX result. It is an authentic per-basic-region accounting array, but the accepted controls do not show a production branch that directly consumes the raw array to select the divergent post-Restart cohort. The first supported split is the earlier `free_regions[basic_free_region]` region-list cohort state. The quantity is therefore the messenger/footprint of the earlier geometry/list decision, not an isolated causal input.

The C75-native hot-path observer was rejected during development because adding a new native observer changed the accepted control geometry. The final C75 evidence uses the C74 runtime and harness unchanged and performs a bounded, host-side reconstruction from the accepted C71/C72/C73/C67 serial records. This is intentional: no C75 production callback, threshold, planner operand, allocator, region, region-list, candidate, policy, root, or survivor was mutated or compiled into the accepted control.

## Source provenance

Locked source: `out/dotnet/c68-locked-nativeaot-runtime-2/src/coreclr/gc/` at runtime source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

- `gcpriv.h:3630`: `PER_HEAP_FIELD_SINGLE_GC size_t* survived_per_region;`; the companion field is `old_card_survived_per_region`.
- `gc.cpp:2637`: the pointer is initialized to `nullptr` as a per-heap field.
- `gc.cpp:29842-29857`: during mark setup it points into the mark-list-piece storage, is indexed by `region_count`, and is zeroed with `memset`; when mark-list storage is unavailable both pointers are set to `nullptr`.
- `gc.cpp:26657-26666`: the sole production increment is `add_to_promoted_bytes(object,obj_size,thread)`, which adds the runtime `obj_size` to `survived_per_region[get_basic_region_index_for_address(object)]`.
- `gc.cpp:24526-24568`: `get_promoted_bytes()` reads nonzero entries and sums them into one aggregate.
- `gc.cpp:24615-24617`: `sync_promoted_bytes()` copies the per-region value into `heap_segment_survived(current_region)` for the single-heap Workstation path.
- `gc.cpp:30166-30182`: `total_promoted_bytes = get_promoted_bytes()` precedes `sync_promoted_bytes()`.
- `gc.cpp:30229` and `29462-29489`: the promotion decision consumes `hp->total_promoted_bytes`, not a raw `survived_per_region[index]`; the condition is `(threshold > older_gen_size) || (promoted > threshold)`.
- `gc.cpp:32345-32379`: `save_current_survived()` bulk-copies the array to `old_card_survived_per_region`; `update_old_card_survived()` reads the array again and subtracts the prior card value.
- `gc.cpp:35094-35180`: the later planner reads `heap_segment_survived(region)` and compares a calculated survival ratio with `sip_surv_ratio_th`; it does not read the raw array.

The units are aligned runtime object bytes. The source does not add a separate header field or a separate alignment field here: it adds the `size()`/`obj_size` quantity supplied by the GC. The array is per basic region for one GC/mark accounting window, not a global lifetime counter. The aggregate is global to the heap decision only after `get_promoted_bytes()` sums it. It is not independently a pinned-byte or fragmentation counter.

## Chronology and causal classification

The source-backed order is:

1. mark setup assigns/zeros the array;
2. `add_to_promoted_bytes()` accumulates marked/promoted object sizes by basic-region index;
3. `get_promoted_bytes()` reads and aggregates the array;
4. `sync_promoted_bytes()` publishes region survival into `heap_segment_survived`;
5. `decide_on_promotion_surv()` reads `total_promoted_bytes` and the threshold operands;
6. later region planning/list transitions use the published region state;
7. RestartEE and managed resume occur;
8. the C73 accepted observer reports the final basic/free cohort.

The accepted C73 summaries report `plannerObserved=0` for both controls. Thus no differing `should_sweep_in_plan` branch was observed, and no direct raw-array comparison separates ONE from SIX.

The first supported ONE/SIX semantic divergence is the post-Restart basic-region cohort:

| control | promotion | `survived_per_region` | basic before/after RestartEE | post-resume basic |
| --- | ---: | ---: | ---: | ---: |
| ONE | positive | `0xFD980` | `1` | `1` |
| SIX | positive | `0x100180` | `6` | `6` |

The C72 ONE source ledger records `firstDecisionOrdinal=0x9` and `firstRegionDivergenceOrdinal=0xC`; SIX's accepted C73-only image does not publish the C72 ordinal, so C75 does not invent a common production ordinal. The host reconstruction assigns its own bounded C75 event ordinal and preserves the original C72/C67 ordinal separately.

### Region geometry

ONE's accepted C72 region ledger publishes the basic survivor-cohort role with canonical range `0x101800028..0x101900000`, committed `0x1018E1000`, allocated `0x101800028`, used `0x1018E0AA0`, live `0`, free `0xFFFD8`, generation/state `0/0 -> 0/0xB`, and `basicMembershipAtRestart=1`.

SIX's accepted C67 list ledger leaves six basic roles at the corresponding cohort boundary. The stable canonical range starts are:

| role | range start | range end | allocated | used | free | live |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| common | `0x101800028` | `0x101900000` | `0x101800028` | `0x1018FCBF0` | `0xFFFD8` | not published by C73 |
| extra 1 | `0x101400028` | `0x101500000` | `0x1014F02E0` | `0x1014F02F0` | `0xFD20` | not published by C73 |
| extra 2 | `0x101500028` | `0x101600000` | `0x101570160` | `0x1015F02F0` | `0x8FEA0` | not published by C73 |
| extra 3 | `0x101600028` | `0x101700000` | `0x101600028` | `0x1016F02F0` | `0xFFFD8` | not published by C73 |
| extra 4 | `0x101700028` | `0x101800000` | `0x101700028` | `0x1017FC6E0` | `0xFFFD8` | not published by C73 |
| extra 5 | `0x101A00028` | `0x101B00000` | `0x101A00028` | `0x101A503E0` | `0xFFFD8` | not published by C73 |

ONE's C72-native image publishes per-region records; SIX's accepted C73-only image publishes the region-list link/unlink records but not per-object retained-region identities. Consequently, the five extra SIX roles are substantially accounted for as basic free-region list roles, but their upstream object-placement/live-byte eligibility remains the first unsupported link.

The difference `0x100180 - 0xFD980 = 0x2800` is not shown to straddle a direct production threshold. The relevant observed condition is list membership/cohort geometry, not a hard-coded 1 MiB, region-size, or inferred `survived_per_region` threshold.

## Evidence and method

Accepted evidence is under `out/dotnet/c011ec75-survived-per-region-provenance/`:

- `discovery/baseline-one` and `discovery/baseline-six` are the one-boot C74-safe baseline reproductions.
- `accepted-confirmation-runs/one` contains three fresh ONE boots.
- `accepted-confirmation-runs/six-correct` contains three fresh SIX boots using the exact C74 SIX managed mode, `PromotionPositiveRegionCohort`, tail `216`, and case `baseline16`.
- `accepted-confirmation-runs/c75-analysis/c75-summary.log` contains bounded C75 markers: `C75_CASE`, `C75_SURVIVED_PER_REGION_WRITE`, `C75_SURVIVED_PER_REGION_FINAL`, `C75_SURVIVED_PER_REGION_READ`, `C75_PLANNER_DECISION`, `C75_FIRST_ONE_SIX_DIVERGENCE`, `C75_BASIC_TRANSITION`, `C75_EVENT_ORDINAL`, `C75_REGION_LIVE`, and `C75_DIAGNOSTIC_OVERFLOW`.
- `c75-final-manifest.json` identifies ONE and SIX explicitly, records 3/3 semantic agreement, host event capacity `0x800`, C75 overflow `0`, and inherited diagnostic overflow `0`.

The synthetic C75 event count is `896` for each accepted ONE boot and `179` for each accepted SIX boot, below the fixed capacity `2048`. This count is host-side reconstruction evidence, not a claim that the accepted runtime carried a new C75 event buffer.

## Numbered final report

1. **Outcome:** Outcome C — `survived_per_region` is consequence/correlated with the earlier region-list cohort.
2. **Success Level:** Level 2.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `0299283f0627f9744690c2e4e08b8769545acca3`.
6. **Starting subject:** `Restore NativeAOT promotion-positive ONE control`.
7. **Final HEAD:** assigned at C75 commit; recorded in the final handoff.
8. **Final subject:** `Trace NativeAOT survived-per-region provenance`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** observed `0/0`; the requested context expected ahead `1`, behind `0`.
11. **Final ahead/behind:** `1/0` after the local C75 commit.
12. **Starting worktree:** clean.
13. **Final worktree:** clean after the local commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C72 SHA:** `9884f4a72223314e047bdda6c32a10663ee8a615`.
18. **C73 SHA:** `36333cb757e31a59188d604d588e7df2865a0b57`.
19. **C74 SHA:** `0299283f0627f9744690c2e4e08b8769545acca3`.
20. **C75 SHA:** final local commit SHA.
21. **Exact C75 question:** What is the production provenance, consumers, and causal role of `survived_per_region`, and does ONE-vs-SIX divergence directly use it or follow earlier state?
22. **ONE fresh reproduction:** 3/3 accepted.
23. **SIX fresh reproduction:** 3/3 accepted.
24. **ONE retained count:** `15`.
25. **ONE object size:** `0x10E80`.
26. **ONE retained-live bytes:** `0xFD980`.
27. **ONE promoted bytes:** `0xFD980`.
28. **ONE `survived_per_region`:** `0xFD980`.
29. **ONE post-Restart basic:** `1`.
30. **SIX retained count:** `16`.
31. **SIX object size:** `0x10018`.
32. **SIX retained-live bytes:** `0x100180`.
33. **SIX promoted bytes:** `0x100180`.
34. **SIX `survived_per_region`:** `0x100180`.
35. **SIX post-Restart basic:** `6`.
36. **Source file:** `src/coreclr/gc/gcpriv.h` and `gc.cpp` in the locked runtime checkout.
37. **Structure/class:** `gc_heap`, per-heap GC field.
38. **Field type:** `size_t*`.
39. **Units:** aligned runtime object bytes.
40. **Initialization:** `nullptr` at static definition; mark setup points into mark-list storage and zeroes `region_count*sizeof(size_t)`.
41. **Reset point:** mark setup `memset` before mark scanning.
42. **Writer count:** one production increment site; initialization/reset is separate bookkeeping.
43. **Writer source:** `gc_heap::add_to_promoted_bytes(uint8_t*,size_t,int)`.
44. **ONE write chronology:** retained object sizes accumulate, aggregate final `0xFD980`, then sync/decision; host reconstructed from accepted C72/C73 records.
45. **SIX write chronology:** retained object sizes accumulate, aggregate final `0x100180`, then sync/decision; raw per-object SIX writes were not published by C73.
46. **ONE final value:** `0xFD980`.
47. **SIX final value:** `0x100180`.
48. **Reader count:** four source-level read families: aggregate loop, sync publication, save copy, and old-card update loop.
49. **Reader sources:** `get_promoted_bytes`, `sync_promoted_bytes`, `save_current_survived`, `update_old_card_survived`.
50. **Direct planner consumer:** none of the accepted divergent branches reads the raw array directly.
51. **Direct region consumer:** `sync_promoted_bytes` publishes to `heap_segment_survived`; later planner reads that published field.
52. **Direct budget consumer:** no direct raw-array budget read; promoted aggregate participates in normal promotion/budget accounting.
53. **Direct free-list consumer:** none; free-list/list cohort is downstream state.
54. **First read after finalization:** `get_promoted_bytes` aggregation, then `sync_promoted_bytes` publication; exact runtime ordinals are retained in source C72/C67 ordinals.
55. **Earliest ONE/SIX production divergence:** post-Restart basic/free-region cohort.
56. **Divergence source file:** locked `gc.cpp` region-list/free-region path, observed through C72/C67 source callbacks.
57. **Divergence function:** `free_regions[basic_free_region]` list operations through `return_free_region`/`get_free_region`.
58. **Divergence condition:** basic/free-region list membership and resulting cohort count, not raw survived bytes.
59. **ONE operands:** post-Restart cohort `1`, insertions `0xB`, removals `0xB`.
60. **SIX operands:** post-Restart cohort `6`, insertions `0xB`, removals `0x6`.
61. **Divergence before `survived_per_region`:** no; survival is an earlier accounting quantity.
62. **Divergence after `survived_per_region`:** yes for the observed cohort decision; the raw array has already been aggregated/published.
63. **Causal class:** `CORRELATED`.
64. **Actual controlling quantity:** basic free-region cohort membership/geometry.
65. **Threshold/condition:** list/cohort state; no direct `survived_per_region` threshold isolated.
66. **ONE retained region count:** one C72-published retained-object range is observable; exact selected cohort placement is not fully published.
67. **SIX retained region count:** unresolved from accepted C73-only per-object evidence.
68. **ONE objects per region:** retained cohort is 15 objects; C72 does not publish a complete retained-object-to-region table.
69. **SIX objects per region:** 16 total; per-region distribution unresolved.
70. **ONE live bytes per relevant region:** accepted C72 basic role live `0`; the retained aggregate is `0xFD980`.
71. **SIX live bytes per relevant region:** not published by C73; list roles are source-backed.
72. **ONE region occupancy:** canonical basic role `0x101800028..0x101900000`, used `0x1018E0AA0`, free `0xFFFD8`.
73. **SIX region occupancy:** six roles; extra canonical ranges and used/free values are recorded above.
74. **ONE generation states:** basic role generation `0 -> 0`, plan `0 -> 0` in C72 ledger.
75. **SIX generation states:** C67 list roles report generation `0 -> 0` at linking; later generation transitions are separately recorded.
76. **ONE region states:** `0x9 -> 0xB` at the accepted basic link.
77. **SIX region states:** extra links include `0x9 -> 0xB` and `0xD -> 0xF` roles.
78. **Planner decision ONE:** `plannerObserved=0`; no C71 planner branch.
79. **Planner decision SIX:** `plannerObserved=0`; no C71 planner branch.
80. **Basic count before divergence ONE/SIX:** C73 post-Restart boundary is `1/6`; no common raw pre-boundary count was published by both paths.
81. **First basic-count divergence ordinal:** no common production ordinal; ONE C72 first region divergence is `0xC`, SIX C67 role ordinals are preserved separately.
82. **Basic insertions ONE:** `0xB`.
83. **Basic insertions SIX:** `0xB`.
84. **Basic removals ONE:** `0xB`.
85. **Basic removals SIX:** `0x6`.
86. **Post-Restart basic ONE:** `1`.
87. **Post-Restart basic SIX:** `6`.
88. **Post-resume basic ONE:** `1`.
89. **Post-resume basic SIX:** `6`.
90. **Extra SIX region 1:** canonical range `0x101400028..0x101500000`, used `0x1014F02F0`, free `0xFD20`.
91. **Extra SIX region 2:** `0x101500028..0x101600000`, used `0x1015F02F0`, free `0x8FEA0`.
92. **Extra SIX region 3:** `0x101600028..0x101700000`, used `0x1016F02F0`, free `0xFFFD8`.
93. **Extra SIX region 4:** `0x101700028..0x101800000`, used `0x1017FC6E0`, free `0xFFFD8`.
94. **Extra SIX region 5:** `0x101A00028..0x101B00000`, used `0x101A503E0`, free `0xFFFD8`.
95. **ONE-side absence:** ONE keeps only the common `0x101800028..0x101900000` basic role; the five other list roles are not produced in its accepted cohort.
96. **Expansion attempted ONE/SIX:** `1/1`.
97. **Expansion result ONE/SIX:** `0/0` succeeded.
98. **Hard-limit-short ONE/SIX:** not published in the accepted C73 summary; no hard-limit-short causal claim.
99. **Tail reclaim ONE/SIX:** `0/0`.
100. **Tail provenance ONE/SIX:** tail generation `1 -> 2` on both; no causal difference.
101. **Gen1 budget before ONE/SIX:** `0x1AB988 / 0x1AD658`.
102. **Gen1 debit ONE/SIX:** `0xDBC80 / 0xE0150`.
103. **Gen1 budget after ONE/SIX:** `0xCFD08 / 0xCD508`.
104. **Budget causal relevance:** downstream correlated accounting; not isolated as the earliest cohort determinant.
105. **Geometry causal relevance:** yes; strongest supported determinant.
106. **Retained-byte causal relevance:** correlational; both controls promote authentically, and no direct cohort branch consumes the raw array.
107. **Strongest causal chain:** managed geometry → per-region/list state → basic free-region cohort → RestartEE/resume counts; `survived_per_region` records promoted/live bytes along that path.
108. **First unsupported link:** the exact upstream eligibility/packing predicate that causes the five extra SIX roles, because SIX C73 did not publish per-object region placement.
109. **Candidate chronology relevance:** downstream diagnostic only; not primary C75 proof.
110. **Candidate selected ONE/SIX:** `0/0`.
111. **Normal refill ONE/SIX:** `0/0`.
112. **`commit_failed` ONE/SIX:** `0/0` in accepted summaries.
113. **OOS reason ONE/SIX:** `0x5 / 0x5`.
114. **Requested generation ONE/SIX:** `0x2 / 0x2`.
115. **Condemned generation ONE/SIX:** accepted C73 summary `0/0`; C72 source events separately record generation `0x2` at late boundary.
116. **B02 evaluated:** no.
117. **B02 future justification:** not justified; C75 isolated a region-list cohort condition, not an authentic candidate/eligibility proof sufficient for B02.
118. **Allocator mutation:** none.
119. **Planner mutation:** none.
120. **Region mutation:** none.
121. **Region-list mutation:** none; only inherited accepted observers and host reconstruction.
122. **Candidate mutation:** none.
123. **Policy mutation:** none.
124. **Survivor fabrication:** none.
125. **Root fabrication:** none.
126. **C18:** pass/inherited C74 validation.
127. **Code manager:** pass/inherited C74 validation.
128. **`FindMethodInfo`:** pass/inherited C74 validation.
129. **Root scan:** authentic/inherited.
130. **Mark closure:** pass/inherited.
131. **Planner authenticity:** unchanged; no C75 planner observer in accepted image.
132. **Survivor integrity:** pass; promoted bytes match retained-live totals.
133. **C75 invariant failures:** `0`.
134. **Sensitive diagnostic allocations:** `0` in inherited accepted records.
135. **C75 event capacity:** `2048` (`0x800`).
136. **C75 event count:** ONE `896`; SIX `179`; all boots below capacity.
137. **C75 diagnostic overflow:** `0`.
138. **Inherited diagnostic overflow:** `0` in accepted summaries.
139. **Fail-fast:** `0`.
140. **Page faults:** `0`.
141. **ONE Boot 1:** pass; `0xFD980`, `1/1`.
142. **ONE Boot 2:** pass; `0xFD980`, `1/1`.
143. **ONE Boot 3:** pass; `0xFD980`, `1/1`.
144. **SIX Boot 1:** pass; `0x100180`, `6/6`.
145. **SIX Boot 2:** pass; `0x100180`, `6/6`.
146. **SIX Boot 3:** pass; `0x100180`, `6/6`.
147. **Semantic agreement:** yes, 3/3 per side.
148. **Nondeterminism:** none semantic; raw addresses, source ordinals, and serial hashes vary.
149. **Serial hashes:** recorded per boot in `c75-final-manifest.json`; ONE `9E921AE9...`, `95B19008...`, `14EEB69F...`; SIX `B1B83DA8...`, `B32B3D21...`, `14D9FCBB...`.
150. **Artifact hashes:** accepted per-run manifests and ELF/PE artifacts are under each accepted run; ordinary kernel/ESP SHA is `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
151. **Runtime-pack validation:** pass through accepted C74-safe smoke builds.
152. **Managed build:** pass, all six accepted boots.
153. **Native build:** pass, all six accepted boots.
154. **PowerShell syntax:** pass for the C75 analyzer and existing smoke script.
155. **JSON/XML parse:** pass for C75 manifest and accepted run manifests.
156. **`git diff --check`:** pass.
157. **PE -> ELF conversion:** pass in accepted smoke artifacts.
158. **Symbol checks:** pass; accepted run symbol logs present.
159. **Linker/source/table/archive guards:** pass in accepted smoke artifacts.
160. **C52 Tier-All:** omitted; not semantically appropriate for this provenance-only milestone.
161. **Ordinary restoration:** pass; ordinary kernel and ESP restored.
162. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
163. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
164. **Proof artifact active:** no; ordinary proof inactive after each harness cleanup.
165. **C75-owned QEMU cleanup:** pass; smoke finally blocks cleaned owned processes.
166. **Unrelated QEMU preservation:** preserved; no unrelated process was intentionally terminated.
167. **Files changed:** this document and `scripts/dotnet/Invoke-C011EC75SurvivedPerRegionProvenance.ps1`.
168. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C75_SURVIVED_PER_REGION_PROVENANCE.md`.
169. **Evidence root:** `out/dotnet/c011ec75-survived-per-region-provenance/`.
170. **Final commit:** local commit subject `Trace NativeAOT survived-per-region provenance`.
171. **Push status:** not pushed.
172. **Remaining limitation:** exact SIX retained-object placement and its upstream region eligibility predicate are not published by the accepted C73-only SIX observer.
173. **Exact next-smallest milestone:** C76 should follow the earlier authentic geometry/list eligibility quantity and add only the minimum safe observation needed to distinguish retained-object placement from basic free-region cohort eligibility; do not start B02.

## C76 decision

Because C75 classified `survived_per_region` as CORRELATED rather than INPUT or INTERMEDIATE, C76 must follow the earlier region geometry/occupancy and basic free-region eligibility state. It should preserve authentic promotion and change only managed workload geometry if a crossover is justified. B02 remains premature.

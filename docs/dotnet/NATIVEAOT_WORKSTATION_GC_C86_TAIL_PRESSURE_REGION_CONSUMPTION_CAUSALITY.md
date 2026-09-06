# NativeAOT Workstation GC C86: tail-pressure region-consumption causality

C86 tested the same accepted `PromotionDecisionLiveByteThreshold` workload at
`15mid8` with only the historical post-debit tail selector changed:
`320` versus `216`. The primary pair reproduced the crossover on three fresh
boots per cell:

| Cell | Retained cohort | Promotion | Pre / post-Restart / post-resume basic | Basic insertions / removals | Tail |
| --- | ---: | --- | --- | --- | ---: |
| `15mid8` T320 | 15 | 15 objects / `0xFD980` | `1 / 1 / 1` | `15 / 15` | 320 |
| `15mid8` T216 | 15 | 15 objects / `0xFD980` | `1 / 6 / 6` | `15 / 10` | 216 |
| `baseline16` reference | 16 | 16 objects / `0x100180` | `1 / 6 / 6` | `13 / 8` | 216 |

C70 initially made the boundary look like a survivor-count/live-byte
phenomenon. C75 showed that `survived_per_region` was only correlated. C76
eliminated basic-size eligibility. C84 proved that all five target regions are
materialized on both sides. C85 proved that ONE consumes four targets through
generation-0 allocation and removes the fifth through surplus/decommit. C86
now shows that the old tail selector is the strongest upstream control found:
it changes the number of ordinary transient generation-0 allocation
iterations by 104 (`0x68`), changes the observed post-Restart basic cohort,
and changes the downstream surplus topology seen by decommit policy.

The exact live numeric values of `total_budget_in_region_units` and
`balance` inside `distribute_free_regions` were not exposed by the existing
scalar observer. C86 therefore proves the selector-to-demand and
demand-to-topology links, while reserving the exact allocation-context byte
threshold and exact EXTRA4 budget boundary for C87.

## Numbered report

1. **Outcome:** Outcome G: the same-workload tail crossover was reproduced; tail demand is the strongest supported upstream cause of the five-region consumption difference.
2. **Success Level:** Level 3 for the causal crossover and source-backed chronology; the live numeric decommit budget operands remain a C87 limitation.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `e500d7df6fdf5e7c08b75c1101aa99d5ccead691`.
6. **Starting subject:** `Trace NativeAOT basic region recycle chronology`.
7. **Final HEAD:** The local C86 commit containing this report; exact hash is recorded in item 187 and in the final handoff.
8. **Final subject:** `Isolate NativeAOT tail-driven region consumption`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** Actual starting state was `0 ahead / 0 behind`; the requested expectation was `1 ahead / 0 behind`.
11. **Final ahead/behind:** `1 ahead / 0 behind` after the local commit.
12. **Starting worktree:** Clean before C86 changes.
13. **Final worktree:** Clean after the local commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31` for `tools/dotnet/runtime-pack/patches/nativeaot-amd64-fp-handoff.patch`.
17. **C84 SHA:** `e261590c4580573cc2f0dc69b18cdf59f310f172`.
18. **C85 SHA:** `e500d7df6fdf5e7c08b75c1101aa99d5ccead691`.
19. **C86 SHA:** The containing local commit; see item 187.
20. **Exact C86 question:** Was the old tail-pressure control driving the four generation-0 consumptions and the fifth decommit, and how does T320 differ from T216 with the retained cohort and promotion accounting held constant?
21. **C85 15mid8+216 development evidence located:** `out/dotnet/c011ec85-basic-free-removal-recycle/15mid8/target-1400000/run-20260906-113625186`.
22. **Development evidence validity:** Valid as hypothesis-generating historical evidence only; it used `PromotionPositiveRegionCohort`, not the formal C86 live-byte mode, and was preserved under `c85-development-import`.
23. **T320 managed mode:** `PromotionDecisionLiveByteThreshold`.
24. **T216 managed mode:** `PromotionDecisionLiveByteThreshold`.
25. **T320 case:** `15mid8`.
26. **T216 case:** `15mid8`.
27. **T320 retained count:** 15.
28. **T216 retained count:** 15.
29. **T320 object size:** `0x10E80` measured object size.
30. **T216 object size:** `0x10E80` measured object size.
31. **T320 retained-live bytes:** `0xFD980`.
32. **T216 retained-live bytes:** `0xFD980`.
33. **T320 promotion:** Authentic positive promotion.
34. **T216 promotion:** Authentic positive promotion.
35. **T320 promoted objects:** 15.
36. **T216 promoted objects:** 15.
37. **T320 promoted bytes:** `0xFD980`.
38. **T216 promoted bytes:** `0xFD980`.
39. **T320 tail selector:** Effective managed selector is the W3 fallback, `postDebitTailAllocations = 320`; the native C66 display is `value=0x140`.
40. **T216 tail selector:** `HOSTLOGPROOF_C011EC66_T216`, `postDebitTailAllocations = 216`; native C66 displays `value=0xD8`.
41. **Tail selector source file:** `samples/managed/HostLogProof/Program.cs`.
42. **Tail selector source expression:** The `#elif HOSTLOGPROOF_C011EC66_T216` branch selects `216`; otherwise the W3 branch selects `320`, and `for (uint offset = 0u; offset < postDebitTailAllocations; offset++)` executes the tail.
43. **Tail selector semantic unit:** Count of post-debit transient managed allocation iterations, not objects retained, bytes, or regions.
44. **Tail allocation size:** `0x4000` payload bytes per `byte[]`; the observed managed object request is `0x4018` including object overhead.
45. **T320 tail allocation count:** 320 (`0x140`).
46. **T216 tail allocation count:** 216 (`0xD8`).
47. **T320 total tail bytes:** `0x500000` payload bytes; `0x501E00` object-request bytes.
48. **T216 total tail bytes:** `0x360000` payload bytes; `0x361440` object-request bytes. The difference is `0x1A0000` payload bytes or `0x1A09C0` object-request bytes.
49. **Tail allocations retained:** No. They are local transient arrays and are not stored in the retained survivor cohort.
50. **Tail allocations live at target GC:** No; they are ordinary short-lived pressure and are dead or collectible by the target promotion boundary.
51. **Tail allocation generation:** Generation 0 ordinary managed allocation.
52. **Tail allocation timing:** After `promotionReady`, after the promotion debit and RestartEE/managed resume boundary, before the natural post-debit allocation/OOS sequence; the loop does not call `GC.Collect` or request a collection directly.
53. **T320 pre-GC basic:** 1.
54. **T216 pre-GC basic:** 1.
55. **T320 post-Restart basic:** 1.
56. **T216 post-Restart basic:** 6.
57. **T320 post-resume basic:** 1.
58. **T216 post-resume basic:** 6.
59. **Same-workload crossover reproduced:** Yes, 3/3 T320 boots and 3/3 T216 boots reproduced their respective semantic results.
60. **T320 allocation-context initial state:** C011EC62 post-GC context valid; pointer `0`, limit `0`, remaining headroom `0`, active segment `0`, context address `0x39ACCC0`.
61. **T216 allocation-context initial state:** C011EC62 post-GC context valid; pointer `0`, limit `0`, remaining headroom `0`, active segment `0`, context address `0x39A9CC0`.
62. **Earliest context divergence:** No headroom divergence is observed at the common post-GC checkpoint. The earliest semantic difference is the compiled tail count and the resulting number of completed ordinary allocations; topology differs later.
63. **T320 post-GC allocation demand bytes:** Tail object-request demand is `0x501E00`.
64. **T216 post-GC allocation demand bytes:** Tail object-request demand is `0x361440`.
65. **T320 context headroom:** `0` at the decisive C011EC65 exhaustion; the failing request is `0x4018`.
66. **T216 context headroom:** `0` at the decisive C011EC65 exhaustion; the failing request is `0x4018`.
67. **T320 get_free_region(0) call count:** C67 aggregate `0x11` (17).
68. **T216 get_free_region(0) call count:** C67 aggregate `0x0F` (15).
69. **T320 get_new_region(0) relevant call count:** C67 aggregate `0x0E` (14); C70’s decisive refill sees one candidate and succeeds.
70. **T216 get_new_region(0) relevant call count:** C67 aggregate `0x0C` (12); C70’s decisive refill sees zero candidates and fails.
71. **EXTRA0 removed T320:** Yes; C85’s accepted target chronology maps the final target removal to `get_free_region(0)` then `get_new_region(0)`.
72. **EXTRA0 removed T216:** Reduced/reordered case: one raw C77 removal is present, but the corresponding later C85 consumer chain is absent; EXTRA0 is not part of a four-removal-equivalent sequence.
73. **EXTRA0 demand cause:** T320’s ordinary generation-0 request consumes the selected basic-free region; the source-backed C85 ordinals were insert `0xAB`, remove `0xC3`, consumer `0xC5/0xC6`.
74. **EXTRA1 removed T320:** Yes; ordinary generation-0 consumption.
75. **EXTRA1 removed T216:** Reduced/reordered case: one raw C77 removal is present, without the T320 post-insertion consumer chain.
76. **EXTRA1 demand cause:** C85 source-backed ordinals insert `0xAC`, remove `0xC8`, consumer `0xCA/0xCB`; the consumer is an ordinary gen0 refill.
77. **EXTRA2 removed T320:** Yes; ordinary generation-0 consumption.
78. **EXTRA2 removed T216:** No corresponding target removal appears in the final T216-clean raw C77 target set; EXTRA2 remains available.
79. **EXTRA2 demand cause:** C85 source-backed ordinals insert `0xAD`, remove `0xCD`, consumer `0xCF/0xD0`; the consumer is an ordinary gen0 refill.
80. **EXTRA3 removed T320:** Yes; ordinary generation-0 consumption.
81. **EXTRA3 removed T216:** Reduced/reordered case: one later raw C77 removal is present, but not the T320 four-acquisition chronology.
82. **EXTRA3 demand cause:** C85 source-backed ordinals insert `0xAE`, remove `0xB4`, consumer `0xB6`; the source branch is ordinary gen0 supply demand.
83. **All four acquisition removals accounted:** T320’s four target-specific C85 chains are accounted for; T216 shows a reduced/reordered topology, not four equivalent post-insertion target consumer chains.
84. **T320 basic-list insertions:** 15.
85. **T320 ordinary removals:** 15 basic-list removals in C77; C85 classifies the four target removals as gen0 consumption and the fifth target removal as decommit migration.
86. **T320 decommit removals:** One target decommit removal, EXTRA4.
87. **T320 final accounting:** `1 + 15 insertions - 15 removals = 1`; the decommit path is one member of the removal chronology.
88. **T216 basic-list insertions:** 15.
89. **T216 ordinary removals:** 10.
90. **T216 decommit removals:** 0 for EXTRA4 in the final T216-clean chronology.
91. **T216 final accounting:** `1 + 15 insertions - 10 removals = 6`.
92. **EXTRA4 decommit T320:** Yes; C85 corrected chronology is insert `0xAB`, basic remove `0xB2`, transfer at `0xB3`, decommit unlink at `0xB7`.
93. **EXTRA4 decommit T216:** No; EXTRA4 is retained as basic-free in the final six-region result.
94. **distribute_free_regions operands T320:** The production source consumes `total_num_free_regions[basic]`, `total_budget_in_region_units[basic]`, `surplus_regions[basic].get_num_free_regions()`, `num_huge_region_units_to_consider`, `region_size`, and `balance`; C77/C85 prove a positive selection path but do not serialize all numeric operands.
95. **distribute_free_regions operands T216:** The same production operands are used; the final T216 topology does not select EXTRA4 for decommit, and the live numeric budget values are not directly observed.
96. **move_highest_free_regions operands T320:** Source passes the positive `num_regions_to_decommit * region_factor`, `kind == basic_free_region`, and `global_regions_to_decommit`; traversal is highest-to-lowest region-map order.
97. **move_highest_free_regions operands T216:** No EXTRA4 move is observed; no positive live argument for this target was serialized.
98. **Decommit threshold/policy source:** Locked `src/coreclr/gc/gc.cpp`, `gc_heap::distribute_free_regions` around lines 13276–13556, `region_allocator::move_highest_free_regions` around 4270, and `decommit_step`/`decommit_region` around 44483–44531.
99. **T320 decommit threshold state:** Source reconstruction requires `balance >= 0` and a positive removal count for the observed EXTRA4 move; the exact T320 `total_budget_in_region_units` and `balance` values are not emitted.
100. **T216 decommit threshold state:** No target EXTRA4 move is observed; the exact T216 budget and balance values are not emitted.
101. **EXTRA4 surplus reason:** T320’s larger tail causes more ordinary gen0 consumption and a different free-list count/topology, making EXTRA4 eligible for the surplus/decommit path; T216 leaves enough basic-free topology that EXTRA4 remains.
102. **EXTRA4 divergence caused by prior four acquisitions:** Supported as the downstream same-tail chronology: C85 shows four ordinary target consumptions before the decommit removal, and T216 lacks the corresponding complete chain; exact internal budget arithmetic is reserved.
103. **EXTRA4 independent cause if not:** No independent source or event evidence was found.
104. **Earliest T320/T216 semantic divergence:** The managed tail selector changes the number of post-resume transient allocation iterations while all retained/promotion inputs remain equal.
105. **Divergence ordinal:** C64 records `0x183` post-resume allocation events for T320 versus `0x11B` for T216, a difference of `0x68` (104), exactly the selector-count difference; the first guaranteed logical divergence is when T216 stops at iteration 216.
106. **Divergence source function:** `HostLogProof`’s `promotionReady` tail loop in `samples/managed/HostLogProof/Program.cs`.
107. **T320 operands:** C011EC65 request `0x4018`, context remaining `0`, active region `0x104011238`; C70 sees candidate count 1 and `get_new_region(0)` result 1 at the decisive refill.
108. **T216 operands:** C011EC65 request `0x4018`, context remaining `0`, active region `0x1040112E0`; C70 sees candidate count 0 and `get_new_region(0)` result 0 at the decisive refill.
109. **Allocation-demand causal status:** Strongly supported; the tail byte arithmetic and exact 104-iteration difference are directly source-backed and observed by C64.
110. **Allocation-context causal status:** Capacity exhaustion is observed, but the common initial context is empty; C86 proves demand reaches exhaustion, not the exact per-iteration headroom trace.
111. **Tail-pressure causal status:** Primary supported upstream variable for the crossover.
112. **Decommit causal status:** Downstream policy consequence of the altered free-region topology; the exact numeric policy boundary remains uninstrumented.
113. **Unified five-region explanation:** Tail320 supplies 104 more transient gen0 allocation iterations, consumes four target regions through ordinary demand, and leaves a topology in which EXTRA4 is moved to decommit; tail216 stops earlier, leaving six basic-free regions.
114. **First supported causal link:** `tail320 - tail216 = 104` ordinary allocation iterations and `0x1A09C0` object-request bytes.
115. **Strongest causal chain:** Tail selector → transient gen0 demand → context exhaustion/refill requests → ordinary basic-free consumption → changed free-list topology/count → EXTRA4 surplus/decommit decision.
116. **First unsupported causal link:** The exact numeric `demand bytes - context headroom` threshold for each of the four target acquisitions and the exact live `balance` operand for EXTRA4.
117. **baseline16/216 reference result:** Fresh reference boot is `1 / 6 / 6`, with authentic 16-object positive promotion.
118. **baseline16/216 ordinary-removal count:** 8.
119. **baseline16/216 decommit-removal count:** 0.
120. **15mid8/216 equivalent to baseline16 chronology:** Semantically equivalent in final six-region retention, but not chronologically identical: T216 has 15 insertions and 10 removals, while baseline16 has 13 insertions and 8 removals.
121. **Survivor/live-byte explanation superseded:** Yes; the primary pair holds retained count, object size, live bytes, promoted objects, and promoted bytes constant.
122. **Region-materialization explanation superseded:** Yes; C84 proved the five regions materialize on both sides.
123. **Basic eligibility explanation superseded:** Yes; C76/C85 found no target rejection by basic-size eligibility.
124. **Candidate-path causal relevance:** Relevant only as the downstream consumption path; candidate eligibility is not the upstream cause, and C85 found no rejected target candidate.
125. **B02 evaluated:** Yes, as a status check; no new candidate rejection was observed.
126. **B02 status:** `STILL_PREMATURE`.
127. **B02 future justification:** Only a future specific candidate-path rejection would justify B02; C86 provides none.
128. **C86 BSS baseline:** The accepted C84/C85 safe diagnostic baselines remain ONE `0x4F0840` and SIX `0x3947E0`.
129. **C86 BSS:** Zero-BSS-delta observer composition was reused; no unconditional C86 diagnostic array or event-capacity expansion was added.
130. **BSS delta:** `0` relative to the appropriate accepted per-cell C84/C85-safe composition.
131. **Production mutation:** None.
132. **Allocator mutation:** None.
133. **Allocation-context mutation:** None.
134. **Free-list mutation:** None.
135. **Decommit-policy mutation:** None.
136. **Region mutation:** None.
137. **Generation mutation:** None.
138. **Candidate mutation:** None.
139. **Planner mutation:** None.
140. **Promotion mutation:** None.
141. **Survivor fabrication:** None.
142. **Root fabrication:** None.
143. **C18:** PASS.
144. **Code manager:** PASS through the existing C18/C67 runtime-pack and unwind/symbol guards.
145. **FindMethodInfo:** PASS.
146. **Root scan:** PASS.
147. **Mark closure:** PASS.
148. **Planner authenticity:** PASS; no synthetic planner decision was used.
149. **Survivor integrity:** PASS.
150. **C86 invariant failures:** 0 in all six primary-pair boots and the reference boot.
151. **Sensitive diagnostic allocations:** 0.
152. **C86 event capacity:** C77 capacity `0x800`; the offline C85 chronology slot capacity remained `0x40`.
153. **C86 peak events:** T320 `0xC6`; T216 `0xD0`; reference `0xC6`.
154. **C86 overflow:** 0 event overflow and 0 region overflow.
155. **Fail-fast:** 0.
156. **Page faults:** 0.
157. **T320 Boot 1:** PASS; authentic promotion, `1/1/1`, zero diagnostics/invariants.
158. **T320 Boot 2:** PASS; authentic promotion, `1/1/1`, zero diagnostics/invariants.
159. **T320 Boot 3:** PASS; authentic promotion, `1/1/1`, zero diagnostics/invariants.
160. **T216 Boot 1:** PASS; authentic promotion, `1/6/6`, zero diagnostics/invariants.
161. **T216 Boot 2:** PASS; authentic promotion, `1/6/6`, zero diagnostics/invariants.
162. **T216 Boot 3:** PASS; authentic promotion, `1/6/6`, zero diagnostics/invariants.
163. **Reference SIX boot(s):** One fresh `baseline16/216` boot PASS; `1/6/6`, 8 ordinary removals, no decommit.
164. **Semantic agreement:** QEMU semantic agreement is true for T320 and T216; all three boots per primary cell agree.
165. **Nondeterminism:** No semantic nondeterminism; serial hashes differ only in run-local addresses/chronology serialization.
166. **Serial hashes:** T320: `30B3561E1223D351FEF9822678394E4627F40EDBA6E9B07E9F8D412BDA76090B`, `1F1E91F56DC037BB8FCE3C0F7FAE38BE5A642D573F6034BCC866134E846D6518`, `F6955B16DF7841B42158D1E895536492AC3D0FDD154E6DD88B75C4B0620F5AB6`; T216: `6759F240D2399136C4694D645C7CAC1BEAED5E36B8BC58A4652D3C3C8E87773A`, `AA291EFCA8869F4DB5B67A533C82C9BAA08840E563C42F5FA6D3346EFAEC4F67`, `9AAC94CDC6EFC9F3D03852E4AC021BB848338E0C4B16ECAC26DE197E749A78D3`; reference: `2B74BED1DA8BFB1393FE7456F6B21EDA4EB7198B64FC1BC4C978D857D21DD158`.
167. **Artifact hashes:** T320 EXE `8B065F7D24B5AB8A229C62827B6FD45B2C35DE98BDE041652CBA289467E22C51`, ELF `2FE6545C6F781472BD3CE1B06202870EF450317561486111F02DF5E33CF2C67C`, MAP `5B21F003F82B29791972CB9B31FC0B5A10A0348D0BF6AC7E60DB4EDD49A7D3FC`; T216 EXE `ED5AA4D8D03163A09BD406F3F753C86EF0C654EC7CFACDDE0BA8D3690A52347E`, ELF `9457A9D98A431ED75352632B61D3BF03CC2F4C21941995C31853CA927BC4134B`, MAP `A80C1081BB117D725D208E2894A98653D24E6A051A30446B92A981B025DCF490`; reference EXE `E0930BF230DB5197675893067FEED28EACA3003580F094084A6F5698E8E10A01`, ELF `59C8391765B5EBCFB17F1575975E42B6D67D47A5D438B52DE80E3AD62045A5D0`, MAP `E22F6CC601283C7046788AF00F1DFC9411D732918D601B2B8FC10774CA8B0F8A`.
168. **Runtime-pack validation:** PASS using `out/dotnet/runtime-pack-c68/runtime-pack.manifest.json`; source commit and FP repair identity match items 14–16.
169. **Managed build:** PASS; all final managed NativeAOT publish steps completed.
170. **Native build:** PASS; runtime-pack, native artifact, kernel link, and QEMU image steps completed.
171. **PowerShell syntax:** PASS for the modified harness.
172. **JSON/XML parse:** PASS for the modified project and runtime/C86 manifests.
173. **git diff --check:** PASS.
174. **PE → ELF conversion:** PASS; final run `pe-to-elf.log` and manifest converter guard are PASS.
175. **Symbol checks:** PASS; exports, entry symbols, unwind, and managed `ManagedMain` checks passed.
176. **Linker/source/table/archive guards:** PASS; runtime-pack archive membership and linker/source/table checks passed.
177. **C52 Tier-All result or reason omitted:** Omitted because C86 is a narrow same-workload causal crossover and C52 Tier-All would not add semantic evidence.
178. **Ordinary restoration:** PASS after `finally` restoration on every run.
179. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
180. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
181. **Proof artifact active:** Active only during the proof boots; proof image was restored inactive afterward.
182. **C86-owned QEMU cleanup:** 0 QEMU processes remained.
183. **Unrelated QEMU preservation:** No unrelated QEMU process was present or modified; unrelated state was preserved.
184. **Files changed:** `samples/managed/HostLogProof/HostLogProof.csproj`, `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, and this C86 report.
185. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C86_TAIL_PRESSURE_REGION_CONSUMPTION_CAUSALITY.md`.
186. **Evidence root:** `out/dotnet/c011ec86-tail-pressure-region-consumption/`, separated into C85 development import, T320/T216 discovery, baseline reference, final confirmations, and offline chronology.
187. **Final commit:** Local commit subject `Isolate NativeAOT tail-driven region consumption`; the exact final SHA is reported in the final handoff because this report is part of that commit.
188. **Push status:** Not pushed.
189. **Remaining limitation:** Existing safe scalar observers do not serialize each tail allocation’s pointer/limit/headroom transition or the live numeric `distribute_free_regions` budget/balance operands; C86 does not claim those values.
190. **Exact next-smallest milestone:** C87 should isolate the allocation-context exhaustion boundary and byte arithmetic around the authentic `216/320` threshold, then separately capture EXTRA4’s exact surplus/decommit threshold with at most one narrow boundary test; do not return to survivor sweeps or B02.

## Evidence layout

- Historical development import: `out/dotnet/c011ec86-tail-pressure-region-consumption/c85-development-import/`.
- T320 discovery: `out/dotnet/c011ec86-tail-pressure-region-consumption/T320-discovery/`.
- T216 discovery: `out/dotnet/c011ec86-tail-pressure-region-consumption/T216-discovery/`.
- Fresh reference: `out/dotnet/c011ec86-tail-pressure-region-consumption/baseline16-reference-final/`.
- Final confirmations: `out/dotnet/c011ec86-tail-pressure-region-consumption/final-confirmations/`.
- Offline C85 chronology: `out/dotnet/c011ec86-tail-pressure-region-consumption/offline-allocation-decommit-chronology/c85-target-chronology.md`.

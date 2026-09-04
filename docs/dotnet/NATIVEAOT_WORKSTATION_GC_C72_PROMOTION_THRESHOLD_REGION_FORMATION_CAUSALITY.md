# NativeAOT Workstation GC C72: Promotion-Threshold-to-Region-Formation Causality

## Result

**Outcome C — region cohort correlated but not yet causally accounted.**

**Success Level 1.** The exact C71 adjacent boundary reproduced on fresh
boots, and the first source-backed production quantity was observed before the
later region/list chronology. C72 traced bounded region identities, list
operations, generation/state snapshots, RestartEE, managed resume, and the
existing C70 downstream markers without mutation. It did not establish a
unique planner-to-region or region-to-candidate identity chain.

The C72 workload did **not** reproduce the historical six-region
post-RestartEE cohort. Both sides produced one observed post-Restart basic
region and zero at managed resume. The C72 planner callback count was zero on
both sides. Consequently this closeout does not claim that the threshold
directly creates six regions, and it does not justify B02.

The raw evidence is under:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec72-promotion-threshold-region-formation\`

The final matrix is:

`D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT\out\dotnet\c011ec72-promotion-threshold-region-formation\c72-matrix.json`

## Source-backed interpretation

C71 established that the earliest directly attributable production quantity is
the marked-object-size accumulation in the locked runtime source:

`src/coreclr/gc/gc.cpp:26657-26665`,
`gc_heap::add_to_promoted_bytes`,
`survived_per_region[get_basic_region_index_for_address(object)] += obj_size;`.

The next policy consumer is
`gc_heap::decide_on_promotion_surv` at `src/coreclr/gc/gc.cpp:29462-29488`,
with condition `(threshold > older_gen_size) || (promoted > threshold)`.
The post-mark publication to `settings.promotion` is at
`src/coreclr/gc/gc.cpp:30216-30230`.

The C72 mark stream first differs at raw event ordinal `0x7`, where the
retained object is `0x10E78` versus `0x10E80`. The human/source marker reports
the subsequent decision observation at ordinal `0x9` and the first region-level
observation at ordinal `0xC`. The later C70 authentic promotion marker is the
semantic class used for LOW/HIGH: `0` versus `1`. C72's observed policy
decision field is `1` on both sides and therefore is not treated as the
LOW/HIGH semantic discriminator.

The C72 observer is fixed-capacity and allocation-free in the instrumented
paths. It records region/list/source/generation/state/order data in fixed arrays,
and it reports overflow independently. The C72 event capacity is `0x1000`
records and region capacity is `0x100`; final C72 event and region overflow are
zero on every final boot.

## LOW/HIGH comparison

| Field | LOW `0xFD908` | HIGH `0xFD980` |
| --- | ---: | ---: |
| Retained count | 15 | 15 |
| Actual object size | `0x10E78` | `0x10E80` |
| Retained live bytes | `0xFD908` | `0xFD980` |
| First production divergence | mark event `0x7`; source accumulation | mark event `0x7`; source accumulation |
| Planner decision | not observed (`count=0`) | not observed (`count=0`) |
| Authentic promotion decision | 0 | 1 |
| Promoted objects | 0 | 15 |
| Promoted bytes | `0x0` | `0xFD980` |
| Region transitions | `0x1E` | `0x14` |
| Basic insertions | `0x12` | `0x0B` |
| Basic removals | `0x12` | `0x0B` |
| Post-Restart basic count | 1 | 1 |
| Post-resume basic count | 0 | 0 |
| Last nonzero candidate count | 1 | 1 |
| Candidate selected | 0 | 0 |
| Normal refill | 0 | 0 |
| `commit_failed` | 0 | 0 |
| OOS reason | 5 | 5 |
| Requested generation | 2 | 2 |
| Condemned generation | 2 | 2 |

## Required final report (171 fields)

1. **Outcome:** Outcome C — region cohort correlated but not yet causally accounted.
2. **Success Level:** Level 1.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `96253f28ff89b020ca129c24544af3efe823a8c5`.
6. **Starting subject:** `Trace NativeAOT promotion live-byte threshold`.
7. **Final HEAD:** The C72 closeout commit created after this report; exact SHA is recorded in the final handoff.
8. **Final subject:** `Trace NativeAOT promotion region formation`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** ahead 1, behind 0.
11. **Final ahead/behind:** ahead 2, behind 0 after the local C72 commit; no push.
12. **Starting worktree:** clean at the C71 baseline before C72 edits.
13. **Final worktree:** clean after the local C72 commit.
14. **Runtime identity:** NativeAOT 9.0.0, AMD64, Workstation GC, interfaces 5.3 / 2.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **Semantic rewrite guard state:** C46/C47/C48 semantic rewrites disabled; durable FP repair inherited from the runtime pack.
18. **C70 SHA:** No separate C70 commit; C70 report/instrumentation is inherited through the C71 baseline `ca7c853f55dce0f4420f40053435db93da534d57`.
19. **C71 SHA:** `96253f28ff89b020ca129c24544af3efe823a8c5`.
20. **C72 SHA:** Assigned by the closeout commit; exact SHA is in the final handoff because a commit cannot embed its own hash without self-reference.
21. **Exact C72 question:** What exact production GC state transition caused by crossing the C71 adjacent retained-live boundary creates the post-collection basic-free-region population and changes candidate/refill/OOS behavior?
22. **LOW retained count:** 15.
23. **LOW actual object size:** `0x10E78`.
24. **LOW retained-live total:** `0xFD908`.
25. **LOW reproduction result:** C71-backed no authentic promotion; reproduced on discovery and all three confirmation boots.
26. **HIGH retained count:** 15.
27. **HIGH actual object size:** `0x10E80`.
28. **HIGH retained-live total:** `0xFD980`.
29. **HIGH reproduction result:** C71-backed authentic promotion; reproduced on discovery and all three confirmation boots.
30. **Total LOW/HIGH live-byte difference:** `0x78` total, from `0x8` per object across 15 objects.
31. **Earliest production divergence:** C72 raw mark event `0x7`; the source-level divergence is marked-object-size accumulation before the later policy marker at `0x9`.
32. **Source file:** `src/coreclr/gc/gc.cpp` in runtime source `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
33. **Source function:** `gc_heap::add_to_promoted_bytes`; next consumer `gc_heap::decide_on_promotion_surv`.
34. **Source condition:** `(threshold > older_gen_size) || (promoted > threshold)`.
35. **LOW operands:** C72 decision sample `threshold=0x2666`, `promotedBytes=0xDBC18`, `olderGenerationSize=0x221D0`, `decision=1`; this later policy sample is not the authentic C70 semantic class.
36. **HIGH operands:** C72 decision sample `threshold=0x2666`, `promotedBytes=0xDBC80`, `olderGenerationSize=0x221E0`, `decision=1`; this later policy sample is not the sole class discriminator.
37. **Planner decision LOW:** Not observed; `plannerCount=0`, `plannerDecision=0`.
38. **Planner decision HIGH:** Not observed; `plannerCount=0`, `plannerDecision=0`.
39. **Promotion decision LOW:** Authentic C70 promotion marker `observed=0`.
40. **Promotion decision HIGH:** Authentic C70 promotion marker `observed=1`.
41. **Promoted objects LOW:** 0 authentic promoted objects.
42. **Promoted objects HIGH:** 15.
43. **Promoted bytes LOW:** `0x0` on the authentic C70 promotion marker.
44. **Promoted bytes HIGH:** `0xFD980`.
45. **Gen1 budget before LOW/HIGH:** LOW `0xCFD80` on the non-authentic downstream accounting marker; HIGH `0x1AB988` on the authentic promotion marker.
46. **Gen1 debit LOW/HIGH:** LOW `0x200300` downstream accounting; HIGH `0xDBC80` authentic promotion debit.
47. **Gen1 budget after LOW/HIGH:** LOW `0xFFFFFFFFFFECFA80` downstream accounting; HIGH `0xCFD08` authentic promotion result.
48. **Relevant region count before divergence:** C70 pre-promotion total region count `0xA` on both sides; C72 later ledger contains `0xB` bounded identities per side.
49. **Relevant HIGH region identities:** C72 ledger IDs are preserved in the latest HIGH run's `region-genealogy.json`; the post-Restart cohort identity is `0x0000000104011040` within that boot.
50. **Relevant LOW region identities:** C72 ledger IDs are preserved in the latest LOW run's `region-genealogy.json`; the post-Restart cohort identity is also `0x0000000104011040` within that boot. Cross-boot pointer equality is not inferred.
51. **Region generation-before states:** The post-Restart cohort region is observed as generation `0 -> 0`; all other ledger before/after values are in the per-case genealogy file.
52. **Region generation-after states:** The post-Restart cohort remains generation `0`; the complete ledger records `0->1`, `1->2`, and related transitions where observed.
53. **Region state-before values:** The post-Restart cohort enters the basic list from state `0x09`; full per-region values are preserved in the ledger.
54. **Region state-after values:** The post-Restart cohort is observed at state `0x0B`; removals include state `0x0A -> 0x08` where applicable.
55. **Region live bytes:** Post-Restart cohort live bytes are `0` on both sides.
56. **Region free bytes:** Post-Restart cohort free bytes are `0xFFFD8` on both sides.
57. **Region allocated bytes:** Post-Restart cohort allocated range begins at `0x10180028`; exact committed/used ranges are preserved per boot.
58. **Reclamation/evacuation/compaction classification:** The cohort is observed through `gc_heap::return_free_region` and basic-list insertion; C72 does not prove it is a promoted region, evacuation source/destination, or compaction product.
59. **Basic-list count before collection:** C72 `beforeBasicCount=0` with observation present on both sides.
60. **Basic insertions LOW:** `0x12` (`18`) authentic observed operations.
61. **Basic insertions HIGH:** `0x0B` (`11`) authentic observed operations.
62. **Basic removals LOW:** `0x12` (`18`) authentic observed operations.
63. **Basic removals HIGH:** `0x0B` (`11`) authentic observed operations.
64. **Post-Restart basic count LOW:** 1.
65. **Post-Restart basic count HIGH:** 1.
66. **Post-resume basic count LOW:** 0.
67. **Post-resume basic count HIGH:** 0.
68. **Exact provenance of each HIGH post-Restart basic region:** One region, `0x10401040` in the final HIGH boot, was created/committed at ordinals `0x133/0x132`, inserted at `0x1FB` through source branch 4 (`gc_heap::return_free_region`), with state `0x09 -> 0x0B`, basic membership at RestartEE `1`, and no later C72 removal. This is one region, not six.
69. **Exact explanation for corresponding LOW absence/difference:** LOW does not lack the cohort: it also has one post-Restart region `0x10401040`, inserted first at `0x1DE`, removed at `0x28C`, and represented by a later surviving basic insertion. Thus the LOW/HIGH difference is list churn (`18/18` versus `11/11`), not a six-versus-zero cohort.
70. **Expansion attempted LOW/HIGH:** Attempted (`1`) on both.
71. **Expansion result LOW/HIGH:** Succeeded (`0`) on both.
72. **Hard-limit-short LOW/HIGH:** No separate C72 hard-limit-short marker; no LOW/HIGH difference observed, with full OOS observed as `1` on both.
73. **Tail reclaim LOW/HIGH:** `0` on both.
74. **Tail provenance LOW/HIGH:** No distinct tail-reclaim provenance was observed; the C72 genealogy does not attribute the cohort to tail reclamation.
75. **Global event count:** LOW `0x3F1` (`1009`); HIGH `0x301` (`769`) in the C72 summaries.
76. **Diagnostic capacity:** `0x1000` C72 events and `0x100` C72 region records.
77. **Diagnostic overflow:** C72 event overflow `0`, region overflow `0` on all six final boots; inherited C65 diagnostic overflow is LOW `0x32`, HIGH `0` and is reported separately.
78. **First region-level divergence ordinal:** `0xC` on both sides, the first region-level observation after the C72 decision marker; it is not a claim that a unique causal class diverged there.
79. **RestartEE ordinal:** `0x0` in the C72 boundary stream on both sides.
80. **Managed-resume ordinal:** LOW `0x367`; HIGH `0x204`.
81. **Last nonzero candidate count LOW:** 1.
82. **Last nonzero candidate count HIGH:** 1.
83. **Candidate genealogy LOW:** C70 candidate head `0x104010F98`, one eligible candidate; C70 final unlink is region `0x1040110E8`; C72 cannot link the candidate head to a unique earlier threshold-classified region.
84. **Candidate genealogy HIGH:** C70 candidate head `0x104010EF0`, one eligible candidate; C70 final unlink is region `0x1040110E8`; C72 cannot link the candidate head to a unique earlier threshold-classified region.
85. **Final candidate unlink LOW:** Observed, ordinal `0xD8`, region `0x1040110E8`, state `0x0A -> 0x08`, count `1 -> 0`.
86. **Final candidate unlink HIGH:** Observed, ordinal `0xA6`, region `0x1040110E8`, state `0x0A -> 0x08`, count `1 -> 0`.
87. **Decisive context exhaustion LOW:** Observed on region `0x1040110E8`, request size `0x10018`, active headroom `0`, generation 0.
88. **Decisive context exhaustion HIGH:** Observed on region `0x1040110E8`, request size `0x4018`, active headroom `0`, generation 0.
89. **`get_new_region(0)` LOW:** Observed result `1`, candidate count `1`, candidate head/tail `0x104010F98`, `commit_failed=0`.
90. **`get_new_region(0)` HIGH:** Observed result `1`, candidate count `1`, candidate head/tail `0x104010EF0`, `commit_failed=0`.
91. **Candidate availability LOW:** `1`.
92. **Candidate availability HIGH:** `1`.
93. **Candidate selected LOW:** `0`.
94. **Candidate selected HIGH:** `0`.
95. **Selected region LOW:** None observed; selected-region marker reports null identity.
96. **Selected region HIGH:** None observed; selected-region marker reports null identity.
97. **Selected region provenance:** No selected-region identity exists to trace; this is the first candidate-to-selection gap.
98. **Normal refill LOW:** `0`.
99. **Normal refill HIGH:** `0`.
100. **`commit_failed` LOW:** `0`.
101. **`commit_failed` HIGH:** `0`.
102. **OOS reason LOW:** reason `5` (`reason_oos_soh`), full OOS `1`.
103. **OOS reason HIGH:** reason `5` (`reason_oos_soh`), full OOS `1`.
104. **`last_gc_before_oom` LOW/HIGH:** `1` on both.
105. **Requested generation LOW:** `2`.
106. **Requested generation HIGH:** `2`.
107. **Condemned generation LOW:** `2`.
108. **Condemned generation HIGH:** `2`.
109. **Whether six-region cohort is directly caused by threshold:** Not established; the final C72 workload observed one region on each side.
110. **Whether an intermediate planner/budget state is required:** Not established. The threshold quantity is upstream, but the C72 planner callback was unobserved and no unique intermediate state was isolated.
111. **Strongest causal chain established:** `15 x 0x10E78` versus `15 x 0x10E80` changes marked-object-size accumulation; the later authentic C70 promotion class is `0` versus `1`; region/list chronology then differs in total churn and generation transitions; both paths still expose one post-Restart basic region, consume no selected candidate, and end in the same reason-5 OOS class.
112. **First unsupported causal link:** Mapping the source-level `survived_per_region`/promotion outcome to a specific region planner classification and then to a uniquely identified basic-list candidate.
113. **Promotion-to-region causal link status:** Partial correlation only; no unique causal region class.
114. **Region-to-candidate causal link status:** Incomplete; candidate head and final unlink are observed, but identity continuity is not proven.
115. **Candidate-to-refill causal link status:** Not established; selection and normal refill are both zero.
116. **Refill-to-OOS causal link status:** Same OOS class is observed on both sides, but C72 does not prove a threshold-specific refill-to-OOS path.
117. **Allocator mutation:** `0`.
118. **Root mutation:** `0`.
119. **Planner mutation:** `0`.
120. **Region mutation:** `0`.
121. **Region-list mutation:** `0`.
122. **Candidate mutation:** `0`.
123. **Policy mutation:** `0`.
124. **Survivor fabrication:** `0`.
125. **Root fabrication:** `0`.
126. **B02 evaluated:** No.
127. **B02 future justification:** Premature. C72 did not establish a specific authentic gen0-eligible region present/selectable at the decisive point that fails solely for the B02 behavior.
128. **C18:** Authentic managed PC path passed; no C18 failure marker.
129. **Code manager:** Valid `CoffNativeCodeManager` path preserved.
130. **`FindMethodInfo`:** Authentic path preserved and passed.
131. **Root scan:** Authentic root scan preserved.
132. **Mark closure:** Preserved; survivor/mark diagnostics completed without failure.
133. **Planner authenticity:** Production planner behavior was not mutated; direct C72 planner observation count was zero on both sides.
134. **Survivor integrity:** `1` on all final promotion markers.
135. **Invariant failures:** C72 aggregate `0`; inherited C65 diagnostic overflow is separately recorded as LOW `0x32`, HIGH `0`.
136. **Sensitive diagnostic allocations:** `0`.
137. **Fail-fast:** `0`.
138. **Page faults:** `0`.
139. **LOW Boot 1:** Discovery fresh boot passed; run `low\run-20260903-173830744\first-run`, authentic promotion `0`, C72 complete `1`.
140. **LOW Boot 2:** Confirmation fresh boot passed; run `low\run-20260903-174319349\repeat-1`, semantic result matched Boot 1.
141. **LOW Boot 3:** Confirmation fresh boot passed; run `low\run-20260903-174319349\repeat-2`, semantic result matched Boot 1.
142. **HIGH Boot 1:** Discovery fresh boot passed; run `high\run-20260903-174105410\first-run`, authentic promotion `1`, C72 complete `1`.
143. **HIGH Boot 2:** Confirmation fresh boot passed; run `high\run-20260903-174627483\repeat-1`, semantic result matched Boot 1.
144. **HIGH Boot 3:** Confirmation fresh boot passed; run `high\run-20260903-174627483\repeat-2`, semantic result matched Boot 1.
145. **Semantic agreement:** True in the C72 matrix; discovery and each 3-boot confirmation set agree on exact boundary values and semantic classes.
146. **Nondeterminism:** No semantic nondeterminism; raw addresses, ordinals, event counts, and serial hashes may differ across fresh boots and are treated as boot-local.
147. **Serial hashes:** LOW discovery `4DA6CA9DF94DE155C63A52FCE2F49CBB1AB5111A145945835E4FA974BF38D717`; LOW confirmation first/repeats `D97F81AE377F6959F927895E4027DE0288302641B1FFB23A0DF560A51955473E`, `EF6E03529A3AD2B1E8AA8DF427E8AA177E2E96FA143219956BEFFC8F152B2F4B`, `79CD374E11B9140D40FC623C4517DEBF13B2985168B579F5DD49524C7372882D`; HIGH discovery `6C2E02AC93EE2A851F216315556A37A261108BF70C6F6B193824ED2ABA7F6EEB`; HIGH confirmation first/repeats `55425137F62AE1D74C41774B87CC1C238E631CCAD84AA871AC375C045D83B7B0`, `5D4D279A86B1DEC9BF12322421BBC4B803819CC1536281E3200FC592CB716828`, `F24E4149860C50B9BE083B4C244C334FDCCD215F554D4D6B73F604278AE0990B`.
148. **Per-case artifact hashes:** Captured for every final run, including serials, ELF/ESP payloads, kernel, managed/native/runtime-pack logs, PE-to-ELF output, symbols, disassembly, commands, and manifests; see each case's `c72-case.json`.
149. **Runtime-pack validation:** Passed on LOW and HIGH; final run logs contain the runtime-pack build output and locked manifest.
150. **Managed build:** Passed on LOW and HIGH; `HostLogProof` published successfully.
151. **Native build:** Passed on LOW and HIGH; instrumented native artifact and kernel ELF built successfully.
152. **PowerShell syntax:** Both the smoke harness and dedicated C72 harness parsed successfully.
153. **JSON/XML parse:** C72 matrix, per-case JSON, genealogy JSON, runtime manifest, and `HostLogProof.csproj` XML parsed successfully.
154. **`git diff --check`:** Passed; only normal line-ending warnings were present during checks.
155. **PE -> ELF conversion:** Passed; `NativeAotGcSingleThreadSuspendEe.elf` was created in both case logs.
156. **Symbol checks:** Passed; symbol, section, unwind, import, and disassembly artifacts were emitted for both cases.
157. **Linker/source/table/archive guards:** Passed through the existing C011EC18 instrumentation and locked queue/source/archive guard artifacts.
158. **C52 Tier-All result or reason omitted:** Omitted because C72 is a focused exact-pair causal experiment; running the broader C52 Tier-All would add unrelated workload and interpretation scope after the required C72 matrix already passed.
159. **Ordinary restoration:** The proof-only kernel was removed from the ordinary path in `finally`; ordinary build and ESP hashes were restored.
160. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
161. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
162. **Proof artifact active:** No; `proofArtifactActive=false` after each run.
163. **C72-owned QEMU cleanup:** Only C70/C72-owned QEMU processes were stopped by the harness; no residual C72 process remained.
164. **Unrelated QEMU preservation:** No unrelated QEMU process was detected or terminated by the final checks.
165. **Files changed:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`; `scripts/dotnet/Invoke-C011EC72PromotionThresholdRegionFormation.ps1`; `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`; this report.
166. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C72_PROMOTION_THRESHOLD_REGION_FORMATION_CAUSALITY.md`.
167. **Evidence root:** `out/dotnet/c011ec72-promotion-threshold-region-formation/`.
168. **Final commit:** Local commit with subject `Trace NativeAOT promotion region formation`; exact SHA is the final `HEAD` reported at delivery.
169. **Push status:** Not pushed.
170. **Remaining limitation:** The direct source-to-planner-to-region-to-candidate identity chain is incomplete; the observed workload did not reproduce the six-region historical cohort.
171. **Exact next-smallest milestone:** Trace the first unsupported link by adding a bounded, source-authentic observation at the planner/generation decision that maps the `survived_per_region` outcome to the exact region identity later entering `basic_free_region`, then repeat only the same LOW/HIGH 1+3 boot matrix.

## C72 decision

The newly established genealogy does **not** justify a direct B02 experiment.
The next experiment should target the first unsupported planner-to-region
identity link, not alter candidate eligibility or region behavior.

# C84 — Exact canonical region materialization provenance

Date: 2026-09-06  
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`  
Branch: `v1.1_DOTNET_SUPPORT`

C82 restored the trustworthy layout-sensitive ONE control. C83 then proved that all five SIX extras are genuine canonical `0x100000` regions, not subranges of a large parent. C84 follows those exact descriptor/range objects into the causal chain.

The result is Outcome G / Success Level 3. The five target ranges are created and committed on both sides before target GC. The earliest normalized checkpoint difference in the direct canonical mapping authority is `PRE_WORKLOAD` for EXTRA3; the common mechanism that explains the final `1` versus `6` is later free-list chronology: SIX's five target entries receive final basic-free insertion and remain there, while ONE later removes/recycles those target entries. The C77 bounded lifecycle observer supplies the source-backed birth, commit, insertion, and removal ordinals; C84 adds only five sequential checkpoint slots by reusing C67 storage.

## Numbered closeout report

1. **Outcome:** Outcome G — mixed mechanism: shared exact descriptor/range creation, a mapping-authority extent boundary for ONE EXTRA3/4, and decisive final basic-free retention/reclaim chronology.
2. **Success Level:** Level 3: all five targets accounted, ONE-side location classified, `0x500000` reconciled, and 3/3 final confirmations per side passed with zero diagnostic failures.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `a8598bdd543bd412845e613f1f64d24540c1e3f7`.
6. **Starting subject:** `Map NativeAOT basic regions without layout drift`.
7. **Final HEAD:** The local C84 commit containing this report; recorded in the final handoff after commit.
8. **Final subject:** `Trace NativeAOT canonical region materialization`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0 ahead / 0 behind`.
11. **Final ahead/behind:** `1 ahead / 0 behind`; no push performed.
12. **Starting worktree:** Clean at the expected committed HEAD before C84 edits.
13. **Final worktree:** Clean after commit and ordinary-artifact restoration.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C82 SHA:** `c187583ccc31b7ba754806263781ccf1e0b30c2f`.
18. **C83 SHA:** `a8598bdd543bd412845e613f1f64d24540c1e3f7`.
19. **C84 SHA:** The final local commit recorded after this file and the two C84 source changes are committed.
20. **Exact C84 question:** When and how do the five additional exact canonical 1 MiB regions first materialize or become part of the relevant canonical supply on SIX compared with ONE?
21. **ONE baseline:** Authentic promotion; pre-Restart basic `1`; post-Restart basic `1`; post-resume basic `1`.
22. **SIX baseline:** Authentic promotion; pre-Restart basic `1`; post-Restart basic `6`; post-resume basic `6`.
23. **ONE post-Restart:** `1`, stable across the accepted controls and final confirmation boots.
24. **SIX post-Restart:** `6`, stable across the accepted controls and final confirmation boots.
25. **Diagnostic BSS baseline:** ONE `0x4F0840`; SIX `0x3947E0`.
26. **C84 diagnostic BSS:** ONE `0x4F0840`; SIX `0x3947E0`.
27. **BSS delta:** `0`; C84 reuses the C83-safe C67 event tail and adds no unconditional static storage.
28. **Kernel size delta:** ONE `+8192`; SIX `+4096`, the native diagnostic image deltas carried by the C83-safe observer composition.
29. **Managed image size delta:** ONE `+4096`; SIX `+4608`; C84 adds no managed workload or crossover.
30. **Canonical materialization source type:** Existing production Workstation region lifecycle, observed by bounded C77 birth/commit events plus the C84 normalized checkpoint probe.
31. **Descriptor initialization source:** `gc_heap::make_heap_segment` followed by `gc_heap::init_heap_segment` (`gc.cpp:12313`, `gc.cpp:12353`).
32. **Range assignment source:** `gc_heap::allocate_new_region` calls `region_allocator::allocate_basic_region`; the latter obtains the exact start/end range (`gc.cpp:35019`, `gc.cpp:4166`).
33. **Region activation source:** `gc_heap::get_new_region` and `gc_heap::init_heap_segment`; free-region reuse also reinitializes through `init_heap_segment` (`gc.cpp:34988`, `gc.cpp:11952`).
34. **Expansion source:** `gc_heap::a_fit_segment_end_p` and its `get_new_region` path (`gc.cpp:17621`, `gc.cpp:17946`).
35. **Commit source:** `gc_heap::make_heap_segment` invokes `virtual_commit` for the initial segment commit (`gc.cpp:12313`, `gc.cpp:12324`); allocator comments state reservation and commit are separate.
36. **Allocation acquisition source:** `gc_heap::get_free_region`, `gc_heap::get_new_region`, and `gc_heap::allocate_new_region` (`gc.cpp:11906`, `gc.cpp:34988`, `gc.cpp:35019`).
37. **Context release source:** `gc_heap::fix_allocation_contexts(TRUE)` (`gc.cpp:24084`), observed as the C84 PRE_GC boundary; no target-specific release predicate was added.
38. **Reclamation source:** `gc_heap::return_free_region`, `region_free_list::unlink_region`, and `gc_heap::thread_final_regions` (`gc.cpp:11860`, `gc.cpp:12931`, `gc.cpp:34809`).
39. **Basic-list insertion source:** `region_free_list::add_region_front` and `add_region_in_descending_order`, reached by the production return/free paths (`gc.cpp:12840`, `gc.cpp:12862`, `gc.cpp:13094`).
40. **EXTRA0 normalized offset:** `0x1400000`.
41. **EXTRA1 normalized offset:** `0x1500000`.
42. **EXTRA2 normalized offset:** `0x1600000`.
43. **EXTRA3 normalized offset:** `0x1700000`.
44. **EXTRA4 normalized offset:** `0x1A00000`.
45. **EXTRA0 exists ONE pre-workload:** Yes; direct C84 exact `0x100000` mapping in the discovery boot; C77 birth/commit also authenticated.
46. **EXTRA0 exists SIX pre-workload:** Yes; direct C84 exact mapping in the discovery boot; C77 birth/commit authenticated.
47. **EXTRA1 exists ONE pre-workload:** Yes; direct C84 exact `0x100000` mapping in the discovery boot; C77 birth/commit also authenticated.
48. **EXTRA1 exists SIX pre-workload:** Yes; direct C84 exact mapping in the discovery boot; C77 birth/commit authenticated.
49. **EXTRA2 exists ONE pre-workload:** Yes; direct C84 exact `0x100000` mapping in the discovery boot; C77 birth/commit also authenticated.
50. **EXTRA2 exists SIX pre-workload:** Yes; direct C84 exact mapping in the discovery boot; C77 birth/commit authenticated.
51. **EXTRA3 exists ONE pre-workload:** The exact descriptor/range exists by C77 commit/create events, but it is outside the C84 bounds-safe mapping authority at this checkpoint: `COMMITTED_NONREGION` in the probe view.
52. **EXTRA3 exists SIX pre-workload:** Yes; exact descriptor/range and exact canonical mapping are authenticated.
53. **EXTRA4 exists ONE pre-workload:** The exact descriptor/range exists by C77 commit/create events, but it is outside the C84 bounds-safe mapping authority at this checkpoint: `COMMITTED_NONREGION` in the probe view.
54. **EXTRA4 exists SIX pre-workload:** Yes; exact descriptor/range and exact canonical mapping are authenticated.
55. **Five extras exist SIX pre-GC:** Yes. C77 creates/commits all five before C84 PRE_WORKLOAD; the C84 checkpoint runs then remain observationally clean.
56. **Five equivalents exist ONE pre-GC:** Yes at descriptor/range level. C84 direct mapping authority sees EXTRA0–2; C77 source-backed birth/commit events account for EXTRA3–4 outside that authority extent.
57. **Earliest differing checkpoint:** `PRE_WORKLOAD` in the direct canonical mapping view.
58. **Earliest differing target region:** EXTRA3 (`0x1700000`); EXTRA4 has the same ONE-side mapping-authority condition.
59. **Earliest differing operation:** Mapping/bookkeeping coverage exposure after the shared commit/create sequence; the final count divergence is later list retention/removal.
60. **Source file:** `src/coreclr/gc/gc.cpp` in the locked runtime source; lifecycle hooks are injected only at the actual named production functions.
61. **Source function:** Earliest boundary: `gc_heap::seg_mapping_table_add_segment` / bookkeeping coverage; decisive common tail: `region_free_list::add_region_*` and `unlink_region`.
62. **Event ordinal:** ONE EXTRA3 birth create `0x64`, commit immediately before it; SIX EXTRA3 create `0x63`. ONE EXTRA4 create `0x91`; SIX EXTRA4 create `0xA2`. Final SIX target adds are `0xB9`, `0xBA`, `0xBB`, `0xBC`, `0xBF` for EXTRA0–4.
63. **ONE state at divergence:** C84 mapping probe has no authenticated canonical entry for EXTRA3/4; C77 has authenticated descriptor/range birth and commit.
64. **SIX state at divergence:** Exact canonical `0x100000` target range is part of the authenticated canonical supply; final target entries remain basic-free.
65. **EXTRA0 ONE-side classification:** `EXACT_REGION` at discovery PRE_WORKLOAD; later target free-list entry is removed/recycled.
66. **EXTRA1 ONE-side classification:** `EXACT_REGION` at discovery PRE_WORKLOAD; later target free-list entry is removed/recycled.
67. **EXTRA2 ONE-side classification:** `EXACT_REGION` at discovery PRE_WORKLOAD; later target free-list entry is removed/recycled.
68. **EXTRA3 ONE-side classification:** `COMMITTED_NONREGION` in the bounds-safe C84 authority view, with exact C77 descriptor/range birth proven.
69. **EXTRA4 ONE-side classification:** `COMMITTED_NONREGION` in the bounds-safe C84 authority view, with exact C77 descriptor/range birth proven.
70. **All five accounted:** Yes: each has a normalized offset, exact-size range evidence, source-backed birth/commit event, and final list chronology.
71. **Expansion count ONE:** `1` in C77 summary.
72. **Expansion count SIX:** `1` in C77 summary.
73. **Expansion requested bytes ONE:** No scalar request-size field is emitted by the bounded observer; not inferred.
74. **Expansion requested bytes SIX:** No scalar request-size field is emitted by the bounded observer; not inferred.
75. **Expansion committed bytes ONE:** Per-target commit events cover the target ranges; no single aggregate scalar is emitted.
76. **Expansion committed bytes SIX:** Per-target commit events cover the target ranges; no single aggregate scalar is emitted.
77. **Extra materialized bytes:** Five exact target regions × `0x100000` = `0x500000`.
78. **`0x500000` arithmetic relevance:** It is the logical size of the five target canonical regions, not evidence of one SIX-only expansion request; both sides report one expansion.
79. **Commit extent difference:** No authenticated SIX-only `+0x500000` commit extent; the difference is representation/coverage and subsequent free-list fate.
80. **Acquisition count ONE:** Not separately scalar-counted per target by C84; C77 records production `get_free_region`/`get_new_region` activity.
81. **Acquisition count SIX:** Not separately scalar-counted per target by C84; C77 records production `get_free_region`/`get_new_region` activity.
82. **Context release count ONE:** No target-specific release count observed in the bounded C84 storage.
83. **Context release count SIX:** No target-specific release count observed in the bounded C84 storage.
84. **Reclaimed target count ONE:** Five target identities are removed after their later target adds in the decisive tail.
85. **Reclaimed target count SIX:** Zero after the final target adds; earlier transient removals occur before the retained final adds.
86. **Target state PRE_GC ONE:** C84 direct state `0` where mapping is visible; `listKind=FFFFFFFF`; C77 is the authoritative lifecycle source for births/list events.
87. **Target state PRE_GC SIX:** C84 direct state `0` where mapping is visible; `listKind=FFFFFFFF`; final list state is supplied by C77 list events.
88. **Target state POST_PLAN ONE:** No target-specific C84 state transition; later C77 remove/recycle chronology determines final supply.
89. **Target state POST_PLAN SIX:** No target-specific C84 state transition; later C77 final adds remain.
90. **Target state PRE_RESTART ONE:** No target-specific C84 state transition; ONE target entries are subsequently removed/recycled.
91. **Target state PRE_RESTART SIX:** No target-specific C84 state transition; SIX target entries are retained in basic free.
92. **Target basic insertion ONE:** Five target basic-free additions occur in the later chronology, followed by target removals/recycling; final basic count is `1`.
93. **Target basic insertion SIX:** Five final target additions occur at ordinals `0xB9`, `0xBA`, `0xBB`, `0xBC`, `0xBF` and remain; final basic count is `6`.
94. **Earliest common mechanism across five:** Production free-list lifecycle after shared region birth: insertion followed by `unlink_region`/recycle on ONE, retained insertion on SIX.
95. **Mechanism classification:** Mixed mechanism, with list chronology/reclamation as the common decisive stage.
96. **First supported causal link:** `allocate_basic_region` → `make_heap_segment`/`virtual_commit` → exact C77 birth events on both sides before GC.
97. **Strongest causal chain:** Shared exact region birth/commit → ONE EXTRA3/4 mapping-authority coverage gap → later ONE target free-list removals versus SIX retained final adds → final basic `1` versus `6`.
98. **First unsupported causal link:** A precise managed-allocation predicate causing the list chronology is not proven by C84 and is deferred.
99. **Five-region conservation valid:** Yes.
100. **ONE-side location of `0x500000`:** Three exact mapping-visible regions plus two exact committed descriptor/ranges outside the C84 mapping-authority extent at the earliest differing checkpoint; later all five are accounted by C77 list/recycle events.
101. **SIX-side location of `0x500000`:** Five exact canonical `0x100000` regions, with five final target entries retained in basic free.
102. **Managed allocation causal relevance:** Upstream workload geometry remains relevant, but C84 did not change the workload and did not isolate a new threshold.
103. **Expansion causal relevance:** Necessary for the shared supply, not SIX-only; expansion count is `1` on both sides.
104. **Context ownership causal relevance:** Possible intermediate influence, not independently isolated by this bounded observer.
105. **Planner causal relevance:** No source-backed planner difference is required before the observed list chronology; no planner mutation was made.
106. **Reclamation causal relevance:** Decisive for ONE's later target removals and SIX's retained final target entries.
107. **List insertion causal relevance:** Decisive common final mechanism; exact target add/remove ordinals are authenticated.
108. **Candidate downstream relevance:** Downstream only; C84 does not require candidate mutation or candidate selection to explain the supply difference.
109. **B02 evaluated:** No.
110. **B02 future justification:** Reconsider only after C85 reaches a later candidate-path predicate; C84 remains upstream and B02 is still premature.
111. **Production mutation:** None.
112. **Allocator mutation:** None.
113. **Expansion forcing:** None.
114. **Commit forcing:** None.
115. **Descriptor mutation:** None.
116. **Region mutation:** None.
117. **Context forcing:** None.
118. **Planner mutation:** None.
119. **Region-list mutation:** None.
120. **Candidate mutation:** None.
121. **Survivor fabrication:** None.
122. **Root fabrication:** None.
123. **C18:** PASS; retained.
124. **Code manager:** Authentic; retained.
125. **`FindMethodInfo`:** Authentic; retained.
126. **Root scan:** Authentic; retained.
127. **Mark closure:** Authentic; retained.
128. **Planner authenticity:** Authentic; no planner replacement.
129. **Survivor integrity:** PASS; no survivor fabrication.
130. **C84 invariant failures:** `0` in every accepted discovery and confirmation boot.
131. **Sensitive diagnostic allocations:** `0`.
132. **C84 event capacity:** `5` sequential checkpoint slots.
133. **C84 event count:** `5` per accepted boot.
134. **C84 overflow:** `0`.
135. **Fail-fast:** `0`.
136. **Page faults:** `0` in accepted runs; the initial EXTRA4 probe fault was rejected and rerun with the safe extent guard.
137. **ONE Boot 1:** Final 3-boot confirmation PASS, target `0x1400000`, C84 Level 1, control `1/1/1`.
138. **ONE Boot 2:** Final 3-boot confirmation PASS, target `0x1400000`, normalized C84 semantics agree.
139. **ONE Boot 3:** Final 3-boot confirmation PASS, target `0x1400000`, normalized C84 semantics agree.
140. **SIX Boot 1:** Final 3-boot confirmation PASS, target `0x1400000`, control `1/6/6`.
141. **SIX Boot 2:** Final 3-boot confirmation PASS, target `0x1400000`, normalized C84 semantics agree.
142. **SIX Boot 3:** Final 3-boot confirmation PASS, target `0x1400000`, normalized C84 semantics agree.
143. **Semantic agreement:** PASS for the required 3/3 sets; the additional EXTRA4 (`0x1A00000`) 3/3 boundary confirmations also passed.
144. **Nondeterminism:** Raw heap bases, descriptors, serial hashes, and proof-kernel hashes vary; normalized offsets, counts, source operations, and clean diagnostics agree.
145. **Serial hashes:** Stored in each confirmation manifest under `qemu.serialSha256`; raw values intentionally differ across fresh boots.
146. **Artifact hashes:** Stored in each confirmation `manifest.json` and artifact manifest; ordinary kernel/ESP hashes restore to the expected value.
147. **Runtime-pack validation:** PASS through the harness runtime-pack manifest and C84 regression gate.
148. **Managed build:** PASS in accepted C84 runs.
149. **Native build:** PASS in accepted C84 runs.
150. **PowerShell syntax:** PASS with `[System.Management.Automation.Language.Parser]` and the harness invocation.
151. **JSON/XML parse:** PASS for generated manifests and repository validation inputs.
152. **`git diff --check`:** PASS; only expected LF/CRLF normalization warnings were emitted by Git.
153. **PE → ELF conversion:** PASS through the harness converter gate.
154. **Symbol checks:** PASS through the harness symbol gate.
155. **Linker/source/table/archive guards:** PASS through the harness `linkerSourceTables` regression gate.
156. **C52 Tier-All:** Omitted because C84 is a bounded provenance observer and the existing C52 Tier-All workload is not semantically appropriate for this five-slot sequential probe.
157. **Ordinary restoration:** PASS after `finally` restoration.
158. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
159. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
160. **Proof artifact active:** No; ordinary artifacts are restored.
161. **C84-owned QEMU cleanup:** `0` remaining after each accepted harness run.
162. **Unrelated QEMU preservation:** Unrelated QEMU processes were not targeted or altered.
163. **Files changed:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this documentation file.
164. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C84_EXACT_CANONICAL_REGION_MATERIALIZATION.md`.
165. **Evidence root:** `out/dotnet/c011ec84-exact-canonical-region-materialization/` with per-case/per-target discovery and confirmation manifests.
166. **Final commit:** Local commit with subject `Trace NativeAOT canonical region materialization`; SHA recorded in the final handoff.
167. **Push status:** Not pushed.
168. **Remaining limitation:** C84's bounds-safe direct mapping probe deliberately refuses `get_region_info_for_address` outside current bookkeeping coverage. For ONE EXTRA3/4, exact descriptor/range existence therefore comes from source-backed C77 birth/commit events rather than an unsafe address lookup; C85 should add no broad snapshot and should isolate the mapping/list predicate if needed.
169. **Exact next-smallest milestone:** C85 should trace the first target-specific `region_free_list::add_region_*` versus `unlink_region` predicate/chronology that makes ONE recycle all five target entries while SIX retains the five final additions; only then consider any later candidate-path question.

## Evidence interpretation

The C84 checkpoint probe is intentionally sequential: one target normalized offset per build/run, five fixed slots, five checkpoints, and no full canonical snapshot. The inherited C77 records are the provenance ledger. In representative discovery manifests, C77 target births show a commit event immediately followed by a create event for every target on both sides. Example for SIX EXTRA4:

```text
commit eventOrdinal=0xA1 region=0x104011190 committed=0x101A01000 reserved=0x101B00000 operation=commit
create eventOrdinal=0xA2 region=0x104011190 mem=0x101A00000 reserved=0x101B00000 operation=create
```

The final target-specific SIX basic-free additions are `0xB9`, `0xBA`, `0xBB`, `0xBC`, and `0xBF`; ONE has later target removals at `0xB4`, `0xB9`, `0xC1`, `0xC3`, `0xC8`, and `0xCD` across the target identities. These are list/recycle events, not a new large-parent decomposition.

Evidence is kept under the C84 root by case and normalized target. The rejected first EXTRA4 page-fault run is retained as a layout-gate failure; accepted single-target probes are the discovery evidence; the final 3/3 target-`0x1400000` and target-`0x1A00000` runs are confirmation evidence. No production workload crossover, forcing, B02, or static diagnostic expansion was used.

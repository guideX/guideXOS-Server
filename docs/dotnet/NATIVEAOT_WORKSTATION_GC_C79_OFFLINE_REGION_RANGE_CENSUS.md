# NativeAOT Workstation GC C79 Offline Region/Range Census

## Executive result

C79 is **Outcome H / Success Level 1 (bounded path validated; full provenance
still unresolved)**. The accepted controls were preserved in the final 3/3
confirmation runs: ONE remained promotion-positive at `1/1`, and SIX remained
promotion-positive at `6/6`. C79 emits only a bounded stop-time projection of
the already accepted C67 fixed event array. It does not walk the live region
lists, allocate census storage, sort, build maps, or mutate GC state.

The offline analyzer makes heap address ranges primary and descriptor pointers
secondary. It normalizes sampled ranges by offset from the observed low range,
sorts them by address, calculates extents, matches semantic roles, and emits
descriptor-reuse, transition, conservation, divergence, and extra-five tables.
The accepted C76 range-bearing records provide 17 ONE and 15 SIX sampled
records, with 11 distinct sampled normalized ranges in each run. The C79
checkpoint projection itself wrote zero records at its four attempted
checkpoints and reported `snapshotCompleteness=0`; therefore it is not a full
checkpoint universe. The five final SIX ranges cannot yet be traced through
the complete ONE range universe without risking the accepted image behavior.

C77 could not identify supply origin. C78 demonstrated that descriptor birth
is not range birth because descriptor identity is reused/recycled. C79 changes
the primary identity to the heap address interval `[start,end)` and moves
reconstruction offline. The result is deliberately conservative: no causal
claim is made beyond the accepted C76 event evidence.

## Evidence and implementation

The offline analyzer is
`scripts/dotnet/Invoke-C011EC79RegionRangeCensus.ps1`. Its output is under
`out/dotnet/c011ec79-offline-region-range-census/offline-analysis/` and
includes raw records, normalized CSV, checkpoint comparison, transition
classification, range matching, descriptor reuse, conservation, divergence,
and extra-five tables. Accepted 3/3 manifests are copied under
`offline-analysis/accepted-manifests/`.

The runtime C79 checkpoints are the reduced safe set
`C79_POST_RETAINED_ALLOC`, `C79_PRE_TARGET_GC`, `C79_POST_PLAN`, and
`C79_POST_RESTART`. The source-backed C79 projection uses the accepted C67
fixed event record (`224` bytes, capacity `2048`) and emits a summary after
the sensitive phase. The analyzer falls back to the accepted C76 range-bearing
records when the projection has no records. Earlier development attempts that
performed a full live range walk changed ONE/SIX image behavior; those attempts
were abandoned and are not treated as causal evidence.

## Numbered closeout report

1. **Outcome.** Outcome H: valid bounded event-backed range evidence, but the full five-range reconstruction remains unresolved.
2. **Success Level.** Level 1 operationally: the safe bounded path and range-primary analyzer are validated; the stricter all-five historical trace criterion for Level 2/3 is not met.
3. **Repository.** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch.** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD.** `aaa6450c3cee64554f7af1663b17a29e5bb844f3`.
6. **Starting subject.** `Extend NativeAOT region supply provenance`.
7. **Final HEAD.** Local closeout commit; exact hash is reported by the final `git rev-parse HEAD` check.
8. **Final subject.** `Reconstruct NativeAOT region ranges offline`.
9. **Upstream.** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind.** `0/0` against upstream.
11. **Final ahead/behind.** `1/0` after the local C79 commit.
12. **Starting worktree.** Clean.
13. **Final worktree.** Clean after commit; ordinary artifacts restored.
14. **Runtime identity.** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interface `5.3`, EE `2`, `net9.0`, `win-x64`.
15. **Runtime source SHA.** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA.** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31` for `nativeaot-amd64-fp-handoff.patch`.
17. **C77 SHA.** `67ae4890`.
18. **C78 SHA.** `aaa6450c3cee64554f7af1663b17a29e5bb844f3`.
19. **C79 SHA.** Local closeout commit containing this C79 report; exact hash is reported by the final `git rev-parse HEAD` check.
20. **Exact C79 question.** Where are the actual heap bytes behind SIX's five extra final basic ranges on ONE, and when do the range histories first diverge despite descriptor reuse?
21. **ONE reproduction.** `15mid8`, tail `320`, promotion-positive, final basic `1/1`.
22. **SIX reproduction.** `baseline16`, tail `216`, promotion-positive, final basic `6/6`.
23. **ONE final basic count.** `1` after RestartEE and `1` after managed resume.
24. **SIX final basic count.** `6` after RestartEE and `6` after managed resume.
25. **Runtime census checkpoints.** Four safe projection checkpoints: POST_RETAINED_ALLOC, PRE_TARGET_GC, POST_PLAN, POST_RESTART; the preferred pre-workload/pre-restart full snapshots were not added because the full live walk perturbed controls.
26. **Census record size.** `0xE0` / `224` bytes, the accepted C67 fixed event record used by the projection.
27. **Census capacity.** `0x800` / `2048` records.
28. **Peak records.** Accepted C76 fallback peak: ONE `17`, SIX `15`; direct C79 checkpoint projection: `0` at each attempted checkpoint.
29. **Census overflow.** `0`; the projection and accepted fallback records are bounded and clean.
30. **Descriptor identity semantics.** Descriptor pointer is secondary metadata only; pointer appearance is not descriptor birth or range origin.
31. **Address-range identity semantics.** `[rangeStart,rangeEnd)` and extent are the primary identity; normalized identity is heap-relative offset plus extent.
32. **Descriptor reuse observed.** C78's reuse correction remains supported; C79 cannot quantify transitions from the sampled event stream because it is not a full checkpoint census.
33. **Same descriptor/different range count ONE.** `0` observed; not determinable from the sampled event-backed set.
34. **Same descriptor/different range count SIX.** `0` observed; not determinable from the sampled event-backed set.
35. **Same range/different descriptor count ONE.** `0` observed; not determinable from the sampled event-backed set.
36. **Same range/different descriptor count SIX.** `0` observed; not determinable from the sampled event-backed set.
37. **ONE region/range count PRE_WORKLOAD.** Unavailable: no full PRE_WORKLOAD snapshot was taken.
38. **SIX region/range count PRE_WORKLOAD.** Unavailable: no full PRE_WORKLOAD snapshot was taken.
39. **ONE committed bytes PRE_WORKLOAD.** Unavailable.
40. **SIX committed bytes PRE_WORKLOAD.** Unavailable.
41. **ONE region/range count POST_RETAINED.** `0` in the C79 projection; authoritative full count unavailable.
42. **SIX region/range count POST_RETAINED.** `0` in the C79 projection; authoritative full count unavailable.
43. **ONE region/range count PRE_GC.** `0` in the C79 projection; authoritative full count unavailable.
44. **SIX region/range count PRE_GC.** `0` in the C79 projection; authoritative full count unavailable.
45. **ONE region/range count POST_PLAN.** `0` in the C79 projection; authoritative full count unavailable.
46. **SIX region/range count POST_PLAN.** `0` in the C79 projection; authoritative full count unavailable.
47. **ONE region/range count PRE_RESTART.** Unavailable: no full PRE_RESTART snapshot was taken.
48. **SIX region/range count PRE_RESTART.** Unavailable: no full PRE_RESTART snapshot was taken.
49. **ONE region/range count POST_RESTART.** `0` in the C79 projection; accepted C76 final basic count is `1`.
50. **SIX region/range count POST_RESTART.** `0` in the C79 projection; accepted C76 final basic count is `6`.
51. **Earliest count divergence checkpoint.** Unresolved; no complete paired checkpoint snapshots.
52. **Earliest range-union divergence checkpoint.** Unresolved; no complete paired range unions.
53. **Earliest ownership/list divergence checkpoint.** Unresolved; sampled owner/list fields are not checkpoint-complete.
54. **Difference predates managed workload.** Not established.
55. **ONE heap base.** Exact committed heap base unavailable; observed low event-range bound `0x100A00028`.
56. **SIX heap base.** Exact committed heap base unavailable; observed low event-range bound `0x100A00028`.
57. **ONE reserved heap bytes.** Observed event envelope `0x12FFFD8` (`19922904`), not a reserved-heap proof.
58. **SIX reserved heap bytes.** Observed event envelope `0x12FFFD8` (`19922904`), not a reserved-heap proof.
59. **ONE committed heap bytes.** Unavailable from a complete range set; C79 projection summary is zero-record.
60. **SIX committed heap bytes.** Unavailable from a complete range set; C79 projection summary is zero-record.
61. **ONE region-covered bytes.** Event-sample sum `0x10FFD58`; not conservation-valid because repeated observations are included.
62. **SIX region-covered bytes.** Event-sample sum `0xEFFDA8`; not conservation-valid because repeated observations are included.
63. **ONE uncovered/tail bytes.** Unresolved; observed envelope cannot be subtracted from a disjoint region union.
64. **SIX uncovered/tail bytes.** Unresolved; observed envelope cannot be subtracted from a disjoint region union.
65. **Range overlaps detected.** Full overlaps not classified; repeated event observations make raw sample arithmetic non-disjoint.
66. **Range gaps detected.** Unresolved; no complete range set.
67. **Conservation valid ONE.** No; deferred because the input is a repeated event sample.
68. **Conservation valid SIX.** No; deferred because the input is a repeated event sample.
69. **ONE common basic range provenance.** Accepted C76 `region_free_list::get_region_kind`, predicate `region_size==BASIC_REGION_SIZE`; 17 basic-eligible sampled events support final `1/1`.
70. **SIX common basic range provenance.** The same accepted C76 source/predicate; 15 basic-eligible sampled events support final `6/6`.
71. **Extra SIX range 1 normalized identity.** Unresolved; no complete SIX POST_RESTART range set.
72. **Extra SIX range 1 earliest checkpoint.** Unresolved.
73. **Extra SIX range 1 ONE-side state.** Unresolved; no corresponding complete ONE range set.
74. **Extra SIX range 2 normalized identity.** Unresolved.
75. **Extra SIX range 2 earliest checkpoint.** Unresolved.
76. **Extra SIX range 2 ONE-side state.** Unresolved.
77. **Extra SIX range 3 normalized identity.** Unresolved.
78. **Extra SIX range 3 earliest checkpoint.** Unresolved.
79. **Extra SIX range 3 ONE-side state.** Unresolved.
80. **Extra SIX range 4 normalized identity.** Unresolved.
81. **Extra SIX range 4 earliest checkpoint.** Unresolved.
82. **Extra SIX range 4 ONE-side state.** Unresolved.
83. **Extra SIX range 5 normalized identity.** Unresolved.
84. **Extra SIX range 5 earliest checkpoint.** Unresolved.
85. **Extra SIX range 5 ONE-side state.** Unresolved.
86. **All five ranges traced backward.** No; C79 stops at a valid bounded event-backed sample.
87. **Exact ONE-side location of the five-region byte delta.** Unresolved.
88. **Exact SIX-side location of the five-region byte delta.** Unresolved beyond the accepted final basic count.
89. **Expansion/commit causal status.** Not established. C76 reports expansion attempted `1`, succeeded `0` in both controls; no range-union delta is available.
90. **Managed-allocation causal status.** Not established.
91. **Split/tail causal status.** Not established.
92. **Planner causal status.** Not established.
93. **Reclamation causal status.** Not established.
94. **Owner/list causal status.** Sampled C76 list/owner fields are valid evidence, but the first ONE/SIX divergence is unresolved.
95. **Descriptor reuse causal relevance.** Conceptually decisive: it invalidates descriptor-birth genealogy; it does not itself explain the five ranges.
96. **Earliest source-backed mechanism.** C76 basic-size eligibility classification, not the supply divergence.
97. **Source file.** `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, with locked GC source used by the inherited C67/C76 harness.
98. **Source function.** `guideXosNativeAotC011EC79Emit` for the stop-time projection and `guideXosNativeAotC011EC76Emit` for accepted range-bearing records.
99. **Source operation.** Project accepted C67 `[mem,reserved)` event fields into bounded C79 records/summaries; reconstruct and compare offline.
100. **ONE operands/state.** C76 promotion `1`, post-Restart basic `1`, post-resume basic `1`, eligibility `17`, expansion attempted `1`/succeeded `0`.
101. **SIX operands/state.** C76 promotion `1`, post-Restart basic `6`, post-resume basic `6`, eligibility `15`, expansion attempted `1`/succeeded `0`.
102. **Mechanism classification.** Outcome H: bounded evidence is clean, causal range supply remains unresolved.
103. **First supported causal link.** `region_free_list::get_region_kind` plus the basic-size predicate identifies accepted basic range candidates.
104. **Strongest causal chain.** C77 aggregate lifecycle evidence -> accepted C76 range-bearing eligibility events -> offline range normalization/matching; it stops before full range supply.
105. **First unsupported causal link.** A sampled eligibility event cannot prove the complete checkpoint range universe or the source of the five final SIX ranges.
106. **Candidate downstream relevance.** None; C79 remains upstream of candidate selection.
107. **B02 evaluated.** No.
108. **B02 future justification.** Still premature; the upstream physical range supply mechanism is not established.
109. **Production runtime mutation.** None.
110. **Allocator mutation.** None.
111. **Expansion forcing.** None.
112. **Region mutation.** None.
113. **Descriptor mutation.** None.
114. **Split/coalesce forcing.** None.
115. **Region-list mutation.** None.
116. **Candidate mutation.** None.
117. **Policy mutation.** None.
118. **Survivor fabrication.** None.
119. **Root fabrication.** None.
120. **C18.** Maintained.
121. **Code manager.** Maintained.
122. **`FindMethodInfo`.** Maintained.
123. **Root scan.** Authentic.
124. **Mark closure.** Maintained.
125. **Planner authenticity.** Maintained.
126. **Survivor integrity.** Maintained.
127. **C79 invariant failures.** `0`.
128. **Sensitive diagnostic allocations.** `0`.
129. **Fail-fast.** `0`.
130. **Page faults.** `0`.
131. **ONE Boot 1.** Pass: promotion-positive, `1/1`.
132. **ONE Boot 2.** Pass: promotion-positive, `1/1`.
133. **ONE Boot 3.** Pass: promotion-positive, `1/1`.
134. **SIX Boot 1.** Pass: promotion-positive, `6/6`.
135. **SIX Boot 2.** Pass: promotion-positive, `6/6`.
136. **SIX Boot 3.** Pass: promotion-positive, `6/6`.
137. **Semantic agreement.** `true` for all accepted controls, diagnostic fields, record counts, and record size across each 3/3 confirmation set; full range-union agreement is not claimed.
138. **Nondeterminism.** None observed in accepted control counts or diagnostic outcomes; full range genealogy nondeterminism is not measurable from the sampled set.
139. **Serial hashes.** ONE: `923C859A881E2206C25E31D578D5299206911C64C73D35245DF1BD183AC40C98`, `C5C5C6A2A8672211A6758DB3B069EE6C2E26EFD74FE3FDB3F3F2E4BDF840AD84`, `741B7C1A34BD35CFB013803FF3A984B4EB2AE4C560F89F853DB25D1A3033CAE1`; SIX: `282E9D6CCABBB9533F6E3E396AD8A159704E3F64C653AFE10DA52A70C2DCBC82`, `AF4BC380282E8FBF06CA8F8897DD23E473D48645E20C2E65721C19F03D304FEF`, `03B8FEDBA011B5E5E361694585C5A34B21F31E987EDB961DA61DB9EFB66E08CF`.
140. **Artifact hashes.** ONE proof kernel `E97AC793B9D70D07468CFF817E2D0554939D6CEC13F714EDD4AFC8EA4FDFE2C6`, PE `066E8A76C040D1C04F99419B0304580358E2CFDE8036B975D59845DCD31B8981`, ELF `57677F0FE08E1361FC04611088F28DF70BF0645FEFBF7D61D2C636D9B900D8CF`, MAP `DBE9F71EAE13EFB96750957E2FAB693E187DF7C9445BFB81B99E1A2B28AC7699`; SIX proof kernel `7D73161D1DD975E9AFC1BE750ADD29991DCF279E37FD6CDF5B8A16D085FAAB7C`, PE `37FD0A13D57E01017E2B52946FCE893204192DADAB079F0D3797AF356898137C`, ELF `27C5D371366EE815FE3184A22BDBC63F23B29B545062090D39A85DB7B671CEAC`, MAP `1993DA97C9436CC7FC6AEA8635D32DBDB602B85F08FBFC047387408A7D782AD8`.
141. **Offline analyzer path.** `scripts/dotnet/Invoke-C011EC79RegionRangeCensus.ps1`.
142. **Offline census output path.** `out/dotnet/c011ec79-offline-region-range-census/offline-analysis/`.
143. **Runtime-pack validation.** Pass: locked runtime-pack manifest/source identity and ordinary restoration validated.
144. **Managed build.** Pass through the smoke harness for ONE and SIX.
145. **Native build.** Pass through the smoke harness for ONE and SIX.
146. **PowerShell syntax.** Pass for the smoke harness and analyzer.
147. **JSON/XML parse.** JSON manifests and analyzer output parse successfully; no XML source was changed, so XML validation is not applicable.
148. **`git diff --check`.** Pass; only expected line-ending normalization warnings were reported by Git.
149. **PE -> ELF conversion.** Pass in the native smoke runs.
150. **Symbol checks.** Pass in the native smoke runs.
151. **Linker/source/table/archive guards.** Pass in the runtime-pack manifest and native smoke guard path.
152. **C52 Tier-All.** Omitted as semantically inappropriate: C79 is upstream provenance and does not evaluate B02/candidate behavior.
153. **Ordinary restoration.** Pass; ordinary kernel and ESP hashes match the pre-run ordinary artifact.
154. **Ordinary kernel SHA.** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
155. **Ordinary ESP SHA.** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
156. **Proof artifact active.** `false` after restoration.
157. **C79-owned QEMU cleanup.** Pass; only C79-owned processes were stopped.
158. **Unrelated QEMU preservation.** Pass; unrelated QEMU processes were preserved.
159. **Files changed.** Smoke harness, NativeAOT diagnostic platform source/header, offline analyzer, C79 documentation; evidence files are preserved under the evidence root.
160. **Documentation path.** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C79_OFFLINE_REGION_RANGE_CENSUS.md`.
161. **Evidence root.** `out/dotnet/c011ec79-offline-region-range-census/`.
162. **Final commit.** `Reconstruct NativeAOT region ranges offline`, local and not pushed; exact hash is reported by the final `git rev-parse HEAD` check.
163. **Push status.** Not pushed.
164. **Remaining limitation.** A complete safe full-region snapshot was not possible: the live walk perturbed accepted ONE/SIX behavior, while the safe inherited event sample lacks complete checkpoint provenance.
165. **Exact next-smallest milestone.** Add one bounded range snapshot after the accepted safe-stop boundary, or add only checkpoint provenance to the accepted C76 records; if a single transition remains, instrument only that transition. Do not return to broad live tracing or workload sweeps.

## C80 decision

C80 should follow the first mechanism only after C79 can supply a complete
range union. If the next safe snapshot shows managed-allocation divergence,
instrument the first acquisition/boundary crossing; if it shows expansion,
isolate the production operand/threshold; if subdivision/tail differs, trace
only split arithmetic; if planner/reclamation differs, trace only that
transformation; and if ownership differs, trace only the transfer predicate.
B02 remains deferred.

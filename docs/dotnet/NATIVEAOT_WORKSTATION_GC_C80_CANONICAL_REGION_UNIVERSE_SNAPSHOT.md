# NativeAOT Workstation GC C80 Canonical Region-Universe Snapshot

Outcome: H / Level 1. The canonical C80 universe is complete; the five-range address localization remains unresolved.

Progression: C77 established bounded region-supply provenance; C78 added supply-origin/ownership observations; C79 normalized offline range census; C80 snapshots the runtime-owned seg_mapping_table at four lifecycle checkpoints and adds conservation, interval, transition, and descriptor-reuse accounting.

Evidence root: out/dotnet/c011ec80-canonical-region-universe/. The C79 accepted artifacts remain controls; C80 adds the canonical snapshot and offline outputs listed below.

Runtime identity: NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64; source commit 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3; FP repair patch SHA 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.

## Exactly 183 numbered findings

1. Outcome H / Level 1: the canonical C80 universe is complete, but the five-range ONE-side localization is unresolved.
2. Level 1 is satisfied because both accepted C80 manifests visit the full canonical mapping envelope with zero overflow.
3. Repository root is D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT.
4. Branch is v1.1_DOTNET_SUPPORT.
5. Analysis started from commit d02f965881d0ff11300fe5eda37e7b366f856a41.
6. Analysis-start commit subject is Snapshot NativeAOT canonical region universe.
7. The final local follow-up commit is the commit containing this report and the analyzer corrections.
8. The follow-up commit subject is Complete C80 offline region-universe accounting.
9. Upstream is origin/v1.1_DOTNET_SUPPORT.
10. At the beginning of this continuation the branch was one local commit ahead and zero behind upstream.
11. After the local follow-up commit the branch is expected to be two commits ahead and zero behind; no push is performed.
12. The continuation began from the clean worktree produced by the prior C80 commit.
13. The intended final worktree state is clean after the follow-up commit.
14. Runtime identity is NativeAOT 9.0.0, AMD64, Workstation GC, GC interface 5.3, EE interface 2, net9.0, win-x64.
15. Locked runtime source commit is 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.
16. NativeAOT FP repair patch SHA is 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.
17. C78 provenance commit is 67ae48907415f3dcbe174467d3a442d5486e2885.
18. C79 analyzer commit is 18f34b346367c727d66b5b4a22d0f5aafa3c50ed2.
19. C80 implementation commit is d02f965881d0ff11300fe5eda37e7b366f856a41; this continuation corrects its accounting/reporting.
20. The exact C80 question is the authoritative canonical region universe at PRE_GC, POST_PLAN, PRE_RESTART, and POST_RESTART, including the final six basic ranges on SIX and five extras.
21. The accepted C76 ONE control reports postRestartBasicCount=0x1 and postResumeBasicCount=0x1.
22. The accepted C76 SIX control reports postRestartBasicCount=0x6 and postResumeBasicCount=0x6.
23. The C76 line emitted during the fresh C80 ONE boot reports postRestartBasicCount=0x0; this is retained as a perturbation/nondeterminism note.
24. The C76 line emitted during the fresh C80 SIX boot reports postRestartBasicCount=0x6.
25. The canonical authority is seg_mapping_table, not the C76 candidate list.
26. Completeness is shown by mapping envelope coverage, visited=represented+explicitly excluded, and overflow=0.
27. The enumeration source is the locked gc.cpp plus the generated copy consumed by the harness.
28. The audited functions are get_region_info_for_address, get_region_at_index, and region_allocator::init.
29. Snapshots are emitted only from GC-owned or EE-stopped execution points; no concurrent reader is introduced.
30. The authentic mapping-entry bound is 0x40, derived from (g_gc_highest_address-g_gc_lowest_address)/regionAlignment.
31. The fixed C80 record capacity is 0x40 per snapshot.
32. The fixed C80 snapshot storage size is 8,840 bytes on this AMD64 layout.
33. ONE peak mapping entries are 0x40.
34. SIX peak mapping entries are 0x40.
35. ONE overflow is zero at all four checkpoints.
36. SIX overflow is zero at all four checkpoints.
37. ONE checkpoint counters are C80_PRE_GC: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_POST_PLAN: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_PRE_RESTART: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_POST_RESTART: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0.
38. ONE C80_PRE_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.
39. ONE C80_POST_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.
40. SIX checkpoint counters are C80_PRE_GC: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_POST_PLAN: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_PRE_RESTART: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0; C80_POST_RESTART: visited=0x40, represented=0x14, excluded=0x2C, records=0x6, capacity=0x40, overflow=0x0, dupDesc=0xE, dupRange=0x0, invalid=0x0.
41. SIX C80_POST_PLAN counters are represented explicitly in the previous field and equal the other complete snapshots.
42. SIX C80_PRE_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.
43. SIX C80_POST_RESTART counters are represented explicitly in the previous field and equal the other complete snapshots.
44. ONE has 6 distinct canonical ranges at every checkpoint.
45. ONE C80_PRE_GC has 6 distinct ranges.
46. ONE C80_POST_PLAN has 6 distinct ranges.
47. ONE C80_PRE_RESTART has 6 distinct ranges.
48. ONE C80_POST_RESTART has 6 distinct ranges.
49. SIX has 6 distinct canonical ranges at every checkpoint.
50. SIX C80_PRE_GC has 6 distinct ranges.
51. SIX C80_POST_PLAN has 6 distinct ranges.
52. SIX C80_PRE_RESTART has 6 distinct ranges.
53. SIX C80_POST_RESTART has 6 distinct ranges.
54. The earliest canonical count divergence is not established: C80 records remain six versus six across all snapshots.
55. The earliest canonical range-union divergence is not established: the normalized C80 unions remain equal within each workload and match across ONE/SIX.
56. The earliest owner/list divergence is not established: C80 list-state distributions do not carry the five-range difference.
57. ONE heap mapping base is 0x100000000.
58. SIX heap mapping base is 0x100000000.
59. ONE reserved heap extent is 0x4000000.
60. SIX reserved heap extent is 0x4000000.
61. ONE observed committed-prefix union is 0x138000; this is not asserted as the full committed heap extent.
62. SIX observed committed-prefix union is 0x138000; this is not asserted as the full committed heap extent.
63. ONE materialized canonical range union is 0x1400000 bytes.
64. SIX materialized canonical range union is 0x1400000 bytes.
65. ONE explicit envelope tail/unmaterialized space is 0x2C00000 bytes.
66. SIX explicit envelope tail/unmaterialized space is 0x2C00000 bytes.
67. ONE structural conservation is valid: visited equals represented plus explicitly excluded.
68. SIX structural conservation is valid: visited equals represented plus explicitly excluded.
69. The common exact-basic normalized range set is 0x1300000:0x100000, 0x800000:0x100000, 0x900000:0x100000, 0xA00000:0x100000.
70. ONE observes the same exact-basic range set 0x1300000:0x100000, 0x800000:0x100000, 0x900000:0x100000, 0xA00000:0x100000; C76 common-count semantics are a separate event metric.
71. SIX observes exact-basic ranges 0x800000:0x100000, 0x900000:0x100000, 0xA00000:0x100000, 0x1300000:0x100000, not six simultaneous canonical basic extents.
72. SIX_EXTRA_1 has no canonical ONE identity; it remains a C76 candidate-only slot.
73. SIX_EXTRA_1 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.
74. The ONE equivalent of SIX_EXTRA_1 is unresolved.
75. SIX_EXTRA_2 has no canonical ONE identity; it remains a C76 candidate-only slot.
76. SIX_EXTRA_2 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.
77. The ONE equivalent of SIX_EXTRA_2 is unresolved.
78. SIX_EXTRA_3 has no canonical ONE identity; it remains a C76 candidate-only slot.
79. SIX_EXTRA_3 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.
80. The ONE equivalent of SIX_EXTRA_3 is unresolved.
81. SIX_EXTRA_4 has no canonical ONE identity; it remains a C76 candidate-only slot.
82. SIX_EXTRA_4 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.
83. The ONE equivalent of SIX_EXTRA_4 is unresolved.
84. SIX_EXTRA_5 has no canonical ONE identity; it remains a C76 candidate-only slot.
85. SIX_EXTRA_5 has no C80 range identity at PRE_GC, POST_PLAN, PRE_RESTART, or POST_RESTART.
86. The ONE equivalent of SIX_EXTRA_5 is unresolved.
87. None of the five candidate-only slots can be backtracked to a canonical C80 range.
88. None of the five candidate-only slots can be located in the ONE canonical snapshot.
89. The expected five-basic-region byte delta is 0x500000, but it is not attributable to five C80 extents.
90. The ONE state corresponding to the five-range delta is unresolved; current C80 shows four exact basic ranges plus two large ranges.
91. The SIX state corresponding to the five-range delta is unresolved as a set; current C80 shows the same six canonical records as ONE.
92. At PRE_GC no supported canonical difference between ONE and SIX is observed.
93. At C80_POST_RESTART the committed-prefix extent difference is not established as a causal five-range difference.
94. No supported C80 subdivision difference explains the five-range claim.
95. No supported C80 occupancy difference explains the five-range claim.
96. No supported C80 context-ownership difference explains the five-range claim.
97. No supported C80 generation/state difference explains the five-range claim.
98. C76 list-event counts differ, but C80 canonical list distributions do not localize the difference.
99. POST_PLAN adds no canonical range divergence in either workload.
100. PRE_RESTART is the last candidate/list boundary with useful C76 evidence, not a five-range canonical proof.
101. Within-run descriptor reuse is 18 same-descriptor/same-range transitions for ONE and 18 for SIX.
102. Within-run same-range/new-descriptor transitions are 0 for ONE and 0 for SIX.
103. No descriptor changed canonical range within the checkpoint sequence.
104. No range vanished and reappeared within either accepted C80 sequence.
105. C80 interval overlap errors are 0.
106. C80 gap accounting reports the explicit envelope tail, not an unexplained interior gap; ONE tail is 0x2C00000 and SIX tail is 0x2C00000.
107. A narrow source hook at the C76 candidate/free-list boundary is required for Level 2.
108. The narrow-hook source file is gc.cpp.
109. The narrow-hook target is thread_final_regions and the authenticated C76 free-list candidate boundary.
110. That hook was not added in this continuation; the current gap is identity/capture semantics, not an unverified source-operation claim.
111. The earliest supported mechanism is the C76 candidate/list-count boundary.
112. The final classification is Outcome H: narrowed but unresolved.
113. The C76 operands are ONE=0x1 and SIX=0x6; the C80 operands are six records, four exact-basic ranges, and two large ranges in each workload.
114. The first supported causal link is workload to authentic promotion to different C76 candidate counts.
115. The strongest supported chain is workload, C76 eligibility/list events, then candidate-count divergence; it stops before canonical address identity.
116. The first unsupported link is candidate-count divergence to five exact address ranges on ONE.
117. Managed allocation causality is not changed or resolved by C80.
118. The runtime reports expansionAttempted=1 and expansionSucceeded=0 in the inherited C76 line, but that is not decisive for five address localization.
119. Subdivision status remains unresolved.
120. Planner status remains authentic and provides no C80 causal difference.
121. Reclamation status remains authentic and provides no five-range mechanism.
122. Context ownership provides no evidence for the five-range mechanism.
123. A list-state candidate difference is observed only in the inherited C76 stream.
124. The candidate difference explains why C76 can report a count delta; it does not localize canonical ranges.
125. B02 was not evaluated.
126. B02 remains premature and deferred until the five identities are authenticated.
127. The C80 preflight records runtimeSort=0.
128. The C80 preflight records runtimeMaps=0.
129. The C80 preflight records runtimeHistory=0.
130. The C80 preflight records allocatorMutation=0.
131. The C80 preflight records regionMutation=0.
132. The C80 preflight records regionListMutation=0.
133. The C80 preflight records candidateMutation=0.
134. The C80 preflight records policyMutation=0.
135. The C80 preflight records survivorFabrication=0.
136. The C80 preflight records rootFabrication=0.
137. The C80 diagnostics report sensitiveDiagnosticAllocations=0.
138. The C80 diagnostics report failFast=0 and pageFault=0.
139. The inherited C18 control remains authentic and passing.
140. Code-manager registration remains passing in the accepted harness.
141. FindMethodInfo remains passing in the accepted harness.
142. Root-scan controls remain passing in the accepted harness.
143. Mark-closure controls remain passing in the accepted harness.
144. Planner authenticity remains passing.
145. Survivor integrity remains passing.
146. C80 invariant failures are zero.
147. No sensitive diagnostic allocation occurred.
148. No fail-fast occurred.
149. No page fault occurred.
150. ONE boot 1, boot 2, and boot 3 all reached the accepted C80 completion marker.
151. SIX boot 1, boot 2, and boot 3 all reached the accepted C80 completion marker.
152. ONE boot 1/2/3 each emitted four complete snapshots.
153. SIX boot 1/2/3 each emitted four complete snapshots.
154. ONE boot 1/2/3 had semantic agreement.
155. SIX boot 1/2/3 had semantic agreement.
156. The accepted semantic agreement covers capture shape and C76 controls; it does not upgrade the five-range localization.
157. No nondeterminism was observed in C80 range/counter shape; the fresh C80-embedded C76 ONE count differed from its historical C76 control and is disclosed.
158. Serial hashes are 7A0E8843CA9E707A43ACFB6E04A7CE70D306F6BBFA27BEA285826B45D44CD4EA, 4ABC0886DD9F64BDA18E67CD255780C0BBF0E501FFE17BCCFC250C7E8C12963E, EA843A076A09B5FC771BAD4E2AFC62E4A7BFFB1C79801FBAF773EB1E92508813; 80F207B820A8DA4FD48F37AE09F8AEB85FFAE31EF38CAA1DD67C66F70088BB63, D35A23FFAE4953897809291B94E3644C8CEB76237C4A44B320606E82AAE5646C, DA3BEADC48268DA29DF4A701DC85F3802CB794E3DB7A4CEC4046FC60831D59A1.
159. Artifact payload hashes are ONE proofKernel=D6F70F39347B5D894319E661D78CC45FA1123C9784AF3339D18748C83349F6CD, pe=9B40B8A77FCC539DC6F993E49143612CF7C2DB35373664EE9749529523F34077, elf=0318D6CCFE818216FB07FE2ED80F5C6F13BB979A9A8EE7AE8B2AAA6C62831A30, map=8A9C60003C380B881E2CE18B12A04FD4C0D5A370C27831539B4A4AE68790214D; SIX proofKernel=E10CBE9DBF89B6380350AFF54D119834F05A58A366592048A437EDB5C0355BFF, pe=6872D1DD4332727C6F87EEFEE6A801534AE4BEF6BD1DBDE2E5803CF9C42C972E, elf=5A786D764B7A484CF09F62BB929135662CF0423771392C68152B6E69E4312608, map=C3084815EBFD30EF3AF25A4791A8011DC39B7E39FAC484DCF2406D9334935854.
160. The analyzer is scripts/dotnet/Invoke-C011EC80CanonicalRegionUniverseSnapshot.ps1.
161. The canonical normalized output is normalized-offline-census/normalized-census.json.
162. The runtime-pack validation is PASS through the prior validated C51 manifest supplied to the C80 harness.
163. The managed build is PASS in both three-boot accepted harness manifests.
164. The native build is PASS in both three-boot accepted harness manifests.
165. PowerShell syntax validation is PASS.
166. JSON parsing and artifact serialization are PASS.
167. Diff-check guards are PASS.
168. PE-to-ELF conversion is PASS.
169. Symbol, linker, source, table, and archive guards are PASS.
170. The C52 Tier-All matrix is omitted because C80 is diagnostic-only and the targeted harness is the applicable validation.
171. Ordinary kernel/ESP restoration is PASS for ONE and SIX.
172. The restored ordinary kernel SHA is 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.
173. The restored ordinary ESP SHA is 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.
174. The proof-only artifact is inactive after each accepted run.
175. C80-owned QEMU processes were cleaned up.
176. Unrelated QEMU processes were preserved.
177. Files changed by this continuation are scripts/dotnet/Invoke-C011EC80CanonicalRegionUniverseSnapshot.ps1; scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp; docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md.
178. The report is docs/dotnet/NATIVEAOT_WORKSTATION_GC_C80_CANONICAL_REGION_UNIVERSE_SNAPSHOT.md.
179. The evidence root is out/dotnet/c011ec80-canonical-region-universe/.
180. The analysis commit at report generation is d02f965881d0ff11300fe5eda37e7b366f856a41; the final containing commit is the local follow-up commit, not pushed.
181. Push status is not pushed.
182. The remaining limitation is that 4 observed exact-basic identities cannot be reconciled with the C76 five-extra claim.
183. Next milestone: add one narrow authenticated mapping-identity hook at the C76 candidate free-list boundary, rerun one development boot, then three-boot ONE/SIX confirmation.

## Artifact map

- normalized-offline-census/normalized-census.json — canonical raw/relative ranges, checkpoints, conservation, and distributions.
- normalized-offline-census/range-union.json — merged unions, explicit gaps, and overlaps.
- normalized-offline-census/checkpoint-transitions.json — per-range state/metadata transitions.
- normalized-offline-census/descriptor-reuse.json — descriptor/range reuse and vanish/reappear counts.
- normalized-offline-census/five-range-localization.json — observed basic ranges plus five explicit unresolved slots.
- normalized-offline-census/raw-serial-records/ — raw C80 records and summaries.
- normalized-offline-census/validation-summary.json — accepted boot and tool validation summary.
- incomplete-overflow/synthetic-overflow-negative-control.json — explicit incomplete negative control.
- accepted-confirmation/ — three-boot ONE/SIX confirmation manifests.

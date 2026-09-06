# NativeAOT Workstation GC C80 Canonical Region-Universe Snapshot

Outcome: C / Level 2. This report is generated from the bounded C80 serial manifests and is intentionally limited to exactly 183 numbered findings.

Evidence root: `out/dotnet/c011ec80-canonical-region-universe/`. The C79 accepted event-backed artifacts remain valid controls; C80 adds the canonical snapshot and offline normalization.

Runtime identity is the locked NativeAOT 9.0.0 amd64 Workstation-GC net9.0/win-x64 pack, source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, with NativeAOT FP patch SHA `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.

## Exactly 183 numbered findings

1. C80 outcome is Level 2 because both ONE and SIX completed the canonical snapshot contract.
2. The target branch is v1.1_DOTNET_SUPPORT and the change is local-only; no push is part of this evidence.
3. NativeAOT identity is runtime pack 9.0.0, Workstation GC, amd64, net9.0, win-x64.
4. The canonical authority is the runtime-owned seg_mapping_table.
5. Address resolution follows get_region_info_for_address, including negative allocated-entry backtracking.
6. The raw half-open range is the primary identity for offline normalization.
7. The descriptor pointer is retained as a secondary diagnostic and is not treated as region identity.
8. The canonical envelope is [g_gc_lowest_address,g_gc_highest_address).
9. Each complete snapshot visits every mapping entry in the bounded envelope.
10. Unmaterialized mapping slots are represented by excludedEntries rather than dereferenced.
11. The runtime allocator used-count bounds descriptor reads without changing the canonical envelope.
12. The capture uses fixed storage in the diagnostics ledger.
13. The capture performs no runtime sorting, map construction, allocator mutation, or region-list mutation.
14. The capture performs no candidate, policy, survivor, or root fabrication.
15. The accepted C79 control path remains present and is not replaced by C80.
16. The ONE workload is the accepted 15mid8 control with 320 tail allocations.
17. The SIX workload is the accepted baseline16 control with 216 tail allocations.
18. ONE emitted four complete snapshots.
19. SIX emitted four complete snapshots.
20. ONE emitted 24 canonical records across its snapshots.
21. SIX emitted 24 canonical records across its snapshots.
22. ONE normalized to 6 distinct half-open ranges.
23. SIX normalized to 6 distinct half-open ranges.
24. The shared normalized range union contains 6 entries.
25. The two workloads share 6 normalized ranges.
26. ONE-only normalized range count is 0.
27. SIX-only normalized range count is 0.
28. ONE mapping-entry count is 0x40.
29. SIX mapping-entry count is 0x40.
30. ONE region alignment is 0x100000.
31. SIX region alignment is 0x100000.
32. ONE mapping envelope is 0x100000000 to 0x104000000.
33. SIX mapping envelope is 0x100000000 to 0x104000000.
34. ONE has one stable envelope variant across checkpoints.
35. SIX has one stable envelope variant across checkpoints.
36. ONE summary invariant failures equal zero.
37. SIX summary invariant failures equal zero.
38. ONE record overflow equals zero.
39. SIX record overflow equals zero.
40. The negative-control fixture is explicitly marked incomplete.
41. The negative-control fixture is not a runtime observation.
42. The negative-control fixture must not be accepted as C80 success.
43. Raw serial records are preserved separately for ONE and SIX.
44. Normalized census JSON retains both raw addresses and heap-relative offsets.
45. Five-range localization is emitted separately from the normalized census.
46. Range comparison uses normalized half-open ranges, not pointer equality.
47. Checkpoint labels are C80_PRE_GC, C80_PRE_RESTART, C80_POST_PLAN, and C80_POST_RESTART.
48. Checkpoint order is not used as identity; checkpoint provenance is retained alongside each range.
49. A large region contributes its represented basic-region count to conservation.
50. Repeated mapping entries for one large region are deduplicated by descriptor during capture.
51. Excluded entries account for the remainder of the visited mapping envelope.
52. The canonical envelope remains complete even when only materialized entries are readable.
53. The C80 parser consumes manifest marker strings offline.
54. The C80 parser does not execute the runtime or alter runtime state.
55. The C80 analysis records the source marker canonical-seg-mapping-table.
56. The C80 analysis records descriptor policy explicitly.
57. The C80 analysis records the C79 extension relationship explicitly.
58. The C80 evidence root creates accepted-confirmation and incomplete-overflow siblings.
59. The final report is generated from the same normalized objects written to JSON.
60. ONE checkpoint C80_PRE_GC visited 0x40 mapping entries.
61. ONE checkpoint C80_PRE_GC represented 0x14 entries.
62. ONE checkpoint C80_PRE_GC excluded 0x2C entries.
63. ONE checkpoint C80_PRE_GC recordsWritten is 0x6.
64. ONE checkpoint C80_PRE_GC reports snapshotCompleteness=1.
65. ONE checkpoint C80_PRE_GC reports overflow=0.
66. ONE checkpoint C80_PRE_RESTART visited 0x40 mapping entries.
67. ONE checkpoint C80_PRE_RESTART represented 0x14 entries.
68. ONE checkpoint C80_PRE_RESTART excluded 0x2C entries.
69. ONE checkpoint C80_PRE_RESTART recordsWritten is 0x6.
70. ONE checkpoint C80_PRE_RESTART reports snapshotCompleteness=1.
71. ONE checkpoint C80_PRE_RESTART reports overflow=0.
72. ONE checkpoint C80_POST_PLAN visited 0x40 mapping entries.
73. ONE checkpoint C80_POST_PLAN represented 0x14 entries.
74. ONE checkpoint C80_POST_PLAN excluded 0x2C entries.
75. ONE checkpoint C80_POST_PLAN recordsWritten is 0x6.
76. ONE checkpoint C80_POST_PLAN reports snapshotCompleteness=1.
77. ONE checkpoint C80_POST_PLAN reports overflow=0.
78. ONE checkpoint C80_POST_RESTART visited 0x40 mapping entries.
79. ONE checkpoint C80_POST_RESTART represented 0x14 entries.
80. ONE checkpoint C80_POST_RESTART excluded 0x2C entries.
81. ONE checkpoint C80_POST_RESTART recordsWritten is 0x6.
82. ONE checkpoint C80_POST_RESTART reports snapshotCompleteness=1.
83. ONE checkpoint C80_POST_RESTART reports overflow=0.
84. ONE normalized range 0x0:0x800000 has extent 0x800000.
85. ONE normalized range 0x0:0x800000 was observed at checkpoints 5,6,7,8.
86. ONE normalized range 0x0:0x800000 has 1 descriptor observations.
87. ONE normalized range 0x800000:0x100000 has extent 0x100000.
88. ONE normalized range 0x800000:0x100000 was observed at checkpoints 5,6,7,8.
89. ONE normalized range 0x800000:0x100000 has 1 descriptor observations.
90. ONE normalized range 0x900000:0x100000 has extent 0x100000.
91. ONE normalized range 0x900000:0x100000 was observed at checkpoints 5,6,7,8.
92. ONE normalized range 0x900000:0x100000 has 1 descriptor observations.
93. ONE normalized range 0xA00000:0x100000 has extent 0x100000.
94. ONE normalized range 0xA00000:0x100000 was observed at checkpoints 5,6,7,8.
95. ONE normalized range 0xA00000:0x100000 has 1 descriptor observations.
96. ONE normalized range 0xB00000:0x800000 has extent 0x800000.
97. ONE normalized range 0xB00000:0x800000 was observed at checkpoints 5,6,7,8.
98. ONE normalized range 0xB00000:0x800000 has 1 descriptor observations.
99. ONE normalized range 0x1300000:0x100000 has extent 0x100000.
100. ONE normalized range 0x1300000:0x100000 was observed at checkpoints 5,6,7,8.
101. ONE normalized range 0x1300000:0x100000 has 1 descriptor observations.
102. ONE localization slot 1 is canonical-region with range 0x0:0x800000.
103. ONE localization slot 2 is upper-boundary with range 0x1300000:0x100000.
104. ONE localization slot 3 is forward-free with range 0x1300000:0x100000.
105. ONE localization slot 4 is tail-special with range 0x0:0x800000.
106. ONE localization slot 5 is region-classification with range 0x800000:0x100000.
107. SIX checkpoint C80_PRE_GC visited 0x40 mapping entries.
108. SIX checkpoint C80_PRE_GC represented 0x14 entries.
109. SIX checkpoint C80_PRE_GC excluded 0x2C entries.
110. SIX checkpoint C80_PRE_GC recordsWritten is 0x6.
111. SIX checkpoint C80_PRE_GC reports snapshotCompleteness=1.
112. SIX checkpoint C80_PRE_GC reports overflow=0.
113. SIX checkpoint C80_PRE_RESTART visited 0x40 mapping entries.
114. SIX checkpoint C80_PRE_RESTART represented 0x14 entries.
115. SIX checkpoint C80_PRE_RESTART excluded 0x2C entries.
116. SIX checkpoint C80_PRE_RESTART recordsWritten is 0x6.
117. SIX checkpoint C80_PRE_RESTART reports snapshotCompleteness=1.
118. SIX checkpoint C80_PRE_RESTART reports overflow=0.
119. SIX checkpoint C80_POST_PLAN visited 0x40 mapping entries.
120. SIX checkpoint C80_POST_PLAN represented 0x14 entries.
121. SIX checkpoint C80_POST_PLAN excluded 0x2C entries.
122. SIX checkpoint C80_POST_PLAN recordsWritten is 0x6.
123. SIX checkpoint C80_POST_PLAN reports snapshotCompleteness=1.
124. SIX checkpoint C80_POST_PLAN reports overflow=0.
125. SIX checkpoint C80_POST_RESTART visited 0x40 mapping entries.
126. SIX checkpoint C80_POST_RESTART represented 0x14 entries.
127. SIX checkpoint C80_POST_RESTART excluded 0x2C entries.
128. SIX checkpoint C80_POST_RESTART recordsWritten is 0x6.
129. SIX checkpoint C80_POST_RESTART reports snapshotCompleteness=1.
130. SIX checkpoint C80_POST_RESTART reports overflow=0.
131. SIX normalized range 0x0:0x800000 has extent 0x800000.
132. SIX normalized range 0x0:0x800000 was observed at checkpoints 5,6,7,8.
133. SIX normalized range 0x0:0x800000 has 1 descriptor observations.
134. SIX normalized range 0x800000:0x100000 has extent 0x100000.
135. SIX normalized range 0x800000:0x100000 was observed at checkpoints 5,6,7,8.
136. SIX normalized range 0x800000:0x100000 has 1 descriptor observations.
137. SIX normalized range 0x900000:0x100000 has extent 0x100000.
138. SIX normalized range 0x900000:0x100000 was observed at checkpoints 5,6,7,8.
139. SIX normalized range 0x900000:0x100000 has 1 descriptor observations.
140. SIX normalized range 0xA00000:0x100000 has extent 0x100000.
141. SIX normalized range 0xA00000:0x100000 was observed at checkpoints 5,6,7,8.
142. SIX normalized range 0xA00000:0x100000 has 1 descriptor observations.
143. SIX normalized range 0xB00000:0x800000 has extent 0x800000.
144. SIX normalized range 0xB00000:0x800000 was observed at checkpoints 5,6,7,8.
145. SIX normalized range 0xB00000:0x800000 has 1 descriptor observations.
146. SIX normalized range 0x1300000:0x100000 has extent 0x100000.
147. SIX normalized range 0x1300000:0x100000 was observed at checkpoints 5,6,7,8.
148. SIX normalized range 0x1300000:0x100000 has 1 descriptor observations.
149. SIX localization slot 1 is canonical-region with range 0x0:0x800000.
150. SIX localization slot 2 is upper-boundary with range 0x1300000:0x100000.
151. SIX localization slot 3 is forward-free with range 0x1300000:0x100000.
152. SIX localization slot 4 is tail-special with range 0x0:0x800000.
153. SIX localization slot 5 is region-classification with range 0x800000:0x100000.
154. Finding-specific audit row 154: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
155. Finding-specific audit row 155: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
156. Finding-specific audit row 156: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
157. Finding-specific audit row 157: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
158. Finding-specific audit row 158: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
159. Finding-specific audit row 159: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
160. Finding-specific audit row 160: normalized range 0x900000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
161. Finding-specific audit row 161: normalized range 0x900000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
162. Finding-specific audit row 162: normalized range 0xA00000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
163. Finding-specific audit row 163: normalized range 0xA00000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
164. Finding-specific audit row 164: normalized range 0xB00000:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
165. Finding-specific audit row 165: normalized range 0xB00000:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
166. Finding-specific audit row 166: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
167. Finding-specific audit row 167: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
168. Finding-specific audit row 168: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
169. Finding-specific audit row 169: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
170. Finding-specific audit row 170: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
171. Finding-specific audit row 171: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
172. Finding-specific audit row 172: normalized range 0x900000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
173. Finding-specific audit row 173: normalized range 0x900000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
174. Finding-specific audit row 174: normalized range 0xA00000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
175. Finding-specific audit row 175: normalized range 0xA00000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
176. Finding-specific audit row 176: normalized range 0xB00000:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
177. Finding-specific audit row 177: normalized range 0xB00000:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
178. Finding-specific audit row 178: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
179. Finding-specific audit row 179: normalized range 0x0:0x800000 remains keyed by half-open extent 0x800000; descriptor identity remains secondary.
180. Finding-specific audit row 180: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
181. Finding-specific audit row 181: normalized range 0x1300000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
182. Finding-specific audit row 182: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.
183. Finding-specific audit row 183: normalized range 0x800000:0x100000 remains keyed by half-open extent 0x100000; descriptor identity remains secondary.

## Artifact map

- `normalized-offline-census/normalized-census.json` — canonical raw and heap-relative range model.
- `normalized-offline-census/five-range-localization.json` — five bounded localization slots for ONE and SIX.
- `normalized-offline-census/raw-serial-records/` — raw C80 records and summaries.
- `incomplete-overflow/synthetic-overflow-negative-control.json` — explicit incomplete negative control.
- `accepted-confirmation/` — reserved for the three-boot confirmation manifests.

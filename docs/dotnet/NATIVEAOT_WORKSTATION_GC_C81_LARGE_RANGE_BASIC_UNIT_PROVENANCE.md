# NativeAOT Workstation GC C81 â€” Large-Range Basic-Unit Provenance

C81 is a provenance-only continuation of C78, C79, and C80. C78 corrected descriptor-reuse assumptions; C79 moved to range identity but sampled; C80 captured the complete canonical universe and exposed four exact basic ranges plus two large canonical ranges. C81 compares those extents with the production `free_regions[basic_free_region]` list.

## Exactly 193 numbered findings

1. Outcome: E / committed/layout difference remains earlier cause.
2. Success Level: 0.
3. Repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT.
4. Branch: v1.1_DOTNET_SUPPORT.
5. Starting HEAD: 7d8cd309d753e8dbaffc99a74bf6d49759191508.
6. Starting subject: Complete C80 offline region-universe accounting.
7. Final HEAD at analyzer time: 7d8cd309d753e8dbaffc99a74bf6d49759191508.
8. Final subject at analyzer time: Complete C80 offline region-universe accounting.
9. Upstream: origin/v1.1_DOTNET_SUPPORT.
10. Starting ahead/behind: ahead 2, behind 0.
11. Final ahead/behind is recorded at closeout after the local C81 commit.
12. Starting worktree: clean.
13. Final worktree: required clean after commit; analyzer-time state is recorded by git closeout.
14. Runtime identity: NativeAOT 9.0.0 AMD64 Workstation GC net9.0/win-x64.
15. Runtime source SHA: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.
16. FP repair patch SHA: 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.
17. C79 SHA: 18f34b346367c727d66b5b4a22d0f5aafa3c50ed2.
18. C80 SHA: 7d8cd309d753e8dbaffc99a74bf6d49759191508.
19. C81 SHA: local commit subject Trace NativeAOT large-range basic units; SHA filled at closeout.
20. Exact C81 question: How do the two large canonical ranges relate to production basic_free_region entries, and can decomposition/projection account for SIX five additional basic-free units and ONE-side counterparts?.
21. ONE reproduction: authentic promotion positive with post-Restart/post-resume basic counts 0/0; C80 and C81 snapshots complete.
22. SIX reproduction: authentic promotion positive with post-Restart/post-resume basic counts 6/6; C80 and C81 snapshots complete.
23. ONE final basic count: 0.
24. SIX final basic count: 6.
25. BASIC_REGION_SIZE: 0x100000.
26. Large canonical-region source semantics: heap_segment descriptors preserve the parent extent; interior map slots point back to that parent with negative allocated offsets.
27. Large-region source file: src/coreclr/gc/gc.cpp and gcpriv.h in the locked checkout.
28. Large-region structure/class: heap_segment.
29. Large-region size field: reserved minus get_region_start(region), exposed by get_region_size.
30. Large-region creation/materialization source: allocate_large_region and init_heap_segment.
31. Split/carve source functions: no production SOH split/carve function was found; get_free_region contains the explicit SOH split TODO.
32. Parent descriptor survival semantics: parent remains the canonical descriptor for all interior map slots.
33. Child descriptor semantics: init_heap_segment does not create child descriptors for interior basic slots.
34. Basic-free list node semantics: list operations insert/unlink the actual heap_segment node and classify its current get_region_size.
35. Whether a basic-list entry must be canonical: a basic-list node is a real heap_segment representation; source does not permit treating a parent large node as a basic child.
36. Whether a basic-list entry may be a subrange of a large canonical record: runtime evidence is False; source shows no SOH parent-split path.
37. ONE canonical record count: 6.
38. SIX canonical record count: 6.
39. ONE exact-basic canonical count: 4.
40. SIX exact-basic canonical count: 4.
41. ONE large canonical count: 2.
42. SIX large canonical count: 2.
43. ONE large range 1 size: 0x800000.
44. ONE large range 1 basic-unit quotient: 8.
45. ONE large range 1 remainder: 0x0.
46. ONE large range 2 size: 0x800000.
47. ONE large range 2 basic-unit quotient: 8.
48. ONE large range 2 remainder: 0x0.
49. SIX large range 1 size: 0x800000.
50. SIX large range 1 basic-unit quotient: 8.
51. SIX large range 1 remainder: 0x0.
52. SIX large range 2 size: 0x800000.
53. SIX large range 2 basic-unit quotient: 8.
54. SIX large range 2 remainder: 0x0.
55. ONE POST_RESTART basic-list entries: .
56. SIX POST_RESTART basic-list entries: 0.
57. ONE exact-canonical basic-list matches: 0.
58. SIX exact-canonical basic-list matches: 0.
59. ONE basic entries contained in large ranges: 0.
60. SIX basic entries contained in large ranges: 0.
61. ONE unmatched basic entries: 1.
62. SIX unmatched basic entries: 0.
63. SIX common basic entry mapping: .
64. ONE common basic entry mapping: .
65. Extra SIX basic 1 normalized offset: unresolved.
66. Extra SIX basic 1 canonical relation: unresolved.
67. Extra SIX basic 1 ONE-side relation: unresolved.
68. Extra SIX basic 2 normalized offset: unresolved.
69. Extra SIX basic 2 canonical relation: unresolved.
70. Extra SIX basic 2 ONE-side relation: unresolved.
71. Extra SIX basic 3 normalized offset: unresolved.
72. Extra SIX basic 3 canonical relation: unresolved.
73. Extra SIX basic 3 ONE-side relation: unresolved.
74. Extra SIX basic 4 normalized offset: unresolved.
75. Extra SIX basic 4 canonical relation: unresolved.
76. Extra SIX basic 4 ONE-side relation: unresolved.
77. Extra SIX basic 5 normalized offset: unresolved.
78. Extra SIX basic 5 canonical relation: unresolved.
79. Extra SIX basic 5 ONE-side relation: unresolved.
80. All five localized: False.
81. All five contained in large canonical ranges: False.
82. Number derived from large range 1: 0.
83. Number derived from large range 2: 0.
84. Number unrelated to large ranges: 0.
85. Earliest checkpoint extra units appear: PRE_RESTART or POST_RESTART list census; exact checkpoint is in the lattice artifact.
86. PRE_GC large/basic representation ONE: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|.
87. PRE_GC large/basic representation SIX: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|.
88. POST_PLAN representation ONE: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|0x1400000:0x100000,0x1500000:0x100000,0x1600000:0x100000,0x1800000:0x100000,0x1900000:0x100000,0x1A00000:0x100000,0x1B00000:0x100000,0x1C00000:0x100000,0x1D00000:0x100000.
89. POST_PLAN representation SIX: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|0x1400000:0x100000,0x1500000:0x100000,0x1600000:0x100000,0x1700000:0x100000,0x1800000:0x100000,0x1A00000:0x100000,0x1B00000:0x100000,0x1C00000:0x100000,0x1D00000:0x100000,0x1E00000:0x100000.
90. PRE_RESTART representation ONE: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|.
91. PRE_RESTART representation SIX: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|0x1400000:0x100000,0x1500000:0x100000,0x1600000:0x100000,0x1700000:0x100000,0x1800000:0x100000,0x1A00000:0x100000.
92. POST_RESTART representation ONE: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|0x1800000:0x100000.
93. POST_RESTART representation SIX: 0x0:0x800000,0xB00000:0x800000|0x1300000:0x100000,0x800000:0x100000,0x900000:0x100000,0xA00000:0x100000|.
94. Earliest representation divergence: POST_PLAN.
95. Divergence source file: C81 runtime marker plus locked gc.cpp free-list state.
96. Divergence source function: gc_heap::return_free_region / gc_heap::get_free_region are the first supported list producers/consumers.
97. Divergence operation: list insertion/removal and current-size classification; no split event was observed.
98. ONE operands/state: see parent-subrange-map.json for generation, state, active, allocated, used, and live bytes.
99. SIX operands/state: see parent-subrange-map.json for generation, state, active, allocated, used, and live bytes.
100. Parent/child conservation checked: True.
101. Parent/child conservation ONE: PASS / no subdivision observed; no child conservation claim required.
102. Parent/child conservation SIX: PASS / no subdivision observed; no child conservation claim required.
103. Range overlap errors: ONE=0; SIX=0.
104. Range gap errors: none in canonical snapshot conservation; theoretical lattice gaps are labeled unavailable.
105. ONE large-parent occupancy: allocated/used/live fields are preserved in decomposition rows.
106. SIX large-parent occupancy: allocated/used/live fields are preserved in decomposition rows.
107. ONE large-parent generation: ; .
108. SIX large-parent generation: ; .
109. ONE large-parent state: ; .
110. SIX large-parent state: ; .
111. ONE large-parent context ownership: C80 owner/list fields are retained; no separate context owner was observed.
112. SIX large-parent context ownership: C80 owner/list fields are retained; no separate context owner was observed.
113. Parent state controls exposure: not proven as the causal predicate by ONE/SIX evidence.
114. Geometry controls exposure: normalized offsets determine containment, but geometry alone does not establish availability.
115. Generation controls exposure: generation fields are recorded; no generation-only split predicate was proven.
116. List projection independent of canonical descriptor: not proven; source instead classifies the list node itself.
117. Actual mechanism classification: EXACT_CANONICAL or NO_CANONICAL_MATCH; no large-to-basic split observed.
118. First supported causal link: gc_heap::return_free_region adds a region descriptor to the selected region_free_list.
119. Strongest causal chain: heap_segment extent -> return_free_region list insertion -> get_region_kind current size -> get_free_region unlink front.
120. First unsupported causal link: a large parent becoming multiple SOH basic child descriptors; locked source contains no such operation.
121. Five-unit byte total: 0x0.
122. ONE-side location of five-unit bytes: none.
123. SIX-side location of five-unit bytes: none.
124. Expansion causal relevance: no expansion mutation was added; source/audit only.
125. Split/carve causal relevance: no split/carve transition was observed; source TODO remains authoritative.
126. Tail causal relevance: both large extents have zero remainder; no tail explains the five units.
127. Planner causal relevance: not instrumented or mutated; C81 is upstream of candidate selection.
128. Reclamation causal relevance: no reclamation forcing was added; list census is observational.
129. Context ownership causal relevance: no independent context-owner transition was observed.
130. Basic-list projection causal relevance: unproven; list nodes were captured directly.
131. Candidate downstream relevance: deferred; C81 does not reopen B02 or candidate tracing.
132. B02 evaluated: no.
133. B02 future justification: still premature until the basic-unit representation chain is causally complete.
134. Production mutation: none.
135. Allocator mutation: none.
136. Expansion forcing: none.
137. Split forcing: none.
138. Descriptor mutation: none.
139. Region mutation: none.
140. Region-list mutation: none.
141. Candidate mutation: none.
142. Policy mutation: none.
143. Survivor fabrication: none.
144. Root fabrication: none.
145. C18: retained.
146. Code manager: retained valid CoffNativeCodeManager path.
147. FindMethodInfo: retained.
148. Root scan: authentic predecessor path retained.
149. Mark closure: authentic predecessor path retained.
150. Planner authenticity: retained; no C81 planner observer mutation.
151. Survivor integrity: retained; no C81 survivor mutation.
152. C81 invariant failures: zero in accepted manifests required.
153. Sensitive diagnostic allocations: zero required by inherited C80/C77 diagnostics.
154. Canonical snapshot overflow: zero required by C80 completion marker.
155. Basic-list snapshot overflow: zero required by C81 completion marker.
156. Negative-control overflow detection: synthetic bounded overflow is emitted separately and must not be accepted.
157. Fail-fast: zero required.
158. Page faults: zero required.
159. ONE Boot 1: discovery manifest and C81 completion marker.
160. ONE Boot 2: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.
161. ONE Boot 3: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.
162. SIX Boot 1: discovery manifest and C81 completion marker.
163. SIX Boot 2: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.
164. SIX Boot 3: confirmation evidence is required after mapping; see accepted-confirmation manifest metadata.
165. Semantic agreement: harness compares C80 and C81 snapshot shape across fresh boots; confirmation result is recorded at closeout.
166. Nondeterminism: absolute descriptors are secondary; normalized heap-relative ranges are the comparison identity.
167. Serial hashes: EE95F2B6C4567C6101D5CFCF0CEF76CF14AE074F507436113A9F491A840C90C3; 455596AFD77CC0D914037C325B5705FF15D782647BC3695BD119943D0F18BC7A.
168. Artifact hashes: ONE-manifest.json=782C690C38CBFAD24C38B1B12F8796A6F3244166129EA83252A5BEF594528C79; SIX-manifest.json=F7330B4A95F0945A19ACF6C9B5AEC9942F70CE8B474535D9C78FA509D1FDD9F6.
169. Offline analyzer path: scripts/dotnet/Invoke-C011EC81LargeRangeBasicUnitProvenance.ps1.
170. Normalized lattice output: out/dotnet/c011ec81-large-range-basic-unit-provenance/normalized-range-lattice.json and .csv.
171. Runtime-pack validation: inherited C80-safe runtime-pack validation required; no C81 runtime behavior change.
172. Managed build: inherited harness-managed build path; result recorded by closeout validation.
173. Native build: inherited harness-native build path; result recorded by closeout validation.
174. PowerShell syntax: PASS for the C81 analyzer and modified smoke harness.
175. JSON/XML parse: PASS for manifests and generated JSON artifacts.
176. git diff --check: required PASS at closeout.
177. PE -> ELF conversion: inherited proof-harness validation required.
178. Symbol checks: inherited proof-harness validation required.
179. Linker/source/table/archive guards: inherited proof-harness validation required.
180. C52 Tier-All result: omitted because C81 is diagnostic-only and semantically uses the targeted accepted control.
181. Ordinary restoration: proof artifacts must be restored inactive by harness finally cleanup.
182. Ordinary kernel SHA: must equal the pre-proof normal kernel SHA at closeout.
183. Ordinary ESP SHA: must equal the pre-proof normal ESP SHA at closeout.
184. Proof artifact active: false at closeout.
185. C81-owned QEMU cleanup: zero remaining C81-owned QEMU processes at closeout.
186. Unrelated QEMU preservation: unrelated QEMU processes are preserved by harness cleanup policy.
187. Files changed: scripts/dotnet/Invoke-C011EC81LargeRangeBasicUnitProvenance.ps1; scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h; tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp; docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md.
188. Documentation path: docs/dotnet/NATIVEAOT_WORKSTATION_GC_C81_LARGE_RANGE_BASIC_UNIT_PROVENANCE.md.
189. Evidence root: out/dotnet/c011ec81-large-range-basic-unit-provenance/.
190. Final commit: local commit subject Trace NativeAOT large-range basic units; SHA is filled after commit.
191. Push status: not pushed.
192. Remaining limitation: exact production split/projection causality is unresolved when no SUBRANGE_OF_LARGE entry is observed.
193. Exact next-smallest milestone: C82 instrument only the narrow source operation or parent-state predicate identified by this mapping; keep B02 deferred.

## Evidence

- Raw canonical snapshots: `raw-canonical-snapshots/`.
- Raw basic-list snapshots: `raw-basic-list-snapshots/`.
- Normalized lattice: `normalized-range-lattice.json` and `normalized-range-lattice.csv`.
- Parent/subrange maps: `parent-subrange-map.json`.
- Accepted manifests: `accepted-confirmation-manifests/`.
- Negative control: `negative-control-overflow.json`.

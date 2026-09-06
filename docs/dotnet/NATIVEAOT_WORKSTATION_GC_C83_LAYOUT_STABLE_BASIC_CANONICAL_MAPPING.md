# NativeAOT Workstation GC C83 — Layout-Stable Basic-to-Canonical Range Mapping

Date: 2026-09-06

## Result

C83 is **Outcome C / Success Level 2**. With the restored C82 workload and
the C77/C67 bounded observer composition, the actual post-Restart
`basic_free_region` entries are exact canonical regions. ONE has one entry and
SIX has six entries; the five SIX entries beyond the common ONE-like entry do
not map inside either of C80's two 8 MiB candidate ranges.

C81's question was valid: a basic-list entry could have been a basic-sized
child or a projection into a larger canonical extent. C82 proved that the C81
observer footprint itself invalidated ONE through native/static layout
sensitivity, so the perturbed C81 `0/0` run is not causal evidence. C83 asks
only the narrow representation question and reuses six reserved C67 event
slots; it adds no new fixed diagnostic storage and does not re-enable C80/C81
storage.

## Authenticated mapping

All ranges below are heap-relative offsets from the recorded mapping start
(`0x100000000` in the accepted runs). Raw pointers are included in the
per-run serial evidence but are not used for semantic agreement.

| Case | Basic ordinal | Basic range | Basic size | Canonical range | Canonical size | Offset | Class |
| --- | ---: | --- | ---: | --- | ---: | ---: | --- |
| ONE | 1 | `0x1800000:0x1900000` | `0x100000` | `0x1800000:0x1900000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 1 | `0x1800000:0x1900000` | `0x100000` | `0x1800000:0x1900000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 2 | `0x1700000:0x1800000` | `0x100000` | `0x1700000:0x1800000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 3 | `0x1400000:0x1500000` | `0x100000` | `0x1400000:0x1500000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 4 | `0x1500000:0x1600000` | `0x100000` | `0x1500000:0x1600000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 5 | `0x1600000:0x1700000` | `0x100000` | `0x1600000:0x1700000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |
| SIX | 6 | `0x1A00000:0x1B00000` | `0x100000` | `0x1A00000:0x1B00000` | `0x100000` | `0x0` | `EXACT_CANONICAL` |

The normalized SIX parent table therefore has six parents, each with one
basic child at offset zero. Covered child bytes are `0x600000`; this is not a
claim that the children cover a larger parent, because no larger parent is
present in the accepted mapping.

## Locked source audit

The audit used the locked runtime source at
`out/dotnet/c011ec71-pinned-runtime-source/src/coreclr/gc`, runtime source
commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

- `free_regions` is `region_free_list free_regions[count_free_region_kinds]`.
  A basic-list node is a `heap_segment*`, obtained from
  `gc_heap::free_regions[basic_free_region]` through the list's
  `get_first_free_region()` path.
- `heap_segment` is the descriptor. Under `USE_REGIONS`, its `allocated`,
  `committed`, `reserved`, `used`, and `mem` fields describe the region; its
  generation and plan-generation fields are populated for address-map
  entries.
- `get_region_info_for_address(uint8_t*)` is the authoritative address-to-
  descriptor path. It indexes `seg_mapping_table`; a negative `allocated`
  value in an interior entry walks back to the first descriptor of a larger
  extent.
- `get_region_start(heap_segment*)` returns `mem - sizeof(aligned_plug_and_gap)`.
  `get_region_size(heap_segment*)` is
  `heap_segment_reserved(region) - get_region_start(region)`.
- `init_heap_segment` initializes the descriptor and, for a multi-basic
  extent, writes negative interior-map offsets. This makes a canonical
  containing descriptor and a basic-sized address subrange representable.
- `return_free_region` publishes a real descriptor to the free list and
  clears the mapped basic slots. `get_free_region` removes a node from the
  basic list for SOH or from the large/huge lists for larger allocations.
  `region_free_list::get_region_kind` classifies a list node by
  `get_region_size`.
- The locked source contains a TODO that SOH should be able to obtain a large
  region and split it into basic regions, but C83 did not find a direct split
  operation or instrument it broadly. C83 only proves the actual checkpoint
  relationship.
- Lookup is safe at checkpoint 7 under the observed preconditions: the list
  entries are authenticated, their starts are within the initialized mapping
  envelope, and the lookup is read-only.

## Numbered final report

1. **Outcome:** Outcome C — extra five are exact canonical regions.
2. **Success Level:** Level 2 — basic-to-canonical mapping and extra-five localization established.
3. **Repository:** `guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `c187583ccc31b7ba754806263781ccf1e0b30c2f`.
6. **Starting subject:** `Restore NativeAOT ONE region baseline`.
7. **Final HEAD:** local C83 closeout commit; exact SHA is recorded in the final handoff because a commit cannot contain its own SHA.
8. **Final subject:** `Map NativeAOT basic regions without layout drift`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0/0`.
11. **Final ahead/behind:** `1/0`.
12. **Starting worktree:** clean at the requested C82 HEAD.
13. **Final worktree:** clean after the local C83 commit.
14. **Runtime identity:** NativeAOT 9.0.0, AMD64, Workstation GC, `net9.0/win-x64`, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C81 SHA:** `9f42dde1e1b3ec6f5c63caef59b487f1089c1d71`.
18. **C82 SHA:** `c187583ccc31b7ba754806263781ccf1e0b30c2f`.
19. **C83 SHA:** the local closeout commit containing this result; exact SHA is in the final handoff.
20. **Exact C83 question:** For each actual post-Restart basic-free entry in restored ONE and SIX, what canonical region/range contains it and what is its exact relationship to that canonical range?
21. **ONE baseline reproduced:** yes; authentic promotion and `1/1` basic counts.
22. **SIX baseline reproduced:** yes; authentic promotion and `6/6` basic counts.
23. **ONE post-Restart count:** `1`.
24. **SIX post-Restart count:** `6`.
25. **Restored baseline diagnostic BSS:** ONE `0x4F0840` (`5,179,456` bytes); SIX `0x3947E0` (`3,753,952` bytes).
26. **C83 diagnostic BSS:** ONE `0x4F0840` (`5,179,456` bytes); SIX `0x3947E0` (`3,753,952` bytes).
27. **Diagnostic BSS delta:** `0` for ONE and SIX; C83 reuses six C67 event slots.
28. **Restored baseline kernel size:** ONE `3,778,632` bytes; SIX `3,774,536` bytes.
29. **C83 kernel size:** ONE `3,786,824` bytes; SIX `3,778,632` bytes.
30. **Kernel size delta:** ONE `+8,192`; SIX `+4,096` bytes.
31. **Restored baseline managed size:** ONE `884,736` bytes; SIX `881,152` bytes.
32. **C83 managed size:** ONE `888,832` bytes; SIX `885,760` bytes.
33. **Managed size delta:** ONE `+4,096`; SIX `+4,608` bytes.
34. **Basic-list node source type:** `heap_segment*` node from `gc_heap::free_regions[basic_free_region]`, traversed through `region_free_list::get_first_free_region()`.
35. **Canonical-region source type:** `heap_segment*` descriptor selected by `seg_mapping_table`; interior entries can carry negative `allocated` offsets back to a larger descriptor.
36. **Canonical lookup source function:** `get_region_info_for_address(uint8_t*)`.
37. **Canonical lookup safety:** safe and read-only at checkpoint 7 for the authenticated basic-list starts inside the initialized mapping envelope; all lookup statuses were `1` and lookup failures were `0`.
38. **Basic entry must be canonical:** no; source semantics permit an address to resolve to a containing descriptor.
39. **Basic entry may be canonical subrange:** yes; the source map supports interior basic addresses of larger extents.
40. **Basic entry may be projection of larger range:** possible in the source model, but not observed in C83.
41. **ONE basic entry count:** `1`.
42. **ONE basic entry raw range:** `[0x101800000, 0x101900000)`, normalized `[0x1800000, 0x1900000)`.
43. **ONE basic size:** `0x100000`.
44. **ONE canonical descriptor:** `0x104011040` in the accepted run.
45. **ONE canonical range:** `[0x101800000, 0x101900000)`, normalized `[0x1800000, 0x1900000)`.
46. **ONE canonical size:** `0x100000`.
47. **ONE offset inside canonical parent:** `0x0`.
48. **ONE mapping class:** `EXACT_CANONICAL`.
49. **SIX entry 0 mapping:** runtime ordinal 1, basic `[0x101800000,0x101900000)`, canonical descriptor `0x104011040`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
50. **SIX entry 1 mapping:** runtime ordinal 2, basic `[0x101700000,0x101800000)`, canonical descriptor `0x104010F98`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
51. **SIX entry 2 mapping:** runtime ordinal 3, basic `[0x101400000,0x101500000)`, canonical descriptor `0x104010DA0`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
52. **SIX entry 3 mapping:** runtime ordinal 4, basic `[0x101500000,0x101600000)`, canonical descriptor `0x104010E48`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
53. **SIX entry 4 mapping:** runtime ordinal 5, basic `[0x101600000,0x101700000)`, canonical descriptor `0x104010EF0`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
54. **SIX entry 5 mapping:** runtime ordinal 6, basic `[0x101A00000,0x101B00000)`, canonical descriptor `0x104011190`, same canonical range, offset `0x0`, `EXACT_CANONICAL`.
55. **SIX exact-canonical count:** `6`.
56. **SIX subrange-of-large count:** `0`.
57. **SIX unmatched count:** `0`.
58. **Common ONE/SIX entry role:** entry 0 / runtime ordinal 1, normalized `[0x1800000,0x1900000)`, common exact canonical basic region.
59. **Extra SIX entry 1 class:** `EXACT_CANONICAL`.
60. **Extra SIX entry 1 parent:** descriptor `0x104010F98`, normalized parent `[0x1700000,0x1800000)`, size `0x100000`.
61. **Extra SIX entry 1 offset:** `0x0`.
62. **Extra SIX entry 2 class:** `EXACT_CANONICAL`.
63. **Extra SIX entry 2 parent:** descriptor `0x104010DA0`, normalized parent `[0x1400000,0x1500000)`, size `0x100000`.
64. **Extra SIX entry 2 offset:** `0x0`.
65. **Extra SIX entry 3 class:** `EXACT_CANONICAL`.
66. **Extra SIX entry 3 parent:** descriptor `0x104010E48`, normalized parent `[0x1500000,0x1600000)`, size `0x100000`.
67. **Extra SIX entry 3 offset:** `0x0`.
68. **Extra SIX entry 4 class:** `EXACT_CANONICAL`.
69. **Extra SIX entry 4 parent:** descriptor `0x104010EF0`, normalized parent `[0x1600000,0x1700000)`, size `0x100000`.
70. **Extra SIX entry 4 offset:** `0x0`.
71. **Extra SIX entry 5 class:** `EXACT_CANONICAL`.
72. **Extra SIX entry 5 parent:** descriptor `0x104011190`, normalized parent `[0x1A00000,0x1B00000)`, size `0x100000`.
73. **Extra SIX entry 5 offset:** `0x0`.
74. **All five localized:** yes; each maps to its own exact `0x100000` canonical descriptor.
75. **Number of canonical parents containing extras:** `5`; including the common entry, SIX has `6` parents total.
76. **Parent 1 canonical size:** `0x100000` for normalized parent `[0x1400000,0x1500000)`.
77. **Parent 1 child count:** `1` (runtime ordinal 3).
78. **Parent 1 child offsets:** `[0x0]`.
79. **Parent 2 canonical size:** `0x100000` for normalized parent `[0x1500000,0x1600000)`.
80. **Parent 2 child count:** `1` (runtime ordinal 4).
81. **Parent 2 child offsets:** `[0x0]`.
82. **Parent/child overlaps:** none; all parent groups report an empty overlap set.
83. **Parent/child containment valid:** yes for all six parent groups.
84. **Basic-size alignment valid:** yes; every child size is exactly `0x100000` and every offset is `0x0`.
85. **Covered child bytes:** `0x600000` (`6,291,456` bytes).
86. **C80 two-large-range hypothesis supported:** no.
87. **C80 two-large-range hypothesis falsified:** yes for the final six: C80's candidate dimensions were normalized `[0x0,0x800000)` and `[0xB00000,0x1300000)`, while all five extras are exact 1 MiB regions outside that parent representation.
88. **Candidate source operation:** `init_heap_segment`, `return_free_region`, `get_free_region`, and `region_free_list::get_region_kind` are the narrowed source candidates; no single split operation was runtime-proven.
89. **Source file:** `src/coreclr/gc/gc.cpp` in the locked runtime source.
90. **Source function:** `get_region_info_for_address`, with list publication/removal in `gc_heap::return_free_region` and `gc_heap::get_free_region`.
91. **Operation semantics:** canonical lookup indexes the region map and walks negative interior offsets; free publication inserts the descriptor and clears basic map slots; list kind is derived from `get_region_size`.
92. **Narrow runtime hook used:** no provenance/event hook beyond the six-slot C83 mapping capture; no new scalar hook was needed.
93. **Narrow hook result:** authenticated lookup status `1` for every entry, exact descriptor/range equality for all 7 captured rows, no hook failures.
94. **Representation mechanism classification:** Outcome C — final basic-free nodes are literal exact canonical basic descriptors, not subranges of the C80 large candidates.
95. **First supported causal link:** each observed basic-list `heap_segment*` start resolves through `get_region_info_for_address` to the same descriptor and identical `[start,end)` extent.
96. **Strongest causal chain:** `free_regions[basic_free_region]` node → `get_region_start`/`heap_segment_reserved` → address-map lookup → same descriptor, same size, offset zero → `EXACT_CANONICAL`.
97. **First unsupported causal link:** why SIX materializes/exposes five additional exact basic descriptors while ONE does not; that is historical/production causality for C84.
98. **Large-range causal relevance:** C80's two 8 MiB records are not the containing representations of the accepted SIX extras; their relevance to the final basic list is falsified.
99. **Descriptor-reuse causal relevance:** plausible from `init_heap_segment` and free-list reuse, but not runtime-proven by C83.
100. **Basic-list projection relevance:** source-level projection is possible for larger mapped regions, but the accepted C83 final list does not use that representation.
101. **Candidate downstream relevance:** none; C83 is upstream of candidate selection and only observes the range representation.
102. **B02 evaluated:** no.
103. **B02 future justification:** still premature; first trace the earliest materialization of the exact canonical entries in C84.
104. **Production mutation:** none.
105. **Allocator mutation:** none.
106. **Region mutation:** none.
107. **Split forcing:** none.
108. **Descriptor mutation:** none.
109. **Region-list mutation:** none.
110. **Planner mutation:** none.
111. **Candidate mutation:** none.
112. **Survivor fabrication:** none.
113. **Root fabrication:** none.
114. **C18:** preserved; valid `CoffNativeCodeManager` path.
115. **Code manager:** preserved.
116. **`FindMethodInfo`:** preserved.
117. **Root scan:** authentic.
118. **Mark closure:** authentic.
119. **Planner authenticity:** authentic.
120. **Survivor integrity:** preserved.
121. **C83 invariant failures:** `0`.
122. **Sensitive diagnostic allocations:** `0`.
123. **C83 event capacity used:** `6` reused C67 slots; SIX accepted runs use all six slots, ONE uses one.
124. **C83 event count:** ONE `1`; SIX `6` per accepted boot.
125. **C83 overflow:** `0`.
126. **Fail-fast:** `0`.
127. **Page faults:** `0`.
128. **ONE Boot 1:** pass; `1/1`, promotion authentic, C83 mapping clean.
129. **ONE Boot 2:** pass; `1/1`, same normalized mapping.
130. **ONE Boot 3:** pass; `1/1`, same normalized mapping.
131. **SIX Boot 1:** pass; `6/6`, promotion authentic, six exact mappings.
132. **SIX Boot 2:** pass; `6/6`, same normalized mapping.
133. **SIX Boot 3:** pass; `6/6`, same normalized mapping.
134. **Semantic agreement:** yes across 3/3 ONE and 3/3 SIX; counts, classes, parent grouping, offsets, and roles agree.
135. **Nondeterminism:** no semantic nondeterminism; raw pointers are observational and excluded from agreement.
136. **Serial hashes:** ONE `C3A577D48C49F26D2D6F30E8E2725203D933DF9C517A4EAEA534365ED52CA2EB`, `C1C59E0F284A7E49AE16E265B84F00C8BCA548F555A8C27F1FE9F385F93C3E51`, `055D3216AEAA2D8D8EF39838E25D0D12DC2190203CF4F7CE460B9DAF3C324843`; SIX `8AE75C84207961A33087921026E426D66ECD68C2FBE724E39E4DFF9D0542BFC5`, `7A843AA9724F4E8432FA47EE7E873DBBF11117124B3D764A609F45C7AD56BBA5`, `92CBB2509856CE5D2A026C2EFD00268E7C88847F9A2DB27D5B645CB929650522`.
137. **Artifact hashes:** accepted ONE baseline/C83 kernel `245216DE5993C7BC09CE532A4D634FC4507C9D97D911E3D5543F6E268E492D5F` / `A9AC2744A19F2F5A4484DD63C7C6C65C823B02FE3758BC9706DA1CFAE6CB5535`; SIX `9961930AF33BB4DC28D9E29D5FE327593DC9D3CE33F01DCDE9391C3829EB8198` / `9B5532BB1200EDBFC5D2610ED4EEA298FDDD54827F85923E7CA0DF642123D2DB`; PE/ELF/map hashes are recorded in `layout-accounting.json` under the evidence root.
138. **Runtime-pack validation:** pass through the locked C68/C51 runtime-pack manifest and the C83 smoke runtime-pack/archive/link phases.
139. **Managed build:** pass for the accepted ONE and SIX NativeAOT managed builds.
140. **Native build:** pass for the accepted ONE and SIX native kernel builds.
141. **PowerShell syntax:** pass for the modified smoke harness and C83 analyzer.
142. **JSON/XML parse:** pass for C83 manifests, generated JSON, runtime manifest, and the managed project XML; no C83 XML artifact was emitted.
143. **`git diff --check`:** pass.
144. **PE → ELF conversion:** pass; the accepted run logs record successful custom-entry conversion.
145. **Symbol checks:** pass; kernel/native-helper symbol and unwind audits were emitted and passed by the harness.
146. **Linker/source/table/archive guards:** pass; stale-artifact, source-rewrite, archive-membership, linker, and table guards passed.
147. **C52 Tier-All result or reason omitted:** full Tier-All was omitted; C83 is a bounded diagnostic-only mapping probe using the already validated targeted C68/C51/C52 preconditions.
148. **Ordinary restoration:** pass; the harness restored the ordinary kernel and ESP in `finally` and verified both hashes.
149. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
150. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
151. **Proof artifact active:** no; proof-only artifacts remain under the evidence root and the ordinary runtime image is active/restored.
152. **C83-owned QEMU cleanup:** zero C83-owned QEMU processes remained after cleanup.
153. **Unrelated QEMU preservation:** unrelated QEMU was not intentionally terminated; cleanup was scoped to C83-owned command lines/evidence roots.
154. **Files changed:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `scripts/dotnet/Invoke-C011EC83LayoutStableBasicCanonicalMapping.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this documentation file.
155. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C83_LAYOUT_STABLE_BASIC_CANONICAL_MAPPING.md`.
156. **Evidence root:** `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/`, separated into `layout-gate-failures`, `discovery`, `accepted-ONE-SIX-confirmation`, and `offline-mapping`.
157. **Final commit:** local commit with subject `Map NativeAOT basic regions without layout drift`; exact SHA is returned by `git rev-parse HEAD` in the final handoff.
158. **Push status:** not pushed.
159. **Remaining limitation:** C83 does not establish when or why SIX exposes the five additional exact descriptors, nor whether an earlier lifecycle reused or split them.
160. **Exact next-smallest milestone:** C84 should abandon the large-parent theory and trace the earliest materialization of SIX's five additional exact canonical basic entries, preserving the six-slot/no-new-BSS architecture; B02 remains deferred.

## Evidence pointers

- Offline analysis: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/c83-analysis.json`
- Layout accounting: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/layout-accounting.json`
- Mapping table: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/offline-mapping/comparison-table.csv`
- Parent grouping: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/offline-mapping/parent-grouping.json`
- Conservation: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/offline-mapping/conservation.json`
- Source audit: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/source-audit.json`
- Accepted ONE manifest: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/accepted-confirmation/ONE/run-20260906-070838678/manifest.json`
- Accepted SIX manifest: `out/dotnet/c011ec83-layout-stable-basic-canonical-mapping/accepted-confirmation/SIX/run-20260906-071224671/manifest.json`

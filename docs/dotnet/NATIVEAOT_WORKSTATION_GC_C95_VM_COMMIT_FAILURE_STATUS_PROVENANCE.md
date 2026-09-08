# guideXOS NativeAOT Workstation GC C95 — VM commit failure status provenance

## Result

C95 crosses the boundary left open by C94. The production path is:

```text
gc_heap::grow_heap_segment
 -> gc_heap::virtual_commit
 -> virtual_alloc_commit_for_heap
 -> GCToOSInterface::VirtualCommit
 -> gcVirtualCommit
 -> guidexos_nativeaot_gc_commit
 -> startupCommit
 -> gxos::runtime::virtual_memory::commit
 -> kernel::memory::address_space::allocateFrame
```

At the decisive T320 tail:80 event, the request is a valid, aligned `0x10000` range. The actual primitive returns `VmResult::OutOfMemory (0x7)` because `allocateFrame(FrameOwner::VmRegion)` returns zero while the page loop is obtaining backing frames. The primitive rolls back and leaves its committed extent unchanged. The existing VM diagnostic is `physical frame allocation exhausted during commit`.

The nearest directly captured T216 successful `+0x10000` grow reaches the same production primitive and returns `VmResult::Ok (0x0)`, but it is a different segment-relative range and reservation extent. C95 therefore classifies the comparison as `DIFFERENT_RANGE` plus `DIFFERENT_RESERVATION`, not equivalent operands with different results. This is Outcome B / physical backing allocation failure at Success Level 2. A same-image experiment is deferred to C96.

C92 found the tail:80 collection selector. C93 proved that the trigger was a `0x10000` committed-boundary difference. C94 proved that `grow_heap_segment` writes the boundary and that `0x10000` is the runtime minimum commit quantum. C95 now identifies the VM primitive and exposes its authentic failure status without changing production VM or GC behavior.

Evidence root: `out/dotnet/c011ec95-vm-commit-failure-status-provenance/`

## Normalized request comparison

| Operand | T320 failed grow | T216 direct successful grow |
| --- | ---: | ---: |
| Requested VA | `0x1018C1000` | `0x101301000` |
| Segment start | `0x101800028` | `0x101300028` |
| Segment-relative offset | `0xC0FD8` | `0xFD8` |
| Requested size | `0x10000` | `0x10000` |
| Page-aligned size | `0x10000` | `0x10000` |
| Current committed end | `0x1018C1000` | `0x101301000` |
| Desired committed end | `0x1018D1000` | `0x101311000` |
| Reserved end | `0x101900000` | `0x101400000` |
| Reserved bytes remaining | `0x3F000` | `0xFF000` |
| Page size | `0x1000` | `0x1000` |
| Primitive status | `OutOfMemory (0x7)` | `Ok (0x0)` |

The accepted C94 T216 tail boundary remains `0x1018D1000` with fit span `0x106D8`; the direct C95 comparator is the nearest successful grow whose call and return are captured directly.

## Numbered report

1. **Outcome:** Outcome B — physical backing allocation failure; the T320/T216 comparison is also `DIFFERENT_RANGE` / `DIFFERENT_RESERVATION`.
2. **Success Level:** Level 2. The low-level primitive, exact T320 status, branch, operands, direct T216 success, and 3/3 stability are authenticated; Level 3 is not claimed because the normalized VM pre-states are not equivalent.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `333589d37842c058ee61a88ee2dbc48d014f6c31`.
6. **Starting subject:** `Trace NativeAOT heap segment commit growth`.
7. **Final HEAD:** local C95 closeout commit recorded by Git at closeout; no remote update.
8. **Final subject:** `Trace NativeAOT VM commit failure`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0/0`.
11. **Final ahead/behind:** `1/0`.
12. **Starting worktree:** dirty, with the three in-progress C95 files: `kernel/core/nativeaot_pal_qemu_test.cpp`, `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, and `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.
13. **Final worktree:** clean.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C93 SHA:** `9d991de775d6dd054eaf92459c622bf4fc04d0d1`.
18. **C94 SHA:** `333589d37842c058ee61a88ee2dbc48d014f6c31`.
19. **C95 SHA:** the final local commit SHA is the closeout value reported in item 193 and the final handoff; this report intentionally avoids a self-referential hash.
20. **Exact C95 question:** At the decisive T320 `grow_heap_segment` failure, what exact VM commit request is issued, what exact low-level commit primitive processes it, and why does that primitive reject/fail the request; then compare it with the nearest successful T216 `+0x10000` commit.
21. **`grow_heap_segment` source file:** `out/dotnet/c52-runtime-source/source-04371d8e/src/coreclr/gc/gc.cpp:15465`.
22. **Low-level VM helper:** `gc_heap::virtual_commit` → `virtual_alloc_commit_for_heap` in `gc.cpp`, followed by `GCToOSInterface::VirtualCommit`.
23. **Platform VM primitive:** `gxos::runtime::virtual_memory::commit(VirtualMemoryRegion&, offset, size, MemoryProtection)`.
24. **Kernel/OS primitive:** guideXOS `address_space::allocateFrame`, `zeroFrame`, `mapPage`, and page-flag updates; there is no host OS commit syscall in this bare-metal path.
25. **Commit primitive return semantics:** `VmResult`; `Ok` commits all pages, failure rolls back tentative work. The startup adapter returns `0` for `Ok` and negative enum value for failure; GC preserves only boolean success/failure.
26. **Existing failure/status representation:** the existing `VmResult` enum and existing VM last-diagnostic string; previously the startup adapter discarded the specific enum and returned `-1`.
27. **New diagnostic status added:** no synthetic status system; C95 reuses the existing enum/diagnostic and adds one bounded primitive marker plus existing sideband scalar fields.
28. **T320 commit coordinate:** tail:80, allocation ordinal `0x93`, decisive failed `grow_heap_segment` request.
29. **T216 comparator coordinate:** nearest directly captured successful `branch=4` `+0x10000` grow; C94’s accepted T216 tail boundary is retained separately.
30. **T320 requested VA:** `0x1018C1000`.
31. **T216 requested VA:** `0x101301000` for the direct C95 comparator.
32. **T320 normalized commit offset:** segment-relative `0xC0FD8`; reservation-relative `0x18C1000` using the accepted `0x100000000` reservation base.
33. **T216 normalized commit offset:** segment-relative `0xFD8`; reservation-relative coordinate is not asserted because the direct comparator’s reservation base is not independently serialized.
34. **T320 requested size:** `0x10000`.
35. **T216 requested size:** `0x10000`.
36. **T320 aligned size:** `0x10000`.
37. **T216 aligned size:** `0x10000`.
38. **T320 segment start:** `0x101800028`.
39. **T216 segment start:** `0x101300028` for the direct comparator.
40. **T320 current committed end:** `0x1018C1000`.
41. **T216 current committed end:** `0x101301000` for the direct comparator; C94’s accepted tail boundary is `0x1018D1000`.
42. **T320 reserved end:** `0x101900000`.
43. **T216 reserved end:** `0x101400000` for the direct comparator.
44. **T320 reserved bytes remaining:** `0x3F000`.
45. **T216 reserved bytes remaining:** `0xFF000` for the direct comparator.
46. **T320 range valid:** yes: `0x101800028 <= 0x1018C1000` and `0x1018D1000 <= 0x101900000`.
47. **T216 range valid:** yes: `0x101300028 <= 0x101301000` and `0x101311000 <= 0x101400000`.
48. **T320 address aligned:** yes, page aligned.
49. **T216 address aligned:** yes, page aligned.
50. **T320 size aligned:** yes, `16 * 0x1000`.
51. **T216 size aligned:** yes, `16 * 0x1000`.
52. **T320 overlap/already-committed condition:** false; request begins exactly at the current committed end.
53. **T216 overlap/already-committed condition:** false; request begins exactly at the current committed end.
54. **T320 VM bookkeeping state:** primitive marker: `committedBefore=0x7C9000`, `committedAfter=0x7C9000`, `reservedSize=0x4000000`, `freeFrames=1`, `allocatedFrames=0xFFF`, `regionOwnedFrames=0xFF1`; status `OutOfMemory`.
55. **T216 VM bookkeeping state:** valid region and successful 16-page grow; full free/allocated-frame counters were not serialized for this success, so equality with T320’s pre-state is not proven.
56. **T320 primitive invoked:** yes, `gxos::runtime::virtual_memory::commit`.
57. **T216 primitive invoked:** yes, the same production primitive, evidenced by the direct successful grow result through the existing startup contract.
58. **T320 primitive result:** `VmResult::OutOfMemory (0x7)`, raw startup result `-7`, GC result false.
59. **T216 primitive result:** `VmResult::Ok (0x0)`, raw startup result `0`, GC result true.
60. **T320 exact status code:** `0x7`.
61. **T216 exact status code:** `0x0`.
62. **T320 exact failure branch:** `allocateFrame(FrameOwner::VmRegion) == 0` in the page loop; rollback then returns `VmResult::OutOfMemory`.
63. **T320 failure predicate:** a required physical frame allocation returns zero.
64. **T320 predicate operands:** 16 pages requested, one tentative frame obtained, next frame result zero; post-rollback counters show one free frame and unchanged committed extent.
65. **T216 corresponding predicate operands:** same page quantum and valid range, with all 16 frame allocations/mappings completing sufficiently for `VmResult::Ok`; per-page values were not serialized.
66. **Same normalized request shape:** no. Both are valid 16-page grows, but segment-relative offsets and reservation extents differ.
67. **Same request size:** yes, `0x10000`.
68. **Same reservation state:** no; both are valid, but remaining reserved extent is `0x3F000` versus `0xFF000` in the direct comparator.
69. **Same committed pre-state:** no; the addresses, segment-relative locations, and direct comparator’s committed boundary differ.
70. **Same VM bookkeeping pre-state:** not established; T216’s full frame counters were not captured.
71. **Request equivalence classification:** `DIFFERENT_RANGE` and `DIFFERENT_RESERVATION`; not `SAME_OPERANDS_DIFFERENT_RESULT`.
72. **Failure class:** `PHYSICAL_RESOURCE` / Outcome B.
73. **Commit all-or-nothing:** yes at the externally visible primitive contract.
74. **Partial commit possible:** tentative page work can occur internally, but rollback prevents a partial committed result; T320 reports actual `0` and unchanged committed end.
75. **T320 pages requested:** 16.
76. **T320 pages eligible:** one page is known to be tentatively eligible before the first inferred failing allocation; no page is published after rollback.
77. **T320 first failing page/offset:** page index 1 / offset `0x1000` is inferred from one free frame after rollback; the exact page index was not independently serialized.
78. **T216 pages requested:** 16.
79. **T216 pages committed:** 16 for the direct successful comparator, implied by `Ok` and the committed-end advance of `0x10000`.
80. **Physical backing involved:** yes; frame allocation is the failing primitive branch.
81. **Physical allocation result T320:** first needed frame nonzero, next needed frame zero; status `OutOfMemory`.
82. **Physical allocation result T216:** all 16 required allocations succeed sufficiently for the successful commit; individual frame handles are not serialized.
83. **VM mapping involved:** yes; successful pages are zeroed and mapped, and T320’s tentative work is rolled back.
84. **Mapping result T320:** no final mapping/committed metadata remains for the failed request; any tentative first-page work is rolled back.
85. **Mapping result T216:** all 16 pages map successfully for the direct comparator.
86. **Hard commit limit involved:** no; `growGateHardLimit=0`, and no GC hard-limit gate was observed.
87. **Commit budget T320:** no policy commit budget; the primitive’s physical-frame pool is exhausted for the request, with `freeFrames=1` after rollback and `allocatedFrames=0xFFF`.
88. **Commit budget T216:** no policy budget was reported; the successful primitive completed its 16-page request.
89. **Tail80 is first T320 failure:** yes in the bounded target chronology.
90. **Earlier T320 failure exists:** no earlier failed grow is present in the accepted chronology; later failures are downstream/unrelated to the decisive first failure.
91. **Earliest divergent commit coordinate:** T320 tail:80 / allocation ordinal `0x93`.
92. **Earliest divergent raw ordinal:** T320 `0x93`; T216’s direct sideband grow has no raw allocation ordinal (its sideband event ordinal is zero).
93. **Earliest T320 commit request:** `0x1018C1000`, size `0x10000`, segment-relative offset `0xC0FD8`.
94. **Earliest T216 comparator:** `0x101301000`, size `0x10000`, segment-relative offset `0xFD8`.
95. **Earliest T320 result:** `VmResult::OutOfMemory (0x7)`, actual commit `0`.
96. **Earliest T216 result:** `VmResult::Ok (0x0)`, actual commit `0x10000`.
97. **First differing VM operand:** physical-frame allocation result, with range/reservation state also concretely different.
98. **Operand source:** `kernel::memory::address_space::allocateFrame` called by `virtual_memory::commit`.
99. **T320 operand value:** one tentative frame allocation succeeds, then the next returns zero; diagnostic says physical frame allocation exhausted.
100. **T216 operand value:** all 16 required frame allocations complete for the direct successful comparator; exact free-frame count is not serialized.
101. **One-step upstream production event:** the tail selector produces a valid requested high address whose committed gap is `0x10000`; the VM primitive then encounters physical-frame exhaustion.
102. **One-step upstream source function:** `gc_heap::grow_heap_segment` creates the request; `gxos::runtime::virtual_memory::commit` consumes it and branches on `allocateFrame`.
103. **C94 `commit_min_th=0x10000` preserved:** yes.
104. **C94 pointer contribution zero preserved:** yes.
105. **C94 boundary contribution `0x10000` preserved:** yes.
106. **T320 tail80 fit span:** `0x6D8`.
107. **T216 tail80 fit span:** `0x106D8`.
108. **T320 shortfall:** `0x3958`.
109. **T216 margin:** `0xC6A8`.
110. **T320 `commit_failed_p`:** `TRUE`.
111. **T216 `commit_failed_p`:** `FALSE`.
112. **Full-compact trigger preserved:** yes.
113. **Collection4 preserved:** yes for T320.
114. **T320 final `1/1/1`:** preserved.
115. **T216 final `1/6/6`:** preserved.
116. **T320 `0x1000` endpoint:** preserved in the C94 downstream endpoint ledger.
117. **T216 `0x21000` endpoint:** preserved in the C94 downstream endpoint ledger.
118. **EXTRA4 downstream OOM/decommit path:** preserved; it occurs after the VM commit failure and is not used as the VM cause.
119. **Strongest causal chain:** T320 tail:80 → `grow_heap_segment` requests `0x10000` → valid range reaches `virtual_memory::commit` → `allocateFrame` returns zero → rollback / `VmResult::OutOfMemory` → grow false → `soh_try_fit` failure → `commit_failed_p=TRUE` → full compact → collection 4 / `1/1/1`.
120. **First unsupported causal link:** the provenance of the differing physical-frame availability from the runtime/image/layout inputs remains open for C96.
121. **Binary/layout causality evaluated:** yes, against the required complete bridge.
122. **Binary/layout status:** `UNPROVEN`.
123. **Exact layout→VM bridge if proven:** none; PE/ELF and symbol identity do not establish frame-pool availability causality.
124. **Same-image experiment justified:** no; the normalized ranges/reservation states are not equivalent and T216’s complete VM pre-state is not known.
125. **Same-image experiment performed:** no.
126. **Same-image experiment deferred to C96:** yes.
127. **Candidate-path relevance:** none; the VM commit trace is upstream of candidate selection and no candidate-path mutation was made.
128. **B02 evaluated:** yes, as a scope gate.
129. **B02 status:** `STILL_PREMATURE`; B02 was not run.
130. **C95 baseline BSS:** kernel `0x062A8770`; per-cell ONE `0x4F0840`; per-cell SIX `0x3947E0`.
131. **C95 BSS:** unchanged from the baseline values.
132. **BSS delta:** `0`.
133. **Production mutation:** none.
134. **VM mutation:** none.
135. **Commit mutation:** none; only status transport and observation were added.
136. **Reservation mutation:** none.
137. **Physical-page mutation:** none.
138. **Mapping mutation:** none.
139. **Grow mutation:** none to behavior; existing source is observed through bounded sideband instrumentation.
140. **Context mutation:** none.
141. **Allocator mutation:** none.
142. **Collection forcing:** none.
143. **Region mutation:** none.
144. **Free-list mutation:** none.
145. **OOM mutation:** none.
146. **Decommit mutation:** none.
147. **Planner mutation:** none.
148. **Candidate mutation:** none.
149. **Promotion mutation:** none.
150. **Survivor fabrication:** none.
151. **Root fabrication:** none.
152. **C18:** preserved and passed.
153. **Code manager:** preserved and authentic.
154. **`FindMethodInfo`:** preserved and authentic.
155. **Root scan:** authentic root scan preserved.
156. **Mark closure:** authentic mark closure preserved.
157. **Planner authenticity:** preserved.
158. **Survivor integrity:** preserved.
159. **C95 invariant failures:** `0` for C95-owned semantic invariants; any retained historical C65 ledger accounting is not attributed to C95.
160. **Sensitive diagnostic allocations:** `0`.
161. **C95 overflow:** `0`.
162. **Fail-fast:** `0`.
163. **Page faults:** `0`.
164. **T320 Boot 1:** PASS — `0x6D8`, failed `0x10000`, `OutOfMemory (0x7)`, `1/1/1`.
165. **T320 Boot 2:** PASS — same semantic result.
166. **T320 Boot 3:** PASS — same semantic result.
167. **T216 Boot 1:** PASS — direct `Ok (0x0)` `+0x10000`, accepted fit `0x106D8`, `1/6/6`.
168. **T216 Boot 2:** PASS — same semantic result.
169. **T216 Boot 3:** PASS — same semantic result.
170. **Semantic agreement:** yes, both cases are 3/3 stable.
171. **Nondeterminism:** none observed in the required semantic fields.
172. **Serial hashes:** T320 `9D1A3168A3850BF5FFA644A1BA6FAE991BD0E578D3860F79DCFF384F54537D78`, `65453C2182E93AB3AC024FC405233CCB89A616E6F058FAE8D1CCE913EF595041`, `52D870C4958F2C70D6AFCA769C255F75757FFFD57A135AFAEB5FD3A6B1A268E8`; T216 `0DD4FFC78FBF70EA0DCA098621F733173496800F0FCD1F2845E8EE1C40F72472`, `5C0975CAC3FA7323A6E36A81647C3D681A015414EA7C56FC4E30DE6263EDBB26`, `23948210C871A2B62F9AF40136D981CE3DD1EED819E078A4A44AB8CBF77582F1`.
173. **Artifact hashes:** proof-kernel hashes recorded by the final manifests are T320 `656E6F3E113A14E09AE080EAC2B4310EDA870529135623A3B8C861DF8878FD04` and T216 `251A0AE46B8D2CB27B3A339F7D88E7FBBCAF9111E326E9458565F11DF91C2FCB`; serial hashes are item 172.
174. **Runtime-pack validation:** PASS in the final run manifests/logs.
175. **Managed build:** PASS.
176. **Native build:** PASS.
177. **PowerShell syntax:** PASS.
178. **JSON parse:** PASS for final manifests and validation records.
179. **`git diff --check`:** PASS.
180. **PE → ELF conversion:** PASS.
181. **Symbol checks:** PASS.
182. **Linker/source/table/archive guards:** PASS.
183. **C52 Tier-All result or reason omitted:** omitted because C95 is a narrow upstream VM-status observer and the dedicated 3/3 T320/T216 confirmations are semantically appropriate.
184. **Ordinary restoration:** PASS after each final run.
185. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
186. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
187. **Proof artifact active:** no; proof inactive at closeout.
188. **C95-owned QEMU cleanup:** count `0`.
189. **Unrelated QEMU preservation:** preserved.
190. **Files changed:** `kernel/core/nativeaot_pal_qemu_test.cpp`, `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this documentation file; bounded evidence is under the evidence root.
191. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C95_VM_COMMIT_FAILURE_STATUS_PROVENANCE.md`.
192. **Evidence root:** `out/dotnet/c011ec95-vm-commit-failure-status-provenance/`.
193. **Final commit:** local commit with subject `Trace NativeAOT VM commit failure`; the exact SHA is emitted by the final Git closeout and handoff.
194. **Push status:** not pushed.
195. **Remaining limitation:** C95 isolates the ordinary physical-frame exhaustion status and proves the direct comparator succeeds, but does not yet explain why T320 and T216 have different normalized ranges and physical-frame availability from their upstream image/workload provenance.
196. **Exact next-smallest milestone:** trace only the `allocateFrame`/free-frame operand one step upstream for an equivalent normalized reservation role; if equivalent VM preconditions can then be established, perform the deferred C96 runtime-selectable 216/320 same-image experiment. Do not run B02.

## Closeout state

The local branch is one commit ahead of `origin/v1.1_DOTNET_SUPPORT`, the worktree is clean, and nothing was pushed. Ordinary kernel/ESP artifacts are restored. C95 is observation-only: no retry, reservation, mapping, physical-page, grow, GC, allocator, collection, region, free-list, OOM, decommit, planner, candidate, promotion, survivor, or root behavior was changed.

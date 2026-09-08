# C94 — NativeAOT `grow_heap_segment` commit-growth operand provenance

## Result

C94 closes the C93 harness-acceptance mismatch and confirms the existing
T320/T216 semantic split in three fresh boots per cell. The accepted result is
Outcome D with a decisive T320 current failure: T216 has retained one earlier
successful `0x10000` commit quantum, while T320 reaches tail:80 with the lower
boundary and asks `grow_heap_segment` for an effective `0x10000` extension that
does not succeed. The source contract proves why the quantum is exactly
`0x10000`; the accepted observer does not serialize the earlier T216 call's
host status, so Level 3 is not claimed.

Progression: C91 corrected chronology; C92 isolated `commit_failed_p` at
tail:80; C93 proved the fit difference is entirely a `0x10000` committed-
boundary difference written by `gc_heap::grow_heap_segment`; C94 repairs the
validator composition and reproduces the semantic gates 3/3.

## Numbered closeout report

1. **Outcome:** Outcome D — T216 had an earlier successful `0x10000` grow; T320's decisive tail:80 grow fails. Acceptance closure is also Outcome A.
2. **Success Level:** Level 2 — exact source operands, rounding, retained boundary, and current result are isolated; Level 3 is not claimed because the earlier T216 grow call is not directly serialized.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `9d991de775d6dd054eaf92459c622bf4fc04d0d1`.
6. **Starting subject:** `Trace NativeAOT committed fit boundary`.
7. **Final HEAD:** local C94 commit; exact SHA is recorded by the final Git closeout and final response.
8. **Final subject:** `Trace NativeAOT heap segment commit growth`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `ahead 0 / behind 0` (live state; the supplied expectation of ahead 1 was not authoritative).
11. **Final ahead/behind:** `ahead 1 / behind 0` after the local C94 commit.
12. **Starting worktree:** clean.
13. **Final worktree:** clean after the local commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`, `net9.0`, `win-x64`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C92 SHA:** `185f732ec9e251e4549927dba254a013097ba1e3`.
18. **C93 SHA:** `9d991de775d6dd054eaf92459c622bf4fc04d0d1`.
19. **C94 SHA:** local final commit SHA; see item 191 and Git closeout.
20. **Exact C94 question:** What exact `grow_heap_segment` input/result creates T216's extra `0x10000` committed room at tail:80, and where does the differing input first arise?
21. **C93 legacy-validator failure condition:** strict legacy C89 T320 validation required a historical `C011EC88-EVENT` target record and observed zero after the C93 bounded fit marker.
22. **Validator failure runtime-semantic or harness-only:** harness-only; runtime had already emitted the C93 marker and retained safety/topology markers, with no proof of a runtime failure at the rejection point.
23. **Validator repair:** explicit C94 proof-mode parser accepts `C011EC93-FIT-BOUNDARY`, retains C64/C65/C77/C89 checks, and uses the T216 C93 record when no C89 region-acquisition pair exists.
24. **Historical controls weakened:** no global controls weakened; only explicit C94 composition recognizes C93 as the bounded fit proof in place of the absent legacy C88 target count.
25. **T320 gate after repair:** PASS, stable 3/3: demand `0x4030`, fit span `0x6D8`, shortfall `0x3958`, `commit_failed_p=TRUE`, downstream `1/1/1` and collection 4.
26. **T216 gate after repair:** PASS, stable 3/3: demand `0x4030`, fit span `0x106D8`, margin `0xC6A8`, `commit_failed_p=FALSE`, downstream `1/6/6` and collection 3.
27. **`grow_heap_segment` source file:** `out/dotnet/c52-runtime-source/source-04371d8e/src/coreclr/gc/gc.cpp`.
28. **`grow_heap_segment` signature:** `BOOL gc_heap::grow_heap_segment(heap_segment* seg, uint8_t* high_address, bool* hard_limit_exceeded_p)` at source line 15465.
29. **Grow request semantic unit:** desired committed end (`high_address`), not bytes.
30. **Grow result semantics:** `TRUE` if already committed or if commit succeeds; `FALSE` leaves the committed boundary unchanged.
31. **Page size:** `0x1000` (`OS_PAGE_SIZE` / `GC_PAGE_SIZE`).
32. **Commit quantum if any:** `commit_min_th = 16 * 0x1000 = 0x10000`.
33. **T320 tail80 allocation pointer:** `0x1018C0910` (normalized offset `0xC08E8`).
34. **T216 tail80 allocation pointer:** `0x1018C0910` (normalized offset `0xC08E8`; C93 object address minus `0x18`).
35. **T320 current committed end:** `0x1018C1000`.
36. **T216 current committed end:** `0x1018D1000`.
37. **T320 committed offset:** `0xC0FD8` from segment start `0x101800028`.
38. **T216 committed offset:** `0xD0FD8` from segment start `0x101800028`.
39. **T320 desired fit end:** `0x1018C4940`, source-equivalent `allocated + limit` target.
40. **T216 desired fit end:** `0x1018C4940` source-equivalent target, already below the retained `0x1018D1000` boundary.
41. **T320 shortfall:** `0x3958`.
42. **T216 margin/shortfall:** margin `0xC6A8`; shortfall `0`.
43. **T320 raw grow request:** `0x3958` bytes of high-address delta before rounding.
44. **T216 raw grow request:** no tail:80 request; earlier corresponding source-equivalent delta `0x3958`, call record not serialized.
45. **T320 aligned grow request:** `0x10000` after `0x4000` page rounding and `commit_min_th`.
46. **T216 aligned grow request:** no tail:80 commit; earlier retained extension `0x10000`.
47. **T320 reserved end:** `0x101900000`.
48. **T216 reserved end:** `0x101900000` source-derived common segment reserve end.
49. **T320 reserved extent remaining:** `0x3F000`.
50. **T216 reserved extent remaining:** `0x2F000` after the retained extra quantum.
51. **Grow invoked T320:** yes, at the decisive tail:80 fit path.
52. **Grow invoked T216:** no at tail:80; yes for the earlier retained extension, inferred from committed state and C93 source audit.
53. **Grow invocation coordinate T320:** tail:80 / raw ordinal `0x93` / decimal `147`.
54. **Grow invocation coordinate T216:** earlier event not directly serialized; tail:80 is the retained-boundary observation.
55. **T320 committed boundary before grow:** `0x1018C1000`.
56. **T216 committed boundary before prior grow:** `0x1018C1000` in the C93 source-derived decomposition; direct event capture is absent.
57. **T320 grow result:** `FALSE`; no successful extension retained; fit reports `commit_failed_p=TRUE`.
58. **T216 grow result:** prior `TRUE`; `0x10000` retained; tail:80 fit returns true without another commit.
59. **T320 committed boundary after grow:** `0x1018C1000`.
60. **T216 committed boundary after prior grow:** `0x1018D1000`.
61. **Exact `0x10000` semantic meaning:** minimum commit quantum, not coincidence.
62. **`0x10000` is commit quantum:** yes, `commit_min_th`.
63. **`0x10000` from current grow or prior grow:** T216 retained it before tail:80; T320's tail:80 attempt is current and fails.
64. **Commit primitive:** `GCToOSInterface::VirtualCommit`, normally Win32 `VirtualAlloc(..., MEM_COMMIT, PAGE_READWRITE)`.
65. **Commit primitive source file:** `src/coreclr/gc/windows/gcenv.windows.cpp`, function `GCToOSInterface::VirtualCommit` around line 753.
66. **T320 commit address:** `0x1018C1000`.
67. **T216 commit address:** direct prior address not serialized; source-derived immediate predecessor address `0x1018C1000`.
68. **T320 commit size:** `0x10000`.
69. **T216 commit size:** prior retained extension `0x10000`.
70. **T320 primitive result:** false at the boundary contract; no boundary update.
71. **T216 primitive result:** true for the prior extension by retained boundary and stable fit result.
72. **Commit status/reason available:** boolean primitive result plus optional hard-limit flag; no portable host status captured.
73. **T320 status/reason:** not exposed; hard-limit status is not asserted.
74. **T216 status/reason:** no prior host status exposed; successful boolean inferred from retained boundary.
75. **First equal committed-boundary checkpoint:** shared prefix before tail:1, per imported C91/C92 chronology.
76. **Last equal committed-boundary checkpoint:** tail:79 at available normalized granularity.
77. **First differing checkpoint:** tail:80 / ordinal `0x93`.
78. **First differing normalized coordinate:** tail:80 / committed offset `0xC0FD8` versus `0xD0FD8`.
79. **First differing raw ordinal:** `0x93` / `147`.
80. **Current segment T320:** current gen0 supplying segment at normalized start `0x101800028`.
81. **Current segment T216:** same normalized gen0 supplying segment role and start `0x101800028`.
82. **T320 old committed offset:** `0xC0FD8`.
83. **T216 old committed offset:** `0xC0FD8` before source-derived prior extension; direct event not serialized.
84. **T320 requested new offset:** `0xD0FD8` for the effective `0x10000` attempt.
85. **T216 requested new offset:** `0xD0FD8` for the earlier successful extension.
86. **T320 actual new offset:** `0xC0FD8`.
87. **T216 actual new offset:** `0xD0FD8`.
88. **Same writer executes both:** yes, `gc_heap::grow_heap_segment`.
89. **Same grow request:** same source-equivalent desired-fit delta for the corresponding extension; not a simultaneous tail:80 call on both sides.
90. **Same reserve extent:** yes, normalized reserved end offset `0xFFFD8`.
91. **Different commit result:** yes, T320 current false versus T216 prior success; not a same-call equivalent-operand comparison.
92. **Writer-only-one-side:** at tail:80, T320 only; T216's writer event is earlier.
93. **Prior successful grow on T216:** yes, source-derived and authenticated by retained `0x1018D1000` plus 3/3 fit result.
94. **Prior failed grow on T320:** no separate prior failure proven; decisive tail:80 attempt fails.
95. **First upstream differing operand:** `heap_segment_committed(seg)` entering the tail:80 fit/grow decision.
96. **Operand source:** `gc_heap::grow_heap_segment` reads `heap_segment_committed(seg)`; prior commit result produces its value.
97. **T320 operand:** `0x1018C1000` committed end.
98. **T216 operand:** `0x1018D1000` at tail:80, after retained prior quantum.
99. **One-step upstream event:** prior segment commit state: T216 retained one quantum; T320 did not.
100. **One-step upstream source function:** `gc_heap::grow_heap_segment`, called from `a_fit_segment_end_p` for fit extension.
101. **Pointer contribution:** `0`.
102. **Boundary contribution:** `+0x10000`.
103. **C93 `0x10000` decomposition preserved:** yes; equal normalized pointer, all span delta in committed boundary.
104. **Tail80 T320 fit span:** `0x6D8`.
105. **Tail80 T216 fit span:** `0x106D8`.
106. **Tail80 T320 shortfall:** `0x3958`.
107. **Tail80 T216 margin:** `0xC6A8`.
108. **`commit_failed_p` T320:** `TRUE` / `1`.
109. **`commit_failed_p` T216:** `FALSE` / `0`.
110. **Full compact trigger T320:** yes, after failed tail:80 fit and OOS selector.
111. **Collection4 consequence confirmed:** yes; `1/1/1` downstream stable.
112. **Collection state causes grow result:** not shown; collection 4 follows failed fit.
113. **Grow failure causes collection:** yes: grow failure -> fit failure -> `commit_failed_p` -> full compact -> collection 4.
114. **EXTRA3 downstream relation:** post-selector reset/reuse, not upstream.
115. **EXTRA0 downstream relation:** later downstream event, unchanged.
116. **EXTRA1 downstream relation:** later downstream event, unchanged.
117. **EXTRA2 downstream relation:** remains at tail:263, unchanged.
118. **T320 `0x1000` downstream endpoint:** preserved.
119. **T216 `0x21000` downstream endpoint:** preserved.
120. **EXTRA4 downstream path:** preserved OOM/decommit path.
121. **Binary/layout causality evaluated:** yes.
122. **Binary/layout causal status:** `UNPROVEN`; hashes/sizes do not bridge to the grow operand.
123. **Exact layout→grow bridge if proven:** none; no complete selector -> image -> operand chain.
124. **Same-image experiment needed:** no; ordinary committed-state/grow-result evidence narrows the frontier without an unexplained equivalent-operand comparison.
125. **Same-image experiment performed:** no.
126. **Strongest causal chain:** shared tail:80 pointer -> T320 committed end `0x1018C1000` / T216 retained `0x1018D1000` -> `grow_heap_segment` `0x10000` minimum quantum -> `0x6D8` versus `0x106D8` -> T320 fit failure -> collection 4.
127. **First unsupported causal link:** exact prior T216 grow call's serialized high address and host/VM status.
128. **Candidate-path relevance:** none; candidate/planner logic is downstream.
129. **B02 evaluated:** no.
130. **B02 status:** `STILL_PREMATURE`.
131. **C94 baseline BSS:** inherited whole proof-kernel BSS `0x062A8770`; per-cell inherited ONE `0x4F0840`, SIX `0x3947E0`.
132. **C94 BSS:** unchanged; no C94 native storage added.
133. **BSS delta:** `0`.
134. **Production mutation:** none.
135. **Commit mutation:** none.
136. **Grow mutation:** none.
137. **Context mutation:** none.
138. **Allocator mutation:** none.
139. **Fit mutation:** none.
140. **Collection forcing:** none.
141. **Region mutation:** none.
142. **Free-list mutation:** none.
143. **OOM mutation:** none.
144. **Decommit mutation:** none.
145. **Planner mutation:** none.
146. **Candidate mutation:** none.
147. **Promotion mutation:** none.
148. **Survivor fabrication:** none.
149. **Root fabrication:** none.
150. **C18:** retained/authentic; no C94 production change.
151. **Code manager:** retained/authentic.
152. **`FindMethodInfo`:** retained/authentic.
153. **Root scan:** retained/authentic.
154. **Mark closure:** retained/authentic.
155. **Planner authenticity:** retained/authentic.
156. **Survivor integrity:** retained/authentic.
157. **C94 invariant failures:** zero in retained C64/C77/C89 semantic fields; inherited C65 diagnostic overflow recorded, not hidden.
158. **Sensitive diagnostic allocations:** `0`.
159. **C94 overflow:** no new C94 storage/overflow; inherited C65 ledger reports overflow and is excluded from fit verdict.
160. **Fail-fast:** `0` in C94 proof batches.
161. **Page faults:** `0` in C94 proof batches.
162. **T320 Boot 1:** PASS, span `0x6D8`, shortfall `0x3958`, result `0`, `commit_failed=1`.
163. **T320 Boot 2:** PASS, same normalized values.
164. **T320 Boot 3:** PASS, same normalized values.
165. **T216 Boot 1:** PASS, span `0x106D8`, margin `0xC6A8`, result `1`, `commit_failed=0`.
166. **T216 Boot 2:** PASS, same normalized values.
167. **T216 Boot 3:** PASS, same normalized values.
168. **Semantic agreement:** PASS; each cell is stable 3/3 and the split agrees with C93.
169. **Nondeterminism:** none in normalized pointer, boundary, span, result, or downstream gate; serial hashes differ per boot.
170. **Serial hashes:** T320 `9D7D8D00F48C32CE8AB3F0C21CEBEC8B99CB53B9595C7D5CF0E8AE20F2572C1E`, `EBDE1297842EEC60C8C1EC8530DAF65C0748EBA8B163367A748511CE3A682B14`, `92EB94D32097E272132160516C06F6CA69C426560C342E900A33236F475D9D2C`; T216 `B4C5DA034077228C6C81FD42362B747C5AE9C14D5C94649AFF9C19FB3AB4FD09`, `54EFE2ECFACE8312202464C59160C2A4E4A464DA9DCAE835202424BF1E574F9B`, `2A09B9FE3104543ECC9F86FBA711FBED19A9B6D13CB79CBA731C0AF5F469A55E`.
171. **Artifact hashes:** per-run proof kernel, PE/ELF, disassembly, symbol, link, and serial hashes are under the C94 evidence root; ordinary hashes are item 183/184.
172. **Runtime-pack validation:** PASS by locked C51 manifest and C94 logs; source identity, FP patch, archive, stale-output, and semantic guards match.
173. **Managed build:** PASS in fresh T320/T216 C94 batches.
174. **Native build:** PASS in fresh T320/T216 C94 batches.
175. **PowerShell syntax:** PASS with the PowerShell parser and scriptblock execution.
176. **JSON parse:** PASS for runtime-pack, C94 manifest, watchdog, and chronology JSON.
177. **`git diff --check`:** PASS before commit.
178. **PE → ELF conversion:** PASS in every fresh proof batch.
179. **Symbol checks:** PASS; kernel entry, native helper, and artifact symbol logs retained.
180. **Linker/source/table/archive guards:** PASS; existing guards retained and no production source diff remains.
181. **C52 Tier-All result or reason omitted:** omitted because C94 is bounded and C52 Tier-All is not semantically appropriate; B02 remains deferred.
182. **Ordinary restoration:** PASS; ordinary kernel and ESP restored to required SHA.
183. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
184. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
185. **Proof artifact active:** no; ordinary artifacts restored and ordinary image has no C94 proof marker.
186. **C94-owned QEMU cleanup:** PASS; owned count `0` at closeout.
187. **Unrelated QEMU preservation:** PASS; unrelated QEMU processes were identified by command line and preserved.
188. **Files changed:** harness script and this C94 documentation; no native runtime source file changed.
189. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C94_GROW_HEAP_SEGMENT_COMMIT_PROVENANCE.md`.
190. **Evidence root:** `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/`, separated into imported C93, validator audit, source audit, per-cell provenance, divergence, and final confirmations.
191. **Final commit:** local commit subject `Trace NativeAOT heap segment commit growth`; exact SHA is the final HEAD after commit.
192. **Push status:** not pushed.
193. **Remaining limitation:** exact earlier T216 `grow_heap_segment` ordering/high-address operand and underlying `VirtualAlloc` status are not directly serialized; C95 may trace only that operand one step upstream.
194. **Exact next-smallest milestone:** capture the prior T216 grow call's high address, commit address/size, boolean result, and hard-limit flag with existing reserved diagnostic storage or an equivalent non-perturbing checkpoint; do not add a tail value, run B02, or perform same-image work unless equivalent runtime operands remain unexplained.

## Source contract and operand table

The locked implementation is `gc.cpp` lines 15465-15498. It checks the
desired end against the reserved end, returns success immediately if the
desired end is already committed, otherwise computes:

```text
c_size = align_on_page(high_address - committed_end)
c_size = max(c_size, commit_min_th)
c_size = min(c_size, reserved_end - committed_end)
```

`virtual_commit` accounts the request, invokes the platform primitive, rolls
accounting back on primitive failure, and returns a boolean plus the optional
hard-limit flag. `heap_segment_committed(seg)` advances only after success.
The Windows primitive is `VirtualAlloc(address, size, MEM_COMMIT,
PAGE_READWRITE)` in `gcenv.windows.cpp`.

| Operand | T320 | T216 |
| --- | ---: | ---: |
| Allocation pointer | `0x1018C0910` | `0x1018C0910` |
| Current committed end | `0x1018C1000` | `0x1018D1000` |
| Desired fit end | `0x1018C4940` | `0x1018C4940` |
| Shortfall / margin | `0x3958` shortfall | `0xC6A8` margin |
| Raw grow request | `0x3958` | no tail:80 call; prior source-equivalent `0x3958` |
| Effective aligned request | `0x10000` | prior `0x10000` |
| Reserved end | `0x101900000` | `0x101900000` |
| Reserved extent remaining | `0x3F000` | `0x2F000` |

The normalized pointer contribution remains zero. The only supported fit-span
contribution is the committed-boundary delta `0x10000`. T320 collection 4 is
downstream of its current grow failure, not the cause.

## Evidence index

- Imported C93: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/imported-c93/`
- Validator audit: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/validator-audit/`
- Source audit: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/source-audit/`
- T320 provenance: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/T320-grow-provenance/`
- T216 provenance: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/T216-grow-provenance/`
- First divergence: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/first-boundary-divergence/`
- Final confirmations: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/final-confirmations/`
- Fresh per-cell manifests and logs: `out/dotnet/c011ec94-grow-heap-segment-commit-provenance/15mid8/`

# C93 — Tail-80 committed-fit boundary writer and commit-state provenance

## Result

C93 isolates the ordinary production writer behind the C92 `0x6D8` fit span.
The writer is `gc_heap::grow_heap_segment` in the locked `gc.cpp`.  Its
successful path increases `heap_segment_committed(seg)` by the page-aligned
commit amount; its failure path leaves the committed end unchanged.  At the
shared tail:80 fit attempt, T320 and T216 have the same normalized allocation
pointer and region start.  T320 retains committed end `0x1018C1000`, whereas
T216 has `0x1018D1000`: exactly one additional `0x10000` commit quantum.

Thus C93 is Outcome A, with the C92 mixed mechanism refined to a prior
commit-growth success/failure distinction.  The tail:80 request is `0x4030`.
T320 sees `0x1018C0FE8 - 0x1018C0910 = 0x6D8`, short by `0x3958`.
T216 sees `0x1018D0FE8 - 0x1018C0910 = 0x106D8`, leaving `0xC6A8`.
The allocation-pointer contribution is zero; the committed-boundary
contribution is `0x10000`.

C91 normalized the allocation chronology. C92 authenticated the exact tail:80
collection selector (`commit_failed_p=TRUE` on T320). C93 identifies where the
`0x6D8` came from: the committed end was not extended by the preceding
`grow_heap_segment` production path on T320, while the corresponding T216
history had one successful page-aligned growth.

## Source audit

Locked source: `out/dotnet/c52-runtime-source/source-04371d8e/src/coreclr/gc/gc.cpp`.

The relevant chain is:

```text
heap_segment_committed(seg)
    -> a_fit_segment_end_p: end = committed_end - pad
    -> soh_try_fit: available span = fit_end - acontext->alloc_ptr
    -> commit_failed_p
    -> allocate_soh state selector
```

For this workload, `limit_from_size` produces a padded demand of `0x4030`
from the C64 request `0x4018` and the `0x18` minimum-object alignment pad.
`a_fit_segment_end_p` first tests the committed end. If committed capacity is
insufficient, it computes a reserved-end target and calls
`grow_heap_segment(seg, allocated + limit, ...)`. `grow_heap_segment` computes
`align_on_page(high - committed)`, raises it to `commit_min_th` when needed,
and updates `heap_segment_committed(seg)` only after `virtual_commit` succeeds.
The locked workstation constants are `OS_PAGE_SIZE=0x1000` and
`commit_min_th=0x10000`.

The exact fields are:

| Meaning | Runtime field/expression | Scope |
| --- | --- | --- |
| Allocation pointer | `acontext->alloc_ptr` | allocation-context-local |
| Allocation limit | `acontext->alloc_limit` | allocation-context-local; fit output |
| Committed end | `heap_segment_committed(seg)` | segment/region-local committed prefix |
| Fit end | `end` in `a_fit_segment_end_p`; normally `committed_end - pad` | fit-call local, derived from segment committed end |
| Region end | `heap_segment_reserved(seg)` | segment/region-local reservation |
| Available fit span | `fit_end - acontext->alloc_ptr` (equivalent to the source comparison after padding) | fit-call quantity |

`alloc_limit` is not an alias for `heap_segment_committed(seg)`. In the
successful path it is the allocation-context limit established from `end`;
the committed end is the production state that bounds the candidate fit end.
The reserved end is distinct again and is used as the target for a possible
commit extension.

Relevant writers and resetters:

| Role | Source function | Operation | Workload relevance |
| --- | --- | --- | --- |
| Region initialization | `make_heap_segment` | initializes `mem`, `reserved`, and `committed` with the initial commit | initial state only |
| Commit growth | `gc_heap::grow_heap_segment` | on successful `virtual_commit`, `heap_segment_committed(seg) += c_size` | causal C93 writer |
| Context refill | `a_fit_segment_end_p`, then `fix_allocation_context` / `soh_try_fit` | derives `alloc_limit` from committed/fit end; does not independently grow the segment | consumes the boundary |
| Reset/reuse | `init_heap_segment`, `return_free_region`, region reset paths | resets allocation metadata / reuses a region; does not manufacture the earlier T320 short boundary | downstream after the C92 failure in T320 |
| Decommit | `decommit_region`, `decommit_heap_segment`, and the locked decommit helpers | reduces committed extent to the retained memory extent | not the first tail:80 divergence in this run |

The production field is segment-local, not allocation-context-local,
generation-global, or image-global. T320 and T216 use the same logical field
on the same normalized region role; their committed prefixes differ.

## Tail:80 normalized snapshot

The pre-fit T320 values are taken from the accepted C92/C89 evidence. The
T216 values are from the C93 exact C89-composition capture. Addresses are
shown both absolutely and as offsets from the common region start
`0x101800028`.

| Field | T320 | T216 |
| --- | ---: | ---: |
| Allocation pointer | `0x1018C0910` | `0x1018C0910` |
| Region start | `0x101800028` | `0x101800028` |
| Pointer offset | `0xC08E8` | `0xC08E8` |
| Fit end | `0x1018C0FE8` | `0x1018D0FE8` |
| Fit-end offset | `0xC0FC0` | `0xD0FC0` |
| Committed end | `0x1018C1000` | `0x1018D1000` |
| Committed-end offset | `0xC0FD8` | `0xD0FD8` |
| Region/reserved end | `0x101900000` | `0x101900000` |
| Region-end offset | `0xFFFD8` | `0xFFFD8` |
| Fit span | `0x6D8` | `0x106D8` |
| Demand | `0x4030` | `0x4030` |
| Margin over demand | `-0x3958` | `+0xC6A8` |
| Collection count at fit | 3 before T320 trigger; 4 after selector | 3 |

The span difference is:

```text
delta_fit_span = 0x106D8 - 0x6D8 = 0x10000
delta_fit_end  = 0x1018D0FE8 - 0x1018C0FE8 = 0x10000
delta_alloc_ptr = 0x1018C0910 - 0x1018C0910 = 0
```

Classification: `FIT_END_SMALLER`; pointer advancement does not contribute.
Both cells use the same current region role and the same normalized pointer at
the first differing fit-boundary checkpoint. The first supported boundary
difference is therefore at tail:80 / raw ordinal `0x93` / decimal 147. The
earlier tail checkpoints remain equal at the granularity covered by the
accepted C91/C92 chronology and the bounded C93 source-backed snapshot.

## Writer provenance

The exact prior production writer is:

```text
gc.cpp:15451-15498
gc_heap::grow_heap_segment(gc_heap_segment* seg, size_t high, ...)
c_size = align_on_page(high - heap_segment_committed(seg));
c_size = max(c_size, commit_min_th);
if (virtual_commit(..., c_size))
    heap_segment_committed(seg) += c_size;
```

T320 and T216 invoke the same source writer in their preceding allocation
histories. The causal difference is the commit result / resulting committed
end, not a different fit rule and not a pointer debit. For the exact tail:80
snapshot, the relevant operands are:

| Operand | T320 | T216 |
| --- | ---: | ---: |
| committed before the causal extension | `0x1018C1000` | `0x1018C1000` |
| reserved/region end | `0x101900000` | `0x101900000` |
| page-aligned causal growth | `0x0` retained / no successful extension | `0x10000` |
| committed end consumed by fit | `0x1018C1000` | `0x1018D1000` |
| fit end after `0x18` pad | `0x1018C0FE8` | `0x1018D0FE8` |

The available C90 growth evidence also records the same locked arithmetic:
page alignment is `align_on_page`, and the minimum growth quantum is
`0x10000`. The C93 conclusion is deliberately limited to the one upstream
writer step requested by C93: the exact successful/absent extension that left
the committed boundary one page apart. C93 does not reopen region genealogy.

The region reset/reuse involving `+0x1700000` is after T320's tail:80 fit
failure. The ordering is:

```text
prior committed-boundary production state
    -> tail:80 a_fit_segment_end_p / soh_try_fit
    -> T320 commit_failed_p=TRUE
    -> a_state_trigger_full_compact_gc
    -> collection 4
    -> EXTRA3 reset/reuse (+0x1700000)
```

That reset cannot be the cause of the pre-fit `0x6D8`; it is a consequence of
the selector decision.

## Confirmation and controls

C93 added one scalar `committedEnd` to the existing C65 refill record and
reused the existing C64 allocation record `reserved[4]` for the raw-ordinal
fit snapshot. It added no array, hot-path callback, allocator mutation,
collection forcing, or production state change. The added C64 marker is emitted
only at lifecycle finish for ordinal `0x93`; the final T216 exact capture is:

```text
C011EC93-FIT-BOUNDARY ordinal=0x93 fitResult=1 commitFailed=0
  committedEnd=0x1018D1000 fitEnd=0x1018D0FE8
  objectAddress=0x1018C0928 allocationPointerAfter=0x1018C4940
```

The T320 C92-safe accepted evidence remains the authoritative pre-fit snapshot:

```text
ordinal=0x93, allocationPointer=0x1018C0910,
committedEnd=0x1018C1000, fitEnd=0x1018C0FE8,
fitSpan=0x6D8, demand=0x4030, commit_failed_p=TRUE
```

The strict C89 harness rejected the T320 C93 confirmation run because its
older C88 target-event assertion was not present in this observer composition;
that harness rejection did not alter the serial observation or the accepted
C92/C91 controls. The T216 exact run passed the C89 arithmetic and restoration
checks. Consequently, C93 claims Level 2 (writer isolated) and records the
remaining three-boot confirmation limitation explicitly rather than presenting
the failed strict harness as a new runtime result.

The downstream C92 observations remain consequences: T320
`commit_failed_p=TRUE`, full compact selection, collection 4, EXTRA3
reset/reuse, later EXTRA0/EXTRA1, EXTRA2 at tail:263, T320 `0x1000`, T216
`0x21000`, and EXTRA4 OOM/decommit. No candidate/planner, promotion, root,
survivor, or B02 claim is changed. B02 remains `STILL_PREMATURE`.

## Numbered closeout report

1. **Outcome:** Outcome A — prior commit-growth success/failure; C92 mixed mechanism refined to a committed-boundary distinction.
2. **Success Level:** Level 2 — exact prior production writer isolated. Level 3 is not claimed because the strict T320 three-run C93 confirmation harness was not completed.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `185f732ec9e251e4549927dba254a013097ba1e3`.
6. **Starting subject:** `Isolate NativeAOT shared-tail GC trigger`.
7. **Final HEAD:** the local C93 commit reported at item 173.
8. **Final subject:** `Trace NativeAOT committed fit boundary`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0/0` (live state; authoritative over the expected description).
11. **Final ahead/behind:** `1/0`.
12. **Starting worktree:** clean.
13. **Final worktree:** clean.
14. **Runtime identity:** NativeAOT 9.0 AMD64 Workstation GC, interfaces 5.3/2.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C91 SHA:** `4693e20d5f496f36172ff6c5404d3a114a1bb7ee`.
18. **C92 SHA:** `185f732ec9e251e4549927dba254a013097ba1e3`.
19. **C93 SHA:** the local commit reported at item 173.
20. **Exact C93 question:** Which production operation last writes or determines the committed-fit boundary consumed by `soh_try_fit`, and where do T320/T216 first differ?
21. **Tail divergence coordinate:** `tail:80`.
22. **Raw ordinal:** `0x93` / decimal 147.
23. **Request demand:** `0x4030`.
24. **T320 fit span:** `0x6D8`.
25. **T320 shortfall:** `0x3958`.
26. **T216 fit span:** `0x106D8`.
27. **Allocation-pointer field:** `acontext->alloc_ptr`.
28. **Fit-end field:** local `end` in `a_fit_segment_end_p`, derived as committed end minus pad.
29. **Committed-end field:** `heap_segment_committed(seg)`.
30. **Region-end field:** `heap_segment_reserved(seg)`.
31. **Fit-span expression:** `fit_end - acontext->alloc_ptr`; for this request `committed_end - 0x18 - alloc_ptr`.
32. **Field scopes:** pointer/limit allocation-context-local; committed/reserved segment-local; fit end fit-call-local derived state.
33. **Relevant fit-end writers:** `a_fit_segment_end_p` and `fix_allocation_context` derive the context limit; `soh_try_fit` consumes it.
34. **Relevant committed-end writers:** `make_heap_segment` initialization, `grow_heap_segment` successful commit, decommit helpers reducing the extent.
35. **Region initialization writer:** `make_heap_segment`.
36. **Commit-growth writer:** `gc_heap::grow_heap_segment`.
37. **Context-refill writer:** `a_fit_segment_end_p` followed by `fix_allocation_context` / `soh_try_fit`.
38. **Reset/reuse writer:** `init_heap_segment` / `return_free_region` and region reset paths.
39. **T320 tail80 allocation pointer:** `0x1018C0910`.
40. **T216 tail80 allocation pointer:** `0x1018C0910`.
41. **T320 tail80 region start:** `0x101800028`.
42. **T216 tail80 region start:** `0x101800028`.
43. **T320 pointer offset:** `0xC08E8`.
44. **T216 pointer offset:** `0xC08E8`.
45. **T320 fit end:** `0x1018C0FE8`.
46. **T216 fit end:** `0x1018D0FE8`.
47. **T320 fit-end offset:** `0xC0FC0`.
48. **T216 fit-end offset:** `0xD0FC0`.
49. **T320 committed end:** `0x1018C1000`.
50. **T216 committed end:** `0x1018D1000`.
51. **T320 committed-end offset:** `0xC0FD8`.
52. **T216 committed-end offset:** `0xD0FD8`.
53. **Pointer contribution to span difference:** `0`.
54. **Boundary contribution to span difference:** `+0x10000`.
55. **Both contribute:** no; `FIT_END_SMALLER` only.
56. **First equal checkpoint:** bounded shared-tail checkpoints through tail:79.
57. **Last equal checkpoint:** tail:79, with the same normalized pointer and region role.
58. **First differing checkpoint:** tail:80 / ordinal `0x93`.
59. **First differing normalized coordinate:** tail:80; committed-end offset changes from `0xC0FD8` to `0xD0FD8`.
60. **First differing raw ordinal:** `0x93` / 147.
61. **T320 current region:** normalized shared region role at `0x101800028`, committed prefix ending `0x1018C1000`.
62. **T216 current region:** same normalized region role at `0x101800028`, committed prefix ending `0x1018D1000`.
63. **T320/T216 collection count:** T320 is at collection 3 before the tail:80 selector and advances to 4; T216 remains at 3.
64. **Exact prior writer source file:** `src/coreclr/gc/gc.cpp` in locked source `source-04371d8e`.
65. **Exact prior writer function:** `gc_heap::grow_heap_segment`.
66. **Exact writer expression:** `if (virtual_commit(..., c_size)) heap_segment_committed(seg) += c_size;`.
67. **T320 writer invoked:** yes in the relevant prior production history; no successful target extension remains in the tail:80 boundary.
68. **T216 writer invoked:** yes, with successful page-aligned growth in the corresponding history.
69. **T320 writer old value:** `0x1018C1000` at the tail:80 boundary.
70. **T216 writer old value:** `0x1018C1000` before the corresponding `0x10000` extension.
71. **T320 writer operands:** reserved end `0x101900000`; high/commit target did not produce a successful additional `0x10000` prefix before the fit.
72. **T216 writer operands:** same reserved end and normalized pointer history; target required and admitted one `0x10000` page-aligned extension.
73. **T320 writer result:** committed end remains `0x1018C1000` for the fit.
74. **T216 writer result:** committed end becomes `0x1018D1000`.
75. **Writer-only-on-one-side:** no; same production writer identity is present in both histories.
76. **Same-writer-different-operands:** not the primary classification; the decisive difference is successful versus absent/failed growth and resulting committed end.
77. **Commit attempt involved:** yes, as the production boundary writer mechanism.
78. **T320 commit request:** no successful additional tail:80 extension; retained boundary `0x1018C1000`.
79. **T216 commit request:** `0x10000` page-aligned growth for the corresponding extension.
80. **T320 aligned commit request:** `0x0` additional successful growth retained at the boundary.
81. **T216 aligned commit request:** `0x10000`.
82. **T320 commit result:** no successful extension before the fit; committed end unchanged.
83. **T216 commit result:** success; committed end advanced by `0x10000`.
84. **T320 committed end after:** `0x1018C1000`.
85. **T216 committed end after:** `0x1018D1000`.
86. **Commit-growth causal status:** causal; Outcome A.
87. **Pointer-advance causal status:** not causal; normalized pointers are equal.
88. **Region-reset causal status:** downstream consequence, not the pre-fit cause.
89. **Earlier-collection causal status:** not independently expanded in C93; C93 stops at the immediate writer input as required.
90. **Reset occurs before or after tail80 fit failure:** after the T320 fit failure and selector.
91. **Event ordering:** prior boundary state -> tail:80 fit -> `commit_failed_p` -> full compact selector -> collection 4 -> reset/reuse.
92. **Exact source of T320 `0x6D8`:** `0x1018C0FE8 - 0x1018C0910`, where fit end is `heap_segment_committed(0x1018C1000) - 0x18`.
93. **Exact source of T216 fit margin:** `0x1018D0FE8 - 0x1018C0910 = 0x106D8`, leaving `0xC6A8` over `0x4030`.
94. **C92 `commit_failed_p` result preserved:** yes, TRUE on T320 and FALSE on T216.
95. **C92 full-compact trigger preserved:** yes.
96. **Collection4 behavior preserved:** yes, T320 only.
97. **EXTRA3 role preserved:** yes, post-selector reset/reuse.
98. **EXTRA0 role preserved:** yes, later downstream event.
99. **EXTRA1 role preserved:** yes, later downstream event.
100. **EXTRA2 role preserved:** yes, at tail:263.
101. **T320 `0x1000` preserved:** yes.
102. **T216 `0x21000` preserved:** yes.
103. **EXTRA4 path preserved:** yes, OOM/decommit path.
104. **Earliest supported causal chain:** shared pointer/region role -> committed-end difference -> fit span difference -> T320 commit failure -> collection4/reset.
105. **Strongest causal chain:** prior `grow_heap_segment` success/retention -> `heap_segment_committed` one-page delta -> `end=committed-pad` -> `0x6D8` versus `0x106D8`.
106. **First unsupported causal link:** the earlier semantic event that caused the two histories to request/admit different growth remains C94's one-step frontier.
107. **Binary/layout causality evaluated:** yes, as a gate.
108. **Binary/layout causal status:** not established; source-level commit-state provenance exists first.
109. **Same-image experiment needed:** no for C93's writer result; only if C94 cannot explain the writer input from ordinary runtime history.
110. **Same-image experiment performed:** no.
111. **Candidate-path relevance:** none; candidate/planner logic is downstream and out of scope.
112. **B02 evaluated:** no.
113. **B02 status:** `STILL_PREMATURE`.
114. **C93 baseline BSS:** unchanged from C92 / `0` delta.
115. **C93 BSS:** unchanged; one scalar reused in existing records.
116. **BSS delta:** `0`.
117. **Production mutation:** none.
118. **Allocator mutation:** none.
119. **Context mutation:** none.
120. **Commit mutation:** none.
121. **Fit mutation:** none.
122. **Collection forcing:** none.
123. **Region mutation:** none.
124. **Free-list mutation:** none.
125. **OOM mutation:** none.
126. **Decommit mutation:** none.
127. **Planner mutation:** none.
128. **Candidate mutation:** none.
129. **Promotion mutation:** none.
130. **Survivor fabrication:** none.
131. **Root fabrication:** none.
132. **C18:** preserved; no new C18 path was introduced.
133. **Code manager:** preserved; no mutation.
134. **`FindMethodInfo`:** preserved; no mutation.
135. **Root scan:** preserved/authentic.
136. **Mark closure:** preserved/authentic.
137. **Planner authenticity:** preserved.
138. **Survivor integrity:** preserved.
139. **C93 invariant failures:** `0`.
140. **Sensitive diagnostic allocations:** `0`.
141. **C93 overflow:** `0`.
142. **Fail-fast:** `0`.
143. **Page faults:** `0`.
144. **T320 Boot 1:** accepted C92/C91 baseline: stable T320 split; `1/1/1`, collection4.
145. **T320 Boot 2:** accepted C92/C91 baseline: stable T320 split; `1/1/1`, collection4.
146. **T320 Boot 3:** accepted C92/C91 baseline: stable T320 split; `1/1/1`, collection4.
147. **T216 Boot 1:** C93 exact capture: fit `0x106D8`, `commit_failed_p=FALSE`, collection3, `1/6/6`.
148. **T216 Boot 2:** accepted C92/C91 baseline: stable T216 fit and `1/6/6`.
149. **T216 Boot 3:** accepted C92/C91 baseline: stable T216 fit and `1/6/6`.
150. **Semantic agreement:** accepted C91/C92 three-boot controls agree; C93 scalar agrees with the exact T216 boundary.
151. **Nondeterminism:** none observed in accepted controls; strict T320 C93 validator mismatch is harness composition, not runtime nondeterminism.
152. **Serial hashes:** accepted T320 C92/C91 hashes `88D652BA...`, `FE252C97...`, `FF1A738B...`; accepted T216 hashes `6C6856DC...`, `847616C8...`, `9924E275...`; C93 exact T216 serial SHA `A4047AE14B0C967A7E07554E26B03414744607A2A2300D4A821E4A352D6EE4AB`.
153. **Artifact hashes:** C93 exact T216 proof kernel `49E11CF485F4A06FE5C6F31C56E3BF3FB95228293BD6AB0F9138C7ED5F8FADAE`; serial `A4047AE14B0C967A7E07554E26B03414744607A2A2300D4A821E4A352D6EE4AB`; runtime-pack manifest source `E9645A5927604588B717577DA0A4684A0E74525B6F6E103DF304FB98566A084B`, object `E4662269BECA916151F02999C40A8E80401BFC5F4A18E542B4991244FD87071A`, and adapted library `FA5CBF07118439151BF6701D5C7AC6CC14E9E0467FE2365FDB56EF0247756D19`.
154. **Runtime-pack validation:** PASS.
155. **Managed build:** PASS in the C93 exact T216 run.
156. **Native build:** PASS in the C93 exact T216 run; T320 capture also built and emitted the marker before the strict legacy assertion failed.
157. **PowerShell syntax:** PASS.
158. **JSON parse:** PASS for the C93 manifests and runtime-pack manifest.
159. **`git diff --check`:** PASS.
160. **PE -> ELF conversion:** PASS in the exact C93 build guard.
161. **Symbol checks:** PASS.
162. **Linker/source/table/archive guards:** PASS.
163. **C52 Tier-All result or reason omitted:** omitted as not semantically appropriate; C93 is an observer-only bounded provenance change and the exact C89 composition supplied the required runtime/build guards.
164. **Ordinary restoration:** PASS; exact run restored ordinary artifacts.
165. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
166. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
167. **Proof artifact active:** no; proof-only mode false after restoration.
168. **C93-owned QEMU cleanup:** `0` owned QEMU processes remaining.
169. **Unrelated QEMU preservation:** unrelated x86 QEMU processes were preserved.
170. **Files changed:** the C93 observer script, allocation diagnostics header, platform implementation, this documentation, and untracked evidence under the ignored evidence root.
171. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C93_TAIL80_COMMITTED_FIT_BOUNDARY_WRITER.md`.
172. **Evidence root:** `out/dotnet/c011ec93-tail80-committed-fit-boundary-writer/`.
173. **Final commit:** local commit subject `Trace NativeAOT committed fit boundary`; exact SHA is recorded by the final `git log` closeout.
174. **Push status:** not pushed.
175. **Remaining limitation:** the strict legacy C89 T320 target-event assertion did not accept the C93 observer composition, so C93 does not claim fresh three-boot Level 3 confirmation for the new scalar.
176. **Exact next-smallest milestone:** C94 should trace the one upstream input to the differing `grow_heap_segment` commit result, without reopening region genealogy.
177. **C93 completion gate:** writer identity, source expression, normalized boundary delta, fit-span arithmetic, downstream C92 selector, clean restoration, and local no-push closeout are recorded; B02 remains deferred.

# NativeAOT Workstation GC C89: exact allocation/OOM arithmetic

C89 audited the producer side of the C88 one-vs-six result for the locked
NativeAOT 9.0.0 AMD64 Workstation GC. The accepted workload is `15mid8` with
15 retained objects, actual retained object size `0x10E80`, and promoted bytes
`0xFD980`. T320 uses tail count 320 and ends `1/1/1`; T216 uses tail count 216
and ends `1/6/6`.

## Result

C89 closes the exact request arithmetic, the exact fit quantum, all four
observed T320 target-region boundary records, T216's page-rounded
`0x1000 -> 0x21000` committed-prefix growth, and the OOM chronology. It does
not honestly promote the result to Level 3: EXTRA3 is observed at a
reset/reuse boundary (`alloc_ptr == alloc_limit == 0`), so the four-region count
cannot be predicted from byte demand alone without also importing the source
free-list/topology state. The appropriate result is Outcome F / Level 2.

The most important semantic correction is that source
`last_gc_before_oom`/`joined_last_gc_before_oom` is `1` on both final no-region
paths. C88's `oomEligible` is the conjunction
`committedSize == GC_PAGE_SIZE && joined_last_gc_before_oom`; it is `1` for
T320 and `0` for T216 because T216 has `committedSize == 0x21000`.

The causal chain that is supported is:

```text
tail320 - tail216 = 104
  -> 104 * 0x4018 = 0x1A09C0 additional object-request bytes
  -> 104 * 0x4030 = 0x1A1380 additional fit/reservation quantum
  -> observed T320 get_free_region boundaries at tail ordinals 80, 143, 203, 263
  -> T320 EXTRA4 remains at its one-page initial commit
  -> T216 EXTRA4 receives eight ordinary objects and grows by page-rounded 0x20000
  -> final no-region path records last_gc_before_oom=1 in both cells
  -> C88 one-page conjunction is true only for T320
  -> EXTRA4 transfers/decommits only for T320
```

C86 proved tail pressure causal. C85 identified the four ordinary acquisitions
and the distinct EXTRA4 consumer. C88 proved that EXTRA4's exact trigger is
`committedSize == 0x1000 && joined_last_gc_before_oom` (within the source OR
predicate, with the age branch false here). C89 supplies the numerical request,
fit, commit, and chronology evidence, while preserving the remaining
free-list/reset limitation.

## Source audit

The locked source is
`out/dotnet/pal-runtime-active-replacement-build/locked-source/src/coreclr/gc/gc.cpp`,
SHA `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.

- `gc_heap::allocate` (19555–19587) aligns the request, advances
  `acontext->alloc_ptr`, and enters refill/failure handling when the pointer is
  beyond `acontext->alloc_limit`.
- `a_size_fit_p` (15451–15462) requires
  `(alloc_limit - alloc_pointer) >= (size + Align(min_obj_size, align_const))`.
- `a_fit_segment_end_p` (17621–17675) tests against
  `heap_segment_committed(seg) - Align(min_obj_size, align_const)` and can then
  call `grow_heap_segment` on the reserved-versus-committed path.
- `adjust_limit_clr` (16736–16760) sets
  `acontext->alloc_limit = start + limit_size - Align(min_obj_size, align_const)`.
- `limit_from_size` (16993–17025) uses
  `desired_size = max(size + Align(min_obj_size), allocation_quantum)`.
- `soh_try_fit` (17896–17969) follows the next region or calls
  `get_new_region(gen_number)`; a null result sets `*commit_failed_p = TRUE`.
- `get_region_committed_size` (3812–3817) returns
  `heap_segment_committed(region) - get_region_start(region)`.
- `make_heap_segment` (12313–12350) starts Workstation regions with
  `SEGMENT_INITIAL_COMMIT = OS_PAGE_SIZE = 0x1000`.
- `grow_heap_segment` (15465–15512) page-aligns the required prefix growth,
  applies `commit_min_th = 0x10000`, caps it at the reserved remainder, and
  advances the committed pointer only after `virtual_commit` succeeds.
- `trigger_full_compact_gc` (18462–18475) and `generation_to_condemn`
  (21721–21728) set `last_gc_before_oom` when the source enters the
  last-GC-before-OOM/no-empty-region state.
- `distribute_free_regions` (13276–13389) copies that state into
  `joined_last_gc_before_oom` and tests the age OR one-page conjunction.

Thus `committedSize` is neither context headroom nor unused tail space. It is
the byte extent of the committed prefix from the region start.

## Request and region arithmetic

The raw managed tail payload is `0x4000`. The production allocator request is
`0x4018`. The object is 8-byte aligned, and the fit test reserves the source
minimum-object pad `0x18`, yielding a fit quantum of `0x4030`.

```text
T320 request bytes       320 * 0x4018 = 0x501E00
T216 request bytes       216 * 0x4018 = 0x361440
request delta            104 * 0x4018 = 0x1A09C0

T320 fit reservation     320 * 0x4030 = 0x503C00
T216 fit reservation     216 * 0x4030 = 0x362880
fit-quantum delta        104 * 0x4030 = 0x1A1380
```

For a nominal basic region of `0x100000`, the authenticated header is `0x28`.
The full committed-fit span is:

```text
0x100000 - 0x28 - 0x18 = 0xFFFC0
```

The source effective maximum small-object request has the second minimum
object pad as well:

```text
0x100000 - 0x28 - (2 * 0x18) = 0xFFFA8
```

The first number is the fit span used in the boundary arithmetic; the second
is the source-defined maximum small-object request bound. They are not
interchangeable.

## T320 boundary table

The sparse C89 observer records the exact C64 allocation ordinal that is
associated with each target `get_free_region(0)` event. There are 67 preceding
`0x10018` allocations, so tail ordinal is C64 ordinal minus `0x43`.

| Region | Trigger tail ordinal | Request | Context headroom | Previous fit headroom | Shortfall to `0x4030` | New usable fit span | `committedSize` after |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| EXTRA0 (`+0x1400000`) | 143 (`0x8F`) | `0x4018` | `0x0` | `0x3408` | `0xC28` | `0xF0FC0` | `0xF1000` |
| EXTRA1 (`+0x1500000`) | 203 (`0xCB`) | `0x4018` | `0x0` | `0x498` | `0x3B98` | `0xF0FC0` | `0xF1000` |
| EXTRA2 (`+0x1600000`) | 263 (`0x107`) | `0x4018` | `0x0` | `0x498` | `0x3B98` | `0xF0FC0` | `0xF1000` |
| EXTRA3 (`+0x1700000`) | 80 (`0x50`) | `0x4018` | `0x0` | N/A after reset | N/A after reset | `0xFFFC0` | `0x100000` |

The chronological order is EXTRA3 at tail 80, EXTRA0 at 143, EXTRA1 at 203,
and EXTRA2 at 263. The report table follows the requested EXTRA0–EXTRA3
ordering. EXTRA0's prior committed-minus-allocated remainder is `0x3420`; the
fit pad leaves `0x3408`, and `0x4030 - 0x3408 = 0xC28`. EXTRA1 and EXTRA2 each
have a `0x4B0` remainder, leaving `0x498`; each shortfall is `0x3B98`.

EXTRA3's event has zeroed pointer/limit and zero previous allocated bytes. That
is source evidence of a reset/reuse transition, not permission to infer a
contiguous prior headroom value. Its full committed region exposes `0xFFFC0`
fit bytes and is the first T320 target boundary by ordinal.

## T216 `0x21000` endpoint

T216's target EXTRA4 starts at the one-page committed prefix. Eight observed
tail requests land in that region. The eighth post-object high address is
`base + 0x20190`, so the source growth calculation is:

```text
high_address - committed = 0x20190 - 0x1000 = 0x1F190
align_on_page(0x1F190)                = 0x20000
max(0x20000, commit_min_th=0x10000)   = 0x20000
new committed size                     = 0x1000 + 0x20000 = 0x21000
```

Consequently `0x21000 - 0x1000 = 0x20000 = 32 pages`. This is committed-prefix
growth, not context headroom or an unused reserve. T320 has no ordinary tail
allocation in EXTRA4; it therefore remains at the initial `0x1000` commit.

## OOM and EXTRA4 ordering

Both final C65 failures service a `0x4018` gen0 request with pointer and limit
zero, no free basic regions, `expansionAttempted=1`, `expansionSucceeded=0`,
and `commitFailed=1`. The source path is the failed `get_new_region(0)`/no-next
basic-region path. There is no separate recorded virtual page-commit request
at that terminal no-region event.

The source flag writes are:

- T320: C65 `LAST-GC-BEFORE-OOM value=1`, write ordinal `0x1E1`; C88 target
  evaluation ordinal `0xAA`; transfer ordinal `0xAC`.
- T216: C65 `LAST-GC-BEFORE-OOM value=1`, write ordinal `0x2A2`; C88 target
  evaluation ordinal `0xC3`; no transfer.

The write precedes the C88 transfer/evaluation. EXTRA4 is not a fifth ordinary
allocation region: T320 has no C64 tail object assigned to it, and the next
operation is the C88 `distribute_free_regions`/decommit policy. The ordinary
T320 region consumers are EXTRA0–EXTRA3; EXTRA4 is the OOM/decommit consumer.

## Required numbered closeout report

1. **Outcome:** Outcome F — the request, fit, and page-rounded endpoint arithmetic is exact, but a reset/free-list boundary prevents a demand-only four-region proof.
2. **Success Level:** Level 2.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `ad0b9ece1c624fc3fa3f68d0282380e6fb3250ca`.
6. **Starting subject:** `Trace NativeAOT aged region decommit transfer`.
7. **Final HEAD:** Local C89 closeout commit; exact SHA is recorded in the final handoff.
8. **Final subject:** `Quantify NativeAOT allocation OOM arithmetic`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0 / 0`.
11. **Final ahead/behind:** `1 / 0`; not pushed.
12. **Starting worktree:** Clean.
13. **Final worktree:** Clean after the local C89 commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C87 SHA:** `669259ab82581e30ba61e6d56f122751cde1bd23`.
18. **C88 SHA:** `ad0b9ece1c624fc3fa3f68d0282380e6fb3250ca`.
19. **C89 SHA:** The local closeout commit SHA; see the final handoff.
20. **Exact C89 question:** How do the extra 104 transient gen0 allocations / `0x1A09C0` request bytes consume allocation-context and region capacity so T320 acquires four basic regions and reaches `committedSize == 0x1000`, while T216 retains `0x21000` and is not one-page eligible?
21. **T320 baseline:** Retained 15, promoted `0xFD980`, final `1/1/1`, EXTRA4 age 0 / committed `0x1000` / `oomEligible=1`.
22. **T216 baseline:** Retained 15, promoted `0xFD980`, final `1/6/6`, EXTRA4 age 0 / committed `0x21000` / `oomEligible=0`.
23. **T320 retained count:** 15.
24. **T216 retained count:** 15.
25. **T320 promoted bytes:** `0xFD980`.
26. **T216 promoted bytes:** `0xFD980`.
27. **T320 tail count:** 320.
28. **T216 tail count:** 216.
29. **Tail-count delta:** 104 (`0x68`).
30. **C86 request-byte delta:** `0x1A09C0`.
31. **Raw tail object request:** `0x4000` payload.
32. **Aligned tail allocation size:** `0x4018` production request.
33. **T320 total request bytes:** `320 * 0x4018 = 0x501E00`.
34. **T216 total request bytes:** `216 * 0x4018 = 0x361440`.
35. **Allocator-consumed-byte delta:** `0x1A1380` at the fit/reservation quantum (`104 * 0x4030`); request-byte delta remains `0x1A09C0`.
36. **Allocation-context type:** Thread `alloc_context` (`acontext`).
37. **Allocation pointer field:** `acontext->alloc_ptr`.
38. **Allocation limit field:** `acontext->alloc_limit`.
39. **Fit predicate:** `a_size_fit_p`: `(alloc_limit - alloc_pointer) >= (size + Align(min_obj_size, align_const))`; fast allocation advances `alloc_ptr` by aligned `size` and accepts `alloc_ptr <= alloc_limit`.
40. **Alignment rule:** 8-byte object alignment; source fit pad `Align(min_obj_size, align_const) = 0x18`.
41. **committedSize source field/expression:** `get_region_committed_size(region) = heap_segment_committed(region) - get_region_start(region)`.
42. **committedSize semantic unit:** Bytes in the committed prefix from region start.
43. **joined-OOM flag source:** `last_gc_before_oom`, copied to `joined_last_gc_before_oom` in `distribute_free_regions` for the single heap.
44. **joined-OOM flag set condition:** Full-compact/no-empty-region handling after `soh_try_fit` cannot obtain another basic region; source writes `last_gc_before_oom = TRUE` in `trigger_full_compact_gc` and when `generation_to_condemn` cannot get an empty region.
45. **Basic region nominal size:** `0x100000`.
46. **Basic region usable bytes:** Full committed-fit span `0xFFFC0`; source effective maximum small-object request `0xFFFA8`.
47. **Per-region fixed loss:** `0x58 = 0x28` header plus two `0x18` source minimum-object pads for the effective request bound.
48. **T320 initial allocation pointer:** C64 tail-entry observer `0x0`; decisive C65 post-debit failure also `0x0`.
49. **T216 initial allocation pointer:** C64 tail-entry observer `0x0`; decisive C65 post-debit failure also `0x0`.
50. **T320 initial allocation limit:** C64 tail-entry observer `0x4EE9AD0`; decisive C65 failure limit `0x0`.
51. **T216 initial allocation limit:** C64 tail-entry observer `0x4EE7AD0`; decisive C65 failure limit `0x0`.
52. **T320 initial headroom:** C64 observer snapshot has pointer zero; decisive C65 context headroom is `0x0`.
53. **T216 initial headroom:** C64 observer snapshot has pointer zero; decisive C65 context headroom is `0x0`.
54. **Initial state equivalent:** Yes semantically: same retained cohort, payload/request sizes, alignment, and zero decisive context headroom; virtual context addresses/stack limits differ by cell layout.
55. **Earliest allocator divergence:** No request-byte divergence precedes the tail-count difference; first target-specific T320 boundary is EXTRA3 at tail ordinal 80, and its reset/reuse state is already part of the source topology.
56. **EXTRA0 trigger ordinal:** Tail 143 (`C64 allocation ordinal 0xD2`).
57. **EXTRA0 request bytes:** `0x4018` request; `0x4030` fit quantum.
58. **EXTRA0 headroom before:** `0x3408` fit headroom; C89 context-headroom field is zero at the callback.
59. **EXTRA0 shortfall:** `0xC28`.
60. **EXTRA0 usable bytes:** `0xF0FC0` selected committed-fit span.
61. **EXTRA0 committedSize after:** `0xF1000`.
62. **EXTRA1 trigger ordinal:** Tail 203 (`C64 allocation ordinal 0x10E`).
63. **EXTRA1 request bytes:** `0x4018` request; `0x4030` fit quantum.
64. **EXTRA1 headroom before:** `0x498`.
65. **EXTRA1 shortfall:** `0x3B98`.
66. **EXTRA1 usable bytes:** `0xF0FC0`.
67. **EXTRA1 committedSize after:** `0xF1000`.
68. **EXTRA2 trigger ordinal:** Tail 263 (`C64 allocation ordinal 0x14A`).
69. **EXTRA2 request bytes:** `0x4018` request; `0x4030` fit quantum.
70. **EXTRA2 headroom before:** `0x498`.
71. **EXTRA2 shortfall:** `0x3B98`.
72. **EXTRA2 usable bytes:** `0xF0FC0`.
73. **EXTRA2 committedSize after:** `0xF1000`.
74. **EXTRA3 trigger ordinal:** Tail 80 (`C64 allocation ordinal 0x93`); chronological first among the four.
75. **EXTRA3 request bytes:** `0x4018` request; `0x4030` fit quantum.
76. **EXTRA3 headroom before:** Not defined by source record; pointer/limit and previous allocated value are zero after reset/reuse.
77. **EXTRA3 shortfall:** Not defined after reset/reuse; no fabricated contiguous predecessor.
78. **EXTRA3 usable bytes:** `0xFFFC0` full committed-fit span.
79. **EXTRA3 committedSize after:** `0x100000`.
80. **Four-region prediction:** Byte arithmetic predicts the exact per-boundary fit shortfalls for EXTRA0–EXTRA2; it does not independently predict the complete count because EXTRA3 requires reset/free-list/topology state.
81. **Four-region observed result:** Four target `get_free_region(0)` boundaries: EXTRA3, EXTRA0, EXTRA1, EXTRA2.
82. **Prediction correct:** No for a byte-only independent prediction; yes for the source-assisted observed boundary set.
83. **Total usable capacity added:** Selected committed-fit spans sum to `3*0xF0FC0 + 0xFFFC0 = 0x3D2F00`; this is exposed capacity, not a claim that all bytes were consumed.
84. **Total boundary waste:** Known pre-boundary fit residues `0x3408 + 0x498 + 0x498 = 0x3D38`; EXTRA3 residue is undefined after reset. The corresponding known shortfall sum is `0x8358`.
85. **Total alignment waste:** Cross-cell fit-pad term `104 * 0x18 = 0xF60`; it is the distinction between request bytes and fit quantum, not a claim that every pad is independently committed.
86. **T320 final request ordinal:** Tail request 320; C64 ordinal `0x183`.
87. **T216 final request ordinal:** Tail request 216; C64 ordinal `0x11B`.
88. **T320 final context headroom:** `0x0`.
89. **T216 final context headroom:** `0x0`.
90. **T320 committedSize:** `0x1000`.
91. **T216 committedSize:** `0x21000`.
92. **committedSize delta:** `0x20000`.
93. **`0x20000` difference semantics:** 32 pages of page-rounded committed-prefix growth from the one-page initial commit.
94. **T320 OOM flag:** Source `last_gc_before_oom=1`; C88 `oomEligible=1`.
95. **T216 OOM flag:** Source `last_gc_before_oom=1`; C88 `oomEligible=0`. The user-facing zero is the conjunction result, not the source flag scalar.
96. **Exact one-page OOM predicate:** `get_region_committed_size(region) == GC_PAGE_SIZE && joined_last_gc_before_oom`, inside the source age OR predicate.
97. **T320 OOM operands:** request `0x4018`; context headroom `0x0`; commit request none recorded; free-region count `0`; `commitFailed=1`; expansion attempted `1`, succeeded `0`; committed `0x1000`; joined flag `1`.
98. **T216 OOM operands:** request `0x4018`; context headroom `0x0`; commit request none recorded; free-region count `0`; `commitFailed=1`; expansion attempted `1`, succeeded `0`; committed `0x21000`; joined flag `1`.
99. **OOM flag set ordinal:** T320 C65 write `0x1E1`; T216 C65 write `0x2A2`.
100. **EXTRA4 transfer ordinal:** T320 C88 `AGE_TRANSFER` `0xAC`; T216 none.
101. **OOM precedes EXTRA4 transfer:** Yes; T320 C65 flag write `0x1E1` precedes C88 transfer `0xAC` in the observed phase chronology.
102. **Why EXTRA4 is not fifth normal region:** No C64 ordinary tail allocation reaches EXTRA4 in T320; it remains initial-commit free state and is consumed by `distribute_free_regions`/decommit handling after the ordinary no-region failure.
103. **T320 request-byte conservation:** `0x501E00 = 320 * 0x4018`.
104. **T216 request-byte conservation:** `0x361440 = 216 * 0x4018`.
105. **Cross-cell delta conservation:** `0x501E00 - 0x361440 = 0x1A09C0 = 104 * 0x4018`.
106. **Conservation residual:** Zero in request-byte and fit-quantum identities; causal residual is reset/free-list/topology state, not an unexplained byte.
107. **Residual explanation:** EXTRA3 exposes a reset/reuse boundary with no valid predecessor headroom, so exact four-region prediction needs that source state in addition to byte demand.
108. **`0x1000` endpoint predicted:** Yes, once the observed topology establishes that EXTRA4 receives no ordinary tail allocation; `make_heap_segment` initial commit is exactly one page.
109. **`0x1000` endpoint observed:** Yes, all three T320 boots.
110. **`0x21000` endpoint predicted:** Yes, from eight target objects and `align_on_page(0x1F190)=0x20000`.
111. **`0x21000` endpoint observed:** Yes, all three T216 boots.
112. **Starting headroom causal status:** Supportive but not sufficient alone; decisive final context headroom is zero in both cells and the source context snapshot does not encode the full prior free-list topology.
113. **Region usable-capacity causal status:** Exact for selected spans and EXTRA0–EXTRA2 shortfalls; insufficient alone to predict EXTRA3 after reset.
114. **Alignment causal status:** Material; the `0x18` pad changes the fit quantum from `0x4018` to `0x4030` and contributes `0xF60` across the 104-request delta.
115. **Commit-page causal status:** Exact and decisive for T216's `0x1F190 -> 0x20000` growth and the `0x21000` endpoint.
116. **Tail-pressure causal status:** Proven by C86 with the retained cohort, object size, and promotion held constant.
117. **OOM causal status:** Exact for the C88 one-page conjunction; source last-GC-before-OOM is set on both final no-region paths, while only T320 satisfies the one-page operand.
118. **Unified five-region allocator explanation:** Narrowed but not Level 3 closed: four ordinary target boundaries are observed and numerically characterized, while EXTRA4 is a downstream OOM/decommit consumer and EXTRA3 retains a topology residual.
119. **Source-derived boundary test required:** No; no additional tail count was run because the exact one-page threshold was not independently derivable.
120. **Derived tail count:** Not applicable; optional boundary test omitted.
121. **Predicted boundary result:** Not applicable.
122. **Observed boundary result:** Not applicable.
123. **Boundary prediction correct:** Not applicable.
124. **First supported causal link:** C86 tail-count delta to exact additional request bytes: `104 * 0x4018 = 0x1A09C0`.
125. **Strongest causal chain:** Tail pressure → exact request and fit quanta → observed normal-region boundary crossings → target-specific committed-prefix states → source last-GC-before-OOM write → C88 one-page conjunction → EXTRA4 transfer/decommit.
126. **First unsupported causal link:** Pure byte demand alone predicting the complete four-region count, especially the reset/reuse EXTRA3 boundary.
127. **Candidate-path relevance:** None; candidate rejection was not entered as a causal path.
128. **B02 evaluated:** No experimental B02 evaluation; C89 remained allocator/OOM-scoped.
129. **B02 status:** `STILL_PREMATURE`.
130. **C89 baseline BSS:** Inherited zero-BSS diagnostic baseline: ONE `0x4F0840`, SIX `0x3947E0`; whole proof-kernel `.bss` section in final artifacts is `0x062A8770` for both cell builds.
131. **C89 BSS:** Same inherited per-cell diagnostic BSS and same whole proof-kernel `.bss` section size.
132. **BSS delta:** `0`.
133. **Production mutation:** None.
134. **Allocator mutation:** None.
135. **Context mutation:** None.
136. **Commit mutation:** None.
137. **Request-size mutation:** None.
138. **Region mutation:** None.
139. **Free-list mutation:** None.
140. **OOM predicate mutation:** None.
141. **OOM flag mutation:** None.
142. **Decommit mutation:** None.
143. **Planner mutation:** None.
144. **Candidate mutation:** None.
145. **Promotion mutation:** None.
146. **Survivor fabrication:** None.
147. **Root fabrication:** None.
148. **C18:** Pass; startup, manager, and GC safety gate remained valid.
149. **Code manager:** Pass; registered NativeAOT managed code manager and preserved inherited proof.
150. **`FindMethodInfo`:** Pass on the valid control path.
151. **Root scan:** Pass/inherited; no C89 change to root enumeration.
152. **Mark closure:** Pass/inherited; no C89 change to mark closure.
153. **Planner authenticity:** Pass/inherited source planner; no planner mutation.
154. **Survivor integrity:** Pass; retained cohort remains 15 and promoted bytes remain `0xFD980`.
155. **C89 invariant failures:** `0`.
156. **Sensitive diagnostic allocations:** `0`.
157. **C89 event capacity:** No per-allocation C89 array; inherited fixed C67 capacity `0x800` events.
158. **C89 peak events:** C67 summary T320 `0xBC`, T216 `0xC6`; raw C89 marker records are sparse and bounded.
159. **C89 overflow:** Event and region overflow `0`.
160. **Fail-fast:** `0`.
161. **Page faults:** `0`.
162. **T320 Boot 1:** Pass — `1/1/1`, four boundaries, `0x1000`, `oomEligible=1`.
163. **T320 Boot 2:** Pass — `1/1/1`, four boundaries, `0x1000`, `oomEligible=1`.
164. **T320 Boot 3:** Pass — `1/1/1`, four boundaries, `0x1000`, `oomEligible=1`.
165. **T216 Boot 1:** Pass — `1/6/6`, zero target EXTRA0–EXTRA3 boundaries, `0x21000`, `oomEligible=0`.
166. **T216 Boot 2:** Pass — `1/6/6`, zero target EXTRA0–EXTRA3 boundaries, `0x21000`, `oomEligible=0`.
167. **T216 Boot 3:** Pass — `1/6/6`, zero target EXTRA0–EXTRA3 boundaries, `0x21000`, `oomEligible=0`.
168. **Semantic agreement:** Yes, 3/3 within each cell and across the accepted pair.
169. **Nondeterminism:** None observed in boundary count, endpoint, eligibility, or final semantic result.
170. **Serial hashes:** T320 `88D652BA2D6C67D50F8388E8D85029F9C30ADBFFCD9E151C8F0C28DD8BAFC0EB`, `FE252C974824AC4DF19AAB066E7CD70CD3439CE5ED8E340991D007248D7BB62E`, `FF1A738BAD5ECBF152DB0E69C70BAF2731ADA5A286E48B3A973E22AD144110F3`; T216 `6C6856DCD76904067BE0C7693DD295764B043CD34E52C3C2CD446633C04FF8B3`, `847616C8C5A3C36DDA42BC5B0969EFDDFE2E17EF38FD28C8DFF4940B0D290D96`, `9924E275E9EDCC3F0C83F03E5F2F33F5AA7A906A87AFB97AD099FCA82B1892B3`.
171. **Artifact hashes:** T320 proof kernel ELF `979A2F71EB7930871ABDDC80AF282328971A23D4E8833234885E4E8AC231335A`, PE `8C1BBB63AD4AED9810B0EC021E608533894E479CD8C52FCAB2AE47AFDDBC906B`, ELF `A43488CDB12668FF920B680E9F5797EF253C2424C15309D2A9AA41F75A9AB21D`, MAP `09FA36532F7E8C0EC25FAA5B4E94FDC02F8EDFF6BC919C168155CD175200DD0C`; T216 proof kernel ELF `010E8A845CE4ED8FF347A257C140A70258A5608A4D0284BBD16EEBD2D77A0D0A`, PE `1CEBF69A5F81D2C3C9D8224BAB0D98F75D6C82BDDB4E824227B76D4822890F30`, ELF `B893AA0008373F0D392BDFC300C5626D0F69A712BDE99BA04D257F69EB915E84`, MAP `61DBBE3A5F6125D7F9E6FDE5F6A3A8AA25268098245A2711BD0B0390BA31F768`.
172. **Runtime-pack validation:** Pass; manifest identity and locked source/FP identities validated.
173. **Managed build:** Pass for both final confirmation cells.
174. **Native build:** Pass for both final confirmation cells.
175. **PowerShell syntax:** Pass; parser reports `PowerShell parse OK`.
176. **JSON/XML parse:** Pass; generated manifests and validation inputs parsed.
177. **`git diff --check`:** Pass.
178. **PE → ELF conversion:** Pass; converter logs present in both final roots.
179. **Symbol checks:** Pass; entry, managed, unwind, and NativeAOT symbol checks completed.
180. **Linker/source/table/archive guards:** Pass; runtime archive, linker, source needles, tables, and artifact guards completed.
181. **C52 Tier-All result or reason omitted:** Omitted because C89 is a narrow allocator/OOM arithmetic milestone and Tier-All would not add relevant semantic evidence.
182. **Ordinary restoration:** Pass; `finally` restored ordinary kernel and ESP artifacts.
183. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
184. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
185. **Proof artifact active:** Active only inside the final confirmation evidence roots; not left active in ordinary artifacts.
186. **C89-owned QEMU cleanup:** 0 C89-owned QEMU processes remain.
187. **Unrelated QEMU preservation:** The unrelated QEMU process rooted in `D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration` was preserved.
188. **Files changed:** This documentation file, `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, and `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`; generated evidence is under the ignored C89 evidence root.
189. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C89_EXACT_ALLOCATION_OOM_ARITHMETIC.md`.
190. **Evidence root:** `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/` with `source-audit`, `discovery`, `offline-byte-accounting`, and `final-confirmations`.
191. **Final commit:** Local subject `Quantify NativeAOT allocation OOM arithmetic`; exact SHA is recorded in the final handoff.
192. **Push status:** Not pushed, as required.
193. **Remaining limitation:** The source observer does not expose a valid pre-reset predecessor headroom for EXTRA3, so it cannot derive all four boundaries from demand bytes alone; source free-list/topology state remains necessary.
194. **Exact next-smallest milestone:** Stop this ONE-vs-SIX extension at C89; select the next milestone from a real remaining NativeAOT integration blocker. Keep B02 deferred unless an authentic candidate rejection independently becomes actionable.

## Evidence map

- Source audit: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/source-audit/C89-source-audit.md`.
- Offline ledger: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/offline-byte-accounting/C89-byte-ledger.md`.
- T320 discovery: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/discovery/T320-v8/`.
- T216 discovery: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/discovery/T216-v2/`.
- T320 final confirmations: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/final-confirmations/T320/`.
- T216 final confirmations: `out/dotnet/c011ec89-exact-allocation-oom-arithmetic/final-confirmations/T216/`.

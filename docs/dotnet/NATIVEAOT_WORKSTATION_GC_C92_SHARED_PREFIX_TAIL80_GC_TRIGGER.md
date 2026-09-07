# NativeAOT Workstation GC C92: shared-prefix tail:80 GC trigger

Status: Outcome G — mixed context-fit and commit-state/region-refill mechanism.

Success level: Level 2. The locked-source collection selector is isolated and
mapped to the authenticated T320 tail:80 collection. Level 3 is not claimed
because the accepted C91/C89 records do not serialize the earlier production
write that gave T216 a larger committed-fit extent before tail:80.

## Executive result

C91 corrected raw C64 ordinal aliasing. There are 67 post-resume allocation
entries before the tail. Thus raw ordinal 0x93 (decimal 147) is tail:80.
The four crossings normalize to EXTRA3 tail:80, EXTRA0 tail:143, EXTRA1
tail:203, and EXTRA2 tail:263. Three are inside the shared first 216 tail
allocations. The question is therefore why the same managed tail:80 request
encounters different allocator/GC state, not how many extra requests T320
performs.

The locked source expression in gc_heap::allocate_soh is:

    can_use_existing_p = soh_try_fit(..., &commit_failed_p, ...);
    soh_alloc_state =
        can_use_existing_p ? a_state_can_allocate :
        (commit_failed_p ? a_state_trigger_full_compact_gc :
                           a_state_trigger_ephemeral_gc);

The T320 raw-0x93 C64 policy record is callerRequestedGeneration=2,
nInitial=2, collectionReason=5, originBranch=4, collectionOrdinal=4.
Reason 5 is reason_oos_soh. The separate budget gate in
try_allocate_more_space calls trigger_gc_for_alloc(0, reason_alloc_soh, ...).
It cannot produce the observed generation-2/reason-5 call. The exact scalar
that selects the observed full-compaction collection is therefore
commit_failed_p=TRUE after the failed soh_try_fit/refill decision.

The accepted comparison is:

| Trigger operand | T320 before tail:80 | T216 before tail:80 | Unit |
| --- | ---: | ---: | --- |
| Request size | 0x4018 | 0x4018 | bytes |
| Allocation pointer | 0x1018C0910 | 0x1018C0910 | address |
| Context headroom | 0 | at least 0x4030 | bytes |
| T320 committed endpoint | 0x1018C1000 | not serialized | address |
| T320 committed fit end | 0x1018C0FE8 | not serialized | address |
| Current fit span | 0x6D8 | at least 0x4030 | bytes |
| can_use_existing_p | 0 | 1 | Boolean |
| commit_failed_p | 1 on the decisive path | 0/not consumed | Boolean |
| Predicate | full-compaction true | continue-allocation true | Boolean |

T320 tail:80 enters collection 4 and reset/reuse at normalized region
+0x1700000. T216 returns the same 0x4018 object request in collection 3 and
continues the +0x1800000 allocation context. The exact prior production write
that gave T216 the larger committed-fit extent is not present in the accepted
observer fields, so binary/layout causality remains unresolved.

## Locked source audit

Runtime checkout:

    out/dotnet/c52-runtime-source/source-04371d8e
    NativeAOT 9.0.0 AMD64 Workstation GC
    source SHA 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3
    GC interfaces 5.3 / 2

Relevant source locations in src/coreclr/gc/gc.cpp:

| Function | Lines | C92 use |
| --- | ---: | --- |
| gc_heap::a_fit_segment_end_p | 17621–17675 | committed fit, reserved fit/growth, commit_failed_p |
| gc_heap::soh_try_fit | 17896–17979 | free-list/end fit and region refill |
| gc_heap::allocate_soh | 17982–18256 | maps fit and commit failure to allocation state |
| gc_heap::trigger_full_compact_gc | 18462–18522 | sets last_gc_before_oom and requests max generation |
| gc_heap::trigger_gc_for_alloc | 18881–18927 | calls GarbageCollectGeneration |
| gc_heap::try_allocate_more_space | 18949–19060 | separate budget check before allocate_soh |
| gc_heap::new_allocation_allowed | 8085–8138 | budget/timing predicate |
| gc_heap::allocate | 19555–19587 | alloc_ptr plus size versus alloc_limit |
| GCHeap::Alloc | 49905–49997 | production SOH allocation entry |
| GCHeap::GarbageCollectGeneration | 50960–51040 | consumes generation and reason |

The exact budget expression is:

    if (check_budget_p && !(new_allocation_allowed(gen_number)))
        trigger_gc_for_alloc(0,
            ((gen_number == 0) ? reason_alloc_soh : reason_alloc_loh), ...);

The exact fit/refill terminal is:

    next_seg = get_new_region(gen_number);
    if (!next_seg)
    {
        *commit_failed_p = TRUE;
        return FALSE;
    }

The observed generation-2/reason-5 record at raw ordinal 0x93 proves that the
tail:80 collection came through trigger_full_compact_gc, not the generation-0
allocation-budget call. The accepted C65 failure record independently confirms
the same production chain: failed fit/refill, commit_failed=1, state 0xF,
reason_oos_soh, then max-generation collection. At tail:80 the C64
generation/reason record is the available direct boundary; the observer does
not distinguish first soh_try_fit from a post-ephemeral retry.

## Tail:79 and tail:80 chronology

Both cells have collection number 3 at raw ordinals 0x90, 0x91, and 0x92.
They both enter raw ordinal 0x93 with collection number 3. T216 returns:

    objectAddress          0x1018C0928
    allocationPointerAfter 0x1018C4940
    allocationLimitAfter   0x1018C4940
    completed               1

The return interval is exactly 0x4018. With the source minimum-object pad
0x18, the authentic fit demand is 0x4030, so T216 fit succeeds with at least
0x4030 pre-request headroom.

T320 raw ordinal 0x93 records a collection-4 C64 N-INITIAL event before the
return and then returns after reset/reuse:

    objectAddress          0x101700028
    allocationPointerAfter 0x101704040
    allocationLimitAfter   0x101704040
    completed               1

The C89/C67 boundary for T320 records prior active region +0x1800000,
allocation 0x1018C0910, committed endpoint 0x1018C1000, reserved endpoint
0x101900000, and zero context headroom. The committed fit end after the 0x18
pad is 0x1018C0FE8, leaving 0x6D8, which is short of 0x4030 by 0x3958.
The T216 endpoint at this exact boundary is not serialized; its successful
return establishes a committed endpoint lower bound of 0x1018C4958.

The first authenticated semantic transition is tail:80. No earlier
collection-number or managed-return split was found. The C92 result is
source-exact at the collection selector, but the upstream committed-state
producer remains the next trace.

## Shared-prefix arithmetic and downstream results

There are 79 identical tail requests before the split:

    79 * 0x4018 = 0x13C7C8 request bytes
    79 * 0x4030 = 0x13CFD0 fit demand

The demand arithmetic is equal. C90 remains valid:

    request size         0x4018
    fit quantum          0x4030
    extra-tail fit delta 104 * 0x4030 = 0x1A1380
    T216 commit growth   0x20000
    T216 endpoint        0x21000
    T320 endpoint        0x1000

Those later quantitative results are downstream effects and cannot be treated
as the root cause of the first shared-prefix divergence until tail:80 trigger
provenance is fully upstream-resolved.

## Binary/layout boundary

Measured artifact sizes remain correlation only:

    managed PE  T320 890368,  T216 886784
    kernel PE   T320 1563648, T216 1555968
    kernel ELF  T320 3795016, T216 3786824
    .data       0x1200 in both

C92 did not establish the chain selector to exact binary difference to exact
initial heap/commit scalar to commit_failed_p. No same-image experiment was
implemented. C93 should first trace the ordinary committed/region extent
producer one step upstream; only if that is not source-explained should it
perform a tightly controlled same-image experiment.

## Evidence and validation

Evidence root:

    out/dotnet/c011ec92-shared-prefix-tail80-gc-trigger/

The root is separated into source-audit, imported-c91-evidence,
tail79-tail80-comparison, trigger-operand-provenance, and final-confirmations.
C92 adds no runtime observer, no callback, no storage, and no workload value.
Accepted C89/C90/C91 records are reused because they already contain the raw
0x93 allocation/return chronology, the T320 collection-entry record, the
C67/C89 region boundary, and all semantic controls. No QEMU was launched.

## Numbered closeout report

1. Outcome: Outcome G — mixed CONTEXT_FIT plus COMMIT_STATE/region refill; commit_failed_p is the exact collection selector.
2. Success Level: Level 2; Level 3 is not claimed because the prior committed-state write is not serialized.
3. Repository: D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT.
4. Branch: v1.1_DOTNET_SUPPORT.
5. Starting HEAD: 4693e20d5f496f36172ff6c5404d3a114a1bb7ee.
6. Starting subject: Resolve NativeAOT allocation request chronology.
7. Final HEAD: the exact final C92 commit SHA is reported in the final Git closeout.
8. Final subject: Isolate NativeAOT shared-tail GC trigger.
9. Upstream: origin/v1.1_DOTNET_SUPPORT.
10. Starting ahead/behind: 0/0.
11. Final ahead/behind: 1/0; not pushed.
12. Starting worktree: clean.
13. Final worktree: clean after the C92 local commit.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC, interfaces 5.3/2.
15. Runtime source SHA: 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.
16. FP patch SHA: 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.
17. C90 SHA: 77996cad18c83db75c0fd83b6355c957414adb33.
18. C91 SHA: 4693e20d5f496f36172ff6c5404d3a114a1bb7ee.
19. C92 SHA: the exact final local commit SHA is reported in the final Git closeout.
20. Exact C92 question: At managed tail:80, what production operand causes T320 collection 4/reset-reuse while T216 remains in collection 3, and where did it first differ?
21. Managed divergence target: tail:80.
22. Raw ordinal for tail:80: 0x93, decimal 147.
23. Tail request size: 0x4018.
24. Fit quantum: 0x4030.
25. Collection-trigger source file: locked src/coreclr/gc/gc.cpp.
26. Collection-trigger source function: gc_heap::allocate_soh, after gc_heap::soh_try_fit.
27. Exact trigger expression: can_use_existing_p ? can_allocate : (commit_failed_p ? full_compact : ephemeral).
28. Trigger generation: request generation 0; observed full collection request generation 2.
29. Trigger operand units: Boolean predicates; request and endpoints are bytes/addresses.
30. Trigger evaluated before/after fit: commit_failed_p after soh_try_fit; budget predicate before allocate_soh.
31. Trigger evaluated before/after refill: after the current/next-region refill work; exact retry number is not serialized.
32. T320 tail:79 collection number: 3.
33. T216 tail:79 collection number: 3.
34. T320 tail:80 entry collection number: 3.
35. T216 tail:80 entry collection number: 3.
36. T320 tail:80 exit collection number: 4.
37. T216 tail:80 exit collection number: 3.
38. Collection split occurs during tail:80: yes.
39. Earlier split found: no; raw 0x90 through 0x92 agree.
40. Earliest divergence normalized coordinate: tail:80.
41. Earliest divergence raw ordinal: 0x93 / decimal 147.
42. T320 allocation pointer: 0x1018C0910 before the request.
43. T216 allocation pointer: 0x1018C0910 before the request; return ends at 0x1018C4940.
44. T320 fit end/limit: context headroom zero; committed fit end 0x1018C0FE8.
45. T216 fit end/limit: exact endpoint not serialized; successful fit requires at least 0x1018C4940.
46. T320 headroom: context zero; committed fit span 0x6D8.
47. T216 headroom: at least 0x4030.
48. T320 fit result: false on the decisive fit/refill path.
49. T216 fit result: true.
50. T320 trigger scalar 1: can_use_existing_p=0.
51. T216 trigger scalar 1: can_use_existing_p=1.
52. Trigger scalar 1 meaning: soh_try_fit did not yield a usable existing fit.
53. T320 trigger scalar 2: commit_failed_p=1.
54. T216 trigger scalar 2: commit_failed_p=0/not consumed.
55. Trigger scalar 2 meaning: failed committed-fit/region-refill path; true selects full GC.
56. T320 predicate result: full-compaction predicate true.
57. T216 predicate result: continue-allocation predicate true.
58. First causal differing operand: soh_try_fit result, specifically commit_failed_p on T320.
59. Last checkpoint where operand equal: tail:79 completion, collection 3, pointer 0x1018C0910.
60. First checkpoint where operand differs: tail:80 fit/refill decision.
61. Production event causing difference: same 0x4030 demand meets different committed-fit state; exact earlier write is not captured.
62. Event source file: locked src/coreclr/gc/gc.cpp.
63. Event source function: gc_heap::a_fit_segment_end_p and gc_heap::soh_try_fit.
64. Event normalized coordinate: tail:80.
65. T320 value before event: committed 0x1018C1000, allocated 0x1018C0910, context headroom 0.
66. T216 value before event: exact endpoint not serialized; successful fit requires committed endpoint at least 0x1018C4958.
67. T320 value after event: commit_failed_p=1, full-compaction state, collection 4.
68. T216 value after event: can_use_existing_p=1, collection 3 continues.
69. Shared tail requests before divergence: 79.
70. Shared fit demand before divergence: 0x13CFD0.
71. Demand arithmetic equal: yes.
72. Allocation budget arithmetic T320: no causal budget scalar difference is serialized.
73. Allocation budget arithmetic T216: same limitation; budget call has a distinct generation/reason signature.
74. Context arithmetic T320: zero context headroom, 0x6D8 committed span, 0x3958 shortfall.
75. Context arithmetic T216: successful request interval, at least 0x4030 headroom.
76. Commit arithmetic T320: committed 0x1018C1000, fit end 0x1018C0FE8, reserve 0x101900000.
77. Commit arithmetic T216: committed endpoint at least 0x1018C4958; exact value not emitted.
78. Current region T320 before divergence: normalized +0x1800000.
79. Current region T216 before divergence: normalized +0x1800000.
80. Region state T320: current committed fit is insufficient; refill path reaches commit_failed_p.
81. Region state T216: current context remains usable; exact committed/reserved snapshot is absent.
82. EXTRA3 tail:80 interpretation: confirmed shared-prefix reset/reuse crossing.
83. EXTRA0 tail:143 relation: downstream shared-prefix crossing, not T320-only demand.
84. EXTRA1 tail:203 relation: downstream shared-prefix crossing, not T320-only demand.
85. EXTRA2 tail:263 relation: the one crossing in T320's additional 104 iterations.
86. C90 extra-tail fit delta: valid, 0x1A1380.
87. C90 T216 commit-growth result: preserved, 0x1000 to 0x21000 by 0x20000.
88. C90 T320 one-page result: preserved, 0x1000.
89. C88 OOM result: preserved; T320 eligible, T216 not eligible.
90. Earliest supported causal chain: tail:79 state to failed T320 soh_try_fit to commit_failed_p to full GC to collection 4.
91. Strongest causal chain: GCHeap::Alloc to gc_heap::allocate to try_allocate_more_space to allocate_soh to soh_try_fit to commit_failed_p.
92. First unsupported causal link: exact prior production operation that made committed-fit extents differ.
93. Tail selector source semantics: loop-bound only.
94. Managed PE difference: T320 890368; T216 886784.
95. Kernel PE difference: T320 1563648; T216 1555968.
96. ELF difference: T320 3795016; T216 3786824.
97. Binary/layout causality evaluated: yes, only after runtime operand isolation.
98. Binary/layout causality status: unresolved.
99. Runtime scalar plausibly layout-fed: committed heap/region extent is possible but unproven.
100. Exact layout-to-runtime bridge: none proven.
101. Same-image experiment required next: only if ordinary committed-state provenance remains unexplained; recommend C93.
102. Same-image experiment implemented in C92: no.
103. Candidate-path relevance: none; candidate rejection was not entered.
104. B02 evaluated: no.
105. B02 status: STILL_PREMATURE.
106. C92 baseline BSS: inherited whole proof-kernel 0x062A8770; per-cell ONE 0x4F0840 and SIX 0x3947E0.
107. C92 BSS: unchanged; no observer/storage added.
108. BSS delta: 0.
109. Production mutation: none.
110. GC-trigger mutation: none.
111. Allocator mutation: none.
112. Context mutation: none.
113. Commit mutation: none.
114. Collection forcing: none.
115. Region mutation: none.
116. Free-list mutation: none.
117. OOM mutation: none.
118. Planner mutation: none.
119. Candidate mutation: none.
120. Promotion mutation: none.
121. Survivor fabrication: none.
122. Root fabrication: none.
123. C18: inherited pass.
124. Code manager: inherited pass.
125. FindMethodInfo: inherited pass.
126. Root scan: inherited authentic pass.
127. Mark closure: inherited pass.
128. Planner authenticity: inherited pass.
129. Survivor integrity: inherited pass; retained 15 and promoted 0xFD980.
130. C92 invariant failures: 0 in imported accepted confirmations.
131. Sensitive diagnostic allocations: 0.
132. C92 overflow: 0.
133. Fail-fast: 0 in all six imported confirmation boots.
134. Page faults: 0 in all six imported confirmation boots.
135. T320 Boot 1: pass; tail:80/143/203/263, endpoint 1/1/1.
136. T320 Boot 2: pass with the same semantic roles.
137. T320 Boot 3: pass with the same semantic roles.
138. T216 Boot 1: pass; tail:80 collection 3, endpoint 1/6/6.
139. T216 Boot 2: pass with the same semantic roles.
140. T216 Boot 3: pass with the same semantic roles.
141. Semantic agreement: yes, 3/3 per cell for imported accepted controls.
142. Nondeterminism: none in semantic roles, collection split, endpoints, or controls.
143. Serial hashes: T320 88D652BA2D6C67D50F8388E8D85029F9C30ADBFFCD9E151C8F0C28DD8BAFC0EB, FE252C974824AC4DF19AAB066E7CD70CD3439CE5ED8E340991D007248D7BB62E, FF1A738BAD5ECBF152DB0E69C70BAF2731ADA5A286E48B3A973E22AD144110F3; T216 6C6856DCD76904067BE0C7693DD295764B043CD34E52C3C2CD446633C04FF8B3, 847616C8C5A3C36DDA42BC5B0969EFDDFE2E17EF38FD28C8DFF4940B0D290D96, 9924E275E9EDCC3F0C83F03E5F2F33F5AA7A906A87AFB97AD099FCA82B1892B3.
144. Artifact hashes: inherited C91/C89; T320 ELF 979A2F71EB7930871ABDDC80AF282328971A23D4E8833234885E4E8AC231335A and PE 8C1BBB63AD4AED9810B0EC021E608533894E479CD8C52FCAB2AE47AFDDBC906B; T216 ELF 010E8A845CE4ED8FF347A257C140A70258A5608A4D0284BBD16EEBD2D77A0D0A and PE 1CEBF69A5F81D2C3C9D8224BAB0D98F75D6C82BDDB4E824227B76D4822890F30.
145. Runtime-pack validation: inherited pass.
146. Managed build: inherited pass.
147. Native build: inherited pass.
148. PowerShell syntax: pass for accepted harness.
149. JSON parse: pass for imported manifests.
150. git diff --check: pass at closeout.
151. PE to ELF conversion: inherited pass.
152. Symbol checks: inherited pass.
153. Linker/source/table/archive guards: inherited pass.
154. C52 Tier-All result or reason omitted: omitted; no runtime composition changed.
155. Ordinary restoration: pass.
156. Ordinary kernel SHA: 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.
157. Ordinary ESP SHA: 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.
158. Proof artifact active: no.
159. C92-owned QEMU cleanup: 0; C92 launched no QEMU.
160. Unrelated QEMU preservation: preserved.
161. Files changed: tracked C92 documentation and ignored C92 evidence notes; no runtime or managed source.
162. Documentation path: docs/dotnet/NATIVEAOT_WORKSTATION_GC_C92_SHARED_PREFIX_TAIL80_GC_TRIGGER.md.
163. Evidence root: out/dotnet/c011ec92-shared-prefix-tail80-gc-trigger/.
164. Final commit: local C92 commit; the exact final SHA is reported in the final Git closeout.
165. Push status: not pushed.
166. Remaining limitation: exact prior production writer of the committed-fit difference remains unresolved; layout is correlation only.
167. Exact next-smallest milestone: trace the ordinary committed/region extent producer at the last equal tail:79 state with one existing scalar/slot, BSS delta 0; only then consider C93 same-image work.

## C92 conclusion

The exact production operand that says collect is commit_failed_p, after
soh_try_fit fails to produce a usable fit/refill and allocate_soh selects
a_state_trigger_full_compact_gc. T320 reaches that branch at tail:80, then
requests generation 2 with reason_oos_soh and enters collection 4. T216
satisfies the same 0x4030 fit demand in collection 3. The trigger is isolated;
the next milestone is the ordinary upstream committed-state transition.

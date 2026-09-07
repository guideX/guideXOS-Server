# NativeAOT Workstation GC C91: allocation-request ordinal provenance

Status: **Outcome F — mixed ordinal aliasing plus a real shared-tail allocator divergence**
Success level: **Level 3 for normalized chronology and earliest divergence**
Date: 2026-09-07

## Executive result

C90 successfully closed the fit quantum, raw/fit deltas, carry-in arithmetic,
the T216 commit growth, and the T320/T216 committed endpoints. It also falsified
the assumption that all four T320 region crossings arise from tail iterations
217–320.

C91 proves that the C90 “request ordinal” is the C64 post-resume
`allocationCount`/`allocationOrdinal`. It is incremented at the C64 managed
allocation-entered callback, once per observed `RhpNewArray` entry, not once per
managed tail iteration and not once per C65 `soh_try_fit` attempt. The accepted
workload has 67 C64-observable pre-tail allocation entries, so:

```text
tail iteration = C64 allocation ordinal - 67
```

Consequently, C90 raw ordinals `0x93`, `0xD2`, `0x10E`, and `0x14A` normalize
to `tail:80`, `tail:143`, `tail:203`, and `tail:263`. Three named crossings are
genuinely inside the shared first 216 tail iterations; only EXTRA2 is after
`tail:216` and is chargeable to the additional 104 T320 iterations.

C91 also locates the first real allocator divergence at `tail:80` / raw C64
ordinal `0x93`: T320 enters a collection-4 reset/reuse region transition while
T216 continues the same managed allocation in its collection-3 context. This
is a real early divergence, not just a label error. The source proves that the
216/320 selector changes the tail loop bound only; the accepted artifacts prove
the compiled images differ in size/section placement, but do not prove that
binary layout is the cause of the tail-80 transition. C91 therefore records
that causal link as the remaining narrow limitation and does not claim Outcome
E.

No runtime instrumentation, allocator policy, workload size, request size,
free-list, commit, OOM, promotion, survivor, or root behavior was changed.
No third tail value was run. B02 was not run.

## Numbered closeout report

1. **Outcome:** Outcome F — ordinal aliasing exists and a real earlier T320/T216 allocator divergence exists.
2. **Success Level:** Level 3 for normalized chronology: all four crossings are normalized, the shared-prefix divergence is isolated, the causal decomposition is internally consistent, and the accepted 3/3 controls agree. The selector-to-binary-layout mechanism remains explicitly unproven.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `77996cad18c83db75c0fd83b6355c957414adb33`.
6. **Starting subject:** `Close NativeAOT allocation fit arithmetic`.
7. **Final HEAD:** local C91 closeout commit; exact SHA is recorded in the final handoff and Git closeout.
8. **Final subject:** `Resolve NativeAOT allocation request chronology`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** `0/0` from live Git state. This is authoritative and differs from the requested expected ahead `1`.
11. **Final ahead/behind:** `1/0`; not pushed.
12. **Starting worktree:** clean.
13. **Final worktree:** clean after the local C91 commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3/2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C89 SHA:** `bb383ce55ccf50d49d61287f2c5d026f7f6ecbf8`.
18. **C90 SHA:** `77996cad18c83db75c0fd83b6355c957414adb33`.
19. **C91 SHA:** local closeout commit created by this report; exact SHA is recorded after commit.
20. **Exact C91 question:** Why do three authenticated T320 EXTRA-region acquisitions appear before recorded request 217 even though T320 and T216 differ only in tail count?
21. **Raw C90 request-ordinal variable:** C89/C90 `allocationOrdinal`, sourced from C64 `allocationCount` and serialized through C67 `planGenerationBefore`.
22. **Ordinal source file:** `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`.
23. **Ordinal initialization:** C64 `Start` calls `guideXosNativeAotC011EC64Reset`, which byte-clears the lifecycle record; `allocationCount` therefore starts at zero.
24. **Ordinal increment site:** `guideXosNativeAotC011EC64ManagedAllocationEntered`, source line 20733 in the audited tree: `const uint32_t ordinal = ++r.allocationCount;`.
25. **Ordinal reset site:** `guideXosNativeAotC011EC64Start`, source lines 20795–20803, via `guideXosNativeAotC011EC64Reset`.
26. **Event types counted:** post-resume C64 managed allocation-entered callbacks that reach the C64 gate; in this workload those are `RhpNewArray` entries.
27. **One managed allocation equals one ordinal:** one post-resume `RhpNewArray` entry equals one C64 ordinal; one user-level phase or whole-process managed allocation count does not necessarily equal one C64 ordinal.
28. **Retry attempts counted:** no. C65 `allocationAttemptCount` and C65 event records count allocator attempt callbacks separately.
29. **Setup allocations counted:** only if they occur after the C64 `managedResumeObserved` gate; earlier setup allocations are not counted by this ordinal.
30. **Retained allocations counted:** only post-resume retained/cohort allocations that reach the C64 callback; the source-local `allocationOrdinal` is a different counter.
31. **Tail allocations counted:** yes, exactly once per tail `RhpNewArray` in the accepted tail loop.
32. **Non-tail allocations counted:** yes when post-resume and callback-visible; the accepted records contain 67 `0x10018` pre-tail entries.
33. **Managed proof source:** `samples/managed/HostLogProof/Program.cs`.
34. **Tail selector source:** `samples/managed/HostLogProof/HostLogProof.csproj` property/define mapping, then `Program.cs` lines 1188–1193 and 1575.
35. **Tail selector semantic effect:** `HOSTLOGPROOF_C011EC66_T216` selects 216 and `HOSTLOGPROOF_C011EC64_W3` selects 320 for the same `new byte[postDebitPayloadSize]` loop; no pre-loop workload branch was found.
36. **Tail selector binary/layout effect:** T320 and T216 produce different NativeAOT managed and kernel images, sizes, section placement, and hashes; C91 does not treat that fact alone as causal.
37. **Managed allocation phases:** setup/runtime warmup; early retained cohort; early transient pressure; five main pressure cohorts; natural promotion/debit/resume; post-resume pre-tail pressure; post-debit tail; proof completion.
38. **Pre-tail allocations T320:** source phases are identical to T216; C64 observes 67 post-resume `0x10018` entries before tail raw ordinal 68. Earlier pre-resume source allocations are outside this ordinal.
39. **Pre-tail allocations T216:** same 67 C64-observable `0x10018` entries before tail raw ordinal 68; source phase structure is identical.
40. **Pre-tail semantic equivalence:** yes for source counts, sizes, retained cohort, promotion/debit control, and the observed 67-entry prefix; absolute diagnostic addresses differ between separately built images.
41. **Tail iterations T320:** 320.
42. **Tail iterations T216:** 216.
43. **First shared tail iteration:** `tail:1`.
44. **Last shared tail iteration:** `tail:216`.
45. **First extra T320 iteration:** `tail:217`.
46. **Runtime ordinal corresponding to tail iteration 1:** C64 ordinal `68` (`0x44`).
47. **Runtime ordinal corresponding to tail iteration 216:** C64 ordinal `283` (`0x11B`).
48. **Runtime ordinal corresponding to tail iteration 217:** C64 ordinal `284` (`0x11C`), T320 only.
49. **Runtime ordinal corresponding to tail iteration 320:** C64 ordinal `387` (`0x183`), T320 only.
50. **Mapping exact or bounded:** tail mapping is exact; the source allocation identity of the earlier 67-entry post-resume prefix is bounded because the C64 gate does not serialize the source-local phase index.
51. **EXTRA0 raw ordinal:** `0xD2` = 210.
52. **EXTRA0 managed phase:** `tail`.
53. **EXTRA0 managed iteration:** `tail:143`.
54. **EXTRA0 T216 counterpart:** T216 executes `tail:143`, but has no equivalent `+0x1400000` target acquisition; it remains in its own continuing region context.
55. **EXTRA1 raw ordinal:** `0x10E` = 270.
56. **EXTRA1 managed phase:** `tail`.
57. **EXTRA1 managed iteration:** `tail:203`.
58. **EXTRA1 T216 counterpart:** T216 executes `tail:203`, but has no equivalent `+0x1500000` target acquisition.
59. **EXTRA2 raw ordinal:** `0x14A` = 330.
60. **EXTRA2 managed phase:** `tail`.
61. **EXTRA2 managed iteration:** `tail:263`.
62. **EXTRA2 T216 counterpart:** none; T216 ends at `tail:216`.
63. **EXTRA3 raw ordinal:** `0x93` = 147.
64. **EXTRA3 managed phase:** `tail`.
65. **EXTRA3 managed iteration:** `tail:80`.
66. **EXTRA3 T216 counterpart:** T216 executes `tail:80`, but continues in `+0x1800000`; it has no equivalent reset/reuse acquisition.
67. **Number of crossings truly after tail216:** 1 — EXTRA2 at `tail:263`.
68. **Number truly within shared first216:** 3 — EXTRA3 at 80, EXTRA0 at 143, and EXTRA1 at 203.
69. **Number outside tail phase:** 0 for the four named C90 EXTRA0–EXTRA3 crossings.
70. **“Three before request217” explanation:** “request 217” was a tail coordinate only after subtracting the 67 preceding C64-observable allocation entries. The three crossings are real shared-tail events at 80, 143, and 203; their raw labels are 147, 210, and 270.
71. **T320 pre-tail allocation pointer:** raw C64 first-tail snapshot `0x0`; this is an invalid/empty pre-refill context field, not a usable fit pointer.
72. **T216 pre-tail allocation pointer:** raw C64 first-tail snapshot `0x0`; same diagnostic invalid/empty context shape.
73. **T320 pre-tail limit:** raw C64 first-tail snapshot `0x4EE9AD0`.
74. **T216 pre-tail limit:** raw C64 first-tail snapshot `0x4EE7AD0`.
75. **T320 pre-tail headroom:** not a valid production headroom value because the pointer is zero; raw `limit - pointer` is `0x4EE9AD0`, explicitly not used as fit budget.
76. **T216 pre-tail headroom:** not a valid production headroom value because the pointer is zero; raw `limit - pointer` is `0x4EE7AD0`, explicitly not used as fit budget.
77. **Pre-tail committedSize:** not serialized as one global C64 checkpoint. The source initial region commit invariant is `0x1000`; C91 does not invent a global committed size from the invalid context snapshot.
78. **Pre-tail state equal:** semantically yes; absolute context addresses and invalid wrapper limits differ because the images are separately laid out.
79. **T320 after-tail216 pointer:** raw C64 post-allocation pointer `0x1015382B0` in the accepted T320 first run.
80. **T216 after-tail216 pointer:** raw C64 post-allocation pointer `0x1019CC9A0` in the accepted T216 first run.
81. **T320 after-tail216 limit:** raw C64 post-allocation limit equals `0x1015382B0`; C90 fit-ledger limit is the selected committed fit end `0x1015F0FE8`.
82. **T216 after-tail216 limit:** raw C64 post-allocation limit equals `0x1019CC9A0`; the corresponding source-fit end is `0x1019F0FE8` for the active `+0x1900000` context.
83. **T320 after-tail216 headroom:** `0x1015F0FE8 - 0x1015382B0 = 0xB8D38` in the C90 fit ledger.
84. **T216 after-tail216 headroom:** `0x1019F0FE8 - 0x1019CC9A0 = 0x24648` in the analogous source-fit span; raw wrapper limit-after is the object-end snapshot and is not the committed fit end.
85. **State equal through shared 216:** no.
86. **Earliest true divergence checkpoint:** C64 raw ordinal `0x93` / C67 first `get_free_region` source event at the tail-80 boundary.
87. **Earliest divergence managed phase:** `tail`.
88. **Earliest divergence managed iteration:** `tail:80`.
89. **Earliest divergence runtime function:** `RhpNewArray` → stock `GcAllocInternal`/`GCHeap::Alloc` → `gc_heap::allocate`/`soh_try_fit` → C67 `get_free_region`/`get_new_region` source boundary.
90. **T320 differing state:** collection 4; reset/reuse of `+0x1700000`; after-pointer `0x101704040`; subsequent T320 tail interval begins at `tail:80`.
91. **T216 differing state:** collection 3; continues `+0x1800000`; after-pointer `0x1018C4940`; no equivalent reset/reuse acquisition.
92. **Divergence caused by loop count:** no. Both cases execute `tail:80`; the nominal extra loop iterations do not exist yet.
93. **Divergence caused by pre-loop semantics:** no source-level pre-loop semantic difference was found; setup, retained counts, object sizes, pressure cohorts, and the loop body are shared.
94. **Divergence caused by binary layout:** not established. The binaries differ, but no method-RVA/stack/unwind-to-tail-80 causal chain is authenticated.
95. **Binary-layout causal evidence:** managed PE sizes are T320 `890368` and T216 `886784`; proof-kernel PE sizes are T320 `1563648` and T216 `1555968`; proof-kernel ELF sizes are T320 `3795016` and T216 `3786824`. Managed `.text/.rdata/.pdata` sizes also differ, while `.data` is `0x1200` in both. Hashes differ. These prove layout difference, not causality.
96. **Normalized coordinate definition:** `phaseName:managedIteration`; the tail coordinate is exact and raw C64 ordinal is retained beside it.
97. **EXTRA0 normalized coordinate:** `tail:143`.
98. **EXTRA1 normalized coordinate:** `tail:203`.
99. **EXTRA2 normalized coordinate:** `tail:263`.
100. **EXTRA3 normalized coordinate:** `tail:80`.
101. **Raw ordinal retained for evidence:** yes; raw C64 ordinals remain `0xD2`, `0x10E`, `0x14A`, and `0x93`.
102. **C90 `0x1A1380` delta still valid:** yes, exactly `104 * 0x4030`.
103. **Requests 217–320 fit-cost contribution:** `46 * 0x4030 + 58 * 0x4030 = 0xB88A0 + 0xE8AE0 = 0x1A1380`.
104. **Crossings attributed to extra delta:** 1 — EXTRA2 at `tail:263`.
105. **Crossings not attributed to extra delta:** 3 — EXTRA3, EXTRA0, and EXTRA1 at `tail:80`, `tail:143`, and `tail:203`.
106. **Earlier-state contribution:** the three earlier crossings consume/reset contexts before `tail:217`; their exact source topology is observed but not reducible to the post-216 delta.
107. **Four-region decomposition equation:** `3 shared-first-216 crossings + 1 extra-tail crossing = 4 named T320 crossings`.
108. **Revised carry-in ledger:** `tail:217–262`, 46 requests, `0xB88A0` fit cost, `0x498` remainder, then EXTRA2 at `tail:263`.
109. **Revised EXTRA0 ledger:** `tail:143–202`, 60 requests, `0xF0B40` fit cost, `0x498` remainder; it is earlier-state chronology, not extra-tail delta.
110. **Revised EXTRA1 ledger:** `tail:203–262`, 60 requests, `0xF0B40` fit cost, `0x498` remainder; it is earlier-state chronology, not extra-tail delta.
111. **Revised EXTRA2 ledger:** `tail:263–320`, 58 requests, `0xE8AE0` fit cost, `0x84F8` final residual.
112. **Revised EXTRA3 ledger:** `tail:80–142`, 63 requests, `0xFCBD0` fit cost, `0x3408` remainder; reset/reuse topology is required.
113. **Ledger residual:** zero in the fit-cost identity; chronology residual is the missing independently serialized pre-`tail:217` topology/layout cause.
114. **Residual explanation:** the three early crossings are not hidden extra-tail bytes; they belong to shared-tail context consumption and reset/reuse.
115. **C90 request263 interpretation corrected/confirmed:** confirmed as normalized `tail:263`, not raw C64 request ordinal 263. Its raw C64 ordinal is 330 (`0x14A`).
116. **T216 `0x21000` result preserved:** yes; raw shortfall `0x1F190`, page-aligned growth `0x20000`, initial `0x1000`, final `0x21000`.
117. **T320 `0x1000` result preserved:** yes; no successful equivalent target commit growth.
118. **OOM predicate preserved:** yes; T320 `committedSize == 0x1000 && joined_last_gc_before_oom` is true, T216 fails the size operand at `0x21000`.
119. **EXTRA4 path preserved:** yes; EXTRA4 remains the downstream one-page OOM/decommit consumer, not a fifth ordinary tail allocation.
120. **Strongest causal chain:** C64 ordinal alias (`67` pre-tail entries) → normalized C90 crossings → first true shared-tail divergence at `tail:80` → three shared-prefix crossings plus one extra-tail crossing → C90 carry-in/commit arithmetic and preserved OOM endpoints.
121. **First unsupported causal link:** why the two separately compiled images reach different natural collection/region state at the same shared `tail:80` prefix when source semantics before `tail:217` are identical; binary layout is a candidate, not a demonstrated cause.
122. **Candidate-path relevance:** none; no candidate rejection path was entered and no candidate mutation was made.
123. **B02 evaluated:** no.
124. **B02 status:** `STILL_PREMATURE`.
125. **C91 baseline BSS:** inherited C89 whole proof-kernel `.bss` section `0x062A8770`; inherited per-cell diagnostic BSS ONE `0x4F0840`, SIX `0x3947E0`.
126. **C91 BSS:** unchanged from C89/C90; no C91 observer or slot was added.
127. **BSS delta:** `0`.
128. **Production mutation:** none.
129. **Allocator mutation:** none.
130. **Context mutation:** none.
131. **Commit mutation:** none.
132. **Workload mutation:** none; no new tail value and no runtime workload count change.
133. **Request mutation:** none; request remains `0x4018`.
134. **Free-list mutation:** none.
135. **OOM mutation:** none.
136. **Decommit mutation:** none.
137. **Candidate mutation:** none.
138. **Planner mutation:** none.
139. **Promotion mutation:** none.
140. **Survivor fabrication:** none.
141. **Root fabrication:** none.
142. **C18:** inherited pass; startup, manager, and GC safety gate remained valid.
143. **Code manager:** inherited pass; registered NativeAOT managed code manager remained valid.
144. **`FindMethodInfo`:** inherited pass on the valid control path.
145. **Root scan:** inherited pass; C91 added no root behavior.
146. **Mark closure:** inherited pass; C91 added no mark behavior.
147. **Planner authenticity:** inherited pass; source planner remained unmodified.
148. **Survivor integrity:** pass in imported confirmations; retained cohort 15 and promoted bytes `0xFD980` agree.
149. **C91 invariant failures:** `0` in the imported accepted C89 confirmation records; C91 added no runtime state.
150. **Sensitive diagnostic allocations:** `0` in imported accepted records.
151. **C91 overflow:** `0`; no C91 arrays or callbacks were added.
152. **Fail-fast:** `0` in all six accepted confirmation boots.
153. **Page faults:** `0` in all six accepted confirmation boots.
154. **T320 Boot 1:** pass; normalized EXTRA3 `tail:80`, EXTRA0 `tail:143`, EXTRA1 `tail:203`, EXTRA2 `tail:263`; `1/1/1`, `0x1000`.
155. **T320 Boot 2:** pass with the same normalized roles and semantic controls.
156. **T320 Boot 3:** pass with the same normalized roles and semantic controls.
157. **T216 Boot 1:** pass; no equivalent EXTRA0–EXTRA3 targets; `1/6/6`, `0x21000`.
158. **T216 Boot 2:** pass with the same normalized roles and semantic controls.
159. **T216 Boot 3:** pass with the same normalized roles and semantic controls.
160. **Semantic agreement:** yes, 3/3 per cell; normalized crossing roles and first-divergence role agree.
161. **Nondeterminism:** none observed in normalized roles, crossing count, endpoint, eligibility, or semantic controls. Raw serial hashes remain boot-specific.
162. **Serial hashes:** imported C89 T320: `88D652BA2D6C67D50F8388E8D85029F9C30ADBFFCD9E151C8F0C28DD8BAFC0EB`, `FE252C974824AC4DF19AAB066E7CD70CD3439CE5ED8E340991D007248D7BB62E`, `FF1A738BAD5ECBF152DB0E69C70BAF2731ADA5A286E48B3A973E22AD144110F3`; T216: `6C6856DCD76904067BE0C7693DD295764B043CD34E52C3C2CD446633C04FF8B3`, `847616C8C5A3C36DDA42BC5B0969EFDDFE2E17EF38FD28C8DFF4940B0D290D96`, `9924E275E9EDCC3F0C83F03E5F2F33F5AA7A906A87AFB97AD099FCA82B1892B3`.
163. **Artifact hashes:** T320 proof-kernel ELF `979A2F71EB7930871ABDDC80AF282328971A23D4E8833234885E4E8AC231335A`, kernel PE `8C1BBB63AD4AED9810B0EC021E608533894E479CD8C52FCAB2AE47AFDDBC906B`; T216 proof-kernel ELF `010E8A845CE4ED8FF347A257C140A70258A5608A4D0284BBD16EEBD2D77A0D0A`, kernel PE `1CEBF69A5F81D2C3C9D8224BAB0D98F75D6C82BDDB4E824227B76D4822890F30`.
164. **Runtime-pack validation:** inherited C89 pass; runtime identity and locked source SHA match.
165. **Managed build:** inherited pass for both accepted cells; managed source was not changed.
166. **Native build:** inherited pass for both accepted cells; native runtime source was not changed.
167. **PowerShell syntax:** pass for the existing accepted build/confirmation scripts; C91 added no script.
168. **JSON parse:** pass for imported manifests; no C91 JSON producer was added.
169. **`git diff --check`:** pass at closeout.
170. **PE → ELF conversion:** inherited pass; converter logs and final ELF artifacts are present in both imported roots.
171. **Symbol checks:** inherited pass; C91 uses source line provenance and accepted symbol audits.
172. **Linker/source/table/archive guards:** inherited pass; no composition change invalidates them.
173. **C52 Tier-All result or reason omitted:** omitted because C91 is an offline ordinal/chronology remap and does not add a runtime composition; C52 Tier-All would not add evidence.
174. **Ordinary restoration:** pass; ordinary kernel and ESP artifacts were already restored by the accepted C89 confirmation closeout and C91 did not activate proof artifacts.
175. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
176. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
177. **Proof artifact active:** no; only imported evidence artifacts are under the ignored C89/C90/C91 roots.
178. **C91-owned QEMU cleanup:** `0`; C91 launched no QEMU.
179. **Unrelated QEMU preservation:** preserved; no unrelated process was targeted.
180. **Files changed:** tracked C91 documentation; ignored C91 evidence files under `out/dotnet/c011ec91-allocation-request-ordinal-provenance/`. No runtime or managed source file changed.
181. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C91_ALLOCATION_REQUEST_ORDINAL_PROVENANCE.md`.
182. **Evidence root:** `out/dotnet/c011ec91-allocation-request-ordinal-provenance/`, separated into source audit, managed workload mapping, imported C89/C90 evidence, normalized chronology, revised fit ledger, and final confirmations.
183. **Final commit:** local subject `Resolve NativeAOT allocation request chronology`; exact SHA is in the Git closeout below after commit.
184. **Push status:** not pushed.
185. **Remaining limitation and exact next-smallest milestone:** the ordinal meaning and earliest real divergence are resolved, but the selector-to-layout/runtime-state causal link at `tail:80` is not. The next-smallest milestone is a source-backed comparison of only the existing `tail:80` `RhpNewArray` → `soh_try_fit` boundary using one reused scalar or existing proof state, with BSS delta `0`; do not add a tail value, reopen broad region provenance, reopen promotion sweeps, or run B02.

## C91 conclusion

The corrected decomposition is:

```text
four observed T320 named crossings
  = EXTRA3 at tail:80   (shared first 216; reset/reuse)
  + EXTRA0 at tail:143  (shared first 216)
  + EXTRA1 at tail:203  (shared first 216)
  + EXTRA2 at tail:263  (additional T320 tail)
```

The C90 fit identity remains exact:

```text
104 * 0x4030 = 0x1A1380
0xB8D38 = 46 * 0x4030 + 0x498
0x1F190 -> align_on_page -> 0x20000
0x1000 + 0x20000 = 0x21000       T216
0x1000 + 0x00000 = 0x1000        T320
```

The correction is temporal and provenance-based: raw C64 ordinals are not tail
ordinals. The first true T320/T216 allocator divergence is `tail:80`, before
the nominal extra interval begins. C91 therefore stops the arithmetic model at
the correct boundary and leaves only the earliest shared-tail divergence for a
future narrowly scoped trace.

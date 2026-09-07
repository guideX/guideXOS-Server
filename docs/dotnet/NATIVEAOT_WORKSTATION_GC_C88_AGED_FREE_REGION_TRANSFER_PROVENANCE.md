# NativeAOT Workstation GC C88: aged free-region transfer and EXTRA4 decommit provenance

C88 answers the narrow C87 boundary: EXTRA4 is transferred before the final
free-region balance checkpoint, but the transfer is not caused by the explicit
age threshold. The locked `gc_heap::distribute_free_regions` predicate is an
OR. On T320, `age_in_free` is `0`, below `0x14`, while the one-page OOM clause
is true. On T216, `age_in_free` is also `0` at evaluation, the committed size
is `0x21000` rather than `0x1000`, and the OOM clause is false. The result is
Outcome G: the aged/decommit label is a consequence of the earlier OOM-special
eligibility branch, not an age threshold crossing.

The observer is target-filtered to normalized EXTRA4 and reuses the inherited
C67 scalar event state plus immediate serial emission. It adds no C88 event
array or mutable C88 storage. C88 does not run B02 and does not mutate age,
epoch, list, allocator, generation, candidate, planner, promotion, survivor,
or root state.

Evidence was collected under
`out/dotnet/c011ec88-aged-free-region-transfer/` with three fresh QEMU boots
for each of T320 (`15mid8`, tail 320) and T216 (`15mid8`, tail 216). The
per-cell manifests intentionally report the exact C88 trace result at Level 1;
the cross-cell report below is Level 3 because it closes the requested
comparison and regression gates.

1. **Outcome:** Outcome G — the aged label is consequence, not cause; T320 EXTRA4 passes the one-page OOM eligibility clause, not the age threshold.
2. **Success Level:** Level 3 cross-cell closeout; each generated per-cell C88 manifest records Level 1 exact-path evidence.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `669259ab82581e30ba61e6d56f122751cde1bd23`.
6. **Starting subject:** `Isolate NativeAOT decommit region balance`.
7. **Final HEAD:** The final local C88 commit; its exact immutable SHA is recorded in `out/dotnet/c011ec88-aged-free-region-transfer/final-commit.txt` and the final handoff.
8. **Final subject:** `Trace NativeAOT aged region decommit transfer`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** ahead 1, behind 0.
11. **Final ahead/behind:** ahead 1, behind 0 against the current `origin/v1.1_DOTNET_SUPPORT`; the upstream ref advanced to include C87 during the work, while C88 itself was not pushed.
12. **Starting worktree:** Clean.
13. **Final worktree:** Clean after the local commit; generated evidence remains under the ignored `out/dotnet` tree.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C86 SHA:** `63bc5e61a208ba8b3637a72fa9355bc4ab8afcf6`.
18. **C87 SHA:** `669259ab82581e30ba61e6d56f122751cde1bd23`.
19. **C88 SHA:** The exact immutable SHA is recorded in `out/dotnet/c011ec88-aged-free-region-transfer/final-commit.txt` and the final handoff.
20. **Exact C88 question:** What production condition transfers normalized EXTRA4 on T320 while the equivalent T216 region remains basic-free, and is that transfer downstream of tail320 allocation chronology or an independent age predicate?
21. **T320 baseline:** `15mid8`, retained 15, promoted `0xFD980`, tail 320, authentic promotion, `1 / 1 / 1`.
22. **T216 baseline:** Same retained/promotion workload, tail 216, authentic promotion, `1 / 6 / 6`.
23. **T320 final basic count:** `0x1` at post-Restart and post-resume in all three boots.
24. **T216 final basic count:** `0x6` at post-Restart and post-resume in all three boots.
25. **C87 final balance T320:** `4 + 0 - 18 = -14`.
26. **C87 final balance T216:** `6 + 0 - 18 = -12`.
27. **Final balance causal status:** Negative final balance does not prevent an already-populated aged/global-decommit queue; it is not the cause of the observed T320 EXTRA4 transfer.
28. **EXTRA4 normalized range:** `[0x1A00000, 0x1B00000)` relative to `g_gc_lowest_address`; `heap_segment_mem` is normalized `0x1A00028` and `get_region_start` is normalized `0x1A00000`.
29. **EXTRA4 T320 birth:** C77 commit/create birth ordinals `0x90 / 0x91`, region descriptor `0x10401190`, reserved envelope ending at `0x101B00000`.
30. **EXTRA4 T216 birth:** C77 commit/create birth ordinals `0xA2 / 0xA3`, same normalized target role and reserved envelope.
31. **EXTRA4 T320 final basic insertion:** C88 `LIST_ADD`, C67 event ordinal `0xA5`, age `0`, basic-free list count `6 -> 7`.
32. **EXTRA4 T216 final basic insertion:** C88 `LIST_ADD`, C67 event ordinal `0xBF`, age `0`, basic-free list count `6 -> 7`.
33. **Aged-transfer source file:** `out/dotnet/pal-runtime-active-replacement-build/locked-source/src/coreclr/gc/gc.cpp`.
34. **Aged-transfer source function:** `gc_heap::distribute_free_regions`.
35. **Source list:** Per-heap `hp->free_regions[kind]`, a `region_free_list` iterated from `get_first_free_region()`.
36. **Destination list:** `global_regions_to_decommit[kind]`, populated by `region_free_list::add_region` after `unlink_region`.
37. **Eligibility expression:** `heap_segment_age_in_free(region) >= min(max(AGE_IN_FREE_TO_DECOMMIT,n_heaps),MAX_AGE_IN_FREE) || (get_region_committed_size(region) == GC_PAGE_SIZE && joined_last_gc_before_oom)`.
38. **Age representation:** Explicit `heap_segment_age_in_free(region)` backed by the `age_in_free` field in `heap_segment`.
39. **Age unit:** Count of end-of-GC free-list aging passes/collections; it is not a timestamp, allocator epoch, or address-list index.
40. **Age initialization source:** `region_free_list::add_region_in_descending_order` sets `heap_segment_age_in_free(region_to_add) = 0`; C88 T320/T216 `LIST_ADD` records confirm `0`.
41. **Age increment/update source:** `region_free_list::age_free_regions`; `gc_heap::age_free_regions` invokes it after `distribute_free_regions` at the end-of-GC path.
42. **Age reset source:** Descending-order insertion resets age; `unlink_region` and `transfer_regions` do not reset it, while `add_region_front` does not reset it either.
43. **Age threshold:** `min(max(AGE_IN_FREE_TO_DECOMMIT,n_heaps),MAX_AGE_IN_FREE)`, observed as `0x14` (`20`) with `AGE_IN_FREE_TO_DECOMMIT=20` and `MAX_AGE_IN_FREE=99`.
44. **T320 age/free-since value:** `age_in_free=0` at `AGE_EVAL`, initialized at `0`, maximum observed target age `0`.
45. **T216 age/free-since value:** `age_in_free=0` at `AGE_EVAL`, then one observed `AGE_UPDATE` to `1`; maximum observed target age `1`.
46. **T320 threshold result:** Age comparison false: `0 >= 20` is false; OOM-special comparison true: `0x1000 == GC_PAGE_SIZE` and `joined_last_gc_before_oom=1`.
47. **T216 threshold result:** Age comparison false: `0 >= 20` is false; OOM-special comparison false: `0x21000 != GC_PAGE_SIZE` and `joined_last_gc_before_oom=0`.
48. **T320 state before eligibility:** `state=0x0A` (`AGE_EVAL`).
49. **T216 state before eligibility:** `state=0x0A` (`AGE_EVAL`).
50. **T320 generation before eligibility:** `generation=0`.
51. **T216 generation before eligibility:** `generation=0`.
52. **T320 list ordinal:** `listOrdinal=5` at `AGE_EVAL`; final basic insertion was ordinal `7`.
53. **T216 list ordinal:** `listOrdinal=6` at `AGE_EVAL`; final basic insertion was ordinal `7`.
54. **T320 address-order role:** None for the target event; C88 observed no target `ADDRESS_ORDER_MOVE`, so `move_highest_free_regions` did not select EXTRA4 in this transfer.
55. **T216 address-order role:** None for the target; no target address-order move or decommit transfer occurred.
56. **T320 eligibility result:** Combined predicate true only through `oomEligible=1`; `ageEligible=0`.
57. **T216 eligibility result:** Combined predicate false: `ageEligible=0`, `oomEligible=0`.
58. **Earliest eligibility divergence ordinal:** The shared stage is `AGE_EVAL`; raw C67 ordinals are T320 `0xAA` and T216 `0xC3`. The first semantic divergence is the OOM operands, not the raw ordinal.
59. **Divergence source function:** `gc_heap::distribute_free_regions`.
60. **Divergence source expression:** The second OR branch, `(get_region_committed_size(region) == GC_PAGE_SIZE && joined_last_gc_before_oom)`.
61. **T320 operands:** `age=0`, `threshold=0x14`, `committedSize=0x1000`, `joined_last_gc_before_oom=1`, `ageEligible=0`, `oomEligible=1`, `state=0x0A`.
62. **T216 operands:** `age=0`, `threshold=0x14`, `committedSize=0x21000`, `joined_last_gc_before_oom=0`, `ageEligible=0`, `oomEligible=0`, `state=0x0A`.
63. **EXTRA0 removal occurs before age decision:** Yes in the accepted C86/C87 same-workload chronology; C88 remains target-only and does not re-census EXTRA0.
64. **EXTRA1 removal occurs before age decision:** Yes in the accepted C86/C87 chronology; it precedes the target eligibility checkpoint.
65. **EXTRA2 removal occurs before age decision:** Yes in the accepted C86/C87 chronology; it precedes the target eligibility checkpoint.
66. **EXTRA3 removal occurs before age decision:** Yes in the accepted C86/C87 chronology; it precedes the target eligibility checkpoint.
67. **Four acquisitions affect age state:** They do not increment EXTRA4's `age_in_free`; the target is `0` at T320 evaluation and `0` at T216 evaluation. They affect the downstream committed/OOM state consumed by the predicate.
68. **Four acquisitions affect list ordering:** The target's observed basic-list ordinal differs at evaluation (`5` T320 versus `6` T216), so topology differs; C88 does not claim that every intervening pointer change is solely attributable to one acquisition.
69. **Four acquisitions affect address-selection role:** No target `move_highest_free_regions` event was observed; address-order selection is not the causal role for EXTRA4 in C88.
70. **Four acquisitions affect pass invocation:** The aging pass exists on both sides; T320's target is transferred during distribution before its target-specific age update, while T216's target remains and records `AGE_UPDATE`.
71. **T320 aged-list count before:** At target `AGE_EVAL`, source basic list count is `5` and `global_regions_to_decommit` count is `1`.
72. **T320 aged-list count after:** Target transfer changes the source list count `5 -> 4` and global count `1 -> 2`.
73. **T216 aged-list count:** At `AGE_EVAL`, source basic list count is `7` and global count is `0`; no target transfer count change occurs.
74. **Another T216 region aged/transferred:** No equivalent target transfer is claimed by the C88 target-only observer; no other-region identity is inferred.
75. **T216 aging pass invoked:** Yes; target `AGE_UPDATE` records `age 0 -> 1` after evaluation.
76. **T320 aging pass invoked:** Yes globally by the locked end-of-GC source path, but target EXTRA4 is no longer on the source free list when the target-specific increment hook would run.
77. **EXTRA4 highest eligible T320:** Not applicable; the target passes the per-region OOM branch in the list scan, not the address-ordered allocator selector.
78. **EXTRA4 highest eligible T216:** No; the target is not eligible under either branch and is not selected.
79. **Selection criterion:** `distribute_free_regions` uses free-list iteration plus the OR predicate; `move_highest_free_regions` separately scans the region map right-to-left and is not the target's C88 transfer mechanism.
80. **EXTRA4 transfer ordinal:** T320 C88 `AGE_TRANSFER`, event ordinal `0xAC`.
81. **Global-decommit insertion ordinal:** T320 `AGE_TRANSFER` at `0xAC` is the `global_regions_to_decommit` insertion; the later queue-consumer event is `0xAD`.
82. **Global-decommit count before/after:** `1 -> 2` during T320 target transfer.
83. **`decommit_step` ordinal:** T320 C88 `DECOMMIT_QUEUE`, event ordinal `0xAD`; the queue count changes `2 -> 1`.
84. **`decommit_region` ordinal:** T320 C88 `DECOMMIT_COMPLETE`, event ordinal `0xAD`, after successful `virtual_decommit`.
85. **EXTRA4 state after transfer:** `state=0x0A` while the target is transferred into the global-decommit list.
86. **EXTRA4 state after decommit:** `state=0x00`.
87. **EXTRA4 generation after decommit:** `generation=0`.
88. **EXTRA4 pages decommitted:** `decommitSize=0x1000` (`GC_PAGE_SIZE`).
89. **EXTRA4 range remains reserved:** Yes; `heap_segment_reserved` remains `0x101B00000`, so the normalized reserved envelope remains `[0x1A00000,0x1B00000)`.
90. **EXTRA4 descriptor survives:** The completion hook still has the `heap_segment*`; `global_region_allocator::delete_region(get_region_start(region))` removes the active allocator range record, so it is not an active basic-free entry. The reserved envelope remains available for later allocator knowledge/recommit semantics.
91. **Aged transfer independent of final balance:** Yes. It is executed in the earlier free-list scan before the final balance branch.
92. **Relationship between aged transfer and final balance:** Sequential, separate policies: aged/OOM candidates are moved first; the later `balance` arithmetic controls additional surplus-region movement and can remain negative after the global queue is already populated.
93. **Offline T320 counterfactual:** Holding all later observed operands fixed but removing the four acquisitions leaves age `0 < 20`; no age-driven transfer follows. Whether the OOM branch remains true cannot be derived without changing the unobserved producer of the committed/OOM operands.
94. **Offline T216 counterfactual:** If only list pressure were changed while T216's observed `age=0`, `committedSize=0x21000`, and OOM flag remained fixed, the target would still fail the predicate. Substituting T320's exact OOM operands would make the OR true.
95. **Counterfactual supports causal classification:** Yes; it separates the workload's upstream effect on OOM/committed operands from the explicit age threshold and supports Outcome G.
96. **Third threshold value required:** No. T320/T216 do not straddle an age threshold; age is not the causal boundary.
97. **Third value:** Not run.
98. **Third-value predicted result:** Not applicable.
99. **Third-value observed result:** Not applicable.
100. **Explicit age-threshold causal status:** Disproved for this crossover; T320 transfers at age `0`, below `20`.
101. **Address-order causal status:** Disproved for EXTRA4's observed transfer; no target address-order move exists in the C88 event stream.
102. **List-order causal status:** List position is contextual and records the different topology, but the target transfer is predicate-controlled and not the address-order selector path.
103. **Tail-pressure causal status:** Supported upstream by C86: tail320 adds 104 transient gen0 allocations and `0x1A09C0` requested bytes while retained/promotion accounting is held constant.
104. **Allocation-demand causal status:** Supported for the four ordinary acquisitions; C88 connects the changed chronology to distinct committed/OOM operands at EXTRA4, without claiming an uninstrumented producer-level proof.
105. **Independent-policy causal status:** The one-page OOM branch is an independent production eligibility policy from the final balance arithmetic and from explicit age; its T320/T216 operands still arise in the two workload chronologies.
106. **Unified five-region explanation:** `tail320 -> extra demand -> four ordinary acquisitions and changed region state -> EXTRA4 one-page/OOM eligibility -> global decommit -> T320 1/1`; tail216 leaves EXTRA4 at `0x21000`, below neither age nor OOM eligibility, preserving six basic regions.
107. **First supported causal link:** C86's controlled tail delta to four additional ordinary region acquisitions.
108. **Strongest causal chain:** The exact source OR predicate and exact T320/T216 operands are reproduced in 3/3 boots; T320 transfers, queues, and decommits EXTRA4, while T216 ages it only to `1` and retains it basic-free.
109. **First unsupported causal link:** The exact producer-level mechanism that makes T320 `get_region_committed_size` equal `0x1000` and sets `joined_last_gc_before_oom=1`, versus T216 `0x21000/0`, is outside the bounded C88 observer.
110. **Candidate-path relevance:** Sanity-only; C18 candidate rejection is not part of this causal path and no candidate event was used.
111. **B02 evaluated:** No.
112. **B02 status:** Not run; C88 remains outside candidate rejection and no authentic candidate-eligibility event appeared.
113. **B02 future justification:** Run only if a later upstream investigation produces an authentic candidate rejection that can affect the accepted path.
114. **C88 baseline BSS:** Accepted diagnostic baseline remains ONE `0x4F0840` and SIX `0x3947E0`.
115. **C88 BSS:** Same per-cell diagnostic footprint; the C88 observer emits scalar serial fields and adds no event buffer. The proof section audit also reports identical full-kernel `.bss` size `0x062A8770` for paired T320/T216 composition checks.
116. **BSS delta:** `0` relative to the accepted C84/C87-safe composition.
117. **Production mutation:** None.
118. **Allocator mutation:** None.
119. **Age mutation:** None; C88 reads and serializes age only.
120. **Decommit-policy mutation:** None; the locked predicate and thresholds are unchanged.
121. **Free-list mutation:** None by the observer; production list calls execute unchanged and C88 only observes target calls.
122. **Region mutation:** None.
123. **Generation mutation:** None.
124. **Candidate mutation:** None.
125. **Planner mutation:** None.
126. **Promotion mutation:** None.
127. **Survivor fabrication:** None.
128. **Root fabrication:** None.
129. **C18:** PASS; inherited NativeAOT startup and GC safety path remains intact.
130. **Code manager:** PASS; inherited code-manager provenance and startup path remain intact.
131. **`FindMethodInfo`:** PASS; no C88 regression in the inherited managed/native handoff.
132. **Root scan:** PASS; inherited root-scan path completes for both controls.
133. **Mark closure:** PASS; inherited mark/closure accounting remains accepted.
134. **Planner authenticity:** PASS; promotion/planner decisions remain authentic and are not fabricated by C88.
135. **Survivor integrity:** PASS; retained survivor accounting remains `15` and promotion bytes remain `0xFD980`.
136. **C88 invariant failures:** `0` in every boot.
137. **Sensitive diagnostic allocations:** `0` in every boot.
138. **C88 event capacity:** No C88-specific capacity; inherited C67 capacity is `0x800` records.
139. **C88 peak records:** Target-only C88 peaks are `6` events per T320 boot and `3` per T216 boot; inherited C77 C67 event counts are `0xBC` and `0xC6` respectively.
140. **C88 overflow:** `0` event overflow and `0` region overflow.
141. **Fail-fast:** `0` in every boot.
142. **Page faults:** `0` in every boot.
143. **T320 Boot 1:** PASS; six target events `[LIST_ADD, AGE_EVAL, LIST_ADD, AGE_TRANSFER, DECOMMIT_QUEUE, DECOMMIT_COMPLETE]`, OOM-driven path true, serial SHA256 `31B13F0BFDDEA864E70FD88C8ADFDF52533D704609A239E25E118D19DA31B5EE`.
144. **T320 Boot 2:** PASS; same normalized signature and path, serial SHA256 `9989F59049F6685EF67740E25BD7FCA802738B2D81962E955EDD3C596A2EE819`.
145. **T320 Boot 3:** PASS; same normalized signature and path, serial SHA256 `107E19F4DF73D7F0AD7251001C33B10C4036A05EF740C2359367684936AEB583`.
146. **T216 Boot 1:** PASS control; three target events `[LIST_ADD, AGE_EVAL, AGE_UPDATE]`, no transfer/decommit path, serial SHA256 `19F9AA30B7808DEA5686AAC2D3DA1C1773237037C6D166B6BB09535FC421E95D`.
147. **T216 Boot 2:** PASS control; same normalized signature and no-transfer result, serial SHA256 `F10B2F34B1A9233D3CB86876E6E7CA52EF8D62119FF0D9E323D497AED434776A`.
148. **T216 Boot 3:** PASS control; same normalized signature and no-transfer result, serial SHA256 `975B4CADAA9165DB924936B86AAA5C86273AE98C7C21990CC6597798976190B6`.
149. **Semantic agreement:** True for T320 and T216; raw addresses are excluded from the normalized signature and all three boots agree.
150. **Nondeterminism:** No semantic nondeterminism; serial hashes differ as expected for complete boot logs, but target roles, fields, stages, and inherited controls agree.
151. **Serial hashes:** T320 `31B13F...`, `9989F5...`, `107E19...`; T216 `19F9AA...`, `F10B2F...`, `975B4C...`; full SHA256 values are in items 143–148 and each run manifest.
152. **Artifact hashes:** T320 proof kernel `D2723731FD185C3891171D92B7C9594D30F1D59AAD0F978879F2A9F6B97C7EB3`, PE `ED92C046A5FED9189975DF0F396A342A4137E6B1644814F3A04C258D65F49BA1`, ELF `C238E207B9794804A234D717E8F278FED9F41010ADB801D3711FEA5DD584D6EA`, map `20BF44B59828D4323BC33E9CBD87BE112F80E57FE90F312557EC9EEB2A774E71`; T216 proof kernel `A283DD29A437851417270C5051970FAA0DCCE0EC67FFFB8899747FF3322A25A4`.
153. **Runtime-pack validation:** PASS using `out/dotnet/runtime-pack-c68/runtime-pack.manifest.json`; locked source and FP repair state are validated.
154. **Managed build:** PASS; `managed-artifact-build.log` and `managed-link.log` complete, with existing warnings only.
155. **Native build:** PASS; runtime-pack, GC archive, native artifact, kernel link, and QEMU image complete.
156. **PowerShell syntax:** PASS; the smoke script parses with the PowerShell parser after the C88 branch and manifest fixes.
157. **JSON/XML parse:** PASS for C88 manifests and generated JSON; no XML artifact was generated by this harness, so the XML check is not applicable.
158. **`git diff --check`:** PASS.
159. **PE -> ELF conversion:** PASS; custom entry point was found and both proof artifacts were converted/inspected as ELF64.
160. **Symbol checks:** PASS; native-helper and kernel-entry symbol audits complete, and the required C88 entry is present in the generated link map alongside the inherited C67 symbols.
161. **Linker/source/table/archive guards:** PASS; locked-source needles, runtime-pack archive, linker, table, archive, and artifact guards complete.
162. **C52 Tier-All result or reason omitted:** Omitted; C88 is a narrow free-region predicate investigation and Tier-All would not add semantic evidence.
163. **Ordinary restoration:** PASS in `finally`; ordinary kernel and ESP artifacts are restored after proof runs.
164. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
165. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
166. **Proof artifact active:** No; post-run ordinary hashes match and the proof-only kernel is not left active.
167. **C88-owned QEMU cleanup:** `0` C88-owned QEMU processes remain.
168. **Unrelated QEMU preservation:** No unrelated QEMU process was terminated; the remaining process belongs to `D:\dev\guideXOS_NET10_nativeaot-managed-kernel-integration`, outside the C88 evidence root. Final total QEMU count is `1`; C88-owned count is `0`.
169. **Files changed:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this documentation file.
170. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C88_AGED_FREE_REGION_TRANSFER_PROVENANCE.md`.
171. **Evidence root:** `out/dotnet/c011ec88-aged-free-region-transfer/`, with T320 and T216 run roots and manifests under `15mid8/tail-320` and `15mid8/tail-216`.
172. **Final commit:** Local subject `Trace NativeAOT aged region decommit transfer`; the exact immutable SHA is recorded in `out/dotnet/c011ec88-aged-free-region-transfer/final-commit.txt`.
173. **Push status:** Not pushed.
174. **Remaining limitation:** C88 proves the exact eligibility operands and downstream transfer/decommit chain, but does not instrument the upstream producer of T320's one-page committed state and `joined_last_gc_before_oom=1`.
175. **Exact next-smallest milestone:** C89 should quantify why the additional `0x1A09C0` requested bytes require exactly four extra regions by isolating allocation-context headroom, usable payload, alignment/header losses, and acquisition thresholds; do not return to survivor/materialization work or run B02 without a genuine candidate rejection.

## Mechanism map

`basic_free` → `gc_heap::distribute_free_regions` list scan and age/OOM
predicate → `region_free_list::unlink_region` + `add_region` to
`global_regions_to_decommit` → `gc_heap::decommit_step`/
`unlink_region_front` → `gc_heap::decommit_region` → `virtual_decommit`,
`heap_segment_committed = heap_segment_mem`, and allocator `delete_region`.

The per-region predicate is age/OOM-controlled. The final balance is
count/budget-controlled and runs later. `move_highest_free_regions` is a
separate address-order/count-controlled path and was not the target's C88
transfer. Once EXTRA4 is in `global_regions_to_decommit`, queue consumption is
unconditional apart from the normal decommit operation's success result.

## C85–C88 progression

C85 identified EXTRA4's exact basic-free removal and decommit/recycle
chronology. C86 established that tail pressure, with the retained cohort held
constant, is responsible for the overall `1/1` versus `1/6/6` crossover. C87
isolated the final free-region balance and disproved the assumption that its
negative value directly moves EXTRA4, while discovering a T320 aged global
entry. C88 closes the on-ramp: EXTRA4 is encountered in
`distribute_free_regions` with age `0`; T320 satisfies the one-page OOM clause,
enters the global queue, and is successfully decommitted, while T216 fails
both predicate branches and remains basic-free.

# NativeAOT Workstation GC C87: decommit budget and free-region balance

C87 continues C84–C86 on the locked NativeAOT 9.0.0 AMD64 Workstation GC.
C84 proved equal region materialization. C85 proved four ordinary
`EXTRA0`–`EXTRA3` allocation consumptions and `EXTRA4` decommit/reclaim. C86
proved tail pressure is causal with the retained cohort held constant. C87
resolves the production free-region arithmetic and narrows the last unexplained
member of the five-region set without changing production policy.

## Result

The exact production balance is a region-unit balance:

```text
balance = total_num_free_regions[kind]
        + num_huge_region_units_to_consider[kind]
        - total_budget_in_region_units[kind]
```

At the final live C87 OOS checkpoint, T320 evaluates `4 + 0 - 18 = -14` and
T216 evaluates `6 + 0 - 18 = -12`. Both are negative and both compute zero
regions for the final `move_highest_free_regions` call. T320 also has one aged
region already on `global_regions_to_decommit`; T216 has none. The direct C87
observer therefore captures the exact policy operands and an independent
policy-state difference, but it does not capture the accepted C85 EXTRA4
descriptor at that exact target-specific move. The exact fifth-region boundary
is consequently narrowed, not fully isolated.

Outcome: **G — narrowed but unresolved**.

Success level: **Level 1 — decommit policy operands captured**.

## Numbered report

1. **Outcome:** G — narrowed but unresolved; the production balance is isolated, but the live observer did not capture the accepted C85 EXTRA4 selection point.
2. **Success Level:** Level 1.
3. **Repository:** `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. **Branch:** `v1.1_DOTNET_SUPPORT`.
5. **Starting HEAD:** `63bc5e61a208ba8b3637a72fa9355bc4ab8afcf6`.
6. **Starting subject:** `Isolate NativeAOT tail-driven region consumption`.
7. **Final HEAD:** The local C87 commit; exact SHA is reported in the final handoff.
8. **Final subject:** `Isolate NativeAOT decommit region balance`.
9. **Upstream:** `origin/v1.1_DOTNET_SUPPORT`.
10. **Starting ahead/behind:** Actual `0 ahead / 0 behind`; the requested expectation was `1 ahead / 0 behind`.
11. **Final ahead/behind:** `1 ahead / 0 behind`; not pushed.
12. **Starting worktree:** Clean.
13. **Final worktree:** Clean after the local C87 commit.
14. **Runtime identity:** NativeAOT `9.0.0`, AMD64, Workstation GC, GC interfaces `5.3 / 2`.
15. **Runtime source SHA:** `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. **FP patch SHA:** `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. **C85 SHA:** `e500d7df6fdf5e7c08b75c1101aa99d5ccead691`.
18. **C86 SHA:** `63bc5e61a208ba8b3637a72fa9355bc4ab8afcf6`.
19. **C87 SHA:** The final local C87 commit SHA; see item 175 and the final handoff.
20. **Exact C87 question:** What exact free-region/decommit budget arithmetic causes `distribute_free_regions` / `move_highest_free_regions` to remove EXTRA4 under T320 but not T216, and is that removal downstream of four T320 acquisitions or independent?
21. **T320 baseline:** PASS; one fresh final-code boot, authentic promotion, `1 / 1 / 1`.
22. **T216 baseline:** PASS; one fresh final-code boot, same promotion accounting, `1 / 6 / 6`.
23. **T320 promotion:** Authentic positive promotion; 15 retained objects.
24. **T216 promotion:** Authentic positive promotion; 15 retained objects.
25. **T320 promoted bytes:** `0xFD980`.
26. **T216 promoted bytes:** `0xFD980`.
27. **T320 retained count:** 15.
28. **T216 retained count:** 15.
29. **T320 tail:** 320 transient gen0 allocations (`0x140`).
30. **T216 tail:** 216 transient gen0 allocations (`0xD8`).
31. **Tail request-byte delta:** `0x1A09C0` object-request bytes (`0x501E00 - 0x361440`).
32. **EXTRA0 T320 removal:** Yes; ordinary gen0 acquisition, C85 remove ordinal `0xC3`, basic count `4 -> 3`, range `0x1400000`, descriptor `0x104010DA0`.
33. **EXTRA1 T320 removal:** Yes; ordinary gen0 acquisition, C85 remove ordinal `0xC8`, basic count `3 -> 2`, range `0x1500000`, descriptor `0x104010E48`.
34. **EXTRA2 T320 removal:** Yes; ordinary gen0 acquisition, C85 remove ordinal `0xCD`, basic count `2 -> 1`, range `0x1600000`, descriptor `0x104010EF0`.
35. **EXTRA3 T320 removal:** Yes; ordinary gen0 acquisition, C85 remove ordinal `0xB4`, basic count `9 -> 8`, range `0x1700000`, descriptor `0x104010F98`.
36. **EXTRA4 T320 removal:** Yes in the accepted C85 chronology; C85 remove ordinal `0xB2`, basic count `6 -> 5`, transfer `0xB3`, decommit-list unlink `0xB7`, range `0x1A00000`, descriptor `0x104011190`.
37. **EXTRA4 T216 removal:** No target-specific removal or decommit transfer in the accepted T216 chronology; the corresponding target remains basic-free.
38. **`distribute_free_regions` source file:** `src/coreclr/gc/gc.cpp`, locked copy at `out/dotnet/pal-runtime-active-replacement-build/locked-source/src/coreclr/gc/gc.cpp`.
39. **`distribute_free_regions` source function:** `gc_heap::distribute_free_regions`, `gc.cpp:13276-13665`.
40. **`move_highest_free_regions` source:** `region_allocator::move_highest_free_regions`, `gc.cpp:4270-4305`.
41. **`global_regions_to_decommit` source:** `gc_heap::global_regions_to_decommit`, declaration `gc.cpp:2830`.
42. **`decommit_step` source:** `gc_heap::decommit_step`, `gc.cpp:44483-44528`.
43. **`decommit_region` source:** `gc_heap::decommit_region`, `gc.cpp:44531-44595`.
44. **Exact decommit predicate:** In the normal foreground branch, move-highest is eligible only when `balance >= 0`; a positive balance moves exactly that many region units.
45. **Predicate source expression:** `ptrdiff_t balance = total_num_free_regions[kind] + num_huge_region_units_to_consider[kind] - total_budget_in_region_units[kind];` followed by `if (background_running_p() || (balance < 0)) ... else { num_regions_to_decommit[kind] = balance; ... }`.
46. **Policy unit:** Regions/region units; basic policy factor is 1 and basic `region_size` is `0x100000`. Actual decommit size is a separate byte quantity.
47. **Free-count operand T320:** `total_num_free_regions[basic_free_region] = 4` at final OOS policy.
48. **Free-count operand T216:** `total_num_free_regions[basic_free_region] = 6` at final OOS policy.
49. **Desired/retained operand T320:** `total_budget_in_region_units[basic_free_region] = 0x12` (18).
50. **Desired/retained operand T216:** `total_budget_in_region_units[basic_free_region] = 0x12` (18).
51. **Surplus operand T320:** `num_regions_to_decommit[basic_free_region] = 0` at the final callback before the balance branch.
52. **Surplus operand T216:** `num_regions_to_decommit[basic_free_region] = 0` at the final callback before the balance branch.
53. **Decommit-budget operand T320:** No byte budget enters the `distribute_free_regions` predicate; the separate `decommit_step` quota is `max_decommit_step_size = DECOMMIT_SIZE_PER_MILLISECOND * step_milliseconds`.
54. **Decommit-budget operand T216:** Same; no byte budget enters the balance predicate.
55. **Commit-balance operand T320:** No committed-byte balance operand in the predicate; the live T320 aged record has `committedSize=0x1000`.
56. **Commit-balance operand T216:** No committed-byte balance operand in the predicate; no C87 aged transfer was observed.
57. **Regions-to-move T320:** 0 at the final OOS checkpoint; an earlier common positive checkpoint computed 4.
58. **Regions-to-move T216:** 0 at the final OOS checkpoint; an earlier common positive checkpoint computed 4.
59. **EXTRA4 selected T320:** Yes by accepted C85 target chronology; not directly emitted by the final C87 `C87_MOVE` observer at the exact target-specific point.
60. **EXTRA4 selected T216:** No in the accepted target chronology; no direct C87 EXTRA4 move was emitted.
61. **EXTRA4 selection criterion:** Highest-address eligible free basic region in allocator map order, subject to unit size and not already on the destination list.
62. **EXTRA4 list ordinal:** Direct map ordinal was not captured in the accepted C85 target record; normalized range slot is `0x1A` (`0x1A00000 / 0x100000`).
63. **EXTRA4 address-order role:** It is selected because it is the highest eligible address when the source scan reaches it; it is not selected by list head/tail, age, or largest size.
64. **EXTRA4 state before move:** C85 target chronology records basic-free state `0xA` before the unlink.
65. **EXTRA4 state after move:** C85 records state `0x8` after the basic unlink; the subsequent decommit transition clears the allocator allocation state.
66. **EXTRA4 generation before:** 0.
67. **EXTRA4 generation after:** 0; decommit does not promote or fabricate a generation transition.
68. **EXTRA4 remains reserved after move:** Yes; `delete_region` frees allocator-map units within the reserved global mapping envelope.
69. **EXTRA4 committed state after move:** `heap_segment_committed(region)` is reset to `heap_segment_mem(region)` after successful decommit; the C85 target-specific committed endpoint is not a C87 live operand.
70. **Decommit immediate/deferred:** Deferred from list movement to `decommit_step`; `decommit_region` then calls `virtual_decommit` and `delete_region`.
71. **T320 pre-EXTRA0 basic count:** 4 in the accepted C85 target-specific removal window.
72. **T320 post-EXTRA0 basic count:** 3.
73. **T320 post-EXTRA1 basic count:** 2.
74. **T320 post-EXTRA2 basic count:** 1.
75. **T320 post-EXTRA3 basic count:** 8 in the accepted C85 local window; intervening insertions make the target chronology non-monotonic.
76. **T320 pre-decommit basic count:** 6 immediately before the accepted EXTRA4 remove `0xB2`.
77. **T320 post-EXTRA4 basic count:** 5 after `0xB2`; then `0xB3` moves it to `global_regions_to_decommit`.
78. **T320 RestartEE basic count:** 1 overall; accepted C85 ledger `1 + 15 insertions - 15 removals = 1`.
79. **T216 equivalent checkpoint counts:** Target-specific EXTRA0–EXTRA4 removal counts are not observed as an equivalent five-step chain; the accepted same-workload ledger is `1 + 15 insertions - 10 removals = 6`, with post-Restart and post-resume count 6.
80. **Decommit calculation occurs after four acquisitions:** Source ordering is after the current free-list scan and count accumulation; C85 proves the four target-specific ordinary removals precede the accepted EXTRA4 consumer, but C87 does not emit a target ordinal for that exact call.
81. **T216 invokes distribute policy:** Yes; `C87_POLICY` records are present.
82. **T216 invokes move-highest policy:** Yes at an earlier positive common checkpoint; no EXTRA4-specific move is emitted.
83. **T216 computed surplus:** 0 at the final OOS checkpoint.
84. **T216 regions moved:** 0 at the final OOS checkpoint; earlier common positive checkpoint moved 4 source-defined high-map regions.
85. **Offline T320 counterfactual without four acquisitions:** Holding observed final operands fixed, `(4 + 4) + 0 - 18 = -10`.
86. **Offline T216 counterfactual with four removals:** Holding observed final operands fixed, `(6 - 4) + 0 - 18 = -16`.
87. **Counterfactual predicts predicate flip:** No; neither calculation crosses zero.
88. **Four acquisitions sufficient:** No for the final live C87 predicate; exact accepted EXTRA4 checkpoint sufficiency is not proven.
89. **Four acquisitions necessary:** Not provable from the captured final checkpoint; the source target-specific sequence is interleaved and the live C87 observer does not identify the exact fifth-region call.
90. **Independent decommit operand differs:** Yes at the observed final policy state: T320 has one aged global-decommit entry and T216 has zero; whether that is independent of the accepted four-acquisition chain remains unresolved.
91. **Unified five-region explanation:** Narrowed only: tail pressure explains the four ordinary acquisitions, and C85 explains the fifth consumer, but C87 does not unify them into one proven numeric flip.
92. **Earliest decommit-policy divergence ordinal:** No C87 ordinal field; the earliest direct distinction is the T320-only `C87_AGING` record before its final OOS policy callback.
93. **Divergence source function:** `gc_heap::distribute_free_regions`, aged-free scan at `gc.cpp:13375-13398`.
94. **T320 predicate LHS:** `total_num_free_regions + huge_units = 4 + 0 = 4`.
95. **T320 predicate RHS:** `total_budget_in_region_units = 18` (`0x12`).
96. **T320 predicate result:** `4 - 18 = -14` (`0xFFFFFFFFFFFFFFF2`); deficit branch, zero move-highest count.
97. **T216 predicate LHS:** `total_num_free_regions + huge_units = 6 + 0 = 6`.
98. **T216 predicate RHS:** `total_budget_in_region_units = 18` (`0x12`).
99. **T216 predicate result:** `6 - 18 = -12` (`0xFFFFFFFFFFFFFFF4`); deficit branch, zero move-highest count.
100. **Exact threshold identified:** Yes for the production predicate: `balance >= 0` enters the surplus branch, and `balance > 0` is required to call `move_highest_free_regions`; the EXTRA4-specific threshold remains unresolved.
101. **Third boundary value required:** No; the observed exact threshold does not straddle the accepted target-specific boundary in a way that justifies a derived third workload.
102. **Third boundary value:** Not applicable; no third tail was run.
103. **Threshold boot result:** Not applicable; no third-tail boot was run.
104. **Threshold result matches prediction:** Not applicable.
105. **Free-region reserve causal status:** Source-proven immediate determinant through `total_budget_in_region_units`; T320 and T216 both show budget 18 at final OOS, so reserve arithmetic alone does not explain the accepted EXTRA4 split.
106. **Commit-budget causal status:** The byte/time decommit quota is downstream in `decommit_step`, not the `distribute_free_regions` balance predicate; not the identified fifth-region threshold.
107. **Region ordering causal status:** Source-proven selector for which region is moved once a positive count exists; it explains highest-address selection, but not why the accepted C85 target call had a positive count.
108. **Tail-pressure causal status:** Supported upstream cause from C86; 104 extra transient gen0 allocations and `0x1A09C0` extra object-request bytes.
109. **Allocation-demand causal status:** Supported; the tail loop is the only intended workload difference and C85/C86 account for four T320 ordinary target consumptions.
110. **Decommit causal status:** Downstream decommit/reclaim is proven for EXTRA4 by C85 and direct decommit mechanics are proven by C87; the exact target-specific balance remains unresolved.
111. **First supported causal link:** `tail320 - tail216 = 104` transient allocation iterations and `0x1A09C0` object-request bytes.
112. **Strongest causal chain:** Tail pressure -> transient gen0 demand -> four ordinary basic-region acquisitions -> changed free-list/global-decommit state -> decommit policy; the fifth target-specific numeric boundary remains open.
113. **First unsupported causal link:** The exact live `total_num_free_regions`/budget operands at the accepted C85 EXTRA4 selection and the proof that the four acquisitions alone flip them.
114. **Candidate-path relevance:** Sanity evidence only; C85/C86/C87 do not place EXTRA0–EXTRA4 in candidate rejection.
115. **B02 evaluated:** No.
116. **B02 status:** `STILL_PREMATURE`.
117. **B02 future justification:** Only a new authentic candidate rejection with its required production markers would justify B02.
118. **C87 baseline BSS:** ONE `0x4F0840`; SIX `0x3947E0`, inherited from the accepted C84/C85-safe composition.
119. **C87 BSS:** Same per-cell safe diagnostic footprint; direct C87 scalars serialize immediately and add no diagnostic array.
120. **BSS delta:** `0`.
121. **Production mutation:** None; the source predicate and production operands are unchanged.
122. **Allocator mutation:** None.
123. **Free-list mutation:** None beyond the unchanged production operations executing naturally; observers only serialize state.
124. **Decommit-policy mutation:** None.
125. **Region mutation:** None.
126. **Generation mutation:** None.
127. **Planner mutation:** None.
128. **Candidate mutation:** None.
129. **Promotion mutation:** None.
130. **Survivor fabrication:** None.
131. **Root fabrication:** None.
132. **C18:** PASS.
133. **Code manager:** PASS.
134. **`FindMethodInfo`:** PASS.
135. **Root scan:** PASS.
136. **Mark closure:** PASS.
137. **Planner authenticity:** PASS.
138. **Survivor integrity:** PASS.
139. **C87 invariant failures:** 0.
140. **Sensitive diagnostic allocations:** 0.
141. **C87 event capacity:** Direct C87 records have no dedicated event array; inherited C67 capacity is `0x800` records.
142. **C87 peak records:** T320 `14 policy / 5 move / 1 aging / 10 decommit`; T216 `14 policy / 5 move / 0 aging / 8 decommit`; inherited C67 peaks are T320 `0xB2`, T216 `0xBC`.
143. **C87 overflow:** 0 event overflow and 0 region overflow.
144. **Fail-fast:** 0.
145. **Page faults:** 0.
146. **T320 Boot 1:** PASS; authentic promotion, `1/1/1`, clean diagnostics.
147. **T320 Boot 2:** Not run; Level 2 was not established, so no confirmation budget was spent.
148. **T320 Boot 3:** Not run; Level 2 was not established.
149. **T216 Boot 1:** PASS; authentic promotion, `1/6/6`, clean diagnostics.
150. **T216 Boot 2:** Not run; Level 2 was not established.
151. **T216 Boot 3:** Not run; Level 2 was not established.
152. **Semantic agreement:** Each discovery cell agreed with its expected semantic result; no third-boot confirmation set was required at Level 1.
153. **Nondeterminism:** No semantic nondeterminism observed; raw addresses and serialized traces are run-local.
154. **Serial hashes:** T320 `B3460E4F63354BA884DCF4B935F7840E27E0469B662D82816B1C02BBA18B8121`; T216 `0FEC5EA199279758D7A28D497C5B98AD338405EFC2AA0B2A2EEAB2573D029A54`.
155. **Artifact hashes:** T320 EXE `50A228E585C7DDD5E7EE9EFFBA9E676158C7772C67EF27CE50D5375D02E947DC`, ELF `82AE5E44A419FDE9E7F5C51A512FD82B86509666D7E8A0F244233D952FFBA62D`, MAP `9BCF5378F204D279A0607F938C79986057E8CE22FA3AEC95AA1F5653B5BAE947`; T216 EXE `D88BFFEB348C1DDF92E321846F808DE3CAEE9A483CF3C0B258637F211534559D`, ELF `F307C103AF9F41626D821D7E045EC555F1F16EFD8C3E5609458CA058307DF2A6`, MAP `0326B997017F047CF77C03FAC003E87BDD229BD1E4ACF291E630FD2B49249C6E`.
156. **Runtime-pack validation:** PASS using `out/dotnet/runtime-pack-c68/runtime-pack.manifest.json`; patched source and FP provenance match.
157. **Managed build:** PASS for both final discovery builds.
158. **Native build:** PASS for runtime-pack, native artifact, GC archive, kernel link, and QEMU image.
159. **PowerShell syntax:** PASS for the modified smoke script.
160. **JSON/XML parse:** PASS for the runtime-pack and final run JSON manifests; no applicable XML parse failure.
161. **`git diff --check`:** PASS.
162. **PE -> ELF conversion:** PASS for both final artifacts; ELF64 inspection and map generation completed.
163. **Symbol checks:** PASS; exports, kernel entry, native helper, and runtime symbols were emitted and inspected.
164. **Linker/source/table/archive guards:** PASS; C87 source needles, runtime-pack archive, linker, table, and artifact guards completed.
165. **C52 Tier-All result or reason omitted:** Omitted because C87 is a narrow free-region/decommit policy investigation and Tier-All would not add relevant semantic evidence.
166. **Ordinary restoration:** PASS in both final manifests.
167. **Ordinary kernel SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
168. **Ordinary ESP SHA:** `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
169. **Proof artifact active:** No; final manifests report `proofOnlyArtifactActive=False`.
170. **C87-owned QEMU cleanup:** 0 C87-owned QEMU processes remain.
171. **Unrelated QEMU preservation:** Final total QEMU count is 0; the unrelated process observed during an intermediate check was outside the C87 evidence roots and was not terminated by C87 cleanup.
172. **Files changed:** `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`, `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`, and this report.
173. **Documentation path:** `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C87_DECOMMIT_BUDGET_FREE_REGION_BALANCE.md`.
174. **Evidence root:** `out/dotnet/c011ec87-decommit-budget-free-region-balance/`, with source audit, discovery, offline arithmetic, and final-confirmation copies.
175. **Final commit:** Local subject `Isolate NativeAOT decommit region balance`; exact SHA is the final C87 HEAD reported in the handoff.
176. **Push status:** Not pushed.
177. **Remaining limitation:** C87 does not directly serialize the accepted C85 EXTRA4 target-specific policy call; therefore it cannot claim Level 2 or prove four-acquisitions sufficiency for that exact call.
178. **Exact next-smallest milestone:** C88 should isolate the allocation-context exhaustion/byte-demand threshold that converts the extra `0x1A09C0` transient request bytes into four acquisitions; do not reopen survivor count, region materialization, or B02.

## Source audit details

The complete source audit is in:

`out/dotnet/c011ec87-decommit-budget-free-region-balance/source-audit/decommit-policy-source-audit.md`

The complete offline arithmetic is in:

`out/dotnet/c011ec87-decommit-budget-free-region-balance/offline-counterfactual-arithmetic/threshold-calculations.md`

The direct final serial traces and manifests are copied under:

`out/dotnet/c011ec87-decommit-budget-free-region-balance/final-confirmations/T320/`

`out/dotnet/c011ec87-decommit-budget-free-region-balance/final-confirmations/T216/`

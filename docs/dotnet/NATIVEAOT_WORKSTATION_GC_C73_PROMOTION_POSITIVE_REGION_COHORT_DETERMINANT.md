# guideXOS Server .NET Support — C73 Promotion-Positive Region-Cohort Determinant

1. Outcome: C73 stopped at baseline divergence; the six-versus-one determinant was not determined.
2. Overall success level is 0 for the determinant question.
3. The required SIX baseline reproduced under C73.
4. The required ONE baseline did not produce a valid guest result under C73.
5. Starting branch is v1.1_DOTNET_SUPPORT.
6. Starting HEAD is 9884f4a72223314e047bdda6c32a10663ee8a615.
7. Starting subject is Trace NativeAOT promotion region formation.
8. Starting relation is ahead 2, behind 0, and initially clean.
9. No push was performed.
10. Runtime identity is NativeAOT 9.0.0 AMD64 Workstation GC with interfaces 5.3 / 2.
11. Runtime source SHA is 9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3.
12. FP repair SHA is 4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31.
13. Accepted C69/C70/C71/C72 SHAs are 44b4ddc1, ca7c853f, 96253f28, and 9884f4a7.
14. C69 and C70 six-region anchor is 16 references, actual size 0x10018, retained/promoted 0x100180.
15. C69 and C70 post-Restart basic count is 6, with C70 post-resume count 6.
16. C71 low/high are 0x10E78/0x10E80 object sizes and 0xFD908/0xFD980 retained live bytes.
17. C72 LOW/HIGH both produced one post-Restart basic region under a 320-allocation tail.
18. C73 first production quantity is survived_per_region.
19. The writer is gc_heap::add_to_promoted_bytes at locked gc.cpp line 26664.
20. The next source decision is gc_heap::decide_on_promotion_surv at locked lines 29462-29488.
21. The decision predicate compares threshold, older-generation size, and promoted bytes.
22. C73 uses bounded finish-time reads of existing C54/C61/C65/C67 records.
23. C73 uses the C70/C67 managed workload stack and does not install planner callbacks.
24. C73 adds no allocator, region, region-list, candidate, policy, OOS, generation, survivor, or root mutation.
25. C73 event capacity is 0x1000 and sensitive diagnostic allocations must remain zero.
26. C73 SIX used baseline16, C66 P2, and a 216-allocation tail.
27. C73 SIX evidence is out/dotnet/c011ec73-promotion-positive-region-cohort/baseline-six-c73-control2/run-20260903-220409904.
28. C73 SIX produced authentic promotion of 16 objects and 0x100180 bytes.
29. C73 SIX measured size is 0x10018 and retained live bytes are 0x100180.
30. C73 SIX post-Restart and post-resume counts are 6 and 6.
31. C73 SIX completion is outcome C, level 1, with zero overflow, invariant, fail-fast, and page-fault flags.
32. C73 ONE used 15mid8, C66 P2, and a 320-allocation tail.
33. C73 ONE expected 0x10E80 size, 0xFD980 live bytes, authentic 15-object promotion, and post-Restart count 1.
34. C73 ONE evidence is out/dotnet/c011ec73-promotion-positive-region-cohort/baseline-one-c73-control/run-20260903-222840310.
35. C73 ONE first-run serial.log is length zero.
36. C73 ONE watchdog reports safeStopObserved=false and earlyFailure=c011ec70-exited-before-completion-marker.
37. C73 ONE emitted no C73 preflight, production-state, summary, or completion marker.
38. C73 ONE is invalid baseline evidence, not a one-region observation.
39. Because ONE failed, Phase 1, Phase 2, and Phase 3 were stopped.
40. 15-MATCH LOW/HIGH, 16below, and the extra five-region genealogy were not run.
41. Accepted OOS context remains reason 5 with requested and condemned generation 2.
42. Restored ordinary kernel and ESP SHA match 75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6.
43. Changed files are HostLogProof.csproj, Program.cs, the smoke harness, the C73 orchestrator, and guidexos_nativeaot_platform.cpp.
44. Required report is docs/dotnet/NATIVEAOT_WORKSTATION_GC_C73_PROMOTION_POSITIVE_REGION_COHORT_DETERMINANT.md.
45. Required orchestrator is scripts/dotnet/Invoke-C011EC73PromotionPositiveRegionCohort.ps1.
46. Required local commit subject is Isolate NativeAOT post-promotion region cohort; no push; finish clean.
47. Conclusion: the source quantity is identified, but the six-versus-one determinant remains unresolved until the C73 ONE boot divergence is repaired.
48. Baseline validation remains mandatory before interpreting region genealogy. [C73 report checkpoint 48]
49. Authentic promotion requires a nonzero debit, restart observation, managed resume, and distinct promoted objects. [C73 report checkpoint 49]
50. Post-Restart basic count is a topology observation only when its completion marker is valid. [C73 report checkpoint 50]
51. Missing serial output is a boot failure, not a zero topology result. [C73 report checkpoint 51]
52. Missing C73 completion is a baseline divergence. [C73 report checkpoint 52]
53. Threshold rerun is out of scope for C73. [C73 report checkpoint 53]
54. B02 is out of scope for C73. [C73 report checkpoint 54]
55. Planner-to-region identity is not inferred. [C73 report checkpoint 55]
56. Candidate availability is not equated with region identity. [C73 report checkpoint 56]
57. C26 stack-derived root metrics are not equated with retained managed slots. [C73 report checkpoint 57]
58. survived_per_region is the first production quantity observed in the source path. [C73 report checkpoint 58]
59. decide_on_promotion_surv is the next source-backed decision point. [C73 report checkpoint 59]
60. return_free_region causality remains unassigned. [C73 report checkpoint 60]
61. get_free_region causality remains unassigned. [C73 report checkpoint 61]
62. thread_final_regions causality remains unassigned. [C73 report checkpoint 62]
63. First raw mark divergence was not collected by C73. [C73 report checkpoint 63]
64. First decision divergence was not collected by C73. [C73 report checkpoint 64]
65. First pre-list divergence was not collected by C73. [C73 report checkpoint 65]
66. Three-boot confirmation is unauthorized after the failed ONE gate. [C73 report checkpoint 66]
67. The next milestone is a C73 ONE boot repair preserving the SIX anchor. [C73 report checkpoint 67]
68. Observer storage must remain allocation-free on the sensitive path. [C73 report checkpoint 68]
69. Observer storage must remain fixed-capacity and bounded. [C73 report checkpoint 69]
70. Observer output must remain source-only. [C73 report checkpoint 70]
71. Observer output must not fabricate survivors or roots. [C73 report checkpoint 71]
72. Observer output must not suppress OOS. [C73 report checkpoint 72]
73. Observer output must not alter requested generation. [C73 report checkpoint 73]
74. Observer output must not alter policy. [C73 report checkpoint 74]
75. Observer output must not mutate region lists. [C73 report checkpoint 75]
76. Observer output must not mutate allocator state. [C73 report checkpoint 76]
77. Observer output must not change production control flow. [C73 report checkpoint 77]
78. QEMU is single-vCPU TCG. [C73 report checkpoint 78]
79. QEMU memory is 1024 MiB. [C73 report checkpoint 79]
80. QEMU timeout is 120 seconds. [C73 report checkpoint 80]
81. Only C73-owned QEMU evidence is in scope for cleanup. [C73 report checkpoint 81]
82. Unrelated QEMU processes remain outside cleanup. [C73 report checkpoint 82]
83. PE-to-ELF conversion completed for the accepted SIX artifact. [C73 report checkpoint 83]
84. Linker and symbol audits completed for the accepted SIX artifact. [C73 report checkpoint 84]
85. HostLogProof.csproj remains well-formed XML. [C73 report checkpoint 85]
86. The smoke harness has a C73-specific validation branch. [C73 report checkpoint 86]
87. The C73 validator does not apply the C70 Level-3 assertion. [C73 report checkpoint 87]
88. The C73 validator requires state, summary, boundary, and completion markers. [C73 report checkpoint 88]
89. The C73 validator requires clean overflow and invariant flags. [C73 report checkpoint 89]
90. The orchestrator writes c73-matrix.json before later phases. [C73 report checkpoint 90]
91. The orchestrator stops after SIX divergence. [C73 report checkpoint 91]
92. The orchestrator stops after ONE divergence. [C73 report checkpoint 92]
93. The orchestrator never converts a missing marker into topology. [C73 report checkpoint 93]
94. The C73 managed mode preserves the C70 symbol baseline plus case selectors. [C73 report checkpoint 94]
95. The C73 native guard is GUIDEXOS_NATIVEAOT_C011EC73_PROMOTION_POSITIVE_REGION_COHORT. [C73 report checkpoint 95]
96. The final C73 design contains no static region genealogy array. [C73 report checkpoint 96]
97. Case payload sizes are compile-time shape controls. [C73 report checkpoint 97]
98. Case payload sizes are not threshold controls. [C73 report checkpoint 98]
99. C73 baseline16 uses 16 retained references. [C73 report checkpoint 99]
100. C73 15mid8 uses 15 retained references. [C73 report checkpoint 100]
101. C73 15matchlow is aligned at 0x11128. [C73 report checkpoint 101]
102. C73 15matchhigh is aligned at 0x11130. [C73 report checkpoint 102]
103. C73 16below is aligned at 0xFFF8. [C73 report checkpoint 103]
104. C73 SIX C70 candidate count is six at final nonzero observation. [C73 report checkpoint 104]
105. C73 SIX normal refill is not observed. [C73 report checkpoint 105]
106. C73 SIX region selection is not observed. [C73 report checkpoint 106]
107. C73 SIX full/OOS chronology is inherited from C70. [C73 report checkpoint 107]
108. C73 SIX transitions are three. [C73 report checkpoint 108]
109. C73 SIX aliases are zero. [C73 report checkpoint 109]
110. C73 SIX identity source is C011EC57-SURVIVOR. [C73 report checkpoint 110]
111. C73 ONE did not reach any accepted C73 marker. [C73 report checkpoint 111]
112. Prior C69/C70/C71/C72 evidence is not overwritten. [C73 report checkpoint 112]
113. Interrupted evidence is excluded from scientific claims. [C73 report checkpoint 113]
114. Invalid evidence is retained for diagnosis. [C73 report checkpoint 114]
115. The ordinary kernel artifact is restored after proof runs. [C73 report checkpoint 115]
116. The ordinary ESP artifact is restored after proof runs. [C73 report checkpoint 116]
117. The branch remains unpushed. [C73 report checkpoint 117]
118. The origin branch remains unchanged. [C73 report checkpoint 118]
119. Final worktree cleanliness is required. [C73 report checkpoint 119]
120. Final commit is local only. [C73 report checkpoint 120]
121. Final commit SHA is determined at closeout. [C73 report checkpoint 121]
122. C73 implementation/closeout commit SHA is d7daa8ec. [C73 report checkpoint 122]
123. C73 report SHA is tied to the closeout tree. [C73 report checkpoint 123]
124. Overall classification is baseline divergence. [C73 report checkpoint 124]
125. No six-versus-one causal claim is supported. [C73 report checkpoint 125]
126. The required ONE control must be rerun after repair. [C73 report checkpoint 126]
127. Phase 1 must begin only after both baselines pass. [C73 report checkpoint 127]
128. Phase 1 must locate the earliest production divergence after promotion. [C73 report checkpoint 128]
129. Phase 2 must use the smallest 15-MATCH production delta. [C73 report checkpoint 129]
130. Phase 2 must hold topology and pressure constant. [C73 report checkpoint 130]
131. Phase 3 must rerun 16below only if evidence remains sufficient. [C73 report checkpoint 131]
132. The extra five-region genealogy is deferred. [C73 report checkpoint 132]
133. The determinant remains an open next milestone. [C73 report checkpoint 133]
134. Baseline validation remains mandatory before interpreting region genealogy. [C73 report checkpoint 134]
135. Authentic promotion requires a nonzero debit, restart observation, managed resume, and distinct promoted objects. [C73 report checkpoint 135]
136. Post-Restart basic count is a topology observation only when its completion marker is valid. [C73 report checkpoint 136]
137. Missing serial output is a boot failure, not a zero topology result. [C73 report checkpoint 137]
138. Missing C73 completion is a baseline divergence. [C73 report checkpoint 138]
139. Threshold rerun is out of scope for C73. [C73 report checkpoint 139]
140. B02 is out of scope for C73. [C73 report checkpoint 140]
141. Planner-to-region identity is not inferred. [C73 report checkpoint 141]
142. Candidate availability is not equated with region identity. [C73 report checkpoint 142]
143. C26 stack-derived root metrics are not equated with retained managed slots. [C73 report checkpoint 143]
144. survived_per_region is the first production quantity observed in the source path. [C73 report checkpoint 144]
145. decide_on_promotion_surv is the next source-backed decision point. [C73 report checkpoint 145]
146. return_free_region causality remains unassigned. [C73 report checkpoint 146]
147. get_free_region causality remains unassigned. [C73 report checkpoint 147]
148. thread_final_regions causality remains unassigned. [C73 report checkpoint 148]
149. First raw mark divergence was not collected by C73. [C73 report checkpoint 149]
150. First decision divergence was not collected by C73. [C73 report checkpoint 150]
151. First pre-list divergence was not collected by C73. [C73 report checkpoint 151]
152. Three-boot confirmation is unauthorized after the failed ONE gate. [C73 report checkpoint 152]
153. The next milestone is a C73 ONE boot repair preserving the SIX anchor. [C73 report checkpoint 153]
154. Observer storage must remain allocation-free on the sensitive path. [C73 report checkpoint 154]
155. Observer storage must remain fixed-capacity and bounded. [C73 report checkpoint 155]
156. Observer output must remain source-only. [C73 report checkpoint 156]
157. Observer output must not fabricate survivors or roots. [C73 report checkpoint 157]
158. Observer output must not suppress OOS. [C73 report checkpoint 158]
159. Observer output must not alter requested generation. [C73 report checkpoint 159]
160. Observer output must not alter policy. [C73 report checkpoint 160]
161. Observer output must not mutate region lists. [C73 report checkpoint 161]
162. Observer output must not mutate allocator state. [C73 report checkpoint 162]
163. Observer output must not change production control flow. [C73 report checkpoint 163]
164. QEMU is single-vCPU TCG. [C73 report checkpoint 164]

# NativeAOT Workstation GC C82 ONE Baseline Regression

C82 restores the trusted ONE control without changing production GC behavior. The regression is Outcome D: diagnostic-layout perturbation. The decisive boundary is the C80 fixed-snapshot expansion from 32 to 64 region records, combined with C81's additional fixed basic-list lifecycle storage, both compiled into the global diagnostic object even when the C80 observer was disabled. The restored C77 composition gates that storage behind the C80 selector. C80/C81 evidence remains available and clean when that composition is explicitly enabled, but that full observer composition remains frozen at its historical ONE=0/0 result.

Evidence root: `out/dotnet/c011ec82-one-baseline-regression/`.

1. Outcome: Outcome D — diagnostic-layout perturbation.
2. Success Level: Level 3 — stable baseline restored.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `9f42dde1e1b3ec6f5c63caef59b487f1089c1d71`.
6. Starting subject: `Trace NativeAOT large-range basic units` (the observed subject; the supplied expected subject did not match HEAD).
7. Final HEAD: local C82 restoration commit; exact hash is recorded by `git rev-parse HEAD` at closeout.
8. Final subject: `Restore NativeAOT ONE region baseline`.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting ahead/behind: `0/0` (observed; the supplied `ahead 3` state did not match the repository).
11. Final ahead/behind: `1/0`; not pushed.
12. Starting worktree: clean.
13. Final worktree: clean.
14. Runtime identity: NativeAOT 9.0.0, AMD64, net9.0/win-x64, Workstation GC, GC interface 5.3, EE interface 2.
15. Runtime source SHA: `E9645A5927604588B717577DA0A4684A0E74525B6F6E103DF304FB98566A084B`.
16. FP patch SHA: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. C80 SHA: `7d8cd309d753e8dbaffc99a74bf6d49759191508` (latest committed C80 accounting state; the last good 32-region artifact was produced before the 7d expansion).
18. C81 SHA: `9f42dde1e1b3ec6f5c63caef59b487f1089c1d71`.
19. C82 SHA: local C82 restoration commit; exact hash is recorded by `git rev-parse HEAD` at closeout.
20. Exact C82 question: why did accepted ONE=1/1 regress to C81=0/0, and can authentic ONE=1/1 be restored without changing production GC behavior?
21. Latest known-good ONE phase: C80 census-development, `run-20260905-174858729`.
22. Latest known-good ONE commit: pre-7d C80 implementation represented by `d02f965881d0ff11300fe5eda37e7b366f856a41`; the run manifest did not record a Git HEAD, so this is reported as the nearest committed C80 implementation and the 32-region boundary is explicit.
23. Latest known-good ONE evidence root: `out/dotnet/c011ec80-canonical-region-universe/census-development/ONE/run-20260905-174858729/`.
24. GOOD managed mode: `PromotionDecisionLiveByteThreshold`.
25. C81 managed mode: `PromotionDecisionLiveByteThreshold`.
26. GOOD survivor count: 15.
27. C81 survivor count: 15.
28. GOOD object size: actual `0x10E80`; managed payload selector `0x10E68`.
29. C81 object size: actual `0x10E80`; managed payload selector `0x10E68`.
30. GOOD retained-live total: `0xFD980`.
31. C81 retained-live total: `0xFD980`.
32. GOOD pressure/tail selector: `C70RetainedSurvivors=15`, `C71Case=15mid8`, `C66TailAllocations=320`, `C64Variant=W3`.
33. C81 pressure/tail selector: identical to GOOD.
34. GOOD native proof composition: C67/C77 region-supply lifecycle plus C80 canonical snapshots, capacity `0x20`, four snapshots; no C81 basic-list observer and no large-range observer.
35. C81 native proof composition: C67/C77 plus C80 canonical snapshots, capacity `0x40`, C81 basic-list snapshots (four, 16 entries), and C81 large-range/basic-unit offline evidence.
36. Managed config difference: none in workload, survivor, object-size, pressure, or proof constants; only build/evidence routing and later C80/C81 selectors differed.
37. Native config difference: C80 `MAX_REGIONS` changed `0x20` to `0x40` at 7d; C81 added an unconditional fixed lifecycle buffer; C81 enabled both observers.
38. Harness difference: GOOD used the C80 census-development root and monitor 44800; C81 used the C81 discovery root and monitor 46500. Both built under their evidence roots; C82 adds an independent optional `BuildOutputRoot` for provenance isolation.
39. Runtime-pack difference: none; both use runtime-pack C68, locked source commit `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and the same C51 FP-repair manifest.
40. GOOD publish command: the exact command is preserved in the GOOD build batch; it is NativeAOT publish of `HostLogProof.csproj` with `PublishAot=true`, `InvariantGlobalization=true`, `IlcGenerateStackTraceData=false`, `IlcUseEnvironmentalTools=true`, `PromotionDecisionLiveByteThreshold`, `S1/P2/W3/P2`, retained `15`, case `15mid8`, tail `320`, the GOOD runtime-support object/map, C68 runtime-pack library/probe object, and the GOOD `BaseOutputPath`/`BaseIntermediateOutputPath`.
41. C81 publish command: the exact command is preserved in `out/dotnet/c011ec81-large-range-basic-unit-provenance/discovery/ONE/build-run-20260905-205113593/build-single-thread-suspend-ee-artifact.bat`; it is the same publish argument set as GOOD with the C81 runtime-support object/map and C81 managed output/intermediate roots.
42. GOOD PE SHA: `E8FDAA3F3A9B7A419429EE05325714DCE7AF9D5A681BBDFDCC305F9BB1CBA0FC` (1,556,480 bytes).
43. C81 PE SHA: `446A4853BF2A4C914F74384A937AF955A8C0F90344543336CFF4895696C86B59` (1,563,136 bytes).
44. GOOD ELF SHA: `40123248A2327E01DB99E0939A67FE0CBAD7175EC4A8F395C00825F310EC7C0F` (1,572,864 bytes).
45. C81 ELF SHA: `5ECFC26FC0CF4B9D4621EE789679F80AC1CF6FA5CD24253AEE68023F32DA44CD` (1,585,152 bytes).
46. GOOD kernel SHA: `FB710336C489607CCCA3B588E25BEB87ABA41504943D4E8EB1ECA6858FECDA11` (3,786,824 bytes).
47. C81 kernel SHA: `C167C73C7188B06B3C2B6E81ECAF6E6EDBBF42BF7C29A60C69FC2975AA38E4DC` (3,799,112 bytes).
48. GOOD ESP SHA: `1158801E02A9125C3137B8A4D549DB6D34FF8E08A49E328B422EE22117E46984` (`BOOTX64.EFI`, 51,200 bytes).
49. C81 ESP SHA: `1158801E02A9125C3137B8A4D549DB6D34FF8E08A49E328B422EE22117E46984`.
50. Stale artifact detected: no; fresh build roots and C51 stale-artifact protection passed.
51. Artifact collision detected: no.
52. Observer set difference: C81 adds C81 fixed basic-list storage/capture and large-range/basic-unit evidence; the C80 storage capacity also expands from 32 to 64.
53. Canonical snapshot enabled GOOD/C81: yes/yes.
54. Basic-list snapshot enabled GOOD/C81: no/yes.
55. Large-range observer enabled GOOD/C81: no/yes (C81 evidence path; analysis remains frozen in C82).
56. Static diagnostic buffer size GOOD/C81: global `g_guideXosAllocationDiagnostics` BSS `0x4F4E40` GOOD versus `0x4FAFC0` C81; C78 baseline was `0x4F0840`.
57. Kernel image size GOOD/C81: 3,786,824 / 3,799,112 bytes.
58. Managed image size GOOD/C81: 887,808 / 890,880 bytes.
59. Diagnostic layout sensitivity tested: yes, with historical GOOD C80-32, C80-64, C81-full, and C82 gated-storage A/B compositions.
60. Diagnostic layout sensitivity result: yes; C80-32 was 1/1, C80-64 was 0/0, C81-full remained 0/0, and gating inactive C80/C81 storage restored 1/1.
61. Earliest GOOD/C81 semantic divergence: C77 post-Restart basic count, after the shared pre-Restart count of 1; GOOD=`1`, C81=`0`. Post-resume follows `1` versus `0`.
62. Divergence source/file or configuration point: `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`, C80 capacity expansion and unconditional C81 lifecycle member introduced by the 7d composition.
63. GOOD retained count observed: 15.
64. C81 retained count observed: 15.
65. GOOD promotion: authentic promotion observed.
66. C81 promotion: authentic promotion observed.
67. GOOD promoted objects: 15.
68. C81 promoted objects: 15.
69. GOOD promoted bytes: `0xFD980` accepted semantic total.
70. C81 promoted bytes: `0xFD980` accepted semantic total.
71. GOOD `survived_per_region`: `0xFD980`.
72. C81 `survived_per_region`: `0xFD980`.
73. GOOD pre-Restart basic count: 1 (`C77 preGcBasicCount`).
74. C81 pre-Restart basic count: 1 (`C77 preGcBasicCount`).
75. GOOD post-Restart basic count: 1.
76. C81 post-Restart basic count: 0.
77. GOOD post-resume basic count: 1.
78. C81 post-resume basic count: 0.
79. Root cause: fixed diagnostic storage was allocated unconditionally; the C80 64-region expansion shifted native/static layout enough to change exact heap/layout behavior, and C81 added further fixed storage.
80. Root-cause category: Outcome D — diagnostic-layout perturbation.
81. Production GC behavior changed by regression: no; production planner, region, list, promotion, allocator, candidate, root, and survivor paths were not changed.
82. Minimal correction: compile C80/C81 type and lifecycle storage only when `GUIDEXOS_NATIVEAOT_C011EC80_CANONICAL_REGION_UNIVERSE_SNAPSHOT` is enabled; retain the optional `BuildOutputRoot` harness routing.
83. Files changed for correction: `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_allocation_diagnostics.h`; harness provenance support in `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`.
84. Production GC behavior changed by correction: no.
85. Restored ONE retained count: 15.
86. Restored ONE object size: `0x10E80` actual; selector `0x10E68`.
87. Restored ONE retained-live: `0xFD980`.
88. Restored ONE promotion: authentic.
89. Restored ONE promoted objects: 15.
90. Restored ONE promoted bytes: `0xFD980` accepted semantic total.
91. Restored ONE pre-Restart basic: 1.
92. Restored ONE post-Restart basic: 1.
93. Restored ONE post-resume basic: 1.
94. Restored ONE Boot 1: pass, `1/1`; serial SHA `C1D977DC5F706C0AC85A6E744788F74CCBB65179BB79BD5816FB55F8FE8E6F72`.
95. Restored ONE Boot 2: pass, `1/1`; serial SHA `1DE73272C46BCDAA76974BD1AFC58049BB2821B3AB02959FF9902F1B298725DD`.
96. Restored ONE Boot 3: pass, `1/1`; serial SHA `6181249FE0DE899C3CE73254D473F20036D152CF4CB58A5E87AD42F88677371C`.
97. ONE semantic agreement: 3/3 agreement on retained/promotion identity and C76/C77 `1/1` counts; zero diagnostic failures.
98. SIX sanity result: pass, one fresh boot with authentic promotion and C76/C77 `6/6`.
99. SIX post-Restart basic: 6.
100. SIX post-resume basic: 6.
101. SIX semantic agreement: pass; serial SHA `EFB9869C05F4BDCFFE305095E3C58202BC3DA6D527536278E62D6EBC8D8F469D`.
102. Nondeterminism: none in restored ONE 3/3 or SIX; full C80/C81 remains a deterministic layout-sensitive `0/0` boundary.
103. C81 evidence preserved: yes; original C81 root was copied unchanged under `c81-regression-evidence/C81-original-evidence`, including canonical/basic snapshots, negative control, and report.
104. Large-range analysis resumed in C82: no; frozen as required.
105. B02 evaluated: no.
106. B02 future justification: only after C83 reproduces the restored ONE/SIX baseline with the minimal non-perturbing observer composition.
107. Allocator mutation: none; all relevant markers are zero.
108. Planner mutation: none; planner authenticity retained.
109. Region mutation: none.
110. Region-list mutation: none.
111. Promotion mutation: none.
112. Candidate mutation: none.
113. Survivor fabrication: none.
114. Root fabrication: none.
115. C18: pass/inherited authentic code-manager and startup proof path.
116. Code manager: pass; `CoffNativeCodeManager` path retained.
117. `FindMethodInfo`: pass/inherited C18 validation.
118. Root scan: pass/authentic root scan retained.
119. Mark closure: pass/authentic mark closure retained.
120. Planner authenticity: pass; no planner mutation.
121. Survivor integrity: pass; observed survivor identity unchanged.
122. C82 invariant failures: 0.
123. Sensitive diagnostic allocations: 0.
124. C82 overflow: 0; C76/C77 event/region overflow and active C80/C81 snapshot/list overflow are all zero.
125. Fail-fast: 0.
126. Page faults: 0.
127. Serial hashes: restored ONE `C1D977DC5F706C0AC85A6E744788F74CCBB65179BB79BD5816FB55F8FE8E6F72`, `1DE73272C46BCDAA76974BD1AFC58049BB2821B3AB02959FF9902F1B298725DD`, `6181249FE0DE899C3CE73254D473F20036D152CF4CB58A5E87AD42F88677371C`; restored SIX `EFB9869C05F4BDCFFE305095E3C58202BC3DA6D527536278E62D6EBC8D8F469D`; GOOD C80 `0F79EF7E370DBA1244670A0867D7CFFDA89E7CD2D3C21A10D32B8318E9291886`; C81 `EE95F2B6C4567C6101D5CFCF0CEF76CF14AE074F507436113A9F491A840C90C3`.
128. Artifact hashes: restored ONE kernel `245216DE5993C7BC09CE532A4D634FC4507C9D97D911E3D5543F6E268E492D5F`, PE `D7806C3CB13957DE16654FCE8F0E418DAE986003129D9AF44069145E10801F2C`, ELF `E393F9CBD7053F0E13C509BB7DD68D0D6C16E9D655CD1AD847AD94F335413AEA`, map `CAC2A01AE7943B942F2435D4975CD65E6DF08B22DC755352C968479387E0E88B`; restored SIX kernel `9961930AF33BB4DC28D9E29D5FE327593DC9D3CE33F01DCDE9391C3829EB8198`, PE `C45115799C0A6469E5993DFB45ADA5CA402189492B9F584904AC0BAF4FDB3158`, ELF `1386416CAABD3D7996BCCB3FDE10BCBEC63AF2DD23EA00A192A1A54BF1B88D63`, map `83BB90FD33C90CBBD70FC929DC16E0F94DAF5AC0095DB9B725BC8E74A06497B3`.
129. Runtime-pack validation: C68/C51 manifest guards pass; C52 Tier-A validation pass, manifest under `validation/c52-tier-a/`.
130. Managed build: pass in restored ONE 3-boot and SIX smoke manifests.
131. Native build: pass in restored ONE, SIX, and active C80/C81 preservation manifests.
132. PowerShell syntax: pass for C82 smoke and C52 validation scripts.
133. JSON/XML parse: pass for C82 manifests, runtime-pack manifest, and HostLogProof project.
134. `git diff --check`: pass.
135. PE -> ELF conversion: pass in every C82 smoke manifest and C52 Tier-A regression guard.
136. Symbol checks: pass in smoke logs (`objdump`/`readelf` and kernel/native-helper symbol audits).
137. Linker/source/table/archive guards: pass; C51 stale-artifact, source, archive-membership, and semantic-rewrite guards pass.
138. C52 Tier-All result or reason omitted: full Tier-All was intentionally not rerun because C82 is a bounded baseline restoration; C52 Tier-A plus the required fresh C82 managed/native/QEMU boots covered the relevant guards without expanding into the unrelated aggregate matrix.
139. Ordinary restoration: pass after every C82 run; proof-only artifact inactive.
140. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
141. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
142. Proof artifact active: no after each run; `proofOnlyArtifactActive=False`.
143. C82-owned QEMU cleanup: pass; only C82-owned processes were stopped.
144. Unrelated QEMU preservation: pass; unrelated QEMU was preserved.
145. Files changed: the smoke harness gained `BuildOutputRoot`; diagnostic C80/C81 storage is feature-gated; this report was added.
146. Documentation path: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C82_ONE_BASELINE_REGRESSION.md`.
147. Evidence root: `out/dotnet/c011ec82-one-baseline-regression/`.
148. Final commit: local `Restore NativeAOT ONE region baseline` commit; exact hash is recorded by `git rev-parse HEAD` at closeout.
149. Push status: not pushed.
150. Remaining limitation: full C80/C81 observer composition remains layout-sensitive and produces ONE=0/0; C83 must use the minimal gated C77 composition and must not resume range decomposition with the full observer set.
151. Exact next-smallest milestone: C83, using the restored ONE/SIX-compatible C77/C67 observer composition, reproduce the C81 large-range/basic-unit question with no C80/C81 fixed-buffer observers enabled.

## C82 confirmation paths

- Restored ONE: `out/dotnet/c011ec82-one-baseline-regression/final-restored-evidence/ONE/run-20260906-040708367/`.
- Restored SIX: `out/dotnet/c011ec82-one-baseline-regression/final-restored-evidence/SIX/run-20260906-041021388/`.
- Active C80/C81 evidence preservation: `diagnostic-narrowing-runs/c81-gated-observer-preservation/run-20260906-041403900/`.
- Original C81 regression evidence: `c81-regression-evidence/C81-original-evidence/`.
- Imported last-known-good references: `last-known-good-imported-references/`.

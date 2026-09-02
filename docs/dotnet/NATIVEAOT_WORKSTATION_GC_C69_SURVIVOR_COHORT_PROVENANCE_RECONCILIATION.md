# NativeAOT Workstation GC C69 — Survivor-Cohort Provenance Reconciliation

Date: 2026-09-02
Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`
Branch: `v1.1_DOTNET_SUPPORT`
No push was performed.

## Result

C69 reached Outcome A / Success Level 3. The exact source-backed C67 control was reconstructed with 16 retained managed references. Promotion returned authentically, the promotion/debit/RestartEE/managed-resume boundary was observed, and the later production path reached the same semantic basic-free-region scarcity and full/OOS chronology established by C67.

The first known C68 divergence was the invalid requested baseline of four references. Source and emitted C67 evidence establish 16 retained references. C69 resolves the discrepancy: the values 4 and 16 are not comparable metrics. The historical 4 is a C26 stack-derived `Promote` entry metric; 16 is the managed proof workload's retained early object-reference cohort.

## Evidence and provenance audit

The authoritative C67/C68 managed source is `samples/managed/HostLogProof/Program.cs`, in `RunC011EC61PreFinalN0PromotionCycle`:

* `earlySurvivorCount = 16` allocates 16 distinct `byte[payloadSize]` objects.
* `retainedSurvivorCount = earlySurvivorCount` in the C67 control, and C69 fixes the same value at 16.
* `survivors[offset] = value` stores each object in an ordinary managed ownership array; the first 16 slots contain 16 distinct objects and there is no aliasing.
* Each object is patterned and read back before and after observation. Initial generation is checked as gen0.
* The later P2 schedule adds the same two 16-object increments (survivor totals 32 and 48) with 48 transient allocations per cohort.
* The post-debit tail remains ordinary managed allocation: 216 allocations of the inherited 16384-byte payload.

The C69 native anchor emits the managed slot count, C26 stack-derived metric, C61 promotion accounting, C57 survivor identity endpoints, and the production region chronology. It does not fabricate roots, survivors, candidates, regions, selection, refills, or policy outcomes.

The provenance mapping is:

`survivors[0..15] managed slots -> 16 distinct byte[] object identities -> C011EC57-SURVIVOR records -> C011EC61/C011EC62 promotion count and bytes -> gen1 debit`

The C26 value 4 comes from `d.c011ec26PromoteEntriesSnapshot = d.c011ec19FirstStackDerivedPromoteEntryCount`, emitted by the C26 stack-walk completion diagnostics. It counts stack-derived `Promote` entries for that proof, not the number of C69 managed ownership slots or promoted objects. C69 records this value as a separate metric (`enumeratedPromotingRoots=4`, `promotingRootMetricIsC26StackDerived=1`). A per-root callback count for the C69 managed array itself was not invented or inferred.

## Reconstructed workload

| Quantity | C67 | C69 | Meaning |
|---|---:|---:|---|
| Early retained references | 16 | 16 | Managed ownership slots containing distinct early objects |
| Early allocated objects | 16 | 16 | `byte[65536]`, aligned allocation size `0x10018` |
| Early transient allocations | 48 | 48 | Ordinary short-lived pressure |
| Later main survivor totals | 32, 48 | 32, 48 | P2 schedule; 16 new survivors in each later cohort |
| Transient allocations per later cohort | 48 | 48 | Ordinary pressure |
| Post-debit payload | 16384 | 16384 | Ordinary tail allocation |
| Post-debit tail allocations | 216 | 216 | `C66TailAllocations=216` |
| C64/C66 configuration | W3/P2 | W3/P2 | Held constant |

Retention is in allocation order. Retained slots are not nulled before the promotion boundary. Transient locals become unreachable by ordinary managed scope/lifetime. C69 added no managed ownership structure beyond the existing C67/C68 path and added no native ownership.

## Promotion and region evidence

The three fresh confirmation boots all reported:

* retained references: `0x10`;
* C26 stack-derived promoting-root metric: `0x4`;
* unique promoted objects: `0x10`;
* promoted bytes: `0x100180`;
* gen1 budget before: `0x1AD658`;
* gen1 debit: `0xE0150`;
* gen1 budget after: `0xCD508`;
* generation boundary: gen0 to gen1;
* RestartEE and managed resume: observed;
* survivor integrity: valid.

The C69 snapshot replay records the basic-free-region candidate count at the meaningful C67 checkpoints: 0 before promotion, 0 immediately after promotion, 6 after RestartEE and managed resume, and 0 before the decisive request. The final nonzero candidate count is 6, with head `0x104011040` at checkpoint 7. The final candidate is removed authentically at event ordinal `0xC8` on region `0x104011238`, with state `0xA -> 0x8` and list count `1 -> 0`, from `region_free_list::unlink_region`. C67's corresponding event ordinal was `0xDC`; this difference is diagnostic-record density/layout, not a production decision divergence.

The decisive gen0 context exhaustion used request size `0x4018`, active region `0x104011238`, and active headroom `0`. `get_new_region(0)` then observed candidate count 0, null head/tail, no eligible candidate, result 0, and `commit_failed=1`. No region was selected and no normal allocation-context refill occurred. The inherited full/OOS path observed reason `5`, `last_gc_before_oom=1`, requested generation 2, `GarbageCollectGeneration` generation 2, `n_initial=2`, and final condemned generation 2.

## Boot agreement and hashes

Evidence root:

`out/dotnet/c011ec69-survivor-cohort-provenance-reconciliation/run-20260902-061209981`

Confirmation serial hashes:

1. `E6A181664295AE35677E1086A30BF613041D3F137661E51F04C362B4325ABA5C`
2. `3428E5A1A36D01A85EEAFB84F8207472D00AD1866DFC77BA4EA5ED7C932AE406`
3. `B83E92F3A9491C46FD94ADA71EF5AB7342473CE926398AA89FE28E6AD01AA63E`

All three agree on Outcome A / Level 3 and all semantic fields. Confirmation proof-kernel SHA-256 is `41EFEF176B05396E9F9FE54C60A09959F1A9F4589F976C264457CDB68A0F039B`; PE, ELF, and map hashes are recorded in `manifest.json`.

## Validation and closeout

The C51 runtime-pack prerequisite passed using the validated C52 runtime-pack manifest with locked NativeAOT 9.0.0 AMD64 Workstation GC identity, runtime source SHA `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`, and durable FP repair SHA `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`. Managed build, native/runtime-pack build, GC archive, kernel build, PE→ELF conversion, symbol/ELF checks, linker/source/table/archive guards, JSON/XML parsing, PowerShell parsing, and `git diff --check` passed. C52 Tier-All was not run: it is a separate full productionized-second-collection aggregate and is not ordinarily required or semantically appropriate for this proof-only control; the C69 harness did execute the applicable runtime identity and proof guards.

The ordinary kernel and ESP were restored to SHA-256 `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`. The proof artifact is inactive. Only C69-owned QEMU processes were cleaned; unrelated QEMU instances were preserved. The final commit SHA is reported by the git closeout accompanying this document.

## Required numbered closeout

1. Outcome: Outcome A.
2. Success Level: Level 3.
3. Repository: `D:\dev\guideXOSServerV1.1_DOTNET_SUPPORT`.
4. Branch: `v1.1_DOTNET_SUPPORT`.
5. Starting HEAD: `63059e02c9d2547cfafe346a64018aedd92632c8`.
6. Starting subject: `Test NativeAOT survivor influence on gen0 regions`.
7. Final HEAD: recorded at git closeout.
8. Final subject: recorded at git closeout.
9. Upstream: `origin/v1.1_DOTNET_SUPPORT`.
10. Starting divergence: C68 requested 4 while source/emitted C67 cohort was 16.
11. Final divergence: no uncontrolled workload divergence; only non-semantic event/snapshot density and layout differences.
12. Starting worktree: clean at task start.
13. Final worktree: clean after commit closeout.
14. Runtime identity: NativeAOT 9.0.0, AMD64, Workstation GC.
15. Runtime source SHA: `9d5a6a9aa463d6d10b0b0ba6d5982cc82f363dc3`.
16. FP patch SHA: `4185495724D48E2962BA9042AF352718BF9188032DEE4C9DE6FFE9F145A1DC31`.
17. Semantic rewrite guard state: C46/C47/C48 disabled; durable FP repair inherited.
18. C67 SHA: `3dc01629dda0016208f7b84c06f50feafc205064`.
19. C68 SHA: `63059e02c9d2547cfafe346a64018aedd92632c8`.
20. C69 SHA: recorded at git closeout.
21. Exact C69 question: restoring 16 retained references while preserving C67/C68 workload and production GC behavior reproduces the C67 promotion boundary and decisive basic-free-region exhaustion chronology.
22. Historical “4” provenance: C26 stack-derived `Promote` entry snapshot.
23. Historical “16” provenance: C67 managed early retained-reference cohort in `Program.cs` and C57/C61/C62 emitted evidence.
24. Whether 4 and 16 are semantically comparable: no.
25. Exact C67 retained-reference count: 16.
26. Exact C69 retained-reference count: 16.
27. C67/C69 workload differences: none intentional; C69 adds observational markers only.
28. Potentially behavior-changing differences: none identified.
29. Promotion observed: yes.
30. Retained managed references: 16.
31. Enumerated/promoting roots: C26 stack-derived metric 4; not equated with the 16 managed slots.
32. Unique promoted objects: 16.
33. Promoted bytes: `0x100180`.
34. Gen1 budget before: `0x1AD658`.
35. Gen1 debit: `0xE0150`.
36. Gen1 budget after: `0xCD508`.
37. RestartEE observed: yes.
38. Managed resume observed: yes.
39. Generation boundary before: gen0.
40. Generation boundary after: gen1.
41. Survivor integrity result: pass; readbacks valid.
42. Region transition count: 3.
43. Basic free-region count before promotion: 0 at checkpoint 3.
44. Count after promotion: 0 at checkpoint 5.
45. Count after RestartEE: 6 at checkpoint 7.
46. Count after managed resume: 6 at checkpoint 7.
47. Last nonzero candidate count: 6.
48. Final candidate unlink ordinal: `0xC8`.
49. Final candidate region: `0x104011238`.
50. Final candidate state transition: `0xA -> 0x8`.
51. Final candidate unlink source: `region_free_list::unlink_region`.
52. Decisive context exhaustion observed: yes.
53. Decisive request size: `0x4018`.
54. Active region: `0x104011238`.
55. Active-region headroom: `0`.
56. `get_new_region(0)` observed: yes.
57. Candidate count entering `get_new_region`: 0.
58. Candidate identities: none; head and tail were null.
59. Candidate states: none; list was empty.
60. Candidate eligibility: false.
61. `POST_DEBIT_GEN0_CANDIDATE`: false.
62. Candidate selected: false.
63. Selected region: none.
64. Generation assignment: none; no region selected.
65. `POST_DEBIT_REGION_SELECTED`: false.
66. Allocation-context refill result: no normal refill.
67. `commit_failed`: 1 at decisive `get_new_region(0)`; normal-refill commit field remained 0 because no normal refill was attempted.
68. `POST_DEBIT_NORMAL_REFILL`: false.
69. Full/OOS trigger: observed.
70. Reason code: `0x5` (`reason_oos_soh`).
71. `last_gc_before_oom`: 1.
72. Requested generation: 2.
73. `GarbageCollectGeneration` generation: 2.
74. Final condemned generation: 2.
75. Whether C67 candidate chronology reproduced: yes, semantically.
76. Whether C67 promotion anchor reproduced: yes, authentically.
77. Whether B02 was evaluated: no.
78. Whether B02 remains unjustified: yes.
79. Allocator mutation: none.
80. Region mutation: none.
81. Region-list mutation: none.
82. Candidate mutation: none.
83. Policy mutation: none.
84. Survivor fabrication: none.
85. OOS suppression: none.
86. Requested-generation mutation: none.
87. C18 status: pass; inherited production root/frame path retained.
88. Code-manager status: pass; valid managed code-manager gate retained.
89. `FindMethodInfo`: pass on the valid managed frame path.
90. Root scan: completed authentically.
91. Mark closure: completed authentically.
92. Planner: observed authentically.
93. Survivor integrity: pass.
94. Invariant failures: 0.
95. Sensitive diagnostic allocations: 0.
96. Fail-fast: 0.
97. Page faults: 0.
98. Boot 1 result: Outcome A / Level 3.
99. Boot 2 result: Outcome A / Level 3.
100. Boot 3 result: Outcome A / Level 3.
101. Semantic agreement: yes across 3 fresh boots.
102. Serial hashes: `E6A181664295AE35677E1086A30BF613041D3F137661E51F04C362B4325ABA5C`; `3428E5A1A36D01A85EEAFB84F8207472D00AD1866DFC77BA4EA5ED7C932AE406`; `B83E92F3A9491C46FD94ADA71EF5AB7342473CE926398AA89FE28E6AD01AA63E`.
103. Proof artifact hashes: kernel `41EFEF176B05396E9F9FE54C60A09959F1A9F4589F976C264457CDB68A0F039B`; PE `3FFE2EC7C463B2C94B45E89791AF22B45E7A4084D4AFD268E7D789C23088A607`; ELF `9CAB2E9020C95B460316BB5623E1AAD7ABF25F5DAAE23EBB2E05943BDC923E24`; map `512B734BC2AAEECEC53BF5314096646548ADFCAEE12EAEFB465DA1A056FEEBC1`.
104. Runtime-pack validation: pass; C51 prerequisite validated the supplied C52 runtime-pack manifest.
105. Managed build: pass.
106. Native build: pass, including runtime-pack, GC archive, and kernel.
107. PowerShell syntax: pass.
108. JSON/XML parse: pass.
109. `git diff --check`: pass.
110. PE -> ELF conversion: pass.
111. Symbol checks: pass.
112. Linker/source/table/archive guards: pass.
113. C52 Tier-All: not run; separate aggregate and not semantically required for this proof-only control.
114. Ordinary restoration: pass.
115. Ordinary kernel SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
116. Ordinary ESP SHA: `75317E61229C1F138AFA35E4B67BD0CD45A229374EE0AFE0EED0835A67CC7DB6`.
117. Proof artifact active: no.
118. C69-owned QEMU cleanup: completed.
119. Unrelated QEMU preservation: preserved.
120. Files changed: `samples/managed/HostLogProof/HostLogProof.csproj`; `samples/managed/HostLogProof/Program.cs`; `scripts/smoke-nativeaot-gc-single-thread-suspend-ee-qemu.ps1`; `tools/dotnet/runtime-pack/src/platform/guidexos_nativeaot_platform.cpp`; this report.
121. Documentation path: `docs/dotnet/NATIVEAOT_WORKSTATION_GC_C69_SURVIVOR_COHORT_PROVENANCE_RECONCILIATION.md`.
122. Evidence root: `out/dotnet/c011ec69-survivor-cohort-provenance-reconciliation/run-20260902-061209981`.
123. Final commit: reported by git closeout.
124. Push status: not pushed.
125. Remaining limitation: C69 proves 16 as the valid control but does not test any lower survivor count.
126. Exact next-smallest milestone: C70, a bounded coarse-to-fine sweep from the known-good anchor, initially `16 -> 12 -> 8 -> 4`, narrowing only around the first semantic transition.
